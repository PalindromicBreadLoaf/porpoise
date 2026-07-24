// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Deko3D/DKCommandBufferManager.h"

#include <algorithm>
#include <utility>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/Logging/Log.h"

#include "VideoBackends/Deko3D/DKContext.h"
#include "VideoBackends/Deko3D/DKSwapChain.h"

namespace Deko3D
{
std::unique_ptr<DKCommandBufferManager> g_dk_command_buffer_mgr;

void DKCommandBufferManager::CommandMemory::Rewind()
{
  // dkCmdBufClear only rewinds to the start of the most recently added slice, so the initial one
  // has to be re-added to get back to the beginning.
  cmdbuf.clear();
  cmdbuf.addMemory(initial_block, initial_offset, initial_size);
  next_growth_chunk = 0;
}

void DKCommandBufferManager::CommandMemory::Grow(size_t min_req_size)
{
  const u32 size = static_cast<u32>(Common::AlignUp(
      std::max<size_t>(min_req_size, COMMAND_BUFFER_GROWTH_SIZE), DK_MEMBLOCK_ALIGNMENT));

  if (next_growth_chunk == growth_chunks.size())
    growth_chunks.push_back(std::make_unique<GrowthChunk>());

  GrowthChunk& chunk = *growth_chunks[next_growth_chunk];
  if (!chunk.block || chunk.size < size)
  {
    // A command buffer is only rewound after the GPU has finished with everything it
    // previously recorded, so nothing still references the old chunk.
    chunk.block = dk::MemBlockMaker{g_dk_context->GetDevice(), size}
                      .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                      .create();
    chunk.size = size;
  }

  cmdbuf.addMemory(chunk.block, 0, chunk.size);
  ++next_growth_chunk;
}

DKCommandBufferManager::DKCommandBufferManager() = default;

DKCommandBufferManager::~DKCommandBufferManager()
{
  if (g_dk_context)
    g_dk_context->WaitIdle();

  for (CmdBufferResources& resources : m_command_buffers)
  {
    for (auto& cleanup : resources.cleanup_resources)
      cleanup();
    resources.cleanup_resources.clear();
  }
}

bool DKCommandBufferManager::Initialize()
{
  if (!CreateCommandBuffers())
    return false;

  // Give the first command buffer a counter so it can be recorded into straight away.
  m_command_buffers[0].fence_counter = m_next_fence_counter++;
  return true;
}

void DKCommandBufferManager::AddMemoryCallback(void* user_data, DkCmdBuf /*cmdbuf*/,
                                               size_t min_req_size)
{
  static_cast<CommandMemory*>(user_data)->Grow(min_req_size);
}

bool DKCommandBufferManager::CreateCommandBuffers()
{
  DkDevice device = g_dk_context->GetDevice();

  const u32 total_size = static_cast<u32>(Common::AlignUp(
      static_cast<u64>(INIT_COMMAND_BUFFER_SIZE + DRAW_COMMAND_BUFFER_SIZE) * NUM_COMMAND_BUFFERS,
      DK_MEMBLOCK_ALIGNMENT));

  m_command_memory = dk::MemBlockMaker{device, total_size}
                         .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                         .create();
  if (!m_command_memory)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: failed to allocate {} bytes of command memory", total_size);
    return false;
  }

  u32 offset = 0;
  const auto create = [&](CommandMemory& mem, u32 size) {
    mem.cmdbuf = dk::CmdBufMaker{device}
                     .setUserData(&mem)
                     .setCbAddMem(&DKCommandBufferManager::AddMemoryCallback)
                     .create();
    if (!mem.cmdbuf)
      return false;

    mem.initial_block = m_command_memory;
    mem.initial_offset = offset;
    mem.initial_size = size;
    mem.cmdbuf.addMemory(m_command_memory, offset, size);
    offset += size;
    return true;
  };

  for (CmdBufferResources& resources : m_command_buffers)
  {
    if (!create(resources.init, INIT_COMMAND_BUFFER_SIZE) ||
        !create(resources.draw, DRAW_COMMAND_BUFFER_SIZE))
    {
      ERROR_LOG_FMT(VIDEO, "deko3d: failed to create command buffer");
      return false;
    }
  }

  return true;
}

DkCmdBuf DKCommandBufferManager::GetCurrentInitCommandBuffer()
{
  CmdBufferResources& resources = m_command_buffers[m_current_cmd_buffer];
  resources.init_cmdbuf_used = true;
  return resources.init.cmdbuf;
}

