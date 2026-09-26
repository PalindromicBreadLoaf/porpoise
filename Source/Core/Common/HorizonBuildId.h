// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include <array>

extern "C" void _start();

namespace Common::HorizonBuildId
{
constexpr size_t BUILD_ID_SIZE = 20;
constexpr size_t NRO_HEADER_BUILD_ID_OFFSET = 0x40;

inline std::array<char, BUILD_ID_SIZE * 2 + 1> GetHex()
{
  static constexpr char DIGITS[] = "0123456789abcdef";
  const auto* id = reinterpret_cast<const unsigned char*>(&_start) + NRO_HEADER_BUILD_ID_OFFSET;

  std::array<char, BUILD_ID_SIZE * 2 + 1> hex{};
  for (size_t i = 0; i < BUILD_ID_SIZE; ++i)
  {
    hex[i * 2] = DIGITS[id[i] >> 4];
    hex[i * 2 + 1] = DIGITS[id[i] & 0xf];
  }
  return hex;
}
}  // namespace Common::HorizonBuildId

#endif
