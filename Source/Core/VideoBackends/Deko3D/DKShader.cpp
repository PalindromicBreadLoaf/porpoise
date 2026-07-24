// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Deko3D/DKShader.h"

#include <cstring>

#include "Common/Align.h"
#include "Common/Logging/Log.h"

#include "VideoBackends/Deko3D/DKContext.h"

namespace Deko3D
{
namespace
{
constexpr u32 DKSH_MAGIC = 0x48534B44;  // 'DKSH'

// Header at the start of every DKSH blob.
struct DkshHeader
{
  u32 magic;
  u32 header_sz;
  u32 control_sz;
  u32 code_sz;
  u32 programs_off;
  u32 num_programs;
};
}  // namespace

DKShader::DKShader(ShaderStage stage) : AbstractShader(stage)
{
}

DKShader::DKShader(ShaderStage stage, std::vector<u8> dksh, dk::UniqueMemBlock code_block,
                   const DkShader& shader)
    : AbstractShader(stage), m_dksh(std::move(dksh)), m_code_block(std::move(code_block)),
      m_shader(shader)
{
}

DKShader::~DKShader() = default;

AbstractShader::BinaryData DKShader::GetBinary() const
{
  return m_dksh;
}

std::unique_ptr<DKShader> DKShader::CreateFromBinary(ShaderStage stage, const void* data,
                                                     size_t length, std::string_view name)
{
  if (length < sizeof(DkshHeader))
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: DKSH blob for '{}' is too small ({} bytes)", name, length);
    return nullptr;
  }

  std::vector<u8> dksh(length);
  std::memcpy(dksh.data(), data, length);

  DkshHeader header;
  std::memcpy(&header, dksh.data(), sizeof(header));
  if (header.magic != DKSH_MAGIC ||
      static_cast<size_t>(header.control_sz) + header.code_sz > length)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: '{}' is not a valid DKSH blob", name);
    return nullptr;
  }

  // The last DK_SHADER_CODE_UNUSABLE_SIZE bytes of any code block cannot hold shader code,
  // so pad the allocation past the code section.
  const u32 block_size = static_cast<u32>(
      Common::AlignUp(header.code_sz + DK_SHADER_CODE_UNUSABLE_SIZE, DK_MEMBLOCK_ALIGNMENT));

  // TODO: recycle a shared code-segment allocator instead of one block per shader.
  dk::UniqueMemBlock code_block =
      dk::MemBlockMaker{g_dk_context->GetDevice(), block_size}
          .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code)
          .create();
  if (!code_block)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: failed to allocate {} bytes of shader code memory", block_size);
    return nullptr;
  }

  // The code section follows the control section in the blob. Only it is copied to GPU code
  // memory. Deko3d reads the control section from the CPU-side copy during initialization.
  std::memcpy(code_block.getCpuAddr(), dksh.data() + header.control_sz, header.code_sz);

  DkShaderMaker maker;
  dkShaderMakerDefaults(&maker, code_block, 0);
  maker.control = dksh.data();

  DkShader shader;
  dkShaderInitialize(&shader, &maker);
  if (!dkShaderIsValid(&shader))
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: dkShaderInitialize rejected '{}'", name);
    return nullptr;
  }

  return std::make_unique<DKShader>(stage, std::move(dksh), std::move(code_block), shader);
}
}  // namespace Deko3D