DkCmdBuf DKCommandBufferManager::GetCurrentCommandBuffer() const
{
  return m_command_buffers[m_current_cmd_buffer].draw.cmdbuf;
}

u64 DKCommandBufferManager::GetCurrentFenceCounter() const
{
  return m_command_buffers[m_current_cmd_buffer].fence_counter;
}

void DKCommandBufferManager::DeferCleanup(std::function<void()> cleanup)
{
  m_command_buffers[m_current_cmd_buffer].cleanup_resources.push_back(std::move(cleanup));
}

void DKCommandBufferManager::SubmitCommandBuffer(bool wait_for_completion,
                                                 DKSwapChain* present_swap_chain, int present_slot)
{
  CmdBufferResources& resources = m_command_buffers[m_current_cmd_buffer];
  dk::Queue queue = g_dk_context->GetGraphicsQueue();

  // Uploads recorded this frame have to land before the draws that read them.
  if (resources.init_cmdbuf_used)
    queue.submitCommands(resources.init.cmdbuf.finishList());

  queue.submitCommands(resources.draw.cmdbuf.finishList());

  // Flush GPU caches as part of the signal, so anything these commands wrote is visible to the CPU.
  // TODO: this flushes on every submit for the benefit of the readback paths.
  // Narrow it to submits that actually have a readback pending.
  queue.signalFence(resources.fence, true);

  // Submitted work does not begin executing until the queue is flushed.
  if (present_swap_chain)
    present_swap_chain->Present(present_slot);
  else
    queue.flush();

  if (wait_for_completion)
    WaitForCommandBufferCompletion(m_current_cmd_buffer);

  BeginCommandBuffer();
}

void DKCommandBufferManager::BeginCommandBuffer()
{
  const u32 next_buffer_index = (m_current_cmd_buffer + 1) % NUM_COMMAND_BUFFERS;
  CmdBufferResources& resources = m_command_buffers[next_buffer_index];

  // Wait for the GPU to finish with everything the memory we are about to reuse still backs.
  if (resources.fence_counter > m_completed_fence_counter)
    WaitForCommandBufferCompletion(next_buffer_index);

  resources.init.Rewind();
  resources.draw.Rewind();
  resources.init_cmdbuf_used = false;
  resources.fence_counter = m_next_fence_counter++;
  m_current_cmd_buffer = next_buffer_index;
}

void DKCommandBufferManager::WaitForCommandBufferCompletion(u32 index)
{
  CmdBufferResources& resources = m_command_buffers[index];

  const DkResult res = dkFenceWait(&resources.fence, -1);
  if (res != DkResult_Success)
    ERROR_LOG_FMT(VIDEO, "deko3d: dkFenceWait failed ({})", static_cast<int>(res));

  // Clean up resources for every command buffer between the last known completed one and this one.
  const u64 now_completed_counter = resources.fence_counter;
  u32 cleanup_index = (m_current_cmd_buffer + 1) % NUM_COMMAND_BUFFERS;
  while (cleanup_index != m_current_cmd_buffer)
  {
    CmdBufferResources& cleanup_resources = m_command_buffers[cleanup_index];
    if (cleanup_resources.fence_counter > now_completed_counter)
      break;

    if (cleanup_resources.fence_counter > m_completed_fence_counter)
    {
      for (auto& cleanup : cleanup_resources.cleanup_resources)
        cleanup();
      cleanup_resources.cleanup_resources.clear();
    }

    cleanup_index = (cleanup_index + 1) % NUM_COMMAND_BUFFERS;
  }

  m_completed_fence_counter = now_completed_counter;
}

void DKCommandBufferManager::WaitForFenceCounter(u64 fence_counter)
{
  if (m_completed_fence_counter >= fence_counter)
    return;

  // Find the first command buffer that covers the counter we are waiting for.
  u32 index = (m_current_cmd_buffer + 1) % NUM_COMMAND_BUFFERS;
  while (index != m_current_cmd_buffer)
  {
    if (m_command_buffers[index].fence_counter >= fence_counter)
      break;

    index = (index + 1) % NUM_COMMAND_BUFFERS;
  }

  ASSERT(index != m_current_cmd_buffer);
  WaitForCommandBufferCompletion(index);
}
}  // namespace Deko3D
