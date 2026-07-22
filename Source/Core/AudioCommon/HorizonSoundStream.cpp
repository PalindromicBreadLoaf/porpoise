// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#if defined(HAVE_AUDOUT) && HAVE_AUDOUT
#include "AudioCommon/HorizonSoundStream.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Common/Logging/Log.h"
#include "Common/Thread.h"

namespace
{
// audoutWaitPlayFinish blocks until a buffer comes back, so this only bounds how long a pause or
// a shutdown has to wait for the audio thread to notice.
constexpr u64 WAIT_TIMEOUT_NS = 100'000'000;

constexpr s32 THREAD_PRIORITY = 0x26;
}  // namespace

bool HorizonSoundStream::Init()
{
  const Result init_result = audoutInitialize();
  if (R_FAILED(init_result))
  {
    ERROR_LOG_FMT(AUDIO, "audoutInitialize failed: {:#010x}", init_result);
    return false;
  }
  m_audout_initialized = true;

  const u32 sample_rate = audoutGetSampleRate();
  const u32 channels = audoutGetChannelCount();
  const PcmFormat format = audoutGetPcmFormat();
  if (sample_rate == 0 || channels != CHANNEL_COUNT || format != PcmFormat_Int16)
  {
    ERROR_LOG_FMT(AUDIO, "Unsupported audout format: {} Hz, {} channels, PCM format {}",
                  sample_rate, channels, static_cast<int>(format));
    return false;
  }

  // Mixer resamples the GC/Wii rates to whatever we ask for, so the sink itself stays a memcpy.
  m_mixer->SetSampleRate(sample_rate);

  for (AudioOutBuffer& buffer : m_buffers)
  {
    void* const memory = std::aligned_alloc(0x1000, BUFFER_BYTES);
    if (memory == nullptr)
    {
      ERROR_LOG_FMT(AUDIO, "Could not allocate a {} byte audio buffer.", BUFFER_BYTES);
      return false;
    }
    std::memset(memory, 0, BUFFER_BYTES);

    buffer = {
        .next = nullptr,
        .buffer = memory,
        .buffer_size = BUFFER_BYTES,
        .data_size = BUFFER_BYTES,
        .data_offset = 0,
    };
  }

  NOTICE_LOG_FMT(AUDIO, "audout initialized: {} Hz, {} channels, {} buffers of {} frames ({} ms)",
                 sample_rate, channels, BUFFER_COUNT, BUFFER_FRAMES,
                 BUFFER_COUNT * BUFFER_FRAMES * 1000 / sample_rate);

  m_thread_status.store(ThreadStatus::PAUSED);
  m_thread = std::thread(&HorizonSoundStream::SoundLoop, this);
  return true;
}

HorizonSoundStream::~HorizonSoundStream()
{
  m_thread_status.store(ThreadStatus::STOPPING);

  // Immediately lock and unlock mutex to prevent cv race.
  std::unique_lock<std::mutex>{m_cv_mutex}.unlock();

  m_cv.notify_one();
  if (m_thread.joinable())
    m_thread.join();

  for (AudioOutBuffer& buffer : m_buffers)
  {
    std::free(buffer.buffer);
    buffer.buffer = nullptr;
  }

  if (m_audout_initialized)
    audoutExit();
}

// Called on audio thread.
void HorizonSoundStream::SoundLoop()
{
  Common::SetCurrentThreadName("Audio thread - horizon");
  if (R_FAILED(svcSetThreadPriority(CUR_THREAD_HANDLE, THREAD_PRIORITY)))
    WARN_LOG_FMT(AUDIO, "Could not raise the audio thread's priority; expect underruns.");

  while (m_thread_status.load() != ThreadStatus::STOPPING)
  {
    if (m_thread_status.load() == ThreadStatus::RUNNING && !StartOutput())
      m_thread_status.store(ThreadStatus::PAUSED);

    while (m_thread_status.load() == ThreadStatus::RUNNING)
    {
      AudioOutBuffer* released = nullptr;
      u32 released_count = 0;
      if (R_FAILED(audoutWaitPlayFinish(&released, &released_count, WAIT_TIMEOUT_NS)))
        continue;

      // Getting every buffer back at once means audout had nothing left to play while we were descheduled.
      if (released_count == BUFFER_COUNT)
        ++m_drain_count;

      while (released != nullptr)
      {
        // Appending relinks the buffer, so its successor has to be read first.
        AudioOutBuffer* const next = released->next;
        FillBuffer(*released);
        audoutAppendAudioOutBuffer(released);
        released = next;
      }
    }

    if (m_thread_status.load() == ThreadStatus::PAUSED)
    {
      StopOutput();

      // Block until thread status changes.
      std::unique_lock<std::mutex> lock(m_cv_mutex);
      m_cv.wait(lock, [this] { return m_thread_status.load() != ThreadStatus::PAUSED; });
    }
  }

  StopOutput();
  m_thread_status.store(ThreadStatus::STOPPED);
}

bool HorizonSoundStream::StartOutput()
{
  if (m_output_started)
    return true;

  const Result start_result = audoutStartAudioOut();
  if (R_FAILED(start_result))
  {
    ERROR_LOG_FMT(AUDIO, "audoutStartAudioOut failed: {:#010x}", start_result);
    return false;
  }
  m_output_started = true;

  for (AudioOutBuffer& buffer : m_buffers)
  {
    FillBuffer(buffer);
    const Result append_result = audoutAppendAudioOutBuffer(&buffer);
    if (R_FAILED(append_result))
    {
      ERROR_LOG_FMT(AUDIO, "audoutAppendAudioOutBuffer failed: {:#010x}", append_result);
      return false;
    }
  }

  return true;
}

void HorizonSoundStream::StopOutput()
{
  if (!m_output_started)
    return;

  // This hands every appended buffer back to us.
  audoutStopAudioOut();
  m_output_started = false;

  if (m_drain_count != 0)
  {
    WARN_LOG_FMT(AUDIO, "audout ran dry {} times.", m_drain_count);
    m_drain_count = 0;
  }
}

void HorizonSoundStream::FillBuffer(AudioOutBuffer& buffer)
{
  m_mixer->Mix(static_cast<s16*>(buffer.buffer), BUFFER_FRAMES);
  buffer.data_size = BUFFER_BYTES;
  buffer.data_offset = 0;
}

bool HorizonSoundStream::SetRunning(bool running)
{
  m_thread_status.store(running ? ThreadStatus::RUNNING : ThreadStatus::PAUSED);

  // Immediately lock and unlock mutex to prevent cv race.
  std::unique_lock<std::mutex>{m_cv_mutex}.unlock();

  // Notify thread that status has changed
  m_cv.notify_one();
  return true;
}

void HorizonSoundStream::SetVolume(int volume)
{
  const Result result = audoutSetAudioOutVolume(std::clamp(volume, 0, 100) / 100.0f);
  if (R_FAILED(result))
    WARN_LOG_FMT(AUDIO, "audoutSetAudioOutVolume failed: {:#010x}", result);
}
#endif  // HAVE_AUDOUT
