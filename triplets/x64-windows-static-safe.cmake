set(VCPKG_TARGET_TRIPLET "x64-windows-static-safe")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v145) # Visual Studio 2026

# Safe, standard optimizations
set(VCPKG_C_FLAGS_RELEASE "/O2 /GL /arch:AVX" CACHE STRING "Safe C flags" FORCE)
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /GL /arch:AVX" CACHE STRING "Safe CXX flags" FORCE)
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG" CACHE STRING "Link-time code generation" FORCE)