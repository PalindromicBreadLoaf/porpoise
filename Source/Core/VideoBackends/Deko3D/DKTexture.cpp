// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Deko3D/DKTexture.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"

#include "VideoBackends/Deko3D/DKCommandBufferManager.h"
#include "VideoBackends/Deko3D/DKContext.h"

namespace Deko3D
{
namespace
{
// Frees a memory block once the command buffer currently being recorded has retired.
void DeferMemBlockDestruction(dk::UniqueMemBlock block)
{
  if (!block)
    return;

  if (g_dk_command_buffer_mgr)
  {
    auto shared = std::make_shared<dk::UniqueMemBlock>(std::move(block));
    g_dk_command_buffer_mgr->DeferCleanup([shared]() {});
  }
  // Otherwise the GPU has already been drained and the block is freed here.
}

DkImageType GetDkImageType(const TextureConfig& config)
{
  if (config.type == AbstractTextureType::Texture_CubeMap)
    return DkImageType_Cubemap;

  if (config.IsMultisampled())
    return config.layers > 1 ? DkImageType_2DMSArray : DkImageType_2DMS;

  if (config.type == AbstractTextureType::Texture_2D)
    return DkImageType_2D;

  return DkImageType_2DArray;
}

DkMsMode GetDkMsMode(u32 samples)
{
  switch (samples)
  {
  case 1:
    return DkMsMode_1x;
  case 2:
    return DkMsMode_2x;
  case 4:
    return DkMsMode_4x;
  case 8:
    return DkMsMode_8x;
  default:
    return DkMsMode_1x;
  }
}
}  // namespace

DKTexture::DKTexture(const TextureConfig& config, dk::UniqueMemBlock memblock,
                     const dk::ImageLayout& layout, const dk::Image& image,
                     const DkImageDescriptor& descriptor)
    : AbstractTexture(config), m_memblock(std::move(memblock)), m_layout(layout), m_image(image),
      m_descriptor(descriptor)
{
}

DKTexture::~DKTexture()
{
  DeferMemBlockDestruction(std::move(m_memblock));
}

DkImageFormat DKTexture::GetDkFormatForHostTextureFormat(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::DXT1:
    return DkImageFormat_RGBA_BC1;
  case AbstractTextureFormat::DXT3:
    return DkImageFormat_RGBA_BC2;
  case AbstractTextureFormat::DXT5:
    return DkImageFormat_RGBA_BC3;
  case AbstractTextureFormat::BPTC:
    return DkImageFormat_RGBA_BC7_Unorm;
  case AbstractTextureFormat::RGBA8:
    return DkImageFormat_RGBA8_Unorm;
  case AbstractTextureFormat::BGRA8:
    return DkImageFormat_BGRA8_Unorm;
  case AbstractTextureFormat::RGB10_A2:
    return DkImageFormat_RGB10A2_Unorm;
  case AbstractTextureFormat::RGBA16F:
    return DkImageFormat_RGBA16_Float;
  case AbstractTextureFormat::R16:
    return DkImageFormat_R16_Unorm;
  case AbstractTextureFormat::D16:
    return DkImageFormat_Z16;
  case AbstractTextureFormat::D24_S8:
    return DkImageFormat_Z24S8;
  case AbstractTextureFormat::R32F:
    return DkImageFormat_R32_Float;
  case AbstractTextureFormat::D32F:
    return DkImageFormat_ZF32;
  case AbstractTextureFormat::D32F_S8:
    return DkImageFormat_ZF32_X24S8;
  case AbstractTextureFormat::Undefined:
    return DkImageFormat_None;
  default:
    PanicAlertFmt("Unhandled texture format.");
    return DkImageFormat_RGBA8_Unorm;
  }
}

std::unique_ptr<DKTexture> DKTexture::Create(const TextureConfig& config, std::string_view name)
{
  DkDevice device = g_dk_context->GetDevice();

  u32 flags = DkImageFlags_Usage2DEngine;
  if (config.IsRenderTarget())
    flags |= DkImageFlags_UsageRender | DkImageFlags_HwCompression;
  if (config.IsComputeImage())
    flags |= DkImageFlags_UsageLoadStore;

  const DkImageType type = GetDkImageType(config);

  dk::ImageLayoutMaker layout_maker{device};
  layout_maker.setType(type)
      .setFlags(flags)
      .setFormat(GetDkFormatForHostTextureFormat(config.format))
      .setMsMode(GetDkMsMode(config.samples))
      .setMipLevels(config.levels);
  if (type == DkImageType_2D || type == DkImageType_2DMS)
    layout_maker.setDimensions(config.width, config.height);
  else
    layout_maker.setDimensions(config.width, config.height, config.layers);

  dk::ImageLayout layout;
  layout_maker.initialize(layout);

  dk::UniqueMemBlock memblock =
      dk::MemBlockMaker{device, static_cast<u32>(Common::AlignUp(layout.getSize(),
                                                                 std::max<u64>(layout.getAlignment(),
                                                                               DK_MEMBLOCK_ALIGNMENT)))}
          .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
          .create();
  if (!memblock)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: failed to allocate image memory for '{}'", name);
    return nullptr;
  }

  dk::Image image;
  image.initialize(layout, memblock, 0);

  dk::ImageView view{image};
  DkImageDescriptor descriptor;
  dkImageDescriptorInitialize(&descriptor, &view, config.IsComputeImage(), false);

  return std::make_unique<DKTexture>(config, std::move(memblock), layout, image, descriptor);
}

