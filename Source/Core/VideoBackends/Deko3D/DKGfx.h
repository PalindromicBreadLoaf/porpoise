// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string_view>

#include <deko3d.hpp>

#include "Common/CommonTypes.h"
#include "VideoCommon/AbstractGfx.h"

namespace Deko3D
{
class DKSwapChain;

class DKGfx final : public ::AbstractGfx
{
public:
  DKGfx(std::unique_ptr<DKSwapChain> swap_chain, float backbuffer_scale);
  ~DKGfx() override;

  static DKGfx* GetInstance() { return static_cast<DKGfx*>(g_gfx.get()); }

  bool IsHeadless() const override;

  std::unique_ptr<AbstractTexture> CreateTexture(const TextureConfig& config,
                                                 std::string_view name) override;
  std::unique_ptr<AbstractStagingTexture>
  CreateStagingTexture(StagingTextureType type, const TextureConfig& config) override;
  std::unique_ptr<AbstractFramebuffer>
  CreateFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                    std::vector<AbstractTexture*> additional_color_attachments) override;

  std::unique_ptr<AbstractShader>
  CreateShaderFromSource(ShaderStage stage, std::string_view source,
                         VideoCommon::ShaderIncluder* shader_includer,
                         std::string_view name) override;
  std::unique_ptr<AbstractShader> CreateShaderFromBinary(ShaderStage stage, const void* data,
                                                         size_t length,
                                                         std::string_view name) override;
  std::unique_ptr<NativeVertexFormat>
  CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl) override;
  std::unique_ptr<AbstractPipeline> CreatePipeline(const AbstractPipelineConfig& config,
                                                   const void* cache_data = nullptr,
                                                   size_t cache_data_length = 0) override;

  DKSwapChain* GetSwapChain() const { return m_swap_chain.get(); }

  void Flush() override;
  void WaitForGPUIdle() override;

  bool BindBackbuffer(const ClearColor& clear_color = {}) override;
  void PresentBackbuffer() override;

  SurfaceInfo GetSurfaceInfo() const override;

private:
  std::unique_ptr<DKSwapChain> m_swap_chain;
  float m_backbuffer_scale;

  int m_current_slot = -1;
};
}  // namespace Deko3D
