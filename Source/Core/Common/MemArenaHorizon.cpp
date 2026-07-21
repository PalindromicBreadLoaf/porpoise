// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/MemArena.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace Common
{
// Horizon exposes no primitive that maps one physical allocation into several address ranges from
// user mode without going through svcMapMemory and the kernel's fixed alias region. Until that is
// worked out (IMPLEMENTATION.md Phase 7.2), the "segment" is one flat allocation and every view is
// a slice of it. Guest memory mirrors are then not host-aliased, so all three fastmem entry points
// below fail and Memmap falls back to MMU.cpp for every access.
constexpr size_t HORIZON_PAGE_SIZE = 0x1000;

MemArena::MemArena() = default;

MemArena::~MemArena()
{
  ReleaseSHMSegment();
}

void MemArena::GrabSHMSegment(size_t size, std::string_view base_name)
{
  ASSERT(!m_shm_segment);

  // aligned_alloc requires the size to be a multiple of the alignment.
  const size_t aligned_size = AlignUp(size, HORIZON_PAGE_SIZE);
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
  std::free(m_shm_segment);
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
  NOTICE_LOG_FMT(MEMMAP, "Fastmem arena is unavailable on Horizon; falling back to the MMU");
  return nullptr;
}

void MemArena::ReleaseMemoryRegion()
{
}

void* MemArena::MapInMemoryRegion(s64 offset, size_t size, void* base, bool writeable)
{
  return nullptr;
}

bool MemArena::ChangeMappingProtection(void* view, size_t size, bool writeable)
{
  return false;
}

void MemArena::UnmapFromMemoryRegion(void* view, size_t size)
{
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

// Not lazy at all: the callers ask for regions far larger than the application heap (the JIT block
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