DkImageView DKTexture::MakeView(u32 level, u32 layer, u32 layer_count) const
{
  dk::ImageView view{m_image};
  view.setMipLevels(static_cast<u8>(level), 1);
  view.setLayers(static_cast<u16>(layer), static_cast<u16>(layer_count));
  return view;
}

void DKTexture::CopyRectangleFromTexture(const AbstractTexture* src,
                                         const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                         u32 src_level, const MathUtil::Rectangle<int>& dst_rect,
                                         u32 dst_layer, u32 dst_level)
{
  const DKTexture* src_texture = static_cast<const DKTexture*>(src);

  ASSERT_MSG(VIDEO,
             static_cast<u32>(src_rect.GetWidth()) <= src_texture->GetWidth() &&
                 static_cast<u32>(src_rect.GetHeight()) <= src_texture->GetHeight(),
             "Source rect is too large for CopyRectangleFromTexture");
  ASSERT_MSG(VIDEO,
             static_cast<u32>(dst_rect.GetWidth()) <= m_config.width &&
                 static_cast<u32>(dst_rect.GetHeight()) <= m_config.height,
             "Dest rect is too large for CopyRectangleFromTexture");

  const DkImageView src_view = src_texture->MakeView(src_level, src_layer, 1);
  const DkImageView dst_view = MakeView(dst_level, dst_layer, 1);

  const DkImageRect src_image_rect{static_cast<u32>(src_rect.left), static_cast<u32>(src_rect.top), 0,
                                   static_cast<u32>(src_rect.GetWidth()),
                                   static_cast<u32>(src_rect.GetHeight()), 1};
  const DkImageRect dst_image_rect{static_cast<u32>(dst_rect.left), static_cast<u32>(dst_rect.top), 0,
                                   static_cast<u32>(dst_rect.GetWidth()),
                                   static_cast<u32>(dst_rect.GetHeight()), 1};

  // The copy engine auto-synchronizes against the 3D engine on the subchannel switch.
  dkCmdBufCopyImage(g_dk_command_buffer_mgr->GetCurrentCommandBuffer(), &src_view, &src_image_rect,
                    &dst_view, &dst_image_rect, 0);
}

void DKTexture::ResolveFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& rect,
                                   u32 layer, u32 level)
{
  const DKTexture* src_texture = static_cast<const DKTexture*>(src);
  DEBUG_ASSERT(m_config.samples == 1 && m_config.width == src_texture->m_config.width &&
               m_config.height == src_texture->m_config.height && src_texture->m_config.samples > 1);

  const DkImageView src_view = src_texture->MakeView(level, layer, 1);
  const DkImageView dst_view = MakeView(level, layer, 1);

  const DkImageRect image_rect{static_cast<u32>(rect.left), static_cast<u32>(rect.top), 0,
                               static_cast<u32>(rect.GetWidth()), static_cast<u32>(rect.GetHeight()),
                               1};

  // A linearly filtered blit doubles as an MSAA resolve while still allowing a sub-rectangle.
  dkCmdBufBlitImage(g_dk_command_buffer_mgr->GetCurrentCommandBuffer(), &src_view, &image_rect,
                    &dst_view, &image_rect, DkBlitFlag_FilterLinear, 0);
}

