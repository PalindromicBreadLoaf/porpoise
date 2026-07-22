// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include <switch.h>

namespace GamePicker
{
// Where games are looked for.
std::string GetRomDirectory();

// Draws to the libnx console and blocks until the user picks a game or asks to quit.
std::string Run(PadState& pad, const std::string& notice);
}  // namespace GamePicker
