// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "Common/CommonTypes.h"

namespace Deko3D
{
// Number of command buffers in flight. Matches the Vulkan backend so frame pacing and the stream
// buffers' fence tracking behave the same way.
constexpr size_t NUM_COMMAND_BUFFERS = 8;

// Backing memory handed to each command buffer up front. The cbAddMem callback grows a command
// buffer past this on demand and the extra chunks are then reused every frame, so these sizes only
// need to cover the common case.
constexpr u32 INIT_COMMAND_BUFFER_SIZE = 64 * 1024;
constexpr u32 DRAW_COMMAND_BUFFER_SIZE = 256 * 1024;

// Size of each additional slice handed to a command buffer that runs out of space.
constexpr u32 COMMAND_BUFFER_GROWTH_SIZE = 256 * 1024;
}  // namespace Deko3D
