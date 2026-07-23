// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include "Common/CommonTypes.h"

#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/AbstractTexture.h"

// TODO: back these with real DkImage/DkImageLayout/DkImageDescriptor storage and the copy/blit
// engine.

namespace Deko3D
{
class DKTexture final : public AbstractTexture
{
public:
  explicit DKTexture(const TextureConfig& config);

  void CopyRectangleFromTexture(const AbstractTexture* src,
                                const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                u32 src_level, const MathUtil::Rectangle<int>& dst_rect,
                                u32 dst_layer, u32 dst_level) override;
  void ResolveFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& rect,
                          u32 layer, u32 level) override;
  void Load(u32 level, u32 width, u32 height, u32 row_length, const u8* buffer, size_t buffer_size,
            u32 layer) override;
};

class DKStagingTexture final : public AbstractStagingTexture
{
public:
  explicit DKStagingTexture(StagingTextureType type, const TextureConfig& config);
  ~DKStagingTexture() override;

  void CopyFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& src_rect,
                       u32 src_layer, u32 src_level,
                       const MathUtil::Rectangle<int>& dst_rect) override;
  void CopyToTexture(const MathUtil::Rectangle<int>& src_rect, AbstractTexture* dst,
                     const MathUtil::Rectangle<int>& dst_rect, u32 dst_layer,
                     u32 dst_level) override;

  bool Map() override;
  void Unmap() override;
  void Flush() override;

private:
  std::vector<u8> m_texture_buf;
};

class DKFramebuffer final : public AbstractFramebuffer
{
public:
  explicit DKFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                         std::vector<AbstractTexture*> additional_color_attachments,
                         AbstractTextureFormat color_format, AbstractTextureFormat depth_format,
                         u32 width, u32 height, u32 layers, u32 samples);

  static std::unique_ptr<DKFramebuffer>
  Create(DKTexture* color_attachment, DKTexture* depth_attachment,
         std::vector<AbstractTexture*> additional_color_attachments);
};
}  // namespace Deko3D
