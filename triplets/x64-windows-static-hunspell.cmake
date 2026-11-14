
set(VCPKG_TARGET_TRIPLET "x64-windows-static-hunspell")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v145)

# S24: this project is a multi-config VS generator build always invoked with
# /p:Configuration=Release (cmake/Linking.cmake only adds lib/debug to the
# link path when CMAKE_BUILD_TYPE=="Debug", which never happens here) - the
# Debug variant of every port on this triplet gets built and copied but is
# never actually linked against. Release-only roughly halves install time
# for every package using this triplet. Matches x64-windows-static-boost,
# which already did this.
set(VCPKG_BUILD_TYPE "release" CACHE STRING "Force Release build" FORCE)
set(VCPKG_BUILD_CONFIGURATIONS "release" CACHE STRING "Only build release configuration" FORCE)

# Moderate, more aggressive optimizations
set(VCPKG_C_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /Gy" CACHE STRING "Moderate C flags" FORCE)
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /Gy" CACHE STRING "Moderate CXX flags" FORCE)
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG" CACHE STRING "Link-time code generation" FORCE)