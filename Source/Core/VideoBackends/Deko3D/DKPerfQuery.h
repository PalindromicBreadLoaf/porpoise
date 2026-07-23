// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/PerfQueryBase.h"

// TODO: implement occlusion queries with dkCmdBufReportCounter into
// a fenced GpuCached readback block.

namespace Deko3D
{
class DKPerfQuery final : public PerfQueryBase
{
public:
  void EnableQuery(PerfQueryGroup type) override {}
  void DisableQuery(PerfQueryGroup type) override {}
  void ResetQuery() override {}
  u32 GetQueryResult(PerfQueryType type) override { return 0; }
  void FlushResults() override {}
  bool IsFlushed() const override { return true; }
};
}  // namespace Deko3D
