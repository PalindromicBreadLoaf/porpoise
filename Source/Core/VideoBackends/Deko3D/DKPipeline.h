// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include <deko3d.hpp>

#include "Common/CommonTypes.h"

#include "VideoCommon/AbstractPipeline.h"

namespace Deko3D
{
// deko3d has no pipeline object, so this mocks one for the Dolphin side of things.
class DKPipeline final : public AbstractPipeline
{
public:
  DKPipeline(const AbstractPipelineConfig& config, std::vector<u32> replay_words,
             DkPrimitive primitive);
  ~DKPipeline() override;

  // Injects the baked state setting commands into the command buffer.
  void Replay(DkCmdBuf cmdbuf) const;

  // False when the config's shaders had no DKSH behind them, which is the case for everything right now.
  bool IsValid() const { return !m_replay_words.empty(); }

  // deko3d carries primitive topology on each draw rather than in the pipeline.
  DkPrimitive GetPrimitive() const { return m_primitive; }
  AbstractPipelineUsage GetUsage() const { return m_config.usage; }

  static std::unique_ptr<DKPipeline> Create(const AbstractPipelineConfig& config);

private:
  std::vector<u32> m_replay_words;
  DkPrimitive m_primitive;
};
}  // namespace Deko3D