void DKTexture::Load(u32 level, u32 width, u32 height, u32 row_length, const u8* buffer,
                     size_t buffer_size, u32 layer)
{
  width = std::max(1u, std::min(width, GetWidth() >> level));
  height = std::max(1u, std::min(height, GetHeight() >> level));

  const u32 block_size = AbstractTexture::GetBlockSizeForFormat(GetFormat());
  const u32 num_rows = Common::AlignUp(height, block_size) / block_size;
  const u32 source_pitch = AbstractTexture::CalculateStrideForFormat(m_config.format, row_length);
  const u32 upload_size = source_pitch * num_rows;

  // TODO: replace the per-upload block with a shared texture-upload ring.
  dk::UniqueMemBlock upload =
      dk::MemBlockMaker{g_dk_context->GetDevice(),
                        static_cast<u32>(Common::AlignUp(upload_size, DK_MEMBLOCK_ALIGNMENT))}
          .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
          .create();
  if (!upload)
  {
    PanicAlertFmt("Failed to allocate {} bytes for a texture upload.", upload_size);
    return;
  }

  std::memcpy(upload.getCpuAddr(), buffer, upload_size);

  // Deko3d's rowLength is the buffer row pitch in bytes.
  const DkCopyBuf src{upload.getGpuAddr(), source_pitch, 0};
  const DkImageView dst_view = MakeView(level, layer, 1);
  const DkImageRect dst_rect{0, 0, 0, width, height, 1};

  dkCmdBufCopyBufferToImage(g_dk_command_buffer_mgr->GetCurrentInitCommandBuffer(), &src, &dst_view,
                            &dst_rect, 0);

  DeferMemBlockDestruction(std::move(upload));
}

DKStagingTexture::DKStagingTexture(StagingTextureType type, const TextureConfig& config,
                                   dk::UniqueMemBlock memblock)
    : AbstractStagingTexture(type, config), m_memblock(std::move(memblock))
{
  m_map_pointer = static_cast<char*>(m_memblock.getCpuAddr());
  m_map_stride = config.GetStride();
}

DKStagingTexture::~DKStagingTexture()
{
  DeferMemBlockDestruction(std::move(m_memblock));
}

std::unique_ptr<DKStagingTexture> DKStagingTexture::Create(StagingTextureType type,
                                                           const TextureConfig& config)
{
  const size_t buffer_size = config.GetStride() * static_cast<size_t>(config.height);

  // GPU->CPU readback cannot invalidate the CPU cache safely, so keep those blocks uncached.
  // Upload blocks stay CPU-cached and are flushed before each copy.
  const u32 cpu_access = type == StagingTextureType::Upload ? DkMemBlockFlags_CpuCached :
                                                              DkMemBlockFlags_CpuUncached;

  dk::UniqueMemBlock memblock =
      dk::MemBlockMaker{g_dk_context->GetDevice(),
                        static_cast<u32>(Common::AlignUp(buffer_size, DK_MEMBLOCK_ALIGNMENT))}
          .setFlags(cpu_access | DkMemBlockFlags_GpuCached)
          .create();
  if (!memblock)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: failed to allocate a {} byte staging texture", buffer_size);
    return nullptr;
  }

  return std::make_unique<DKStagingTexture>(type, config, std::move(memblock));
}

void DKStagingTexture::CopyFromTexture(const AbstractTexture* src,
                                       const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                       u32 src_level, const MathUtil::Rectangle<int>& dst_rect)
{
  const DKTexture* src_tex = static_cast<const DKTexture*>(src);
  ASSERT(m_type == StagingTextureType::Readback || m_type == StagingTextureType::Mutable);
  ASSERT(src_rect.GetWidth() == dst_rect.GetWidth() &&
         src_rect.GetHeight() == dst_rect.GetHeight());

  DkCmdBuf cmdbuf = g_dk_command_buffer_mgr->GetCurrentCommandBuffer();

  const DkImageView src_view = src_tex->MakeView(src_level, src_layer, 1);
  const DkImageRect src_image_rect{static_cast<u32>(src_rect.left), static_cast<u32>(src_rect.top), 0,
                                   static_cast<u32>(src_rect.GetWidth()),
                                   static_cast<u32>(src_rect.GetHeight()), 1};

  const u32 offset = static_cast<u32>(static_cast<size_t>(dst_rect.top) * m_config.GetStride() +
                                      static_cast<size_t>(dst_rect.left) * m_texel_size);
  const DkCopyBuf dst{m_memblock.getGpuAddr() + offset, static_cast<u32>(m_config.GetStride()), 0};

  dkCmdBufCopyImageToBuffer(cmdbuf, &src_view, &src_image_rect, &dst, 0);

  // A forced switch to the 3D engine and invalidation of L2 is required so the written data actually reaches memory.
  const u32 threed_nop = 0x80000040;
  dkCmdBufReplayCmds(cmdbuf, &threed_nop, 1);
  dkCmdBufBarrier(cmdbuf, DkBarrier_None, DkInvalidateFlags_L2Cache);

  m_needs_flush = true;
  m_flush_fence_counter = g_dk_command_buffer_mgr->GetCurrentFenceCounter();
}

