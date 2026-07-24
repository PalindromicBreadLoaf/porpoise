// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/MemArena.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <switch.h>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/HorizonFastmem.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"

namespace Common
{
namespace
{
constexpr size_t HORIZON_PAGE_SIZE = 0x1000;

// Horizon tracks a state per memory block and only lets one of them be aliased into more than one
// address range.
struct AliasableSegment
{
  void* backing = nullptr;
  u8* canonical = nullptr;
  ::VirtmemReservation* reservation = nullptr;
  size_t size = 0;
};

bool CreateAliasableSegment(AliasableSegment& segment, size_t size)
{
  const Handle self = envGetOwnProcessHandle();

  void* const backing = std::aligned_alloc(HORIZON_PAGE_SIZE, size);
  if (!backing)
  {
    WARN_LOG_FMT(MEMMAP, "Failed to allocate {} bytes to back the guest memory segment.", size);
    return false;
  }

  virtmemLock();
  void* const canonical = virtmemFindCodeMemory(size, HORIZON_PAGE_SIZE);
  ::VirtmemReservation* const reservation =
      canonical ? virtmemAddReservation(canonical, size) : nullptr;
  virtmemUnlock();

  if (!reservation)
  {
    WARN_LOG_FMT(MEMMAP,
                 "Failed to find {} bytes of code address space for the guest memory "
                 "segment.",
                 size);
    std::free(backing);
    return false;
  }

  Result result = svcMapProcessCodeMemory(self, reinterpret_cast<u64>(canonical),
                                          reinterpret_cast<u64>(backing), size);
  if (R_FAILED(result))
  {
    WARN_LOG_FMT(MEMMAP, "svcMapProcessCodeMemory({} bytes) failed: {:#010x}", size, result);
    virtmemLock();
    virtmemRemoveReservation(reservation);
    virtmemUnlock();
    std::free(backing);
    return false;
  }

  // AliasCode to AliasCodeData. This transition is what svcSetProcessMemoryPermission is for, and
  // it is only valid here.
  result = svcSetProcessMemoryPermission(self, reinterpret_cast<u64>(canonical), size, Perm_Rw);
  if (R_FAILED(result))
  {
    WARN_LOG_FMT(MEMMAP, "svcSetProcessMemoryPermission({} bytes) failed: {:#010x}", size, result);
    svcUnmapProcessCodeMemory(self, reinterpret_cast<u64>(canonical),
                              reinterpret_cast<u64>(backing), size);
    virtmemLock();
    virtmemRemoveReservation(reservation);
    virtmemUnlock();
    std::free(backing);
    return false;
  }

  segment.backing = backing;
  segment.canonical = static_cast<u8*>(canonical);
  segment.reservation = reservation;
  segment.size = size;
  return true;
}

// Every alias of these pages must already be gone.
void DestroyAliasableSegment(AliasableSegment& segment)
{
  if (!segment.canonical)
    return;

  const Result result =
      svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<u64>(segment.canonical),
                                reinterpret_cast<u64>(segment.backing), segment.size);
  if (R_FAILED(result))
  {
    // Leaking address space is survivable. Handing back a range the kernel still has mapped is not.
    ERROR_LOG_FMT(MEMMAP, "svcUnmapProcessCodeMemory failed: {:#010x}.",
                  result);
    segment = {};
    return;
  }

