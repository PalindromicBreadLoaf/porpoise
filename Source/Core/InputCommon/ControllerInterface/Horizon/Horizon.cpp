// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/Horizon/Horizon.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include <switch.h>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Horizon
{
namespace
{
const std::string SOURCE = "Horizon";

constexpr int MAX_PLAYERS = 8;

// The touch panel reports in its own fixed coordinate space regardless of docked status.
constexpr ControlState TOUCH_WIDTH = 1280;
constexpr ControlState TOUCH_HEIGHT = 720;

// HID reports acceleration in g and angular velocity in rotations per second.
// Dolphin's wants m/s^2 and rad/s.
constexpr ControlState GRAVITY = 9.80665;

// Frequencies the Joy-Con LRA is happiest at apparently?
constexpr float VIBRATION_FREQ_LOW = 160.0f;
constexpr float VIBRATION_FREQ_HIGH = 320.0f;

struct ButtonDef
{
  const char* name;
  u64 mask;
};

constexpr std::array BUTTONS{
    ButtonDef{"A", HidNpadButton_A},
    ButtonDef{"B", HidNpadButton_B},
    ButtonDef{"X", HidNpadButton_X},
    ButtonDef{"Y", HidNpadButton_Y},
    ButtonDef{"L", HidNpadButton_L},
    ButtonDef{"R", HidNpadButton_R},
    ButtonDef{"ZL", HidNpadButton_ZL},
    ButtonDef{"ZR", HidNpadButton_ZR},
    ButtonDef{"Plus", HidNpadButton_Plus},
    ButtonDef{"Minus", HidNpadButton_Minus},
    ButtonDef{"Stick L", HidNpadButton_StickL},
    ButtonDef{"Stick R", HidNpadButton_StickR},
    ButtonDef{"Pad Up", HidNpadButton_Up},
    ButtonDef{"Pad Down", HidNpadButton_Down},
    ButtonDef{"Pad Left", HidNpadButton_Left},
    ButtonDef{"Pad Right", HidNpadButton_Right},
    ButtonDef{"SL", HidNpadButton_AnySL},
    ButtonDef{"SR", HidNpadButton_AnySR},
};

// A given Npad reports exactly one style at a time.
constexpr std::array<HidNpadStyleTag, 6> STYLES{
    HidNpadStyleTag_NpadFullKey, HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadJoyDual,
    HidNpadStyleTag_NpadJoyLeft, HidNpadStyleTag_NpadJoyRight, HidNpadStyleTag_NpadGc,
};

bool SupportsTwoVibrationDevices(HidNpadStyleTag style)
{
  return style == HidNpadStyleTag_NpadFullKey || style == HidNpadStyleTag_NpadHandheld ||
         style == HidNpadStyleTag_NpadJoyDual;
}

class Button final : public Core::Device::Input
{
public:
  Button(const char* name, const u64& buttons, u64 mask)
      : m_name(name), m_buttons(buttons), m_mask(mask)
  {
  }

  std::string GetName() const override { return m_name; }
  ControlState GetState() const override { return (m_buttons & m_mask) != 0; }

private:
  const char* const m_name;
  const u64& m_buttons;
  const u64 m_mask;
};

// One half of an axis. scale selects the direction and carries the unit conversion.
class Axis final : public Core::Device::Input
{
public:
  Axis(std::string name, const ControlState& value, ControlState scale, bool detectable = true)
      : m_name(std::move(name)), m_value(value), m_scale(scale), m_detectable(detectable)
  {
  }

  std::string GetName() const override { return m_name; }
  bool IsDetectable() const override { return m_detectable; }
  ControlState GetState() const override { return m_value * m_scale; }

private:
  const std::string m_name;
  const ControlState& m_value;
  const ControlState m_scale;
  const bool m_detectable;
};

class Trigger final : public Core::Device::Input
{
public:
  Trigger(const char* name, const ControlState& value) : m_name(name), m_value(value) {}

  std::string GetName() const override { return m_name; }
  ControlState GetState() const override { return m_value; }

private:
  const char* const m_name;
  const ControlState& m_value;
};

class Motor final : public Core::Device::Output
{
public:
  Motor(const char* name, std::atomic<ControlState>* low, std::atomic<ControlState>* high)
      : m_name(name), m_low(low), m_high(high)
  {
  }

  std::string GetName() const override { return m_name; }

  void SetState(ControlState state) override
  {
    if (m_low)
      m_low->store(state, std::memory_order_relaxed);
    if (m_high)
      m_high->store(state, std::memory_order_relaxed);
  }

private:
  const char* const m_name;
  std::atomic<ControlState>* const m_low;
  std::atomic<ControlState>* const m_high;
};

class NpadDevice final : public Core::Device
{
public:
  explicit NpadDevice(int player_index)
      : m_player_index(player_index),
        m_npad_id(static_cast<HidNpadIdType>(HidNpadIdType_No1 + player_index))
  {
    // The same Joy-Cons switch between the two ids as they are attached and detached.
    u64 mask = 1ULL << m_npad_id;
    if (player_index == 0)
      mask |= 1ULL << HidNpadIdType_Handheld;
    padInitializeWithMask(&m_pad, mask);

    for (const ButtonDef& button : BUTTONS)
      AddInput(new Button(button.name, m_buttons, button.mask));

    AddStick("Left", m_stick[0], m_stick[1]);
    AddStick("Right", m_stick[2], m_stick[3]);

    // Only ::HidNpadStyleTag_NpadGc fills these in.
    AddInput(new Trigger("Trigger L", m_trigger[0]));
    AddInput(new Trigger("Trigger R", m_trigger[1]));

    AddMotionInputs();

    AddOutput(new Motor("Motor", &m_rumble_low, &m_rumble_high));
    AddOutput(new Motor("Motor L", &m_rumble_low, nullptr));
    AddOutput(new Motor("Motor R", nullptr, &m_rumble_high));
  }

  ~NpadDevice() override { ReleaseHandles(); }

  std::string GetName() const override { return fmt::format("Player {}", m_player_index + 1); }
  std::string GetSource() const override { return SOURCE; }

  // Player one has to sort first.
  int GetSortPriority() const override { return -m_player_index; }

  Core::DeviceRemoval UpdateInput() override
  {
    padUpdate(&m_pad);

    m_buttons = padGetButtons(&m_pad);

    const HidAnalogStickState left = padGetStickPos(&m_pad, 0);
    const HidAnalogStickState right = padGetStickPos(&m_pad, 1);
    m_stick[0] = ControlState(left.x) / JOYSTICK_MAX;
    m_stick[1] = ControlState(left.y) / JOYSTICK_MAX;
    m_stick[2] = ControlState(right.x) / JOYSTICK_MAX;
    m_stick[3] = ControlState(right.y) / JOYSTICK_MAX;

    m_trigger[0] = ControlState(padGetGcTriggerPos(&m_pad, 0)) / 0x7fff;
    m_trigger[1] = ControlState(padGetGcTriggerPos(&m_pad, 1)) / 0x7fff;

    RefreshHandles();
    UpdateMotion();
    UpdateRumble();

    return Core::DeviceRemoval::Keep;
  }

private:
  void AddStick(const char* name, ControlState& x, ControlState& y)
  {
    AddFullAnalogSurfaceInputs(new Axis(fmt::format("{} X-", name), x, -1),
                               new Axis(fmt::format("{} X+", name), x, 1));
    AddFullAnalogSurfaceInputs(new Axis(fmt::format("{} Y-", name), y, -1),
                               new Axis(fmt::format("{} Y+", name), y, 1));
  }

  // HID's sensor frame is +x out the front, +y left, +z up. Dolphin wants
  // +x left, +y backward, +z up.
  //
  // The accelerometer is additionally negated as a whole.
  // Without this the emulated Wii Remote is upside down, which is very noticeable in gameplay.
  void AddMotionInputs()
  {
    const auto add = [this](std::string name, const ControlState& value, ControlState scale) {
      AddInput(new Axis(std::move(name), value, scale, false));
    };

    add("Accel Up", m_accel[2], -GRAVITY);
    add("Accel Down", m_accel[2], GRAVITY);
    add("Accel Left", m_accel[1], -GRAVITY);
    add("Accel Right", m_accel[1], GRAVITY);
    add("Accel Forward", m_accel[0], -GRAVITY);
    add("Accel Backward", m_accel[0], GRAVITY);

    add("Gyro Pitch Up", m_gyro[1], -MathUtil::TAU);
    add("Gyro Pitch Down", m_gyro[1], MathUtil::TAU);
    add("Gyro Roll Left", m_gyro[0], -MathUtil::TAU);
    add("Gyro Roll Right", m_gyro[0], MathUtil::TAU);
    add("Gyro Yaw Left", m_gyro[2], MathUtil::TAU);
    add("Gyro Yaw Right", m_gyro[2], -MathUtil::TAU);
  }

  HidNpadIdType ActiveId() const
  {
    return padIsHandheld(&m_pad) ? HidNpadIdType_Handheld : m_npad_id;
  }

  // Docking, undocking and re-attaching Joy-Cons all change the style, and every sixaxis and
  // vibration handle is style-specific.
  void RefreshHandles()
  {
    const HidNpadIdType id = ActiveId();
    const u32 style_set = hidGetNpadStyleSet(id);

    HidNpadStyleTag style = HidNpadStyleTag{};
    for (const HidNpadStyleTag candidate : STYLES)
    {
      if (style_set & candidate)
      {
        style = candidate;
        break;
      }
    }

    if (id == m_handle_id && style == m_handle_style)
      return;

    ReleaseHandles();
    m_handle_id = id;
    m_handle_style = style;
    if (style == HidNpadStyleTag{})
      return;

    // Two sixaxis handles exist only for a split pair, where the right Joy-Con is the one being
    // pointed and waved.
    // TODO: Make the left be primary as an option?
    const s32 sixaxis_count = style == HidNpadStyleTag_NpadJoyDual ? 2 : 1;
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(m_sixaxis.data(), sixaxis_count, id, style)))
    {
      m_sixaxis_count = sixaxis_count;
      m_log_sensor_sample = true;
      for (s32 i = 0; i < sixaxis_count; ++i)
        hidStartSixAxisSensor(m_sixaxis[i]);
    }
    else
    {
      WARN_LOG_FMT(CONTROLLERINTERFACE, "{}: no sixaxis handles for style {:#x}", GetName(),
                   static_cast<u32>(style));
    }

    const s32 vibration_count = SupportsTwoVibrationDevices(style) ? 2 : 1;
    if (R_SUCCEEDED(hidInitializeVibrationDevices(m_vibration.data(), vibration_count, id, style)))
    {
      m_vibration_count = vibration_count;

      HidVibrationDeviceInfo info{};
      m_vibration_is_gc_erm = R_SUCCEEDED(hidGetVibrationDeviceInfo(m_vibration[0], &info)) &&
                              info.type == HidVibrationDeviceType_GcErm;
    }
    else
    {
      WARN_LOG_FMT(CONTROLLERINTERFACE, "{}: no vibration handles for style {:#x}", GetName(),
                   static_cast<u32>(style));
    }
  }

  void ReleaseHandles()
  {
    StopRumble();

    for (s32 i = 0; i < m_sixaxis_count; ++i)
      hidStopSixAxisSensor(m_sixaxis[i]);

    m_sixaxis_count = 0;
    m_vibration_count = 0;
    m_vibration_is_gc_erm = false;
  }

  void UpdateMotion()
  {
    if (m_sixaxis_count == 0)
      return;

    HidSixAxisSensorState state{};
    if (hidGetSixAxisSensorStates(m_sixaxis[m_sixaxis_count - 1], &state, 1) != 1)
      return;

    m_accel[0] = state.acceleration.x;
    m_accel[1] = state.acceleration.y;
    m_accel[2] = state.acceleration.z;
    m_gyro[0] = state.angular_velocity.x;
    m_gyro[1] = state.angular_velocity.y;
    m_gyro[2] = state.angular_velocity.z;

    if (std::exchange(m_log_sensor_sample, false))
    {
      NOTICE_LOG_FMT(CONTROLLERINTERFACE,
                     "{}: sensor sample accel ({:.3f}, {:.3f}, {:.3f}) g, gyro "
                     "({:.3f}, {:.3f}, {:.3f}) rot/s",
                     GetName(), m_accel[0], m_accel[1], m_accel[2], m_gyro[0], m_gyro[1],
                     m_gyro[2]);
    }
  }

  // Outputs are written from the CPU thread as the game asks for rumble.
  void UpdateRumble()
  {
    if (m_vibration_count == 0)
      return;

    const auto low = static_cast<float>(m_rumble_low.load(std::memory_order_relaxed));
    const auto high = static_cast<float>(m_rumble_high.load(std::memory_order_relaxed));
    if (low == m_sent_low && high == m_sent_high)
      return;

    m_sent_low = low;
    m_sent_high = high;

    if (m_vibration_is_gc_erm)
    {
      const HidVibrationGcErmCommand command = (low > 0.5f || high > 0.5f) ?
                                                   HidVibrationGcErmCommand_Start :
                                                   HidVibrationGcErmCommand_Stop;
      hidSendVibrationGcErmCommand(m_vibration[0], command);
      return;
    }

    const HidVibrationValue value{low, VIBRATION_FREQ_LOW, high, VIBRATION_FREQ_HIGH};
    const std::array<HidVibrationValue, 2> values{value, value};
    hidSendVibrationValues(m_vibration.data(), values.data(), m_vibration_count);
  }

  void StopRumble()
  {
    m_rumble_low.store(0, std::memory_order_relaxed);
    m_rumble_high.store(0, std::memory_order_relaxed);

    // Force the next UpdateRumble to send so a device that is torn down mid-buzz goes quiet.
    // Or I could make it be funny and have it just lose it's shit until the battery dies.
    m_sent_low = -1;
    m_sent_high = -1;
    UpdateRumble();
  }

  const int m_player_index;
  const HidNpadIdType m_npad_id;

  PadState m_pad{};

  u64 m_buttons = 0;
  std::array<ControlState, 4> m_stick{};
  std::array<ControlState, 2> m_trigger{};
  std::array<ControlState, 3> m_accel{};
  std::array<ControlState, 3> m_gyro{};

  std::atomic<ControlState> m_rumble_low = 0;
  std::atomic<ControlState> m_rumble_high = 0;
  float m_sent_low = 0;
  float m_sent_high = 0;

  HidNpadIdType m_handle_id = HidNpadIdType_Other;
  HidNpadStyleTag m_handle_style{};
  std::array<HidSixAxisSensorHandle, 2> m_sixaxis{};
  std::array<HidVibrationDeviceHandle, 2> m_vibration{};
  s32 m_sixaxis_count = 0;
  s32 m_vibration_count = 0;
  bool m_vibration_is_gc_erm = false;
  bool m_log_sensor_sample = false;
};

