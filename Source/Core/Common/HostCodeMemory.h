// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include <cstddef>

#include "Common/CommonTypes.h"

// Horizon enforces W^X, so a page can never be writable and executable at once. libnx hands back
// two mappings of one physical allocation to work around this, one writable, one executableble one.
namespace Common::HostCodeMemory
{
// Distance from an address in the executable mapping to the same byte in the writable one.
extern std::ptrdiff_t g_rw_delta;

inline u8* WritableAlias(const u8* ptr)
{
  return const_cast<u8*>(ptr) + g_rw_delta;
}

// Reserves the arena. Fails when the process was launched without JIT privileges.
bool Init();
void Shutdown();
bool IsAvailable();

// Both return nullptr / do nothing when the arena is unavailable or exhausted.
u8* Allocate(std::size_t size);
void Free(u8* ptr);

// Makes newly written instructions visible to the fetch side.
void FlushCode(const u8* rx_start, std::size_t size);
}  // namespace Common::HostCodeMemory

#endif
