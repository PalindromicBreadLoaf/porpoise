// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Deko3D/DKShaderCompiler.h"

#include <cstdlib>
#include <mutex>
#include <string>

#include "Common/Logging/Log.h"
#include "Common/Timer.h"

#include "VideoBackends/Deko3D/UamBridge.h"

namespace Deko3D::ShaderCompiler
{
namespace
{
// The Vulkan backend's header with the descriptor sets removed.
//
// TEXEL_BUFFER_BINDING and INPUT_ATTACHMENT_BINDING are deliberately left undefined: deko3d has no
// texel buffer object and no input attachments, and the features that use them are reported
// unsupported.
//
// gl_VertexID/gl_InstanceID keep their GL names
constexpr char SHADER_HEADER[] = R"(
  #version 450 core

  #define ATTRIBUTE_LOCATION(x) layout(location = x)
  #define FRAGMENT_OUTPUT_LOCATION(x) layout(location = x)
  #define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y) layout(location = x, index = y)
  #define UBO_BINDING(packing, x) layout(packing, binding = (x - 1))
  #define SAMPLER_BINDING(x) layout(binding = x)
  #define SSBO_BINDING(x) layout(std430, binding = x)
  #define VARYING_LOCATION(x) layout(location = x)
  #define FORCE_EARLY_Z layout(early_fragment_tests) in

  // hlsl to glsl function translation
  #define API_VULKAN 1
  #define float2 vec2
  #define float3 vec3
  #define float4 vec4
  #define uint2 uvec2
  #define uint3 uvec3
  #define uint4 uvec4
  #define int2 ivec2
  #define int3 ivec3
  #define int4 ivec4
  #define frac fract
  #define lerp mix
)";

// Compute pipelines bind their uniform buffer, textures, and images from index zero on the compute stage.
constexpr char COMPUTE_SHADER_HEADER[] = R"(
  #version 450 core

  #define UBO_BINDING(packing, x) layout(packing, binding = (x - 1))
  #define SAMPLER_BINDING(x) layout(binding = x)
  #define IMAGE_BINDING(format, x) layout(format, binding = x)

  #define API_VULKAN 1
  #define float2 vec2
  #define float3 vec3
  #define float4 vec4
  #define uint2 uvec2
  #define uint3 uvec3
  #define uint4 uvec4
  #define int2 ivec2
  #define int3 ivec3
  #define int4 ivec4
  #define frac fract
  #define lerp mix
)";

int StageToUam(ShaderStage stage)
{
  switch (stage)
  {
  case ShaderStage::Vertex:
    return UamStage_Vertex;
  case ShaderStage::Geometry:
    return UamStage_Geometry;
  case ShaderStage::Pixel:
    return UamStage_Fragment;
  case ShaderStage::Compute:
    return UamStage_Compute;
  default:
    return UamStage_Vertex;
  }
}

// uam is mesa 19.0's single-threaded standalone compiler.
std::mutex s_compile_mutex;
}  // namespace

std::optional<std::vector<u8>> CompileShader(ShaderStage stage, std::string_view source,
                                             std::string_view name)
{
  const std::string_view header =
      stage == ShaderStage::Compute ? COMPUTE_SHADER_HEADER : SHADER_HEADER;

  std::string full_source;
  full_source.reserve(header.size() + source.size() + 1);
  full_source.append(header);
  full_source.append(source);

  Common::Timer timer;
  timer.Start();

  size_t dksh_size = 0;
  char* log = nullptr;
  void* dksh = nullptr;
  {
    std::lock_guard guard(s_compile_mutex);
    dksh = UamCompileGlsl(full_source.c_str(), StageToUam(stage), &dksh_size, &log);
  }

  const bool have_log = log != nullptr && log[0] != '\0';
  if (!dksh)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: uam rejected '{}':\n{}", name,
                  have_log ? log : "(no diagnostics)");
    std::free(log);
    return std::nullopt;
  }

  if (have_log)
    WARN_LOG_FMT(VIDEO, "deko3d: uam warnings for '{}':\n{}", name, log);
  std::free(log);

  DEBUG_LOG_FMT(VIDEO, "deko3d: compiled '{}' to {} bytes of DKSH in {} ms", name, dksh_size,
                timer.ElapsedMs());

  const auto* bytes = static_cast<const u8*>(dksh);
  std::vector<u8> blob(bytes, bytes + dksh_size);
  std::free(dksh);
  return blob;
}
}  // namespace Deko3D::ShaderCompiler
