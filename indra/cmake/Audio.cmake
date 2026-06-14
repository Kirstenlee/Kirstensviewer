# -*- cmake -*-
include(Linking)
include(Prebuilt)

include_guard()
add_library(ll::vorbis INTERFACE IMPORTED)

use_prebuilt_binary(ogg_vorbis)
target_include_directories(ll::vorbis SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)

target_link_libraries(ll::vorbis INTERFACE
    ${ARCH_PREBUILT_DIRS_RELEASE}/ogg.lib
    ${ARCH_PREBUILT_DIRS_RELEASE}/vorbisenc.lib
    ${ARCH_PREBUILT_DIRS_RELEASE}/vorbisfile.lib
    ${ARCH_PREBUILT_DIRS_RELEASE}/vorbis.lib
)

