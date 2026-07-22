# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

# Horizon has no Vulkan loader. NXVK is consumed as prebuilt static archives linked in to the NRO.

set(NXVK_ROOT "${CMAKE_SOURCE_DIR}/../nxvk" CACHE PATH "Root of the nxvk checkout")
set(NXVK_BUILD_DIR "${NXVK_ROOT}/switch/build/cross" CACHE PATH "nxvk cross build directory")

set(NXVK_DRIVER "${NXVK_BUILD_DIR}/src/nouveau/vulkan/libnvk.a")

# Everything libnvk.a pulls in. These go in a link group, so this order is only a hint.
set(_nxvk_support
  src/util/libmesa_util.a
  src/util/libmesa_util_simd.a
  src/util/blake3/libblake3.a
  src/c11/impl/libmesa_util_c11.a
  src/nouveau/compiler/libnak.a
  src/nouveau/compiler/libnak_rs.a
  src/compiler/rust/libcompiler_c_helpers.a
  src/nouveau/headers/libnvidia_headers_c.a
  src/nouveau/nil/libnil.a
  src/nouveau/nil/liblibnil_format_table.a
  src/compiler/nir/libnir.a
  src/compiler/libcompiler.a
  src/nouveau/mme/libnouveau_mme.a
  src/nouveau/winsys/libnouveau_ws.a
  src/vulkan/util/libvulkan_util.a
  src/compiler/spirv/libvtn.a
  src/util/libxmlconfig.a
)

set(_nxvk_missing "")
if(NOT EXISTS "${NXVK_DRIVER}")
  list(APPEND _nxvk_missing "${NXVK_DRIVER}")
endif()

set(_nxvk_support_paths "")
foreach(_lib IN LISTS _nxvk_support)
  if(EXISTS "${NXVK_BUILD_DIR}/${_lib}")
    list(APPEND _nxvk_support_paths "${NXVK_BUILD_DIR}/${_lib}")
  else()
    list(APPEND _nxvk_missing "${NXVK_BUILD_DIR}/${_lib}")
  endif()
endforeach()

if(_nxvk_missing)
  string(REPLACE ";" "\n  " _nxvk_missing_pretty "${_nxvk_missing}")
  message(FATAL_ERROR
    "NXVK archives not found:\n  ${_nxvk_missing_pretty}\n"
    "Build the driver (switch/build/configure-mesa.sh, then ninja) or point NXVK_ROOT / "
    "NXVK_BUILD_DIR at an existing build.")
endif()

# Mesa's driconf parser only wants expat. Dolphin can satisfy the rest.
set(_nxvk_portlibs "${DEVKITPRO}/portlibs/switch/lib/libexpat.a")

add_library(nxvk INTERFACE)
target_link_libraries(nxvk INTERFACE
  -Wl,--whole-archive "${NXVK_DRIVER}" -Wl,--no-whole-archive
  -Wl,--start-group ${_nxvk_support_paths} ${_nxvk_portlibs} -Wl,--end-group
)
