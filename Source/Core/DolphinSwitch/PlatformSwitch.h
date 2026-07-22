// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <switch.h>

#include "Common/Flag.h"
#include "Common/WindowSystemInfo.h"

// Owns the libnx window and the host-side run loop, in the role DolphinNoGUI's Platform plays.
class PlatformSwitch
{
public:
  explicit PlatformSwitch(PadState& pad);

  bool Init();
  void MainLoop();

  void SetTitle(const std::string& title);
  WindowSystemInfo GetWindowSystemInfo() const;

  bool IsWindowFocused() const { return m_focused.load(std::memory_order_relaxed); }

  // Shuts down the way the console's power button would.
  void RequestShutdown();
  // Drops out of the run loop without asking the guest.
  void Stop();

private:
  void UpdateRunningFlag();
  void PollHostInput();

  PadState& m_pad;
  NWindow* m_window = nullptr;

  Common::Flag m_running{true};
  Common::Flag m_shutdown_requested{false};
  Common::Flag m_tried_graceful_shutdown{false};

  // Read by Host_RendererHasFocus from the CPU and GPU threads.
  std::atomic<bool> m_focused{true};

  static constexpr auto HEARTBEAT_INTERVAL = std::chrono::seconds(5);
  std::chrono::steady_clock::time_point m_last_heartbeat{};
};

// Non-null only while a game is running.
extern std::unique_ptr<PlatformSwitch> g_platform;
