set(VCPKG_TARGET_TRIPLET "x64-windows-static-viewer")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v145)

# Match the main viewer's exact compiler settings from 00-Common.cmake
# Critical: AVX2, precise FP math, and LTCG
set(VCPKG_C_FLAGS_RELEASE "/O2 /Ot /Oi /Gy /GL /Ob2 /arch:AVX2" CACHE STRING "Viewer-matched C flags" FORCE)
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /Ot /Oi /Gy /GL /Ob2 /arch:AVX2" CACHE STRING "Viewer-matched CXX flags" FORCE)
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF" CACHE STRING "Viewer-matched linker flags" FORCE)
