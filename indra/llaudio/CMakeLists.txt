# -*- cmake -*-

project(llaudio)

include(00-Common)
include(LLAudio)
include(FMODSTUDIO)
include(OpenAL)
include(LLCommon)
include(LLMath)
include(LLMessage)
include(LLFileSystem)

include_directories(
    ${LLAUDIO_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${OPENAL_LIB_INCLUDE_DIRS}
    )

set(llaudio_SOURCE_FILES
    llaudioengine.cpp
    lllistener.cpp
    llaudiodecodemgr.cpp
    llvorbisencode.cpp
    )

set(llaudio_HEADER_FILES
    CMakeLists.txt

    llaudioengine.h
    lllistener.h
    llaudiodecodemgr.h
    llvorbisencode.h
    llwindgen.h
    )

if (FMODSTUDIO)
    include_directories(
        ${FMODSTUDIO_INCLUDE_DIR}
        )
    list(APPEND llaudio_SOURCE_FILES
         llaudioengine_fmodstudio.cpp
         lllistener_fmodstudio.cpp
         llstreamingaudio_fmodstudio.cpp
         )

    list(APPEND llaudio_HEADER_FILES
         llaudioengine_fmodstudio.h
         lllistener_fmodstudio.h
         llstreamingaudio_fmodstudio.h
         )
endif()

if (OPENAL)
  list(APPEND llaudio_SOURCE_FILES
    llaudioengine_openal.cpp
    lllistener_openal.cpp
    )

  list(APPEND llaudio_HEADER_FILES
    llaudioengine_openal.h
    lllistener_openal.h
    )
endif()

list(APPEND llaudio_SOURCE_FILES ${llaudio_HEADER_FILES})

add_library (llaudio ${llaudio_SOURCE_FILES})
target_include_directories( llaudio  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries( llaudio
    ${LLCOMMON_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLMESSAGE_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
        ll::vorbis
    )

get_directory_property(ALLDEFINES COMPILE_DEFINITIONS)
message("llaudio COMPILE_DEFINITIONS = ${ALLDEFINES}")
