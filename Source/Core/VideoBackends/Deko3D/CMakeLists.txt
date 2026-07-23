# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(videodeko3d
  DKBoundingBox.h
  DKContext.cpp
  DKContext.h
  DKGfx.cpp
  DKGfx.h
  DKMain.cpp
  DKPerfQuery.h
  DKPipeline.h
  DKShader.h
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
