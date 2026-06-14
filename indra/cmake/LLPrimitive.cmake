# -*- cmake -*-

include_guard()

include(Prebuilt)
include(TinyGLTF)
include(Boost)

use_prebuilt_binary(colladadom)
use_prebuilt_binary(minizip-ng)
use_prebuilt_binary(libxml2)

# Include paths
set(LLPRIMITIVE_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llprimitive
    ${LIBS_PREBUILT_DIR}/include/tinygltf
    ${LIBS_PREBUILT_DIR}/include/collada
    ${LIBS_PREBUILT_DIR}/include/collada/1.4
    ${LIBS_PREBUILT_DIR}/include/minizip-ng
)

# Linker setup
if (WINDOWS)
    # Dependencies llprimitive itself needs (used in llprimitive/CMakeLists.txt)
    set(LLPRIMITIVE_LINK_LIBRARIES
        libcollada14dom23-s
        ${LIBS_PREBUILT_DIR}/lib/release/libxml2.lib
        ${LIBS_PREBUILT_DIR}/lib/release/iconv.lib    # S24 - libxml2 dependency
        ${LIBS_PREBUILT_DIR}/lib/release/minizip-ng.lib
        ${LIBS_PREBUILT_DIR}/lib/release/bz2.lib
        ${LIBS_PREBUILT_DIR}/lib/release/lzma.lib
        ${LIBS_PREBUILT_DIR}/lib/release/zstd.lib
        Bcrypt.lib
        ${BOOST_SYSTEM_LIBRARIES}
    )

    # For consumers of llprimitive
    set(LLPRIMITIVE_LIBRARIES
        llprimitive
        ${LLPRIMITIVE_LINK_LIBRARIES}
    )
endif()
