// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// Horizon delivers CPU exceptions to a userland handler on a dedicated stack, with the faulting
// thread frozen mid-fault. This is to safely dump the faulting thread to be able to diagnose
// any crashes.
//
// This should only be called after nothing has claimed the fault.

#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include <fcntl.h>
#include <switch.h>
#include <unistd.h>

#include "Common/CommonPaths.h"
#include "DolphinSwitch/HorizonExceptionEntry.h"

// The entry stub fills the slot by hand.
static_assert(sizeof(CpuRegister) == 8);
static_assert(sizeof(FpuRegister) == 16);
static_assert(offsetof(ThreadExceptionDump, error_desc) == DUMP_ERROR_DESC);
static_assert(offsetof(ThreadExceptionDump, cpu_gprs) == DUMP_GPRS);
static_assert(offsetof(ThreadExceptionDump, fp) == DUMP_FP);
static_assert(offsetof(ThreadExceptionDump, lr) == DUMP_LR);
static_assert(offsetof(ThreadExceptionDump, sp) == DUMP_SP);
static_assert(offsetof(ThreadExceptionDump, pc) == DUMP_PC);
static_assert(offsetof(ThreadExceptionDump, padding) == DUMP_PADDING);
static_assert(offsetof(ThreadExceptionDump, fpu_gprs) == DUMP_FPU);
static_assert(offsetof(ThreadExceptionDump, pstate) == DUMP_PSTATE);
static_assert(offsetof(ThreadExceptionDump, far) == DUMP_FAR);
static_assert(sizeof(ThreadExceptionDump) <= SLOT_FRAME);
static_assert(SLOT_INDEX + sizeof(u64) <= SLOT_SIZE);

// pstate, afsr0, afsr1 and esr are copied as one 16-byte pair, so they have to line up on both
// sides.
static_assert(offsetof(ThreadExceptionDump, esr) == DUMP_PSTATE + 12);
static_assert(offsetof(ThreadExceptionFrameA64, esr) == FRAME_PSTATE + 12);

static_assert(offsetof(ThreadExceptionFrameA64, lr) == FRAME_LR);
static_assert(offsetof(ThreadExceptionFrameA64, sp) == FRAME_SP);
static_assert(offsetof(ThreadExceptionFrameA64, elr_el1) == FRAME_PC);
static_assert(offsetof(ThreadExceptionFrameA64, pstate) == FRAME_PSTATE);
static_assert(offsetof(ThreadExceptionFrameA64, far) == FRAME_FAR);

namespace
{
constexpr const char* CRASH_LOG_PATH = NORMAL_USER_DIR DIR_SEP "crash.log";

class DumpWriter
{
public:
  explicit DumpWriter(int fd) : m_fd(fd) {}

  void Printf(const char* format, ...) __attribute__((format(printf, 2, 3)))
  {
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(m_buffer, sizeof(m_buffer), format, args);
    va_end(args);

    if (length <= 0)
      return;

    const size_t size = static_cast<size_t>(length) < sizeof(m_buffer) ?
                            static_cast<size_t>(length) :
                            sizeof(m_buffer) - 1;
    if (m_fd >= 0)
      (void)write(m_fd, m_buffer, size);
    (void)write(STDERR_FILENO, m_buffer, size);
  }

private:
  int m_fd;
  char m_buffer[256];
};

// A `brk` reaches the same handler as an actual fault, but Horizon reports it as an instruction
// abort, which sends you looking for a bad jump that never happened for hours on end which ends up being
// a huge waste of time and no I'm not salty.
constexpr u32 ESR_EC_BRK_AARCH64 = 0x3C;
constexpr u32 BRK_BUILTIN_TRAP = 1000;

u32 EsrExceptionClass(u32 esr)
{
  return (esr >> 26) & 0x3F;
}

const char* DescribeError(u32 error_desc)
{
  switch (error_desc)
  {
  case ThreadExceptionDesc_InstructionAbort:
    return "instruction abort";
  case ThreadExceptionDesc_MisalignedPC:
    return "misaligned PC";
  case ThreadExceptionDesc_MisalignedSP:
    return "misaligned SP";
  case ThreadExceptionDesc_SError:
    return "SError";
  case ThreadExceptionDesc_BadSVC:
    return "bad SVC";
  case ThreadExceptionDesc_Trap:
    return "trap";
  case ThreadExceptionDesc_Other:
    return "data abort or other";
  default:
    return "unknown";
  }
}
}  // namespace

