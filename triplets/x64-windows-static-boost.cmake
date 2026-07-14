# Aggressive release-only triplet tuned for maximum optimization (use with care)
# Note: This forces Release-only builds and aggressive MSVC optimizations that
# can break FP determinism, portability and debugging. Test thoroughly on target CPUs.
# CRITICAL: Uses dynamic CRT linkage (/MD) to match main S24 project runtime

set(VCPKG_TARGET_TRIPLET "x64-windows-static-boost")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)  # /MD to match main project (was static)
set(VCPKG_LIBRARY_LINKAGE static)  # Static .lib files (not .dll)
set(VCPKG_PLATFORM_TOOLSET v145) # Visual Studio 2026

# Force vcpkg to produce only Release configuration
set(VCPKG_BUILD_TYPE "release" CACHE STRING "Force Release build" FORCE)
set(VCPKG_BUILD_CONFIGURATIONS "release" CACHE STRING "Only build release configuration" FORCE)

# Maximize MSVC optimization for Release
# - /O2, /Ob2, /Ot: high optimization level and inline favoring
# - /GL and /LTCG for whole-program optimizations (link-time code gen)
# - /arch:AVX2 to target modern x64 SIMD (change if target supports only AVX)
# - /fp:fast to allow fast-floating optimizations (may change FP semantics)
# - /Oy (omit frame pointers), /Oi (intrinsics), /GF (string pooling)
# - Defines to disable iterator/debug overhead and Boost asserts for max perf
set(VCPKG_C_FLAGS_RELEASE "/O2 /Ob2 /Oi /Ot /GL /arch:AVX2 /fp:fast /Oy /GF /DNDEBUG /D_SECURE_SCL=0 /D_ITERATOR_DEBUG_LEVEL=0 /DBOOST_DISABLE_ASSERTS /Zc:wchar_t-" CACHE STRING "Aggressive C flags" FORCE)

# S24 :: /Zc:wchar_t- matches llfilesystem/dxrender's legacy wchar_t=unsigned
# short ABI convention (and the prebuilt colladadom tarball). Without this,
# vcpkg's boost (default /Zc:wchar_t on) mangles wchar_t-touching symbols
# (e.g. boost::filesystem::detail::path_traits::convert) differently than
# those consumers expect, causing LNK2001 unresolved externals. See
# docs/boost-vcpkg-migration.md.
set(VCPKG_CXX_FLAGS_RELEASE "/O2 /Ob2 /Oi /Ot /GL /arch:AVX2 /fp:fast /Oy /GF /DNDEBUG /D_SECURE_SCL=0 /D_ITERATOR_DEBUG_LEVEL=0 /DBOOST_DISABLE_ASSERTS /permissive- /Zc:wchar_t-" CACHE STRING "Aggressive CXX flags" FORCE)

# Linker: disable incremental linking, enable optimization passes
set(VCPKG_LINKER_FLAGS_RELEASE "/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF" CACHE STRING "Aggressive linker flags" FORCE)

# Note about VS/vcpkg settings:
# - __VCPKG_PLATFORM_TOOLSET__ remains v145
# - __VCPKG_BUILD_CONFIGURATIONS__ is forced to "release"
#
# Warnings:
# - These flags prioritize raw speed over portability, reproducibility and debug-ability.
# - /arch:AVX2 will produce binaries that require at least AVX2-capable CPUs.
# - /fp:fast and removal of iterator/debug checks can change numerical results and hide bugs.
# - Use this triplet only for benchmarking/production builds where those trade-offs are acceptable
