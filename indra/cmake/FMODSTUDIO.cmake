# -*- cmake -*-
include_guard()
include(Prebuilt)

use_prebuilt_binary(fmodstudio)

set(FMODSTUDIO ON CACHE BOOL "Using FMODSTUDIO sound library.")

set(FMODSTUDIO_LIBRARY
	debug fmodL_vc
	optimized fmod_vc)

set(FMODSTUDIO_LIBRARIES ${FMODSTUDIO_LIBRARY})
set(FMODSTUDIO_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/)

if(FMODSTUDIO)
	message(STATUS "Building with FMODSTUDIO audio support")
	add_definitions(-DLL_FMODSTUDIO=1)
endif()