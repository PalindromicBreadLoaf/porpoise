// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#if defined(HAVE_AUDOUT) && HAVE_AUDOUT
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <switch.h>
#endif

#include "AudioCommon/SoundStream.h"
#include "Common/CommonTypes.h"

class HorizonSoundStream final : public SoundStream
{
#if defined(HAVE_AUDOUT) && HAVE_AUDOUT
public:
  ~HorizonSoundStream() override;

  bool Init() override;
  bool SetRunning(bool running) override;
  void SetVolume(int volume) override;

  static bool IsValid() { return true; }

private:
  static constexpr u32 CHANNEL_COUNT = 2;

  // audout takes buffers whose address and size are both 0x1000-aligned, which puts the floor at
  // 1024 stereo frames of 16-bit samples.
  static constexpr u32 BUFFER_FRAMES = 1024;
  static constexpr std::size_t BUFFER_BYTES = BUFFER_FRAMES * CHANNEL_COUNT * sizeof(s16);
  static constexpr std::size_t BUFFER_COUNT = 4;
  static_assert(BUFFER_BYTES % 0x1000 == 0);

  enum class ThreadStatus
  {
    RUNNING,
    PAUSED,
    STOPPING,
    STOPPED,
  };

  void SoundLoop();
  bool StartOutput();
  void StopOutput();
  void FillBuffer(AudioOutBuffer& buffer);

  std::array<AudioOutBuffer, BUFFER_COUNT> m_buffers{};
  std::thread m_thread;
  std::atomic<ThreadStatus> m_thread_status{ThreadStatus::STOPPED};
  std::condition_variable m_cv;
  std::mutex m_cv_mutex;

  bool m_audout_initialized = false;

  bool m_output_started = false;
  u64 m_drain_count = 0;
#endif
};
