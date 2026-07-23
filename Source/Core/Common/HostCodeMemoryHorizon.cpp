// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HostCodeMemory.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include <switch.h>

#include "Common/Align.h"
#include "Common/Logging/Log.h"

namespace Common::HostCodeMemory
{
std::ptrdiff_t g_rw_delta = 0;

namespace
{
// JitArm64 takes 48 MiB of this and the rest goes to VertexLoaderARM64 at 4 KiB per loader.
// Only the writable half is real memory.
constexpr std::size_t ARENA_SIZE = 64 * 1024 * 1024;

constexpr std::size_t BLOCK_ALIGN = 0x1000;

// A span of the arena. Blocks are kept sorted by offset and tile the arena with no gaps.
struct Block
{
  std::size_t offset;
  std::size_t size;
  bool free;
};

std::mutex s_mutex;
Jit s_jit{};
u8* s_rx = nullptr;
std::vector<Block> s_blocks;

}  // namespace

bool Init()
{
  std::lock_guard lock{s_mutex};
  if (s_rx)
    return true;

  const Result result = jitCreate(&s_jit, ARENA_SIZE);
  if (R_FAILED(result))
  {
    WARN_LOG_FMT(COMMON, "jitCreate({}) failed: {:#010x}. Host code memory is unavailable.",
                 ARENA_SIZE, result);
    return false;
  }

  u8* const rw = static_cast<u8*>(jitGetRwAddr(&s_jit));
  u8* const rx = static_cast<u8*>(jitGetRxAddr(&s_jit));

  // JitType_SetProcessMemoryPermission gives one mapping whose permissions flip between W and X
  // for the whole process. Dolphin emits vertex loaders on the video thread while the CPU thread
  // is running JIT code, so flipping the arena to writable would pull the ground out from under
  // another thread.
  if (rw == rx)
  {
    ERROR_LOG_FMT(COMMON, "Host code memory is permission-toggled. "
                          "This cannot be used safely from more than one thread.");
    jitClose(&s_jit);
    return false;
  }

  if (R_FAILED(jitTransitionToExecutable(&s_jit)))
  {
    ERROR_LOG_FMT(COMMON, "jitTransitionToExecutable failed.");
    jitClose(&s_jit);
    return false;
  }

  s_rx = rx;
  g_rw_delta = rw - rx;
  s_blocks.assign(1, Block{0, ARENA_SIZE, true});

  NOTICE_LOG_FMT(COMMON, "Host code memory: {} MiB at {} (rw {}), delta {:#x}",
                 ARENA_SIZE / 0x100000, fmt::ptr(rx), fmt::ptr(rw), g_rw_delta);
  return true;
}

void Shutdown()
{
  std::lock_guard lock{s_mutex};
  if (!s_rx)
    return;

  jitClose(&s_jit);
  s_rx = nullptr;
  g_rw_delta = 0;
  s_blocks.clear();
}

bool IsAvailable()
{
  std::lock_guard lock{s_mutex};
  return s_rx != nullptr;
}

u8* Allocate(std::size_t size)
{
  if (size == 0)
    return nullptr;

  const std::size_t needed = AlignUp(size, BLOCK_ALIGN);

  std::lock_guard lock{s_mutex};
  if (!s_rx)
    return nullptr;

  for (std::size_t i = 0; i < s_blocks.size(); ++i)
  {
    Block& block = s_blocks[i];
    if (!block.free || block.size < needed)
      continue;

    const std::size_t offset = block.offset;
    if (block.size > needed)
    {
      const Block remainder{offset + needed, block.size - needed, true};
      block.size = needed;
      block.free = false;
      s_blocks.insert(s_blocks.begin() + i + 1, remainder);
    }
    else
    {
      block.free = false;
    }
    return s_rx + offset;
  }

  ERROR_LOG_FMT(COMMON, "Host code memory exhausted with {} bytes requested.", size);
  return nullptr;
}

void Free(u8* ptr)
{
  if (!ptr)
    return;

  std::lock_guard lock{s_mutex};
  if (!s_rx)
    return;

  const std::size_t offset = static_cast<std::size_t>(ptr - s_rx);
  auto it =
      std::lower_bound(s_blocks.begin(), s_blocks.end(), offset,
                       [](const Block& block, std::size_t off) { return block.offset < off; });
  if (it == s_blocks.end() || it->offset != offset || it->free)
  {
    ERROR_LOG_FMT(COMMON, "Freeing host code memory at {} that was never allocated.",
                  fmt::ptr(ptr));
    return;
  }

  it->free = true;
  const auto next = it + 1;
  if (next != s_blocks.end() && next->free)
  {
    it->size += next->size;
    s_blocks.erase(next);
  }
  if (it != s_blocks.begin() && (it - 1)->free)
  {
    (it - 1)->size += it->size;
    s_blocks.erase(it);
  }
}

void FlushCode(const u8* rx_start, std::size_t size)
{
  if (size == 0)
    return;

  armDCacheFlush(WritableAlias(rx_start), size);
  armICacheInvalidate(const_cast<u8*>(rx_start), size);
}

}  // namespace Common::HostCodeMemory
