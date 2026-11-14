set(VCPKG_TARGET_TRIPLET "x64-windows-static-extreme")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
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

# Extreme, maximum performance (may break some libraries)
set(VCPKG_C_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /GS- /Gy /Zc:inline /Zc:__cplusplus" CACHE STRING "Extreme C flags" FORCE)
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /GS- /Gy /Zc:inline /Zc:__cplusplus" CACHE STRING "Extreme CXX flags" FORCE)
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG" CACHE STRING "Link-time code generation" FORCE)

# S24: curl's "ssl" default feature pulls in sspi/Schannel on Windows even
# when the "openssl" feature is also requested (vcpkg still resolves "ssl"
# into the build - unexplained). curl supports MultiSSL (multiple backends
# compiled in, one active at runtime) specifically for this case - force
# OpenSSL as the active backend so the cert-validation code's
# CURLOPT_SSL_CTX_FUNCTION hook gets a real SSL_CTX*. Scoped to curl only
# (PORT variable) so it doesn't invalidate every other port's ABI on this
# shared triplet.
if(PORT STREQUAL "curl")
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCURL_DEFAULT_SSL_BACKEND=openssl")
endif()