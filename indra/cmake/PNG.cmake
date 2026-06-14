# -*- cmake -*-
include_guard()
include(Prebuilt)

add_library(ll::libpng INTERFACE IMPORTED)

use_prebuilt_binary(libpng)

set(PNG_LIBRARIES libpng16)
set(PNG_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/libpng16)

target_link_libraries(ll::libpng INTERFACE ${PNG_LIBRARIES})
target_include_directories(ll::libpng SYSTEM INTERFACE ${PNG_INCLUDE_DIRS})