# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

set(NXVK_PKGCONFIG "${DEVKITPRO}/portlibs/switch/bin/aarch64-none-elf-pkg-config"
    CACHE FILEPATH "devkitPro target pkg-config used to resolve the nxvk portlib")

if(NOT EXISTS "${NXVK_PKGCONFIG}")
  message(FATAL_ERROR
    "devkitPro target pkg-config not found:\n  ${NXVK_PKGCONFIG}\n"
    "Install devkitPro's switch-pkg-config, or point NXVK_PKGCONFIG at it.")
endif()

execute_process(
  COMMAND "${NXVK_PKGCONFIG}" --exists nxvk
  RESULT_VARIABLE _nxvk_absent)
if(_nxvk_absent)
  message(FATAL_ERROR
    "nxvk portlib not found via ${NXVK_PKGCONFIG}.\n"
    "Build and install it from the nxvk checkout `make && sudo make install`.")
endif()

execute_process(
  COMMAND "${NXVK_PKGCONFIG}" --cflags nxvk
  OUTPUT_VARIABLE _nxvk_cflags
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${NXVK_PKGCONFIG}" --libs nxvk
  OUTPUT_VARIABLE _nxvk_libs
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

separate_arguments(_nxvk_cflags NATIVE_COMMAND "${_nxvk_cflags}")
separate_arguments(_nxvk_libs NATIVE_COMMAND "${_nxvk_libs}")

set(_nxvk_includes "")
set(_nxvk_compile_opts "")
foreach(_flag IN LISTS _nxvk_cflags)
  if(_flag MATCHES "^-I(.+)$")
    list(APPEND _nxvk_includes "${CMAKE_MATCH_1}")
  else()
    list(APPEND _nxvk_compile_opts "${_flag}")
  endif()
endforeach()

add_library(nxvk INTERFACE)
target_include_directories(nxvk INTERFACE ${_nxvk_includes})
target_compile_options(nxvk INTERFACE ${_nxvk_compile_opts})
target_link_libraries(nxvk INTERFACE ${_nxvk_libs})
