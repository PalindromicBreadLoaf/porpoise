// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/AbstractPipeline.h"

// TODO: deko3d has no pipeline object. DKPipeline should bake this config's BindShaders + all
// Bind*State calls into captured GPU command words once, so SetPipeline becomes a single dkCmdBufReplayCmds.

namespace Deko3D
{
class DKPipeline final : public AbstractPipeline
{
};
}  // namespace Deko3D