  virtmemLock();
  virtmemRemoveReservation(segment.reservation);
  virtmemUnlock();
  std::free(segment.backing);
  segment = {};
}

struct Support
{
  bool arena = false;
  bool read_only_mappings = false;
};

Support DetectSupport()
{
  Support support;

  static constexpr struct
  {
    unsigned number;
    const char* name;
  } REQUIRED_SYSCALLS[] = {
      {0x02, "svcSetMemoryPermission"},  {0x73, "svcSetProcessMemoryPermission"},
      {0x74, "svcMapProcessMemory"},     {0x75, "svcUnmapProcessMemory"},
      {0x77, "svcMapProcessCodeMemory"}, {0x78, "svcUnmapProcessCodeMemory"},
  };
  for (const auto& syscall : REQUIRED_SYSCALLS)
  {
    if (!envIsSyscallHinted(syscall.number))
    {
      WARN_LOG_FMT(MEMMAP, "No fastmem arena available. {} was not hinted to this process.", syscall.name);
      return support;
    }
  }

  if (envGetOwnProcessHandle() == INVALID_HANDLE)
  {
    WARN_LOG_FMT(MEMMAP, "No fastmem arena available. This process has no handle to itself.");
    return support;
  }

  AliasableSegment segment;
  if (!CreateAliasableSegment(segment, HORIZON_PAGE_SIZE))
    return support;
  Common::ScopeGuard segment_guard([&segment] { DestroyAliasableSegment(segment); });

  virtmemLock();
  void* const alias = virtmemFindAslr(HORIZON_PAGE_SIZE, 0);
  ::VirtmemReservation* const reservation =
      alias ? virtmemAddReservation(alias, HORIZON_PAGE_SIZE) : nullptr;
  virtmemUnlock();
  if (!reservation)
  {
    WARN_LOG_FMT(MEMMAP, "No fastmem arena available. Could not reserve a page to alias into.");
    return support;
  }
  Common::ScopeGuard reservation_guard([reservation] {
    virtmemLock();
    virtmemRemoveReservation(reservation);
    virtmemUnlock();
  });

  const Handle self = envGetOwnProcessHandle();
  const u64 source = reinterpret_cast<u64>(segment.canonical);
  Result result = svcMapProcessMemory(alias, self, source, HORIZON_PAGE_SIZE);
  if (R_FAILED(result))
  {
    WARN_LOG_FMT(MEMMAP, "No fastmem arena available. svcMapProcessMemory failed: {:#010x}", result);
    return support;
  }
  Common::ScopeGuard alias_guard(
      [alias, self, source] { svcUnmapProcessMemory(alias, self, source, HORIZON_PAGE_SIZE); });

  // A mapping that succeeded but did not share pages would corrupt guest memory silently, so prove
  // the two views are the same memory before trusting any of this.
  constexpr u32 PATTERN = 0x12345678;
  *static_cast<volatile u32*>(alias) = PATTERN;
  if (*reinterpret_cast<volatile u32*>(segment.canonical) != PATTERN)
  {
    ERROR_LOG_FMT(MEMMAP, "No fastmem arena available. svcMapProcessMemory did not alias the same pages.");
    return support;
  }
  support.arena = true;

  result = svcSetMemoryPermission(alias, HORIZON_PAGE_SIZE, Perm_R);
  if (R_SUCCEEDED(result))
  {
    support.read_only_mappings = true;
    svcSetMemoryPermission(alias, HORIZON_PAGE_SIZE, Perm_Rw);
  }
  else
  {
    WARN_LOG_FMT(MEMMAP,
                 "Read-only fastmem mappings are unavailable. svcSetMemoryPermission on an "
                 "alias failed: {:#010x}",
                 result);
  }

  return support;
}

const Support& GetSupport()
{
  static const Support support = DetectSupport();
  return support;
}
}  // namespace

namespace HorizonFastmem
{
bool IsArenaSupported()
{
  return GetSupport().arena;
}

bool AreReadOnlyMappingsSupported()
{
  return GetSupport().read_only_mappings;
}
}  // namespace HorizonFastmem

MemArena::MemArena() = default;

MemArena::~MemArena()
{
  ReleaseMemoryRegion();
  ReleaseSHMSegment();
}

void MemArena::GrabSHMSegment(size_t size, std::string_view base_name)
{
  ASSERT(!m_shm_segment);

  const size_t aligned_size = AlignUp(size, HORIZON_PAGE_SIZE);

  if (GetSupport().arena)
  {
    AliasableSegment segment;
    if (CreateAliasableSegment(segment, aligned_size))
    {
      m_shm_backing = segment.backing;
      m_shm_segment = segment.canonical;
      m_shm_reservation = segment.reservation;
      m_shm_segment_size = aligned_size;
      std::memset(m_shm_segment, 0, aligned_size);
      return;
    }
  }

  // Without aliasing the segment is just memory and ReserveMemoryRegion() will decline.
  m_shm_segment = static_cast<u8*>(std::aligned_alloc(HORIZON_PAGE_SIZE, aligned_size));
  if (!m_shm_segment)
  {
    NOTICE_LOG_FMT(MEMMAP, "Failed to allocate {} bytes for the guest memory segment",
                   aligned_size);
    return;
  }

  m_shm_segment_size = aligned_size;
  std::memset(m_shm_segment, 0, aligned_size);
}

void MemArena::ReleaseSHMSegment()
{
  if (m_shm_backing)
  {
    AliasableSegment segment{m_shm_backing, m_shm_segment, m_shm_reservation, m_shm_segment_size};
    DestroyAliasableSegment(segment);
    m_shm_backing = nullptr;
    m_shm_reservation = nullptr;
  }
  else
  {
    std::free(m_shm_segment);
  }

  m_shm_segment = nullptr;
  m_shm_segment_size = 0;
}

void* MemArena::CreateView(s64 offset, size_t size)
{
  if (!m_shm_segment || static_cast<size_t>(offset) + size > m_shm_segment_size)
    return nullptr;

  return m_shm_segment + offset;
}

void MemArena::ReleaseView(void* view, size_t size)
{
  // Views are slices of the one allocation released by ReleaseSHMSegment().
}

u8* MemArena::ReserveMemoryRegion(size_t memory_size)
{
  ASSERT(!m_reserved_region);

  if (!m_shm_backing)
  {
    NOTICE_LOG_FMT(MEMMAP, "Fastmem arena is unavailable. The guest memory segment cannot be "
                           "aliased.");
    return nullptr;
  }

  // Libnx bookkeeping for data aborts to keep other allocations out of the window.
  const size_t aligned_size = AlignUp(memory_size, HORIZON_PAGE_SIZE);
  virtmemLock();
  u8* const base = static_cast<u8*>(virtmemFindAslr(aligned_size, 0));
  ::VirtmemReservation* const reservation =
      base ? virtmemAddReservation(base, aligned_size) : nullptr;
  virtmemUnlock();

  if (!reservation)
  {
    NOTICE_LOG_FMT(MEMMAP, "Fastmem arena is unavailable. No {} MiB of contiguous address space.",
                   aligned_size / 0x100000);
    return nullptr;
  }

  m_reserved_region = base;
  m_reserved_region_size = aligned_size;
  m_region_reservation = reservation;
  NOTICE_LOG_FMT(MEMMAP, "Fastmem arena: {} MiB at {}", aligned_size / 0x100000, fmt::ptr(base));
  return base;
}

