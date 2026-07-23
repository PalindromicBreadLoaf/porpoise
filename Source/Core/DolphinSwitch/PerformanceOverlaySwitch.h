// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Drives the on-screen performance overlay through the GFX_SHOW_* toggles the imgui OSD already
// reads.
namespace PerfOverlay
{
// Pushes the current level into the graphics config. Call once per boot.
void ApplyCurrentLevel();

// Advances one level (off, stats, stats + graphs, off) and applies it.
void CycleLevel();
}  // namespace PerfOverlay
