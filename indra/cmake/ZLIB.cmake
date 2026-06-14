# -*- cmake -*-
include_guard()
include(Prebuilt)

add_library(ll::zlib INTERFACE IMPORTED)

use_prebuilt_binary(zlib)

set(ZLIB_LIBRARIES optimized zs.lib debug zsd.lib)
set(ZLIB_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

target_link_libraries(ll::zlib INTERFACE ${ZLIB_LIBRARIES})
target_include_directories(ll::zlib SYSTEM INTERFACE ${ZLIB_INCLUDE_DIRS})
