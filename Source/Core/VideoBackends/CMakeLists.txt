if(NOT PORPOISE)
  add_subdirectory(OGL)
endif()
add_subdirectory(Null)
add_subdirectory(Software)

if(WIN32)
  add_subdirectory(D3DCommon)
  add_subdirectory(D3D)
  add_subdirectory(D3D12)
endif()

if(APPLE)
  add_subdirectory(Metal)
endif()

if(ENABLE_VULKAN)
  add_subdirectory(Vulkan)
endif()

if(ENABLE_DEKO3D)
  add_subdirectory(Deko3D)
endif()
