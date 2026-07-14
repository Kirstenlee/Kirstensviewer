# -*- cmake -*-
include(Prebuilt)

include_guard()

add_library( ll::boost INTERFACE IMPORTED )

use_prebuilt_binary(boost)
set(Boost_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

# S24 :: Boost is sourced from vcpkg (per-port, flat-copied into
# lib/release by DownloadAndUnpack.ps1 -- see
# docs/boost-vcpkg-migration.md), not the old LL tarball. vcpkg builds
# Boost via Boost's own CMake (BoostRoot), which on MSVC tags static lib
# output names the same way b2's "tagged" layout / auto_link.hpp does:
#   boost_<component>-<toolset>-mt-x<addrsize>-<boost_lib_version>.lib
# e.g. boost_context-vc145-mt-x64-1_91.lib
# The other compiled boost libs (atomic, chrono, container, date_time,
# iostreams, json, stacktrace, wave) are NOT explicitly linked anywhere --
# they resolve via MSVC's boost auto-link pragmas (auto_link.hpp) against
# lib/release on the linker path, which generate this exact same tag, so
# they stay in sync automatically as long as this toolset/version match.
set(boost_toolset "vc145") # Visual Studio 2026 / v145 platform toolset

# boost/version.hpp is only written by the build-time 'prepare' custom
# command (DownloadAndUnpack.ps1), which runs AFTER configure -- so on a
# clean checkout this header may not exist yet during this first configure.
# Fall back to the known-current vcpkg boost baseline tag and self-correct
# on the next configure once the header is present (see
# docs/boost-vcpkg-migration.md).
set(boost_lib_version_tag "1_91")
set(_boost_version_header "${Boost_INCLUDE_DIRS}/boost/version.hpp")
if(EXISTS "${_boost_version_header}")
    file(STRINGS "${_boost_version_header}" boost_lib_version_line
         REGEX "^#define[ \t]+BOOST_LIB_VERSION[ \t]+\"[0-9_]+\"")
    if(boost_lib_version_line)
        string(REGEX REPLACE ".*\"([0-9_]+)\".*" "\\1" boost_lib_version_tag "${boost_lib_version_line}")
    endif()
elseif(NOT DEFINED CACHE{_boost_version_fallback_warned})
    # S24 :: Boost.cmake is include()'d separately by every target that
    # links boost (llcommon, llcorehttp, llfilesystem, newview, etc.) since
    # each needs its own directory-scoped copy of the BOOST_*_LIBRARY
    # variables -- so this branch can run ~9x per configure. Gate the
    # message on a CACHE INTERNAL flag (genuinely global, unlike plain
    # set()) so it prints once per configure instead of spamming.
    message(STATUS "Boost.cmake: boost/version.hpp not present yet (pre-'prepare' configure) -- using fallback version tag ${boost_lib_version_tag}. Re-run CMake configure once packages are downloaded if Boost fails to link.")
    set(_boost_version_fallback_warned TRUE CACHE INTERNAL "")
endif()

# As of sometime between Boost 1.67 and 1.72, Boost libraries are suffixed
# with the address size.
set(addrsfx "-x${ADDRESS_SIZE}")
set(boost_tag "-${boost_toolset}-mt${addrsfx}-${boost_lib_version_tag}")

set(BOOST_CONTEXT_LIBRARY          boost_context${boost_tag})
set(BOOST_FIBER_LIBRARY            boost_fiber${boost_tag})
set(BOOST_FILESYSTEM_LIBRARY       boost_filesystem${boost_tag})
set(BOOST_PROGRAM_OPTIONS_LIBRARY  boost_program_options${boost_tag})
set(BOOST_THREAD_LIBRARY           boost_thread${boost_tag})
set(BOOST_URL_LIBRARY              boost_url${boost_tag})

# S24 :: Confirmed empirically (see docs/boost-vcpkg-migration.md) that
# vcpkg's boost-regex and boost-system ports produce NO .lib file at all
# under this triplet -- both are packaged fully header-only (Boost.System
# has been header-only since 1.69; Boost.Regex evidently follows suit in
# this boost/vcpkg build). Leave these empty rather than referencing a
# nonexistent file, which would hard-fail the link step. CMake silently
# drops empty entries from target_link_libraries, so this is a safe no-op
# everywhere these variables are used.
set(BOOST_REGEX_LIBRARY            "")
set(BOOST_SYSTEM_LIBRARY           "")
