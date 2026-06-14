# -*- cmake -*-
include_guard()

add_library(ll::glm INTERFACE IMPORTED)

# vcpkg glm configuration (header-only library)
set(GLM_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

target_include_directories(ll::glm SYSTEM INTERFACE ${GLM_INCLUDE_DIRS})

# Note: GLM is header-only, no libraries to link
