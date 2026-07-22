// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// Horizon delivers CPU exceptions to a userland handler on a dedicated stack, with the faulting
// thread frozen mid-fault. This is to safely dump the faulting thread to be able to diagnose
// any crashes.

#include <cstdarg>
#include <cstdio>

#include <fcntl.h>
#include <switch.h>
#include <unistd.h>

#include "Common/CommonPaths.h"

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

alignas(16) u8 __nx_exception_stack[0x2000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

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
