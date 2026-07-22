// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// Mesa assumes a POSIX host. These are the symbols NXVK references that newlib declares but never defines.

#include <sys/types.h>

#include <cerrno>
#include <cstddef>
#include <malloc.h>
#include <regex.h>
#include <signal.h>
#include <unistd.h>

#include <switch.h>

extern "C" {
ssize_t getrandom(void* buf, size_t buflen, unsigned int flags);
int posix_memalign(void** memptr, size_t alignment, size_t size);

ssize_t getrandom(void* buf, size_t buflen, unsigned int flags)
{
  randomGet(buf, buflen);
  return static_cast<ssize_t>(buflen);
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
  if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0)
    return EINVAL;

  void* ptr = memalign(alignment, size);
  if (!ptr)
    return ENOMEM;

  *memptr = ptr;
  return 0;
}

uid_t getuid()
{
  return 0;
}

uid_t geteuid()
{
  return 0;
}

gid_t getgid()
{
  return 0;
}

gid_t getegid()
{
  return 0;
}

long sysconf(int name)
{
  switch (name)
  {
  case _SC_PAGESIZE:
    return 0x1000;
  case _SC_NPROCESSORS_CONF:
  case _SC_NPROCESSORS_ONLN:
    return 4;
  case _SC_PHYS_PAGES:
  {
    u64 total_memory = 0;
    if (R_FAILED(svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
      return -1;
    return static_cast<long>(total_memory / 0x1000);
  }
  default:
    return -1;
  }
}

// Only reached through driconf, which has no configuration file to match against anyways.
int regcomp(regex_t* preg, const char* regex, int cflags)
{
  if (preg)
    preg->re_nsub = 0;
  return 0;
}

int regexec(const regex_t* preg, const char* string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
  return REG_NOMATCH;
}

void regfree(regex_t* preg)
{
}

// Mesa's worker threads block signals on startup.
int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset)
{
  if (oldset)
    *oldset = 0;
  return 0;
}
}
