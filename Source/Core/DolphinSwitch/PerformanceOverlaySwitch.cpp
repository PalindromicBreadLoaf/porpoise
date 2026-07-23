// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/PerformanceOverlaySwitch.h"

#include <array>

#include "Common/Config/Config.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoCommon/OnScreenDisplay.h"

namespace PerfOverlay
{
namespace
{
enum Level : int
{
  Off = 0,
  Stats = 1,
  StatsAndGraphs = 2,
  Count = 3,
};

// The overlay is on by default.
int s_level = Level::Stats;

constexpr std::array<const char*, Level::Count> LEVEL_NAMES = {
    "Performance overlay: off",
    "Performance overlay: stats",
    "Performance overlay: stats + graphs",
};

void Apply(int level)
{
  const bool stats = level >= Level::Stats;
  const bool graphs = level >= Level::StatsAndGraphs;

  Config::SetCurrent(Config::GFX_SHOW_FPS, stats);
  Config::SetCurrent(Config::GFX_SHOW_FTIMES, stats);
  Config::SetCurrent(Config::GFX_SHOW_VPS, stats);
  Config::SetCurrent(Config::GFX_SHOW_VTIMES, stats);
  Config::SetCurrent(Config::GFX_SHOW_SPEED, stats);
  Config::SetCurrent(Config::GFX_SHOW_SPEED_COLORS, stats);
  Config::SetCurrent(Config::GFX_SHOW_INTERNAL_RESOLUTION, stats);
  Config::SetCurrent(Config::GFX_SHOW_GRAPHS, graphs);
}
}  // namespace

void ApplyCurrentLevel()
{
  Apply(s_level);
}

void CycleLevel()
{
  s_level = (s_level + 1) % Level::Count;
  Apply(s_level);
  OSD::AddMessage(LEVEL_NAMES[s_level]);
}
}  // namespace PerfOverlay
