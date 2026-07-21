// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>

#include <switch.h>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"
#include "Common/MsgHandler.h"
#include "Common/Thread.h"
#include "Common/Version.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/VideoBackendBase.h"

namespace
{
// Verify a proper application is used to launch the emulator via number of CPUs and RAM amount.
bool RunningAsApplication()
{
  switch (appletGetAppletType())
  {
  case AppletType_None:
  case AppletType_SystemApplet:
  case AppletType_LibraryApplet:
  case AppletType_OverlayApplet:
    return false;
  default:
    break;
  }

  u64 total_memory = 0;
  if (R_FAILED(svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
    return false;

  return total_memory >= 1024ull * 1024 * 1024;
}

// System/environment information.
void LogHostEnvironment()
{
  const u32 core_mask = Common::GetAvailableCoreMask();

  u64 total_memory = 0;
  u64 used_memory = 0;
  svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used_memory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

  NOTICE_LOG_FMT(COMMON, "porpoise {}", Common::GetScmDescStr());
  NOTICE_LOG_FMT(COMMON, "Applet type {}, core mask {:#06b}, heap {} MiB used of {} MiB",
                 static_cast<int>(appletGetAppletType()), core_mask, used_memory / 0x100000,
                 total_memory / 0x100000);
  NOTICE_LOG_FMT(COMMON, "User directory {}", File::GetUserPath(D_USER_IDX));
  NOTICE_LOG_FMT(COMMON, "Sys directory {}", File::GetSysDirectory());
}

// Build the system to verify everything works.
void ExerciseCore()
{
  NOTICE_LOG_FMT(COMMON, "Constructing Core::System...");
  auto& system = Core::System::GetInstance();
  system.Initialize();
  NOTICE_LOG_FMT(COMMON, "Core::System constructed.");

  auto& memory = system.GetMemory();
  memory.Init();
  NOTICE_LOG_FMT(COMMON, "Guest memory up: {} MiB RAM, fastmem {}", memory.GetRamSize() / 0x100000,
                 memory.InitFastmemArena() ? "arena" : "off");
  memory.ShutdownFastmemArena();
  memory.Shutdown();
  NOTICE_LOG_FMT(COMMON, "Guest memory torn down.");
}
}  // namespace

int main(int argc, char* argv[])
{
  consoleInit(nullptr);

  const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
  if (have_socket)
    nxlinkStdio();

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);

  std::printf("porpoise %s\n\n", Common::GetScmDescStr().c_str());

  if (!RunningAsApplication())
  {
    std::printf("WARNING: not running as an application.\n"
                "Horizon will not grant JIT capability in applet mode, and the emulator will be\n"
                "unusably slow. Launch Porpoise by holding R while starting a game.\n\n");
  }

  UICommon::SetUserDirectory("");
  UICommon::CreateDirectories();
  UICommon::Init();

  LogHostEnvironment();
  ExerciseCore();

  std::printf("Log written to %s\n", File::GetUserPath(F_MAINLOG_IDX).c_str());
  std::printf("\nVideo backends:");
  for (const auto& backend : VideoBackendBase::GetAvailableBackends())
    std::printf(" %s", backend->GetDisplayName().c_str());

  // TODO: Replaces the wait-for-+ loop with a real Platform run loop.
  std::printf("\n\nPress + to exit.\n");

  while (appletMainLoop())
  {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
      break;

    consoleUpdate(nullptr);
  }

  UICommon::Shutdown();

  if (have_socket)
    socketExit();

  consoleExit(nullptr);
  return 0;
}
