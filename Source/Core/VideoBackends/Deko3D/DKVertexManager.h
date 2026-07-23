// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/VertexManagerBase.h"

// TODO: back the four stream buffers with CpuUncached|GpuCached DkMemBlock ring allocators and
// issue dkCmdBufDraw/dkCmdBufDrawIndexed.

namespace Deko3D
{
class DKVertexManager final : public VertexManagerBase
{
public:
  DKVertexManager();
  ~DKVertexManager() override;

protected:
  void DrawCurrentBatch(u32 base_index, u32 num_indices, u32 base_vertex) override;
};
}  // namespace Deko3D
