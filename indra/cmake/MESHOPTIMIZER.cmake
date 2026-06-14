# -*- cmake -*-
include_guard()
include(Linking)
include(Prebuilt)

add_library(ll::meshoptimizer INTERFACE IMPORTED)

use_prebuilt_binary(meshoptimizer)

set(MESHOPTIMIZER_LIBRARIES meshoptimizer.lib)
set(MESHOPTIMIZER_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/meshoptimizer)

target_link_libraries(ll::meshoptimizer INTERFACE ${MESHOPTIMIZER_LIBRARIES})
target_include_directories(ll::meshoptimizer SYSTEM INTERFACE ${MESHOPTIMIZER_INCLUDE_DIRS})
