# -*- cmake -*-
include_guard()
include(Linking)
include(Prebuilt)

add_library(ll::SDL3 INTERFACE IMPORTED)

use_prebuilt_binary(sdl3)

# S24 : static SDL3 (vcpkg x64-windows-static-md), game controller input only.
# The system libraries are the ones SDL3's own pkg-config file lists.
set(SDL3_LIBRARIES
    SDL3-static.lib
    winmm.lib imm32.lib version.lib setupapi.lib dinput8.lib
    )
set(SDL3_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

target_link_libraries(ll::SDL3 INTERFACE ${SDL3_LIBRARIES})
target_include_directories(ll::SDL3 SYSTEM INTERFACE ${SDL3_INCLUDE_DIRS})
