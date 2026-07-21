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

alignas(16) u8 __nx_exception_stack[0x2000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
  const int fd = open(CRASH_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  DumpWriter out(fd);

  out.Printf("porpoise crashed: %s (error_desc 0x%x)\n", DescribeError(ctx->error_desc),
             ctx->error_desc);

  if (!threadExceptionIsAArch64(ctx))
  {
    out.Printf("AArch32 exception frame; register dump omitted.\n");
  }
  else
  {
    out.Printf("pc  %016lx  lr  %016lx\n", ctx->pc.x, ctx->lr.x);
    out.Printf("sp  %016lx  fp  %016lx\n", ctx->sp.x, ctx->fp.x);
    out.Printf("far %016lx  esr %08x  pstate %08x\n", ctx->far.x, ctx->esr, ctx->pstate);

    for (int i = 0; i < 29; i += 2)
      out.Printf("x%-2d %016lx  x%-2d %016lx\n", i, ctx->cpu_gprs[i].x, i + 1,
                 ctx->cpu_gprs[i + 1].x);
  }

  // The NRO is relocated at load, so the raw addresses above only become source lines once they are
  // rebased. Subtracting this from pc/lr gives an offset into porpoise.elf.
  u64 aslr_base = 0;
  if (R_SUCCEEDED(svcGetInfo(&aslr_base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0)))
    out.Printf("aslr base %016lx\n", aslr_base);

  if (fd >= 0)
  {
    (void)fsync(fd);
    (void)close(fd);
  }

  // Returning from here resumes the faulting instruction and loops forever.
  // Instead, exit to prevent a hang.
  svcExitProcess();
}
