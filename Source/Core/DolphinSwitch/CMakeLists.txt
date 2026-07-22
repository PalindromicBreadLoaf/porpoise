add_executable(porpoise
  CrashHandlerSwitch.cpp
  GamePickerSwitch.cpp
  HorizonCompatSwitch.cpp
  HostSwitch.cpp
  MainSwitch.cpp
  PlatformSwitch.cpp
)

target_link_libraries(porpoise
PRIVATE
  core
  uicommon
)

if(ENABLE_VULKAN)
  # NXVK driver archives are self-contained and resolve backwards into Dolphin's own.
  target_link_libraries(porpoise PRIVATE nxvk)
endif()

nx_generate_nacp(OUTPUT porpoise.nacp
  NAME "Porpoise"
  AUTHOR "Dolphin Emulator Project \& PalindromicBreadLoaf"
  VERSION "${PORPOISE_VERSION}"
)

# Defines the `porpoise_nro` target that the Switch build is driven through.
nx_create_nro(porpoise
  OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/porpoise.nro"
  NACP "${CMAKE_CURRENT_BINARY_DIR}/porpoise.nacp"
)
