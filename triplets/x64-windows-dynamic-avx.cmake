# Inherit from the standard x64-windows triplet!
set(VCPKG_TARGET_TRIPLET "x64-windows-dynamic-avx")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PLATFORM_TOOLSET v145) # Visual Studio 2026 (MSVC 14.50)

# Enable AVX for openjpeg only
set(OPENJPEG_ENABLE_AVX ON CACHE BOOL "Enable AVX for openjpeg" FORCE)
# Do NOT enable SSE/SSE2
# set(OPENJPEG_ENABLE_SSE OFF CACHE BOOL "Disable SSE for openjpeg" FORCE)
# set(OPENJPEG_ENABLE_SSE2 OFF CACHE BOOL "Disable SSE2 for openjpeg" FORCE)

# Aggressive compiler flags for maximum speed
set(VCPKG_C_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /GS- /Gy /Zc:inline /Zc:__cplusplus" CACHE STRING "Aggressive C flags" FORCE)
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /GS- /Gy /Zc:inline /Zc:__cplusplus" CACHE STRING "Aggressive CXX flags" FORCE)

# Link-time optimization (LTO)
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG" CACHE STRING "Link-time code generation" FORCE)

# Favor speed over size
set(VCPKG_C_FLAGS "/Ot" CACHE STRING "Favor speed" FORCE)
set(VCPKG_CXX_FLAGS "/Ot" CACHE STRING "Favor speed" FORCE)