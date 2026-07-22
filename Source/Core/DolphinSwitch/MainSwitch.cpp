// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <memory>
#include <string>

#include <switch.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/Thread.h"
#include "Common/Version.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "DolphinSwitch/GamePickerSwitch.h"
#include "DolphinSwitch/PlatformSwitch.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

std::unique_ptr<PlatformSwitch> g_platform;

namespace
{
// Applet mode is granted neither JIT capability nor enough memory to hold guest RAM, so it is
// worth raising an error on.
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
  NOTICE_LOG_FMT(COMMON, "Video backend {}", VideoBackendBase::GetDefaultBackendDisplayName());
}

std::string RunGame(PadState& pad, const std::string& path)
{
  auto boot = BootParameters::GenerateFromFile(path, BootSessionData{});
  if (!boot)
  {
    ERROR_LOG_FMT(BOOT, "Could not read {}", path);
    return "Not a readable disc image.";
  }

  g_platform = std::make_unique<PlatformSwitch>(pad);
  Common::ScopeGuard platform_guard([] { g_platform.reset(); });

  if (!g_platform->Init())
  {
    ERROR_LOG_FMT(BOOT, "Could not acquire the window.");
    return "Could not acquire the window.";
  }

  const WindowSystemInfo wsi = g_platform->GetWindowSystemInfo();
  UICommon::InitControllers(wsi);
  Common::ScopeGuard controller_guard([] { UICommon::ShutdownControllers(); });

  // The core tearing itself down (a guest reset, or some sort of error) has to
  // break the run loop too.
  auto state_hook = Core::AddOnStateChangedCallback([](const Core::State state) {
    if (state == Core::State::Uninitialized && g_platform)
      g_platform->Stop();
  });

  auto& system = Core::System::GetInstance();
  if (!BootManager::BootCore(system, std::move(boot), wsi))
  {
    ERROR_LOG_FMT(BOOT, "Could not boot {}", path);
    return "Could not boot. Check the log.";
  }

  NOTICE_LOG_FMT(BOOT, "Booted {}", path);
  g_platform->MainLoop();

  Core::Stop(system);
  Core::Shutdown(system);
  NOTICE_LOG_FMT(BOOT, "Core shut down.");
  return {};
}
}  // namespace

int main(int argc, char* argv[])
{
  consoleInit(nullptr);
  Common::ScopeGuard console_guard([] { consoleExit(nullptr); });

  const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
  if (have_socket)
    nxlinkStdio();
  Common::ScopeGuard socket_guard([have_socket] {
    if (have_socket)
      socketExit();
  });

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  // Nothing is logged until UICommon::Init brings up LogManager, so this is the only marker if
  // something below goes wrong early.
  std::printf("porpoise %s\n", Common::GetScmDescStr().c_str());

  UICommon::SetUserDirectory("");
  UICommon::CreateDirectories();
  File::CreateFullPath(GamePicker::GetRomDirectory());
  UICommon::Init();
  Common::ScopeGuard ui_common_guard([] { UICommon::Shutdown(); });

  // DefaultCPUCore() is JITARM64 on AArch64, and AllocateExecutableMemory hands back nullptr here.
  // CachedInterpreter's code block is declared non-executable, so it allocates through
  // AllocateMemoryPages and runs as-is.
  // TODO: drop this override once the emitters can target host executable memory.
  Config::SetCurrent(Config::MAIN_CPU_CORE, PowerPC::CPUCore::CachedInterpreter);

  // The block cache's large entry point map wants 64 GiB of address space, which is twenty times
  // the application heap. There's no point even trying.
  Config::SetCurrent(Config::MAIN_LARGE_ENTRY_POINTS_MAP, false);

  // TODO: The PowerPC core is not the only thing that emits host code. VertexLoaderARM64 takes
  // AllocCodeSpace's null region and poisons it on construction, which faults on address 0 at
  // the first draw.
  Config::SetCurrent(Config::GFX_VERTEX_LOADER_TYPE, VertexLoaderType::Software);

  LogHostEnvironment();

  std::string pending = argc > 1 && argv[1] != nullptr ? argv[1] : std::string{};
  std::string notice;

  if (!RunningAsApplication())
  {
    ERROR_LOG_FMT(COMMON, "Not running as an application. Horizon grants JIT capability and the "
                          "full heap only in application mode.");
    notice = "WARNING: applet mode. Relaunch by holding R while starting a game.";
  }

  while (appletMainLoop())
  {
    std::string path = std::move(pending);
    pending.clear();

    if (path.empty())
      path = GamePicker::Run(pad, notice);
    if (path.empty())
      break;

    notice = RunGame(pad, path);
  }

  return 0;
}
