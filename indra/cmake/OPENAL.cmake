# -*- cmake -*-
include_guard()
include(Linking)
include(Prebuilt)

set(OPENAL OFF CACHE BOOL "Enable OpenAL")

if(OPENAL)

  if(NOT OPENAL_LIB_FOUND OR NOT FREEALUT_LIB_FOUND)
    use_prebuilt_binary(openal-soft)
  endif()
  set(OPENAL_LIBRARIES 
    openal
    alut
    )
endif()

if(OPENAL)
  message(STATUS "Building with OpenAL audio support")
  add_definitions(-DLL_OPENAL=1)
endif()
