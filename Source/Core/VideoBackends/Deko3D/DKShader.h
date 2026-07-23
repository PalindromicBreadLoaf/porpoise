// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/AbstractShader.h"

// TODO: wrap a real DkShader plus its DKSH code region in a code DkMemBlock, and make GetBinary()
// return the DKSH blob for the disk shader cache.

namespace Deko3D
{
class DKShader final : public AbstractShader
{
public:
  explicit DKShader(ShaderStage stage) : AbstractShader(stage) {}
};
}  // namespace Deko3D
