# -*- cmake -*-
include_guard()

add_library(ll::expat INTERFACE IMPORTED)

# vcpkg expat configuration (static build)
set(EXPAT_LIBRARIES libexpatMT.lib)
set(EXPAT_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

target_link_libraries(ll::expat INTERFACE ${EXPAT_LIBRARIES})
target_include_directories(ll::expat SYSTEM INTERFACE ${EXPAT_INCLUDE_DIRS})

# Note: vcpkg x64-windows-static builds expat as a static library
# No DLLs are produced or needed