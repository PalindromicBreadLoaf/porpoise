// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HorizonSampler.h"

#ifdef __SWITCH__

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <switch.h>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/HorizonJitStack.h"
#include "Common/HorizonThreadRegistry.h"
#include "Common/HostCodeMemory.h"
#include "Common/Logging/Log.h"
#include "Common/Thread.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/SystemTimers.h"
#include "Core/PowerPC/JitCommon/JitCache.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/Statistics.h"

extern "C" void _start();
extern "C" char __end__[];

namespace Core::HorizonSampler
{
namespace
{
using Common::HorizonThreadRegistry::ThreadInfo;

constexpr u64 SAMPLE_PERIOD_NS = 2'000'000;
constexpr u64 BUCKET_NS = 250'000'000;
constexpr int PAUSE_RETRIES = 8;
constexpr u64 PAUSE_RETRY_NS = 1'000;

constexpr std::size_t MAX_THREADS = Common::HorizonThreadRegistry::MAX_THREADS;
constexpr std::size_t MAX_REGIONS = 64;
constexpr std::size_t MAX_FRAMES = 16;
constexpr std::size_t MAX_SAMPLES = 512 * 1024;

constexpr std::size_t MAX_REPORTED_BLOCKS = 24;
constexpr std::size_t MAX_LISTED_BLOCKS = 60;
constexpr std::size_t MAX_BLOCK_INSTRUCTIONS = 2048;
constexpr std::size_t MAX_LISTED_UNKNOWN = 12;

constexpr u64 NO_TICKS = std::numeric_limits<u64>::max();

#ifdef PORPOISE_FRAME_POINTERS
constexpr bool FRAME_POINTERS = true;
#else
constexpr bool FRAME_POINTERS = false;
#endif

struct Region
{
  std::string_view name;
  uintptr_t start = 0;
  uintptr_t end = 0;
  bool holds_blocks = false;
};

struct Sample
{
  u64 pc = 0;
  u64 lr = 0;
  std::array<u64, MAX_FRAMES> frames{};
  u32 guest_pc = 0;
  u8 thread = 0;
  u8 depth = 0;
  u16 svc = 0;
  bool blocked = false;
};

struct ThreadRecord
{
  ThreadInfo info;
  bool is_cpu = false;
  bool gone = false;
  u64 failures = 0;
  u64 first_ticks = NO_TICKS;
  u64 first_wall = 0;
  u64 last_ticks = NO_TICKS;
  u64 last_wall = 0;
};

struct Bucket
{
  u64 wall_ticks = 0;
  s64 emulated_ticks = 0;
  u64 gpu_busy_ns = 0;
  u64 presents = 0;
  std::array<u64, MAX_THREADS> thread_ticks{};
};

struct HotBlock
{
  u32 guest_address = 0;
  bool far_code = false;
  u64 self = 0;
  u64 inclusive = 0;
  std::vector<std::pair<u32, u64>> instructions;
};

struct BlockLabel
{
  u32 guest_address = 0;
  bool far_code = false;
  std::size_t hot_index = std::numeric_limits<std::size_t>::max();
};

struct Capture
{
  std::array<Region, MAX_REGIONS> regions{};
  std::size_t region_count = 0;

  uintptr_t module_start = 0;
  uintptr_t module_end = 0;

  uintptr_t jit_start = 0;
  uintptr_t jit_end = 0;

  uintptr_t code_start = 0;
  uintptr_t code_end = 0;

  std::array<ThreadRecord, MAX_THREADS> threads{};
  std::size_t thread_count = 0;
  u32 sampler_handle = INVALID_HANDLE;

  std::vector<Sample> samples;
  std::size_t sample_count = 0;
  u64 dropped_samples = 0;
  u64 rounds = 0;

  std::vector<Bucket> buckets;

  const PowerPC::PowerPCState* ppc_state = nullptr;
  const s64* global_timer = nullptr;
  u32 ticks_per_second = 0;
  bool jit_active = false;

  u64 sampler_ticks = NO_TICKS;
  Result first_error = 0;
  double seconds = 0.0;
  double elapsed = 0.0;
  std::string report_path;

