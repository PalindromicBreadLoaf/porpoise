// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Deko3D/DKGfx.h"

#include "Common/Logging/Log.h"

#include "VideoBackends/Deko3D/DKContext.h"
#include "VideoBackends/Deko3D/DKPipeline.h"
#include "VideoBackends/Deko3D/DKShader.h"
#include "VideoBackends/Deko3D/DKSwapChain.h"
#include "VideoBackends/Deko3D/DKTexture.h"

#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/VideoConfig.h"

namespace Deko3D
{
// Backing store for the scratch clear/present command buffer. A clear is only a handful of command
// words, so a single page should be ample.
static constexpr u32 SCRATCH_CMDBUF_SIZE = 0x1000;

DKGfx::DKGfx(std::unique_ptr<DKSwapChain> swap_chain, float backbuffer_scale)
    : m_swap_chain(std::move(swap_chain)), m_backbuffer_scale(backbuffer_scale)
{
  DkDevice device = g_dk_context->GetDevice();
  m_cmdbuf_memblock = dk::MemBlockMaker{device, SCRATCH_CMDBUF_SIZE}
                          .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                          .create();
  m_cmdbuf = dk::CmdBufMaker{device}.create();
  m_cmdbuf.addMemory(m_cmdbuf_memblock, 0, SCRATCH_CMDBUF_SIZE);

  UpdateActiveConfig();
}

DKGfx::~DKGfx()
{
  if (g_dk_context)
    g_dk_context->WaitIdle();
  UpdateActiveConfig();
}

bool DKGfx::IsHeadless() const
{
  return m_swap_chain == nullptr;
}

std::unique_ptr<AbstractTexture> DKGfx::CreateTexture(const TextureConfig& config,
                                                      std::string_view /*name*/)
{
  return std::make_unique<DKTexture>(config);
}

std::unique_ptr<AbstractStagingTexture> DKGfx::CreateStagingTexture(StagingTextureType type,
                                                                    const TextureConfig& config)
{
  return std::make_unique<DKStagingTexture>(type, config);
}

std::unique_ptr<AbstractFramebuffer>
DKGfx::CreateFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                         std::vector<AbstractTexture*> additional_color_attachments)
{
  return DKFramebuffer::Create(static_cast<DKTexture*>(color_attachment),
                               static_cast<DKTexture*>(depth_attachment),
                               std::move(additional_color_attachments));
}

std::unique_ptr<AbstractShader>
DKGfx::CreateShaderFromSource(ShaderStage stage, std::string_view /*source*/,
                              VideoCommon::ShaderIncluder* /*shader_includer*/,
                              std::string_view /*name*/)
{
  return std::make_unique<DKShader>(stage);
}

std::unique_ptr<AbstractShader> DKGfx::CreateShaderFromBinary(ShaderStage stage,
                                                              const void* /*data*/,
                                                              size_t /*length*/,
                                                              std::string_view /*name*/)
{
  return std::make_unique<DKShader>(stage);
}

std::unique_ptr<NativeVertexFormat>
DKGfx::CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl)
{
  return std::make_unique<NativeVertexFormat>(vtx_decl);
}

std::unique_ptr<AbstractPipeline> DKGfx::CreatePipeline(const AbstractPipelineConfig& /*config*/,
                                                        const void* /*cache_data*/,
                                                        size_t /*cache_data_length*/)
{
  return std::make_unique<DKPipeline>();
}

void DKGfx::Flush()
{
  g_dk_context->GetGraphicsQueue().flush();
}

void DKGfx::WaitForGPUIdle()
{
  g_dk_context->WaitIdle();
}

bool DKGfx::BindBackbuffer(const ClearColor& clear_color)
{
  if (!m_swap_chain)
    return false;

  m_current_slot = m_swap_chain->Acquire();
  if (m_current_slot < 0)
    return false;

  // Record a clear of the acquired image. The presenter's XFB blit and any UI drawing land on top
  // of this once the draw path exists.
  DkCmdBuf cmdbuf = m_cmdbuf;
  dkCmdBufClear(cmdbuf);

  dk::ImageView view{m_swap_chain->GetImage(m_current_slot)};
  dkCmdBufBindRenderTarget(cmdbuf, &view, nullptr);

  // clearColor is scissored so bound the clear to the whole target.
  const DkScissor scissor{0, 0, m_swap_chain->GetWidth(), m_swap_chain->GetHeight()};
  dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);
  dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, clear_color[0], clear_color[1],
                          clear_color[2], clear_color[3]);

  g_dk_context->GetGraphicsQueue().submitCommands(dkCmdBufFinishList(cmdbuf));
  return true;
}

void DKGfx::PresentBackbuffer()
{
  if (!m_swap_chain || m_current_slot < 0)
    return;

  // presentImage flushes the queue, kicking the clear recorded in BindBackbuffer.
  m_swap_chain->Present(m_current_slot);
  m_current_slot = -1;
}

SurfaceInfo DKGfx::GetSurfaceInfo() const
{
  return {m_swap_chain ? m_swap_chain->GetWidth() : 1u,
          m_swap_chain ? m_swap_chain->GetHeight() : 0u, m_backbuffer_scale,
          DKSwapChain::GetTextureFormat()};
}
}  // namespace Deko3D