void DKStagingTexture::CopyToTexture(const MathUtil::Rectangle<int>& src_rect, AbstractTexture* dst,
                                     const MathUtil::Rectangle<int>& dst_rect, u32 dst_layer,
                                     u32 dst_level)
{
  const DKTexture* dst_tex = static_cast<const DKTexture*>(dst);
  ASSERT(m_type == StagingTextureType::Upload || m_type == StagingTextureType::Mutable);
  ASSERT(src_rect.GetWidth() == dst_rect.GetWidth() &&
         src_rect.GetHeight() == dst_rect.GetHeight());

  if (m_type == StagingTextureType::Upload)
    m_memblock.flushCpuCache(0, m_memblock.getSize());

  DkCmdBuf cmdbuf = g_dk_command_buffer_mgr->GetCurrentCommandBuffer();

  const u32 offset = static_cast<u32>(static_cast<size_t>(src_rect.top) * m_config.GetStride() +
                                      static_cast<size_t>(src_rect.left) * m_texel_size);
  const DkCopyBuf src{m_memblock.getGpuAddr() + offset, static_cast<u32>(m_config.GetStride()), 0};

  const DkImageView dst_view = dst_tex->MakeView(dst_level, dst_layer, 1);
  const DkImageRect dst_image_rect{static_cast<u32>(dst_rect.left), static_cast<u32>(dst_rect.top), 0,
                                   static_cast<u32>(dst_rect.GetWidth()),
                                   static_cast<u32>(dst_rect.GetHeight()), 1};

  dkCmdBufCopyBufferToImage(cmdbuf, &src, &dst_view, &dst_image_rect, 0);

  m_needs_flush = true;
  m_flush_fence_counter = g_dk_command_buffer_mgr->GetCurrentFenceCounter();
}

bool DKStagingTexture::Map()
{
  // Persistently mapped through the memory block's CPU address.
  return true;
}

void DKStagingTexture::Unmap()
{
}

void DKStagingTexture::Flush()
{
  if (!m_needs_flush)
    return;

  if (g_dk_command_buffer_mgr->GetCurrentFenceCounter() == m_flush_fence_counter)
    g_dk_command_buffer_mgr->SubmitCommandBuffer(true);
  else
    g_dk_command_buffer_mgr->WaitForFenceCounter(m_flush_fence_counter);

  // Readback/mutable blocks are CPU-uncached.
  m_needs_flush = false;
}

DKFramebuffer::DKFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                             std::vector<AbstractTexture*> additional_color_attachments,
                             AbstractTextureFormat color_format, AbstractTextureFormat depth_format,
                             u32 width, u32 height, u32 layers, u32 samples)
    : AbstractFramebuffer(color_attachment, depth_attachment,
                          std::move(additional_color_attachments), color_format, depth_format,
                          width, height, layers, samples)
{
}

std::unique_ptr<DKFramebuffer>
DKFramebuffer::Create(DKTexture* color_attachment, DKTexture* depth_attachment,
                      std::vector<AbstractTexture*> additional_color_attachments)
{
  if (!ValidateConfig(color_attachment, depth_attachment, additional_color_attachments))
    return nullptr;

  const AbstractTextureFormat color_format =
      color_attachment ? color_attachment->GetFormat() : AbstractTextureFormat::Undefined;
  const AbstractTextureFormat depth_format =
      depth_attachment ? depth_attachment->GetFormat() : AbstractTextureFormat::Undefined;
  const DKTexture* either_attachment = color_attachment ? color_attachment : depth_attachment;
  const u32 width = either_attachment->GetWidth();
  const u32 height = either_attachment->GetHeight();
  const u32 layers = either_attachment->GetLayers();
  const u32 samples = either_attachment->GetSamples();

  return std::make_unique<DKFramebuffer>(color_attachment, depth_attachment,
                                         std::move(additional_color_attachments), color_format,
                                         depth_format, width, height, layers, samples);
}

void DKFramebuffer::Bind(DkCmdBuf cmdbuf) const
{
  std::vector<DkImageView> color_views;
  std::vector<const DkImageView*> color_target_ptrs;
  color_views.reserve(1 + m_additional_color_attachments.size());

  const auto add_color = [&](AbstractTexture* attachment) {
    color_views.push_back(static_cast<DKTexture*>(attachment)->MakeView(0, 0, GetLayers()));
  };

  if (m_color_attachment)
    add_color(m_color_attachment);
  for (auto* attachment : m_additional_color_attachments)
    add_color(attachment);

  // color_views is fully populated, so pointers into it stay valid for the bind call.
  for (const DkImageView& view : color_views)
    color_target_ptrs.push_back(&view);

  DkImageView depth_view;
  if (m_depth_attachment)
    depth_view = static_cast<DKTexture*>(m_depth_attachment)->MakeView(0, 0, GetLayers());

  dkCmdBufBindRenderTargets(cmdbuf, color_target_ptrs.data(),
                            static_cast<u32>(color_target_ptrs.size()),
                            m_depth_attachment ? &depth_view : nullptr);
}
}  // namespace Deko3D
