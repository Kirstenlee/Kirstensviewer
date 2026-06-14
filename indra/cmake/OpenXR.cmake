# -*- cmake -*-
include_guard()
include(Prebuilt)

add_library(ll::openxr INTERFACE IMPORTED)

use_prebuilt_binary(openxr)

target_link_libraries(ll::openxr INTERFACE ${ARCH_PREBUILT_DIRS_RELEASE}/openxr_loader.lib)
target_include_directories(ll::openxr SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)

