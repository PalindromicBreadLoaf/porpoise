// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "Common/CommonTypes.h"

#include "VideoCommon/VertexManagerBase.h"

namespace Deko3D
{
class DKStreamBuffer;

class DKVertexManager final : public VertexManagerBase
{
public:
  DKVertexManager();
  ~DKVertexManager() override;

  bool Initialize() override;

  void UploadUtilityUniforms(const void* uniforms, u32 uniforms_size) override;

protected:
  void ResetBuffer(u32 vertex_stride) override;
  void CommitBuffer(u32 num_vertices, u32 vertex_stride, u32 num_indices, u32* out_base_vertex,
                    u32* out_base_index) override;
  void UploadUniforms() override;

private:
  void UpdateVertexShaderConstants();
  void UpdateGeometryShaderConstants();
  void UpdatePixelShaderConstants();

  // Allocates uniform storage for one stage's constants.
  bool ReserveConstantStorage();
  void UploadAllConstants();

  std::unique_ptr<DKStreamBuffer> m_vertex_stream_buffer;
  std::unique_ptr<DKStreamBuffer> m_index_stream_buffer;
  std::unique_ptr<DKStreamBuffer> m_uniform_stream_buffer;
  u32 m_uniform_buffer_reserve_size = 0;
};
}  // namespace Deko3D
