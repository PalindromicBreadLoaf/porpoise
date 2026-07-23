# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

option(DEKO3D_DEBUG "Link the debug build of deko3d" ON)

set(DEKO3D_INCLUDE_DIR "${DEVKITPRO}/libnx/include")
set(DEKO3D_LIB_DIR "${DEVKITPRO}/libnx/lib")

if(DEKO3D_DEBUG)
  set(DEKO3D_LIBRARY "${DEKO3D_LIB_DIR}/libdeko3dd.a")
else()
  set(DEKO3D_LIBRARY "${DEKO3D_LIB_DIR}/libdeko3d.a")
endif()

set(_deko3d_missing "")
foreach(_f "${DEKO3D_INCLUDE_DIR}/deko3d.h" "${DEKO3D_INCLUDE_DIR}/deko3d.hpp" "${DEKO3D_LIBRARY}")
  if(NOT EXISTS "${_f}")
    list(APPEND _deko3d_missing "${_f}")
  endif()
endforeach()

if(_deko3d_missing)
  string(REPLACE ";" "\n  " _deko3d_missing_pretty "${_deko3d_missing}")
  message(FATAL_ERROR
    "deko3d not found:\n  ${_deko3d_missing_pretty}\n"
    "Install it with dkp-pacman -S switch-tools deko3d.")
endif()

add_library(deko3d INTERFACE)
target_include_directories(deko3d INTERFACE "${DEKO3D_INCLUDE_DIR}")
target_link_libraries(deko3d INTERFACE "${DEKO3D_LIBRARY}")
