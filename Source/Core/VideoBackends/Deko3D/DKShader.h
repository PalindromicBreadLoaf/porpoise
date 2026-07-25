// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include <deko3d.hpp>

#include "Common/CommonTypes.h"

#include "VideoCommon/AbstractShader.h"

namespace Deko3D
{
// Wraps a DkShader plus the code DkMemBlock it lives in.
class DKShader final : public AbstractShader
{
public:
  DKShader(ShaderStage stage, std::vector<u8> dksh, dk::UniqueMemBlock code_block,
           const DkShader& shader);
  ~DKShader() override;

  // The pipeline bakes this into its captured bind commands.
  const DkShader* GetShader() const { return &m_shader; }
  bool IsValid() const { return dkShaderIsValid(&m_shader); }

  // Returns the DKSH blob for the disk shader cache.
  BinaryData GetBinary() const override;

  static std::unique_ptr<DKShader> CreateFromBinary(ShaderStage stage, const void* data,
                                                    size_t length, std::string_view name);

private:
  std::vector<u8> m_dksh;
  dk::UniqueMemBlock m_code_block;
  DkShader m_shader = {};
};
}  // namespace Deko3D
