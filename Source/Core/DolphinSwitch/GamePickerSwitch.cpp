// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// This is the world's most basic game picker just to be able to start something for testing.

#include "DolphinSwitch/GamePickerSwitch.h"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <vector>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"
#include "UICommon/GameFileCache.h"

namespace GamePicker
{
namespace
{
// The libnx console is 80x45 at either resolution.
constexpr size_t VISIBLE_ROWS = 38;
constexpr size_t MAX_NAME_WIDTH = 76;

std::vector<std::string> ScanGames()
{
  const std::string directory = GetRomDirectory();
  const std::string_view directory_view = directory;
  std::vector<std::string> paths = UICommon::FindAllGamePaths({&directory_view, 1}, true);
  std::ranges::sort(paths);
  return paths;
}

void Draw(const std::vector<std::string>& games, size_t selected, size_t scroll,
          const std::string& notice)
{
  std::printf("\x1b[2J\x1b[H");
  std::printf("porpoise %s\n%s\n", Common::GetScmDescStr().c_str(), GetRomDirectory().c_str());
  std::printf("%s\n", notice.c_str());

  if (games.empty())
  {
    std::printf("  Nothing here. Copy GameCube or Wii images into the folder above.\n");
  }
  else
  {
    for (size_t i = scroll; i < std::min(scroll + VISIBLE_ROWS, games.size()); ++i)
    {
      std::string name = PathToFileName(games[i]);
      if (name.size() > MAX_NAME_WIDTH)
        name.resize(MAX_NAME_WIDTH);

      std::printf("%s%s\n", i == selected ? "> " : "  ", name.c_str());
    }
  }

  std::printf("\n[A] boot   [X] rescan   [+] exit\n");
  consoleUpdate(nullptr);
}

// Keeps the cursor on the list and the window around the cursor.
void Clamp(const std::vector<std::string>& games, size_t& selected, size_t& scroll)
{
  if (games.empty())
  {
    selected = 0;
    scroll = 0;
    return;
  }

  selected = std::min(selected, games.size() - 1);
  scroll = std::min(scroll, selected);
  if (selected >= scroll + VISIBLE_ROWS)
    scroll = selected - VISIBLE_ROWS + 1;
}
}  // namespace

std::string GetRomDirectory()
{
  return File::GetUserPath(D_USER_IDX) + "roms" DIR_SEP;
}

std::string Run(PadState& pad, const std::string& notice)
{
  std::vector<std::string> games = ScanGames();
  size_t selected = 0;
  size_t scroll = 0;
  bool dirty = true;

  while (appletMainLoop())
  {
    if (dirty)
    {
      Clamp(games, selected, scroll);
      Draw(games, selected, scroll, notice);
      dirty = false;
    }

    padUpdate(&pad);
    const u64 pressed = padGetButtonsDown(&pad);

    if (pressed & HidNpadButton_Plus)
      return {};

    if (pressed & HidNpadButton_X)
    {
      games = ScanGames();
      dirty = true;
    }

    if (games.empty())
    {
      svcSleepThread(16'000'000);
      continue;
    }

    if ((pressed & (HidNpadButton_Up | HidNpadButton_StickLUp)) && selected > 0)
    {
      --selected;
      dirty = true;
    }
    if ((pressed & (HidNpadButton_Down | HidNpadButton_StickLDown)) && selected + 1 < games.size())
    {
      ++selected;
      dirty = true;
    }
    if (pressed & HidNpadButton_L)
    {
      selected -= std::min(selected, VISIBLE_ROWS);
      dirty = true;
    }
    if (pressed & HidNpadButton_R)
    {
      selected = std::min(selected + VISIBLE_ROWS, games.size() - 1);
      dirty = true;
    }

    if (pressed & HidNpadButton_A)
      return games[selected];

    svcSleepThread(16'000'000);
  }

  return {};
}
}  // namespace GamePicker
