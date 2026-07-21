// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>

#include <switch.h>

#include "Common/Version.h"
#include "VideoCommon/VideoBackendBase.h"

namespace
{
// Force non-applet mode.
bool RunningAsApplication()
{
  const AppletType type = appletGetAppletType();
  return type == AppletType_Application || type == AppletType_SystemApplication;
}
}  // namespace

int main(int argc, char* argv[])
{
  consoleInit(nullptr);

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);

  std::printf("porpoise %s\n\n", Common::GetScmDescStr().c_str());

  if (RunningAsApplication())
  {
    std::printf("Running as an application. JIT is available.\n");
  }
  else
  {
    std::printf("WARNING: not running as an application.\n"
                "Horizon will not grant JIT capability in applet mode, and the emulator will be\n"
                "unusably slow. Launch Porpoise by holding R while starting a game.\n");
  }

  // Nothing starts the core yet.
  // TODO: ^
  std::printf("\nVideo backends:");
  for (const auto& backend : VideoBackendBase::GetAvailableBackends())
    std::printf(" %s", backend->GetDisplayName().c_str());

  std::printf("\n\nHi! You aren't supposed to be here. Press + to exit.\n");

  while (appletMainLoop())
  {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
      break;

    consoleUpdate(nullptr);
  }

  consoleExit(nullptr);
  return 0;
}
