# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(videodeko3d
  Constants.h
  DKBoundingBox.h
  DKCommandBufferManager.cpp
  DKCommandBufferManager.h
  DKContext.cpp
  DKContext.h
  DKGfx.cpp
  DKGfx.h
  DKMain.cpp
  DKPerfQuery.h
  DKPipeline.h
  DKShader.h
  DKStreamBuffer.cpp
  DKStreamBuffer.h
  DKSwapChain.cpp
  DKSwapChain.h
  DKTexture.cpp
  DKTexture.h
  DKVertexManager.cpp
  DKVertexManager.h
  VideoBackend.h
)

target_link_libraries(videodeko3d
PUBLIC
  common
  videocommon

PRIVATE
  deko3d
)
