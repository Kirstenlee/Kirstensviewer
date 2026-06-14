
set(VCPKG_TARGET_TRIPLET "x64-windows-static-hunspell")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v145)

# Moderate, more aggressive optimizations
set(VCPKG_C_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /Gy" CACHE STRING "Moderate C flags" FORCE)
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /Gy" CACHE STRING "Moderate CXX flags" FORCE)
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG" CACHE STRING "Link-time code generation" FORCE)