// switch.ld places _start at 0, so its runtime address is the load base and subtracting it from
// pc turns the dump into something addr2line can resolve against porpoise.elf.
extern "C" void _start();

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx);

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
  const int fd = open(CRASH_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  DumpWriter out(fd);

  const bool is_brk = EsrExceptionClass(ctx->esr) == ESR_EC_BRK_AARCH64;
  if (is_brk)
  {
    const u32 comment = ctx->esr & 0xFFFF;
    out.Printf("porpoise stopped at brk #%u%s\n", comment,
               comment == BRK_BUILTIN_TRAP ? " (__builtin_trap)" : "");
    out.Printf("This is deliberate, not a bad jump. A failed ASSERT or Crash() leaves the "
               "condition, file, line and function in the last lines of dolphin.log. If nothing "
               "is there, the compiler emitted the trap itself and the pc below is the site.\n");
  }
  else
  {
    out.Printf("porpoise crashed: %s (error_desc 0x%x)\n", DescribeError(ctx->error_desc),
               ctx->error_desc);
  }

  if (!threadExceptionIsAArch64(ctx))
  {
    out.Printf("AArch32 exception frame; register dump omitted.\n");
  }
  else
  {
    out.Printf("pc  %016lx  lr  %016lx\n", ctx->pc.x, ctx->lr.x);
    out.Printf("sp  %016lx  fp  %016lx\n", ctx->sp.x, ctx->fp.x);
    out.Printf("far %016lx  esr %08x  pstate %08x\n", ctx->far.x, ctx->esr, ctx->pstate);

    // cpu_gprs holds x0..x28, an odd count, so the last row has no partner to pair with.
    constexpr int gpr_count = 29;
    for (int i = 0; i < gpr_count; i += 2)
    {
      if (i + 1 < gpr_count)
        out.Printf("x%-2d %016lx  x%-2d %016lx\n", i, ctx->cpu_gprs[i].x, i + 1,
                   ctx->cpu_gprs[i + 1].x);
      else
        out.Printf("x%-2d %016lx\n", i, ctx->cpu_gprs[i].x);
    }
  }

  const u64 module_base = reinterpret_cast<u64>(&_start);
  out.Printf("module base %016lx\n", module_base);
  if (ctx->pc.x >= module_base)
    out.Printf("pc +%lx  lr +%lx  (aarch64-none-elf-addr2line -e porpoise.elf)\n",
               ctx->pc.x - module_base, ctx->lr.x - module_base);
  else
    out.Printf("pc is below the module base; the jump left our image.\n");

  if (fd >= 0)
  {
    (void)fsync(fd);
    (void)close(fd);
  }

  // fsync only covers the handle above. Everything else must be flushed manually.
  (void)fsdevCommitDevice("sdmc");

  // Returning from here resumes the faulting instruction and loops forever.
  // Instead, exit to prevent a hang.
  svcExitProcess();
}

// Overrides newlib's abort
//
// newlib's abort() calls exit(), so a library that aborts on one thread runs libnx's __appExit on
// that thread and closes hid, fs, applet and sm while every other thread is still using them. The
// next thread to call into one of those aborts inside libnx, and since it is the second abort that
// reaches the kernel, that is the thread the crash report describes. The original one is gone by
// then, along with any indication that it ever existed, which makes debugging incredible annoying.
// Breaking, instead freezes every thread where it stands, so the report is of the abort that actually happened.
extern "C" void abort()
{
  const int fd = open(CRASH_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  DumpWriter out(fd);

  const u64 module_base = reinterpret_cast<u64>(&_start);
  const u64 caller = reinterpret_cast<u64>(__builtin_return_address(0));

  out.Printf("porpoise aborted. Something called abort().\n");
  if (caller >= module_base)
    out.Printf("abort called from +%lx  (aarch64-none-elf-addr2line -e porpoise.elf)\n",
               caller - module_base);
  out.Printf("module base %016lx\n", module_base);
  out.Printf("The break below is what the crash report describes. Its crashed thread is the one "
             "that aborted, and its stack trace is where the reason is.\n");

  if (fd >= 0)
  {
    (void)fsync(fd);
    (void)close(fd);
  }

  (void)fsdevCommitDevice("sdmc");

  svcBreak(BreakReason_Panic, 0, 0);
  __builtin_unreachable();
}
