# -*- cmake -*-
include_guard()
include(Prebuilt)

add_library(ll::nghttp2 INTERFACE IMPORTED)

use_prebuilt_binary(nghttp2)

# Static nghttp2 requires this define to avoid __declspec(dllimport)
add_definitions(-DNGHTTP2_STATICLIB)

set(NGHTTP2_LIBRARIES nghttp2.lib)
set(NGHTTP2_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

target_link_libraries(ll::nghttp2 INTERFACE ${NGHTTP2_LIBRARIES})
target_include_directories(ll::nghttp2 SYSTEM INTERFACE ${NGHTTP2_INCLUDE_DIRS})