void MemArena::ReleaseMemoryRegion()
{
  if (!m_reserved_region)
    return;

  const Handle self = envGetOwnProcessHandle();
  for (const HorizonMemoryMapping& mapping : m_mappings)
  {
    svcUnmapProcessMemory(mapping.destination, self, reinterpret_cast<u64>(mapping.source),
                          mapping.size);
  }
  m_mappings.clear();

  virtmemLock();
  virtmemRemoveReservation(m_region_reservation);
  virtmemUnlock();

  m_reserved_region = nullptr;
  m_reserved_region_size = 0;
  m_region_reservation = nullptr;
}

void* MemArena::MapInMemoryRegion(s64 offset, size_t size, void* base, bool writeable)
{
  if (!m_shm_backing || !m_reserved_region)
    return nullptr;

  if (!writeable && !GetSupport().read_only_mappings)
    return nullptr;

  const Handle self = envGetOwnProcessHandle();
  u8* const source = m_shm_segment + offset;

  const Result result = svcMapProcessMemory(base, self, reinterpret_cast<u64>(source), size);
  if (R_FAILED(result))
  {
    ERROR_LOG_FMT(MEMMAP, "svcMapProcessMemory({}, {:#x}) failed: {:#010x}", fmt::ptr(base), size,
                  result);
    return nullptr;
  }

  if (!writeable)
  {
    const Result protect = svcSetMemoryPermission(base, size, Perm_R);
    if (R_FAILED(protect))
    {
      ERROR_LOG_FMT(MEMMAP, "svcSetMemoryPermission({}, {:#x}) failed: {:#010x}", fmt::ptr(base),
                    size, protect);
      svcUnmapProcessMemory(base, self, reinterpret_cast<u64>(source), size);
      return nullptr;
    }
  }

  m_mappings.emplace_back(HorizonMemoryMapping{static_cast<u8*>(base), source, size});
  return base;
}

bool MemArena::ChangeMappingProtection(void* view, size_t size, bool writeable)
{
  if (!GetSupport().read_only_mappings)
    return false;

  const Result result = svcSetMemoryPermission(view, size, writeable ? Perm_Rw : Perm_R);
  if (R_FAILED(result))
  {
    ERROR_LOG_FMT(MEMMAP, "svcSetMemoryPermission({}, {:#x}, {}) failed: {:#010x}", fmt::ptr(view),
                  size, writeable, result);
    return false;
  }

  return true;
}

void MemArena::UnmapFromMemoryRegion(void* view, size_t size)
{
  const auto mapping =
      std::ranges::find_if(m_mappings, [view, size](const HorizonMemoryMapping& candidate) {
        return candidate.destination == view && candidate.size == size;
      });
  if (mapping == m_mappings.end())
  {
    ERROR_LOG_FMT(MEMMAP, "Unmapping {} ({:#x} bytes), which was never mapped.", fmt::ptr(view),
                  size);
    return;
  }

  const Result result = svcUnmapProcessMemory(view, envGetOwnProcessHandle(),
                                              reinterpret_cast<u64>(mapping->source), size);
  if (R_FAILED(result))
  {
    ERROR_LOG_FMT(MEMMAP, "svcUnmapProcessMemory({}, {:#x}) failed: {:#010x}", fmt::ptr(view), size,
                  result);
    return;
  }

  m_mappings.erase(mapping);
}

size_t MemArena::GetPageSize() const
{
  return HORIZON_PAGE_SIZE;
}

LazyMemoryRegion::LazyMemoryRegion() = default;

LazyMemoryRegion::~LazyMemoryRegion()
{
  Release();
}

// The callers ask for regions far larger than the application heap (the JIT block
// cache wants 64 GiB) and rely on a null return to select their small fallback path.
void* LazyMemoryRegion::Create(size_t size)
{
  ASSERT(!m_memory);

  if (size == 0)
    return nullptr;

  const size_t aligned_size = AlignUp(size, HORIZON_PAGE_SIZE);
  void* memory = std::aligned_alloc(HORIZON_PAGE_SIZE, aligned_size);
  if (!memory)
  {
    NOTICE_LOG_FMT(MEMMAP, "Memory allocation of {} bytes failed.", aligned_size);
    return nullptr;
  }

  std::memset(memory, 0, aligned_size);
  m_memory = memory;
  m_size = aligned_size;
  return memory;
}

void LazyMemoryRegion::Clear()
{
  ASSERT(m_memory);
  std::memset(m_memory, 0, m_size);
}

void LazyMemoryRegion::Release()
{
  std::free(m_memory);
  m_memory = nullptr;
  m_size = 0;
}

}  // namespace Common
