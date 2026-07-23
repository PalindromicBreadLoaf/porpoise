// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

#include "VideoCommon/BoundingBox.h"

// TODO: back the bounding box with a GpuCached DkMemBlock and fenced readback.

namespace Deko3D
{
class DKBoundingBox final : public BoundingBox
{
public:
  bool Initialize() override { return true; }

protected:
  std::vector<BBoxType> Read(u32 index, u32 length) override
  {
    return std::vector<BBoxType>(length);
  }
  void Write(u32 index, std::span<const BBoxType> values) override {}
};
}  // namespace Deko3D
