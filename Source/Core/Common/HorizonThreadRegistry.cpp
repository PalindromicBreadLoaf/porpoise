// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HorizonThreadRegistry.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#include <switch.h>

#include "Common/Logging/Log.h"

namespace Common::HorizonThreadRegistry
{
namespace
{
std::mutex s_mutex;
std::array<ThreadInfo, MAX_THREADS> s_threads;
std::size_t s_count = 0;

bool IsAlive(Handle handle)
{
  u64 id;
  return R_SUCCEEDED(svcGetThreadId(&id, handle));
}

void CopyName(ThreadInfo& info, const char* name)
{
  info.name.fill('\0');
  std::strncpy(info.name.data(), name, info.name.size() - 1);
}
}  // namespace

void RegisterCurrentThread(const char* name)
{
  const Handle handle = threadGetCurHandle();

  MemoryInfo stack{};
  u32 page_info;
  const u8 local = 0;
  if (R_FAILED(svcQueryMemory(&stack, &page_info, reinterpret_cast<uintptr_t>(&local))))
    stack = {};

  const std::lock_guard lock(s_mutex);

  auto* const end = s_threads.begin() + s_count;
  auto* slot = std::find_if(s_threads.begin(), end,
                            [&](const ThreadInfo& info) { return info.handle == handle; });

  if (slot == end && s_count == s_threads.size())
  {
    slot = std::find_if(s_threads.begin(), end,
                        [](const ThreadInfo& info) { return !IsAlive(info.handle); });
    if (slot == end)
    {
      WARN_LOG_FMT(COMMON, "More than {} live threads; \"{}\" will not be profiled.", MAX_THREADS,
                   name);
      return;
    }
  }
  else if (slot == end)
  {
    ++s_count;
  }

  slot->handle = handle;
  slot->stack_start = stack.addr;
  slot->stack_end = stack.addr + stack.size;
  CopyName(*slot, name);
}

std::size_t Snapshot(std::span<ThreadInfo> out)
{
  const std::lock_guard lock(s_mutex);
  const std::size_t count = std::min(out.size(), s_count);
  std::copy_n(s_threads.begin(), count, out.begin());
  return count;
}
}  // namespace Common::HorizonThreadRegistry
