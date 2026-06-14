# -*- cmake -*-
include_guard()
include(Prebuilt)

# Create imported interface target
add_library(ll::opencl INTERFACE IMPORTED)

# Use the prebuilt OpenCL binary (from vcpkg or your prebuilt folder)
use_prebuilt_binary(opencl)

# Link the OpenCL import library
target_link_libraries(ll::opencl INTERFACE
    ${ARCH_PREBUILT_DIR}/OpenCL.lib
)

# Add the include directory for CL/cl.h
target_include_directories(ll::opencl SYSTEM INTERFACE
    ${LIBS_PREBUILT_DIR}/include
)
