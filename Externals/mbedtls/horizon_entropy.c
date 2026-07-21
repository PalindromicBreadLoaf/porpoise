// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// MBEDTLS_ENTROPY_HARDWARE_ALT hook for Horizon.

#include <switch.h>

#include "mbedtls/entropy_poll.h"

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen)
{
  (void)data;

  randomGet(output, len);
  *olen = len;
  return 0;
}
