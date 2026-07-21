// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// MBEDTLS_USER_CONFIG_FILE for the Switch build: included at the end of config.h to reconcile it
// with Horizon. Everything mbedtls gates on "Unix or Windows" is either turned off here or
// redirected to a libnx implementation.

#pragma once

// timing.c is a wall-clock/alarm module built on gettimeofday and setitimer, both things we don't have.
// This doesn't appear to be needed anyways. Hopefully.
#undef MBEDTLS_TIMING_C

// No /dev/urandom and no CryptGenRandom; horizon_entropy.c feeds mbedtls from libnx instead.
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
