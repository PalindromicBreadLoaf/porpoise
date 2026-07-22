// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/PlatformSwitch.h"

#include "Common/Logging/Log.h"
#include "Core/Core.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/STM/STM.h"
#include "Core/System.h"
#include "VideoCommon/Present.h"

namespace
{
// This thread only pumps host messages, so anything tighter than a frame is wasted time.
constexpr u64 HOST_POLL_INTERVAL_NS = 16'000'000;
}  // namespace

PlatformSwitch::PlatformSwitch(PadState& pad) : m_pad(pad)
{
}

bool PlatformSwitch::Init()
{
  m_window = nwindowGetDefault();
  m_operation_mode = appletGetOperationMode();
  return m_window != nullptr;
}

void PlatformSwitch::MainLoop()
{
  auto& system = Core::System::GetInstance();

  while (m_running.IsSet())
  {
    // Goes false once Horizon has asked us to quit, and stays false, so the second pass through
    // UpdateRunningFlag is what actually stops the loop.
    if (!appletMainLoop())
      RequestShutdown();

    m_focused.store(appletGetFocusState() == AppletFocusState_InFocus, std::memory_order_relaxed);

    PollHostInput();
    PollOperationMode();
    UpdateRunningFlag();
    Core::HostDispatchJobs(system);

    svcSleepThread(HOST_POLL_INTERVAL_NS);
  }
}

void PlatformSwitch::PollHostInput()
{
  padUpdate(&m_pad);

  // TODO: Replace this chord with the in-game overlay menu once there is one to open.
  constexpr u64 exit_chord = HidNpadButton_Plus | HidNpadButton_Minus;
  if ((padGetButtons(&m_pad) & exit_chord) == exit_chord)
    RequestShutdown();
}

void PlatformSwitch::PollOperationMode()
{
  const AppletOperationMode mode = appletGetOperationMode();
  if (mode == m_operation_mode)
    return;

  m_operation_mode = mode;

  // The window can only be resized while no buffers are registered with it, so the size the
  // backend picks up is set from inside the swapchain recreate this triggers.
  if (g_presenter)
    g_presenter->ResizeSurface();
}

void PlatformSwitch::UpdateRunningFlag()
{
  if (!m_shutdown_requested.TestAndClear())
    return;

  const auto& system = Core::System::GetInstance();
  const auto ios = system.GetIOS();
  const auto stm = ios ? ios->GetDeviceByName("/dev/stm/eventhook") : nullptr;
  if (!m_tried_graceful_shutdown.IsSet() && stm &&
      std::static_pointer_cast<IOS::HLE::STMEventHookDevice>(stm)->HasHookInstalled())
  {
    system.GetProcessorInterface().PowerButton_Tap();
    m_tried_graceful_shutdown.Set();
  }
  else
  {
    m_running.Clear();
  }
}

void PlatformSwitch::SetTitle(const std::string& title)
{
  INFO_LOG_FMT(COMMON, "{}", title);

  // The title carries the performance counters, and with no picture on screen, it is the only
  // evidence the core is still executing.
  const auto now = std::chrono::steady_clock::now();
  if (now - m_last_heartbeat < HEARTBEAT_INTERVAL)
    return;

  m_last_heartbeat = now;
  NOTICE_LOG_FMT(COMMON, "{}", title);
}

WindowSystemInfo PlatformSwitch::GetWindowSystemInfo() const
{
  WindowSystemInfo wsi;
  wsi.type = WindowSystemType::Horizon;
  wsi.render_window = m_window;
  wsi.render_surface = m_window;
  return wsi;
}

void PlatformSwitch::Stop()
{
  m_running.Clear();
}

void PlatformSwitch::RequestShutdown()
{
  m_shutdown_requested.Set();
}