  std::unordered_map<u32, BlockLabel> block_labels;
  std::vector<HotBlock> hot_blocks;
  bool blocks_resolved = false;
};

std::atomic<u32> s_cpu_thread_handle{INVALID_HANDLE};
std::atomic<bool> s_running{false};
std::atomic<bool> s_report_pending{false};

std::mutex s_lifecycle_mutex;
std::thread s_sampler_thread;
Capture s_capture;

u64 ReadThreadTicks(Handle handle)
{
  const u32 info =
      hosversionAtLeast(13, 0, 0) ? InfoType_ThreadTickCount : InfoType_ThreadTickCountDeprecated;
  u64 ticks = 0;
  if (R_FAILED(svcGetInfo(&ticks, info, handle, TickCountInfo_Total)))
    return NO_TICKS;
  return ticks;
}

bool InModule(u64 address)
{
  return address >= s_capture.module_start && address < s_capture.module_end;
}

bool InCodeArena(u64 address)
{
  return address >= s_capture.code_start && address < s_capture.code_end;
}

const Region* FindRegion(u64 address)
{
  for (std::size_t i = 0; i < s_capture.region_count; ++i)
  {
    const Region& region = s_capture.regions[i];
    if (address >= region.start && address < region.end)
      return &region;
  }
  return nullptr;
}

bool InBlockCode(u64 address)
{
  const Region* region = FindRegion(address);
  return region != nullptr && region->holds_blocks;
}

bool IsInSyscall(u64 pc, u16* svc)
{
  if (pc % 4 != 0 || !InModule(pc))
    return false;
  u32 instruction;
  std::memcpy(&instruction, reinterpret_cast<const void*>(pc), sizeof(instruction));
  if ((instruction & 0xffe0001f) != 0xd4000001)
    return false;
  *svc = static_cast<u16>((instruction >> 5) & 0xffff);
  return true;
}

u8 WalkFrames(const ThreadRecord& thread, const ThreadContext& ctx,
              std::array<u64, MAX_FRAMES>& frames)
{
  if constexpr (!FRAME_POINTERS)
    return 0;

  struct Range
  {
    uintptr_t start;
    uintptr_t end;
  };
  std::array<Range, 2> ranges{};
  std::size_t range_count = 0;
  if (thread.info.stack_end > thread.info.stack_start)
    ranges[range_count++] = {thread.info.stack_start, thread.info.stack_end};

  uintptr_t jit_stack_start, jit_stack_end;
  if (thread.is_cpu &&
      Common::HorizonJitStack::GetActiveStackRange(&jit_stack_start, &jit_stack_end) &&
      ctx.sp >= jit_stack_start && ctx.sp < jit_stack_end)
  {
    ranges[range_count++] = {jit_stack_start, jit_stack_end};
  }

  uintptr_t fp = ctx.fp;
  uintptr_t previous_fp = 0;
  const Range* previous_range = nullptr;
  u8 depth = 0;
  while (depth < MAX_FRAMES)
  {
    if (fp % 16 != 0)
      break;

    const Range* range = nullptr;
    for (std::size_t i = 0; i < range_count; ++i)
    {
      if (fp >= ranges[i].start && fp + 16 <= ranges[i].end)
        range = &ranges[i];
    }
    if (range == nullptr || (range == previous_range && fp <= previous_fp))
      break;

    u64 record[2];
    std::memcpy(record, reinterpret_cast<const void*>(fp), sizeof(record));
    const u64 return_address = record[1];
    const bool module = InModule(return_address);
    if (!module && !InCodeArena(return_address))
      break;

    frames[depth++] = return_address;

    if (!module)
      break;

    previous_range = range;
    previous_fp = fp;
    fp = record[0];
  }
  return depth;
}

void NoteFailure(Result result)
{
  if (s_capture.first_error == 0)
    s_capture.first_error = result;
}

void TakeSample(std::size_t index)
{
  ThreadRecord& thread = s_capture.threads[index];
  const Handle handle = thread.info.handle;

  const Result pause_result = svcSetThreadActivity(handle, ThreadActivity_Paused);
  if (R_FAILED(pause_result))
  {
    ++thread.failures;
    if (R_VALUE(pause_result) == KERNELRESULT(InvalidHandle))
      thread.gone = true;
    else
      NoteFailure(pause_result);
    return;
  }

  ThreadContext ctx{};
  Result result = 0;
  for (int attempt = 0; attempt < PAUSE_RETRIES; ++attempt)
  {
    result = svcGetThreadContext3(&ctx, handle);
    if (R_SUCCEEDED(result))
      break;
    svcSleepThread(PAUSE_RETRY_NS);
  }

  Sample* const sample = s_capture.sample_count < s_capture.samples.size() ?
                             &s_capture.samples[s_capture.sample_count] :
                             nullptr;
  if (R_SUCCEEDED(result) && sample != nullptr)
  {
    sample->pc = ctx.pc.x;
    sample->lr = ctx.lr;
    sample->thread = static_cast<u8>(index);
    sample->guest_pc =
        thread.is_cpu && s_capture.ppc_state != nullptr ? s_capture.ppc_state->pc : 0;
    sample->blocked = IsInSyscall(ctx.pc.x, &sample->svc);
    sample->depth = WalkFrames(thread, ctx, sample->frames);
  }

  svcSetThreadActivity(handle, ThreadActivity_Runnable);

  if (R_FAILED(result))
  {
    ++thread.failures;
    NoteFailure(result);
    return;
  }

  if (sample != nullptr)
    ++s_capture.sample_count;
  else
    ++s_capture.dropped_samples;
}

void RefreshThreads()
{
  std::array<ThreadInfo, MAX_THREADS> snapshot;
  const std::size_t count = Common::HorizonThreadRegistry::Snapshot(snapshot);
  const u32 cpu_handle = s_cpu_thread_handle.load(std::memory_order_acquire);

  for (std::size_t i = 0; i < count; ++i)
  {
    const ThreadInfo& info = snapshot[i];
    if (info.handle == s_capture.sampler_handle)
      continue;

    auto* const end = s_capture.threads.begin() + s_capture.thread_count;
    auto* record = std::find_if(s_capture.threads.begin(), end, [&](const ThreadRecord& r) {
      return r.info.handle == info.handle;
    });
    if (record != end && record->gone)
      continue;
    if (record == end)
    {
      if (s_capture.thread_count == s_capture.threads.size())
        continue;
      record = &s_capture.threads[s_capture.thread_count++];
    }

    record->info = info;
    record->is_cpu = info.handle == cpu_handle;
  }
}

void RecordBucket()
{
  if (s_capture.buckets.size() == s_capture.buckets.capacity())
    return;

  Bucket& bucket = s_capture.buckets.emplace_back();
  bucket.wall_ticks = armGetSystemTick();
  if (s_capture.global_timer != nullptr)
    bucket.emulated_ticks = __atomic_load_n(s_capture.global_timer, __ATOMIC_RELAXED);
  bucket.gpu_busy_ns = g_stats.gpu_busy_ns_total.load(std::memory_order_relaxed);
  bucket.presents = g_stats.presents_total.load(std::memory_order_relaxed);

  for (std::size_t i = 0; i < MAX_THREADS; ++i)
  {
    bucket.thread_ticks[i] = NO_TICKS;
    if (i >= s_capture.thread_count || s_capture.threads[i].gone)
      continue;

    ThreadRecord& thread = s_capture.threads[i];
    const u64 ticks = ReadThreadTicks(thread.info.handle);
    bucket.thread_ticks[i] = ticks;
    if (ticks == NO_TICKS)
      continue;

    if (thread.first_ticks == NO_TICKS)
    {
      thread.first_ticks = ticks;
      thread.first_wall = bucket.wall_ticks;
    }
    thread.last_ticks = ticks;
    thread.last_wall = bucket.wall_ticks;
  }
}

void SamplerThread()
{
  s_capture.sampler_handle = threadGetCurHandle();
  Common::SetCurrentThreadName("Profiler");

  Common::PinCurrentThreadToRole(Common::ThreadCoreRole::Host);

  const u64 own_start = ReadThreadTicks(s_capture.sampler_handle);
  const u64 start = armGetSystemTick();
  const u64 bucket_ticks = armNsToTicks(BUCKET_NS);
  u64 next_bucket = start + bucket_ticks;
  u32 rng = 0x2545f491;

  RefreshThreads();
  RecordBucket();

  while (s_running.load(std::memory_order_relaxed))
  {
    const u64 now = armGetSystemTick();
    s_capture.elapsed = static_cast<double>(armTicksToNs(now - start)) / 1e9;
    if (s_capture.elapsed >= s_capture.seconds)
      break;
    if (s_cpu_thread_handle.load(std::memory_order_acquire) == INVALID_HANDLE)
      break;

    if (now >= next_bucket)
    {
      RefreshThreads();
      RecordBucket();
      next_bucket += bucket_ticks;
    }

    ++s_capture.rounds;
    for (std::size_t i = 0; i < s_capture.thread_count; ++i)
    {
      if (!s_capture.threads[i].gone)
        TakeSample(i);
    }

    if (s_capture.sample_count == 0 && s_capture.rounds >= 32)
      break;

    rng = rng * 1664525u + 1013904223u;
    svcSleepThread(SAMPLE_PERIOD_NS / 2 + rng % SAMPLE_PERIOD_NS);
  }

  RecordBucket();

  const u64 own_end = ReadThreadTicks(s_capture.sampler_handle);
  if (own_start != NO_TICKS && own_end != NO_TICKS)
    s_capture.sampler_ticks = own_end - own_start;

  s_running.store(false, std::memory_order_relaxed);

  s_report_pending.store(true, std::memory_order_release);
}

// Call with s_lifecycle_mutex held.
void JoinSampler()
{
  s_running.store(false, std::memory_order_relaxed);
  if (s_sampler_thread.joinable())
    s_sampler_thread.join();
}

std::span<const Sample> Samples()
{
  return {s_capture.samples.data(), s_capture.sample_count};
}

bool KeepLinkRegister(const Sample& sample)
{
  return !InBlockCode(sample.pc) && (InModule(sample.lr) || InCodeArena(sample.lr));
}

void AddJitOffset(std::set<u32>& offsets, u64 address)
{
  if (InBlockCode(address))
    offsets.insert(static_cast<u32>(address - s_capture.jit_start));
}

void ResolveBlocks(Core::System& system)
{
  s_capture.blocks_resolved = false;

  std::set<u32> offsets;
  std::map<u32, u64> self_by_offset;
  for (const Sample& sample : Samples())
  {
    AddJitOffset(offsets, sample.pc);
    if (InBlockCode(sample.pc))
      ++self_by_offset[static_cast<u32>(sample.pc - s_capture.jit_start)];
    if (KeepLinkRegister(sample))
      AddJitOffset(offsets, sample.lr);
    for (u8 i = 0; i < sample.depth; ++i)
      AddJitOffset(offsets, sample.frames[i]);
  }
  if (offsets.empty())
    return;

  const Core::CPUThreadGuard guard(system);
  auto ranges = system.GetJitInterface().GetBlockRanges(guard);
  if (ranges.empty())
    return;

  std::sort(ranges.begin(), ranges.end(),
            [](const auto& a, const auto& b) { return a.start < b.start; });

  std::map<const JitCodeBlockRange*, std::size_t> block_index;
  for (const u32 offset : offsets)
  {
    const u8* const host = reinterpret_cast<const u8*>(s_capture.jit_start + offset);
    const auto after = std::upper_bound(ranges.begin(), ranges.end(), host,
                                        [](const u8* pc, const auto& r) { return pc < r.start; });
    if (after == ranges.begin())
      continue;
    const JitCodeBlockRange& range = *std::prev(after);
    if (host >= range.end)
      continue;

    auto [it, inserted] = block_index.emplace(&range, s_capture.hot_blocks.size());
    if (inserted)
    {
      HotBlock block;
      block.guest_address = range.guest_address;
      block.far_code = range.far_code;
      s_capture.hot_blocks.push_back(std::move(block));
    }
    s_capture.block_labels[offset] = {range.guest_address, range.far_code, it->second};
  }

  for (const Sample& sample : Samples())
  {
    std::set<std::size_t> on_stack;
    const auto note = [&](u64 address, bool self) {
      if (!InBlockCode(address))
        return;
      const auto label =
          s_capture.block_labels.find(static_cast<u32>(address - s_capture.jit_start));
      if (label == s_capture.block_labels.end())
        return;
      if (self)
        ++s_capture.hot_blocks[label->second.hot_index].self;
      on_stack.insert(label->second.hot_index);
    };
    note(sample.pc, true);
    if (KeepLinkRegister(sample))
      note(sample.lr, false);
    for (u8 i = 0; i < sample.depth; ++i)
      note(sample.frames[i], false);
    for (const std::size_t index : on_stack)
      ++s_capture.hot_blocks[index].inclusive;
  }

  std::vector<std::pair<const JitCodeBlockRange*, std::size_t>> by_self(block_index.begin(),
                                                                        block_index.end());
  std::sort(by_self.begin(), by_self.end(), [](const auto& a, const auto& b) {
    return s_capture.hot_blocks[a.second].self > s_capture.hot_blocks[b.second].self;
  });
  if (by_self.size() > MAX_REPORTED_BLOCKS)
    by_self.resize(MAX_REPORTED_BLOCKS);

  for (const auto& [range, index] : by_self)
  {
    HotBlock& block = s_capture.hot_blocks[index];
    if (block.self == 0)
      continue;
    for (const u8* pc = range->start;
         pc + sizeof(u32) <= range->end && block.instructions.size() < MAX_BLOCK_INSTRUCTIONS;
         pc += sizeof(u32))
    {
      const u32 offset = static_cast<u32>(reinterpret_cast<uintptr_t>(pc) - s_capture.jit_start);
      const auto count = self_by_offset.find(offset);
      u32 encoding;
      std::memcpy(&encoding, pc, sizeof(encoding));
      block.instructions.emplace_back(encoding, count == self_by_offset.end() ? 0 : count->second);
    }
  }

  s_capture.blocks_resolved = true;
}

std::string_view SvcName(u16 svc)
{
  switch (svc)
  {
  case 0x0b:
    return "SleepThread";
  case 0x18:
    return "WaitSynchronization";
  case 0x1a:
    return "ArbitrateLock";
  case 0x1c:
    return "WaitProcessWideKeyAtomic";
  case 0x21:
    return "SendSyncRequest";
  case 0x22:
    return "SendSyncRequestWithUserBuffer";
  case 0x34:
    return "WaitForAddress";
  case 0x43:
    return "ReplyAndReceive";
  default:
    return "";
  }
}

std::string Label(u64 address)
{
  if (const Region* region = FindRegion(address))
  {
    if (region->holds_blocks)
    {
      const u32 offset = static_cast<u32>(address - s_capture.jit_start);
      const auto label = s_capture.block_labels.find(offset);
      if (label == s_capture.block_labels.end())
        return fmt::format("j:{:x}", offset);
      return fmt::format("{}:{:08x}", label->second.far_code ? "bf" : "b",
                         label->second.guest_address);
    }

    std::string name(region->name);
    if (name.starts_with("JIT asm: "))
      name.erase(0, 9);
    std::replace(name.begin(), name.end(), ' ', '_');
    return "a:" + name;
  }
  if (InModule(address))
    return fmt::format("m:{:x}", address - s_capture.module_start);
  if (InCodeArena(address))
    return fmt::format("v:{:x}", address - s_capture.code_start);
  return fmt::format("?:{:x}", address);
}

std::string_view Where(const Sample& sample)
{
  if (sample.blocked)
    return "blocked in the kernel";
  if (const Region* region = FindRegion(sample.pc))
    return region->name;
  if (InModule(sample.pc))
    return "module (C++)";
  if (InCodeArena(sample.pc))
    return "host code outside the PowerPC JIT (vertex loaders)";
  return "unattributed";
}

double TicksToSeconds(u64 ticks)
{
  return static_cast<double>(armTicksToNs(ticks)) / 1e9;
}

double CpuShare(const ThreadRecord& thread)
{
  if (thread.first_ticks == NO_TICKS || thread.last_wall <= thread.first_wall)
    return -1.0;
  return static_cast<double>(thread.last_ticks - thread.first_ticks) /
         static_cast<double>(thread.last_wall - thread.first_wall);
}

void AppendSummary(std::string& out, const std::vector<std::size_t>& order,
                   const std::vector<u64>& sample_counts, const std::vector<u64>& blocked_counts)
{
  out += fmt::format("Porpoise profile: {:.2f} s, {} sampling rounds (~{} ms apart), {} samples, "
                     "{} dropped\n",
                     s_capture.elapsed, s_capture.rounds, SAMPLE_PERIOD_NS / 1'000'000,
                     s_capture.sample_count, s_capture.dropped_samples);
  out += fmt::format("module base {} size {:#x}\n",
                     fmt::ptr(reinterpret_cast<const void*>(s_capture.module_start)),
                     s_capture.module_end - s_capture.module_start);
  out += fmt::format("frame pointers: {}\n",
                     FRAME_POINTERS ? "on, so stacks are full C++ call chains" :
                                      "off, so stacks are the sampled function and its caller "
                                      "only (configure with -DPORPOISE_FRAME_POINTERS=ON)");
  if (s_capture.first_error != 0)
  {
    out += fmt::format("First failure was {:#x} (module {}, description {}). A process launched "
                       "without the thread-activity syscalls cannot be sampled at all.\n",
                       s_capture.first_error, R_MODULE(s_capture.first_error),
                       R_DESCRIPTION(s_capture.first_error));
  }
  if (!s_capture.jit_active)
    out += "No JIT is active, so every CPU thread sample lands in the module.\n";

  if (s_capture.buckets.size() >= 2)
  {
    const Bucket& first = s_capture.buckets.front();
    const Bucket& last = s_capture.buckets.back();
    const double wall = TicksToSeconds(last.wall_ticks - first.wall_ticks);
    const double emulated = s_capture.ticks_per_second != 0 ?
                                static_cast<double>(last.emulated_ticks - first.emulated_ticks) /
                                    s_capture.ticks_per_second :
                                0.0;
    const u64 presents = last.presents - first.presents;
    const double gpu = static_cast<double>(last.gpu_busy_ns - first.gpu_busy_ns) / 1e9;
    if (wall > 0.0)
    {
      out += fmt::format("emulation speed {:.1f}% ({:.2f} s emulated), {:.1f} presents/s\n",
                         100.0 * emulated / wall, emulated, presents / wall);
      if (gpu > 0.0)
      {
        out += fmt::format("GPU busy {:.1f}% of wall clock, {:.2f} ms per present\n",
                           100.0 * gpu / wall, presents != 0 ? 1000.0 * gpu / presents : 0.0);
      }
      else
      {
        out += "GPU busy: not measured by this video backend\n";
      }
      if (s_capture.sampler_ticks != NO_TICKS)
      {
        out += fmt::format("profiler's own cost: {:.1f}% of its core\n",
                           100.0 * TicksToSeconds(s_capture.sampler_ticks) / wall);
      }
    }
  }

  out += "\nThreads. cpu is the kernel's own accounting of time spent on a core, 100% being one "
         "core. running and blocked split the samples by whether the thread was waiting in the "
         "kernel.\n";
  out += "     cpu  samples  running  blocked  name\n";
  for (const std::size_t i : order)
  {
    const ThreadRecord& thread = s_capture.threads[i];
    const double cpu = CpuShare(thread);
    const u64 samples = sample_counts[i];
    const double blocked = samples != 0 ? 100.0 * blocked_counts[i] / samples : 0.0;
    out += fmt::format("  {:>6}  {:7}  {:6.1f}%  {:6.1f}%  {}{}\n",
                       cpu < 0.0 ? "n/a" : fmt::format("{:.1f}%", 100.0 * cpu), samples,
                       samples != 0 ? 100.0 - blocked : 0.0, blocked, thread.info.name.data(),
                       thread.gone ? " (exited)" : "");
  }
}

void AppendTimeline(std::string& out, const std::vector<std::size_t>& order)
{
  if (s_capture.buckets.size() < 2)
    return;

  out += fmt::format("\nTimeline, {} ms per row. speed is emulated time over wall time, gpu is "
                     "GPU busy time over wall time, the rest are each thread's share of a core:\n",
                     BUCKET_NS / 1'000'000);
  for (std::size_t column = 0; column < order.size(); ++column)
    out += fmt::format("  T{} = {}\n", column, s_capture.threads[order[column]].info.name.data());

  out += "       t  speed  pres/s    gpu";
  for (std::size_t column = 0; column < order.size(); ++column)
    out += fmt::format("  {:>5}", fmt::format("T{}", column));
  out += "\n";

  const Bucket& origin = s_capture.buckets.front();
  for (std::size_t b = 1; b < s_capture.buckets.size(); ++b)
  {
    const Bucket& previous = s_capture.buckets[b - 1];
    const Bucket& bucket = s_capture.buckets[b];
    const u64 wall_ticks = bucket.wall_ticks - previous.wall_ticks;
    const double wall = TicksToSeconds(wall_ticks);
    if (wall <= 0.0)
      continue;

    const double speed = s_capture.ticks_per_second != 0 ?
                             static_cast<double>(bucket.emulated_ticks - previous.emulated_ticks) /
                                 s_capture.ticks_per_second / wall :
                             0.0;
    const double gpu = static_cast<double>(bucket.gpu_busy_ns - previous.gpu_busy_ns) / 1e9 / wall;
    out += fmt::format("  {:6.2f}  {:4.0f}%  {:6.1f}  {:4.0f}%",
                       TicksToSeconds(bucket.wall_ticks - origin.wall_ticks), 100.0 * speed,
                       (bucket.presents - previous.presents) / wall, 100.0 * gpu);
    for (const std::size_t i : order)
    {
      const u64 before = previous.thread_ticks[i];
      const u64 after = bucket.thread_ticks[i];
      if (before == NO_TICKS || after == NO_TICKS || after < before)
        out += "      -";
      else
        out += fmt::format("  {:4.0f}%", 100.0 * (after - before) / wall_ticks);
    }
    out += "\n";
  }
}

void AppendThread(std::string& out, std::size_t index, std::size_t column)
{
  const ThreadRecord& thread = s_capture.threads[index];

  u64 total = 0;
  std::map<std::string_view, u64> where;
  std::map<u16, u64> blocked_in;
  std::map<std::string, u64> stacks;
  std::map<u32, u64> guest_pcs;

  for (const Sample& sample : Samples())
  {
    if (sample.thread != index)
      continue;
    ++total;
    ++where[Where(sample)];
    if (sample.blocked)
      ++blocked_in[sample.svc];
    if (thread.is_cpu)
      ++guest_pcs[sample.guest_pc];

    std::string key = sample.blocked ? fmt::format("svc:{:x}", sample.svc) : "run";
    key += ' ';
    key += Label(sample.pc);
    if (KeepLinkRegister(sample))
      key += " L" + Label(sample.lr);
    for (u8 i = 0; i < sample.depth; ++i)
      key += ' ' + Label(sample.frames[i]);
    ++stacks[std::move(key)];
  }

  out += fmt::format("\n== Thread T{}: {} ==\n", column, thread.info.name.data());
  if (total == 0)
  {
    out += fmt::format("No samples ({} failed).\n", thread.failures);
    return;
  }
  const double percent = 100.0 / static_cast<double>(total);

  std::vector<std::pair<std::string_view, u64>> ranked(where.begin(), where.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  out += fmt::format("Where it was ({} samples):\n", total);
  for (const auto& [name, count] : ranked)
    out += fmt::format("  {:5.1f}%  {:7}  {}\n", count * percent, count, name);

  if (!blocked_in.empty())
  {
    out += "Blocked in:\n";
    for (const auto& [svc, count] : blocked_in)
      out += fmt::format("  {:5.1f}%  {:7}  svc {:#04x} {}\n", count * percent, count, svc,
                         SvcName(svc));
  }

  std::vector<std::pair<std::string_view, u64>> by_count;
  by_count.reserve(stacks.size());
  for (const auto& [key, count] : stacks)
    by_count.emplace_back(key, count);
  std::sort(by_count.begin(), by_count.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  out += fmt::format("Stacks ({} distinct). Innermost frame first. m: module offset, b/bf: JIT "
                     "block near/far code by guest address, a: JIT routine, j: block since "
                     "discarded, v: vertex loader code, ?: anything else. L marks the link "
                     "register, which is the caller only while the sampled function is a leaf.\n",
                     by_count.size());
  for (const auto& [key, count] : by_count)
    out += fmt::format("  {} {}\n", count, key);

  if (!guest_pcs.empty())
  {
    std::vector<std::pair<u32, u64>> pcs(guest_pcs.begin(), guest_pcs.end());
    std::sort(pcs.begin(), pcs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    out += fmt::format("Guest PCs ({} distinct). A linked block exit branches straight to its "
                       "successor without storing pc, so this is the last address the core went "
                       "through the dispatcher for, not the one it is running. Read it as the "
                       "entry point of a hot linked chain.\n",
                       pcs.size());
    for (const auto& [guest_pc, count] : pcs)
      out += fmt::format("  {:5.2f}%  {:7}  {:08x}\n", count * percent, count, guest_pc);
  }
}

void AppendBlocks(std::string& out, u64 cpu_samples)
{
  if (s_capture.hot_blocks.empty())
    return;

  const double percent = cpu_samples != 0 ? 100.0 / static_cast<double>(cpu_samples) : 0.0;
  std::vector<const HotBlock*> ranked;
  for (const HotBlock& block : s_capture.hot_blocks)
    ranked.push_back(&block);
  std::sort(ranked.begin(), ranked.end(),
            [](const HotBlock* a, const HotBlock* b) { return a->inclusive > b->inclusive; });
  if (ranked.size() > MAX_LISTED_BLOCKS)
    ranked.resize(MAX_LISTED_BLOCKS);

  out += "\nHot JIT blocks, as a share of CPU thread samples. self is time in the block's own "
         "code; inclusive adds the C++ and assembly routines it called.\n";
  out += "     self  inclusive  block\n";
  for (const HotBlock* block : ranked)
  {
    out += fmt::format("  {:6.2f}%    {:6.2f}%  guest {:08x}{}\n", block->self * percent,
                       block->inclusive * percent, block->guest_address,
                       block->far_code ? " (far code)" : "");
  }

  for (const HotBlock& block : s_capture.hot_blocks)
  {
    if (block.instructions.empty())
      continue;

    out +=
        fmt::format("\nBlock {:08x}{}, {} host instructions. Columns are samples and the "
                    "AArch64 encoding; Tools/symbolize-switch-profile.py --disasm turns it "
                    "back into mnemonics.\n",
                    block.guest_address, block.far_code ? " far" : "", block.instructions.size());
    u32 offset = 0;
    for (const auto& [encoding, count] : block.instructions)
    {
      out += fmt::format("  +{:#06x}  {:7}  {:08x}\n", offset, count, encoding);
      offset += sizeof(u32);
    }
  }
}

void AppendUnknownCode(std::string& out)
{
  std::map<u64, u64> pages;
  for (const Sample& sample : Samples())
  {
    if (!sample.blocked && FindRegion(sample.pc) == nullptr && !InModule(sample.pc) &&
        !InCodeArena(sample.pc))
    {
      ++pages[sample.pc & ~u64{0xfff}];
    }
  }
  if (pages.empty())
    return;

  std::vector<std::pair<u64, u64>> ranked(pages.begin(), pages.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  if (ranked.size() > MAX_LISTED_UNKNOWN)
    ranked.resize(MAX_LISTED_UNKNOWN);

  out += "\nUnattributed code pages:\n";
  for (const auto& [page, count] : ranked)
  {
    MemoryInfo info{};
    u32 page_info;
    if (R_SUCCEEDED(svcQueryMemory(&info, &page_info, page)))
    {
      out += fmt::format("  {:7}  {:#x}  in block {:#x}+{:#x}, type {:#x}, perm {:#x}\n", count,
                         page, info.addr, info.size, info.type & MemState_Type, info.perm);
    }
    else
    {
      out += fmt::format("  {:7}  {:#x}\n", count, page);
    }
  }
}

std::string BuildReport()
{
  std::vector<u64> sample_counts(s_capture.thread_count);
  std::vector<u64> blocked_counts(s_capture.thread_count);
  for (const Sample& sample : Samples())
  {
    ++sample_counts[sample.thread];
    blocked_counts[sample.thread] += sample.blocked ? 1 : 0;
  }

  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < s_capture.thread_count; ++i)
  {
    if (sample_counts[i] != 0 || CpuShare(s_capture.threads[i]) > 0.0)
      order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return CpuShare(s_capture.threads[a]) > CpuShare(s_capture.threads[b]);
  });

  std::string out;
  AppendSummary(out, order, sample_counts, blocked_counts);
  AppendTimeline(out, order);

  for (std::size_t column = 0; column < order.size(); ++column)
  {
    const std::size_t index = order[column];
    AppendThread(out, index, column);
    if (s_capture.threads[index].is_cpu)
    {
      if (!s_capture.blocks_resolved && !s_capture.hot_blocks.empty())
        out += "Blocks could not be resolved, so JIT code is listed by offset only.\n";
      AppendBlocks(out, sample_counts[index]);
    }
  }

  AppendUnknownCode(out);
  return out;
}

void WriteReport()
{
  const std::string report = BuildReport();

  std::size_t logged_lines = 0;
  std::size_t offset = 0;
  while (offset < report.size() && logged_lines < 60)
  {
    const std::size_t newline = report.find('\n', offset);
    const std::size_t end = newline == std::string::npos ? report.size() : newline;
    NOTICE_LOG_FMT(COMMON, "{}", std::string_view(report).substr(offset, end - offset));
    offset = end + 1;
    ++logged_lines;
  }

  if (File::WriteStringToFile(s_capture.report_path, report))
  {
    NOTICE_LOG_FMT(COMMON, "Full profile written to {}", s_capture.report_path);
    OSD::AddMessage(fmt::format("Profile written to {}", s_capture.report_path), 8000);
  }
  else
  {
    ERROR_LOG_FMT(COMMON, "Could not write the profile to {}", s_capture.report_path);
    OSD::AddMessage("Profile capture done, but the report could not be written.", 8000);
  }

  s_capture.samples = {};
  s_capture.block_labels = {};
  s_capture.hot_blocks = {};
}
}  // namespace

void RegisterCpuThread()
{
  s_cpu_thread_handle.store(threadGetCurHandle(), std::memory_order_release);
}

void UnregisterCpuThread()
{
  s_cpu_thread_handle.store(INVALID_HANDLE, std::memory_order_release);

  const std::lock_guard lock(s_lifecycle_mutex);
  JoinSampler();

  if (s_report_pending.exchange(false, std::memory_order_acq_rel))
    WriteReport();
}

bool IsRunning()
{
  return s_running.load(std::memory_order_relaxed);
}

void Start(Core::System& system, double seconds)
{
  if (IsRunning())
    return;

  const std::lock_guard lock(s_lifecycle_mutex);
  JoinSampler();

  if (s_cpu_thread_handle.load(std::memory_order_acquire) == INVALID_HANDLE)
  {
    OSD::AddMessage("Nothing to profile", 4000);
    return;
  }

  s_capture = {};
  s_capture.seconds = seconds;
  s_capture.module_start = reinterpret_cast<uintptr_t>(&_start);
  s_capture.module_end = reinterpret_cast<uintptr_t>(__end__);
  s_capture.ppc_state = &system.GetPPCState();
  s_capture.global_timer = &system.GetCoreTiming().GetGlobals().global_timer;
  s_capture.ticks_per_second = system.GetSystemTimers().GetTicksPerSecond();
  s_capture.report_path = File::GetUserPath(D_DUMP_IDX) + "profile.txt";

  const auto [code_start, code_end] = Common::HostCodeMemory::GetExecutableRange();
  s_capture.code_start = reinterpret_cast<uintptr_t>(code_start);
  s_capture.code_end = reinterpret_cast<uintptr_t>(code_end);

  for (const auto& [name, range] : system.GetJitInterface().GetCodeRegions())
  {
    if (s_capture.region_count == MAX_REGIONS)
    {
      WARN_LOG_FMT(COMMON, "More JIT code regions than the profiler can hold. The rest will be "
                           "reported as vertex loader code.");
      break;
    }
    const auto start = reinterpret_cast<uintptr_t>(range.first);
    const auto end = reinterpret_cast<uintptr_t>(range.second);
    const bool holds_blocks = name == "JIT block code" || name == "JIT far code";
    s_capture.regions[s_capture.region_count++] = {name, start, end, holds_blocks};

    if (holds_blocks)
    {
      s_capture.jit_start = s_capture.jit_start == 0 ? start : std::min(s_capture.jit_start, start);
      s_capture.jit_end = std::max(s_capture.jit_end, end);
    }
  }
  s_capture.jit_active = s_capture.region_count != 0;

  if (s_capture.jit_end - s_capture.jit_start > 0xffff'ffff)
  {
    WARN_LOG_FMT(COMMON, "JIT code spans more than 4 GiB.");
    for (std::size_t i = 0; i < s_capture.region_count; ++i)
      s_capture.regions[i].holds_blocks = false;
  }

  std::array<ThreadInfo, MAX_THREADS> threads;
  const std::size_t thread_count = Common::HorizonThreadRegistry::Snapshot(threads);
  const double rounds = seconds * 1e9 / SAMPLE_PERIOD_NS;
  s_capture.samples.resize(std::min<std::size_t>(
      MAX_SAMPLES, static_cast<std::size_t>(rounds * static_cast<double>(thread_count + 2))));
  s_capture.buckets.reserve(static_cast<std::size_t>(seconds * 1e9 / BUCKET_NS) + 8);

  File::CreateFullPath(File::GetUserPath(D_DUMP_IDX));

  s_running.store(true, std::memory_order_relaxed);
  s_sampler_thread = std::thread(SamplerThread);

  OSD::AddMessage(fmt::format("Profiling for {:.0f} s...", seconds),
                  static_cast<u32>(seconds * 1000.0));
}

void Stop()
{
  const std::lock_guard lock(s_lifecycle_mutex);
  JoinSampler();
}

void Poll(Core::System& system)
{
  if (!s_report_pending.exchange(false, std::memory_order_acq_rel))
    return;

  const std::lock_guard lock(s_lifecycle_mutex);
  JoinSampler();
  ResolveBlocks(system);
  WriteReport();
}

void Toggle(Core::System& system, double seconds)
{
  if (IsRunning())
    Stop();
  else
    Start(system, seconds);
}
}  // namespace Core::HorizonSampler

#endif