// The panel belongs to the console rather than to any player.
class TouchDevice final : public Core::Device
{
public:
  TouchDevice()
  {
    AddInput(new Axis("Touch X-", m_x, -1, false));
    AddInput(new Axis("Touch X+", m_x, 1, false));
    AddInput(new Axis("Touch Y-", m_y, -1, false));
    AddInput(new Axis("Touch Y+", m_y, 1, false));
    AddInput(new Trigger("Touch", m_pressed));
  }

  std::string GetName() const override { return "Touchscreen"; }
  std::string GetSource() const override { return SOURCE; }
  int GetSortPriority() const override { return -MAX_PLAYERS; }

  Core::DeviceRemoval UpdateInput() override
  {
    HidTouchScreenState state{};
    if (hidGetTouchScreenStates(&state, 1) != 1)
      return Core::DeviceRemoval::Keep;

    m_pressed = state.count > 0;

    // Leave cursor position where it is rather than snap back to centre.
    if (state.count > 0)
    {
      m_x = state.touches[0].x / TOUCH_WIDTH * 2 - 1;
      m_y = state.touches[0].y / TOUCH_HEIGHT * 2 - 1;
    }

    return Core::DeviceRemoval::Keep;
  }

private:
  ControlState m_x = 0;
  ControlState m_y = 0;
  ControlState m_pressed = 0;
};

class InputBackend final : public ciface::InputBackend
{
public:
  explicit InputBackend(ControllerInterface* controller_interface)
      : ciface::InputBackend(controller_interface)
  {
    hidInitializeTouchScreen();
  }

  void PopulateDevices() override
  {
    // Every slot gets a device whether or not anything is attached to it.
    for (int i = 0; i < MAX_PLAYERS; ++i)
      GetControllerInterface().AddDevice(std::make_shared<NpadDevice>(i));

    GetControllerInterface().AddDevice(std::make_shared<TouchDevice>());
  }
};
}  // namespace

std::unique_ptr<ciface::InputBackend> CreateInputBackend(ControllerInterface* controller_interface)
{
  return std::make_unique<InputBackend>(controller_interface);
}
}  // namespace ciface::Horizon
