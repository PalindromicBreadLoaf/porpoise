// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "Common/CommonTypes.h"

namespace Common::HorizonThreadRegistry
{
constexpr std::size_t MAX_THREADS = 32;
constexpr std::size_t MAX_NAME = 40;

struct ThreadInfo
{
  u32 handle = 0;
  uintptr_t stack_start = 0;
  uintptr_t stack_end = 0;
  std::array<char, MAX_NAME> name{};
};

void RegisterCurrentThread(const char* name);

std::size_t Snapshot(std::span<ThreadInfo> out);
}  // namespace Common::HorizonThreadRegistry

#endif
