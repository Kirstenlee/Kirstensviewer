# -*- cmake -*-
include_guard()
include(Linking)
include(Prebuilt)

add_library(ll::hunspell INTERFACE IMPORTED)

use_prebuilt_binary(dictionaries)
use_prebuilt_binary(libhunspell)

target_include_directories(ll::hunspell SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)

if(WINDOWS)
    target_compile_definitions(ll::hunspell INTERFACE HUNSPELL_STATIC=1)
    target_link_libraries(ll::hunspell INTERFACE
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/hunspell-1.7.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/hunspell-1.7.lib  # match vcpkg version, not 1.8 which is the latest release but not yet in vcpkg
        )
else()
    message(FATAL_ERROR "Invalid platform")
endif()




