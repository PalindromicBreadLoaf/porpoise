// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <unordered_map>

#include <deko3d.hpp>

#include "Common/CommonTypes.h"

#include "VideoBackends/Deko3D/Constants.h"
#include "VideoCommon/RenderState.h"

namespace Deko3D
{
// Owns the sampler descriptor set. deko3d has no sampler objects.
// This maps Dolphin's SamplerStates onto those indices, which is the deko3d equivalent
// of the Vulkan backend's VkSampler cache.
class DKObjectCache
{
public:
  ~DKObjectCache();

  static std::unique_ptr<DKObjectCache> Create();

  // Index into the sampler descriptor set for the given state, writing a new descriptor the first
  // time a state is seen. Falls back to the point sampler once the table is full.
  u32 GetSamplerIndex(const SamplerState& state);
  u32 GetPointSamplerIndex() const { return m_point_sampler_index; }

  DkGpuAddr GetSamplerDescriptorSetAddr() const { return m_descriptor_set_addr; }
  static constexpr u32 GetSamplerDescriptorCount() { return NUM_SAMPLER_DESCRIPTORS; }

  // Drops every cached state, keeping only the point sampler.
  void ClearSamplerCache();

private:
  DKObjectCache() = default;
  bool Initialize();

  u32 AllocateSampler(const DkSampler& sampler);

  dk::UniqueMemBlock m_descriptor_block;
  DkSamplerDescriptor* m_descriptors = nullptr;
  DkGpuAddr m_descriptor_set_addr = DK_GPU_ADDR_INVALID;

  std::unordered_map<SamplerState, u32> m_sampler_cache;
  u32 m_next_index = 0;
  u32 m_point_sampler_index = 0;
};

extern std::unique_ptr<DKObjectCache> g_dk_object_cache;
}  // namespace Deko3D
