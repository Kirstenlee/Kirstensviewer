# Dullahan (LL, MIT) + Spotify's prebuilt CEF, built here instead of consuming
# LL's binary packages. Needs a /MD triplet (x64-windows-static-md): the viewer
# is /MD. Spotify CEF builds have no proprietary codecs (H.264/AAC).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

set(CEF_VERSION "156.0.30+g6028375+chromium-156.0.8078.96")
set(CEF_SHA512 0418ce4128135b1ae7d1a8af9145239b856a2f5bb1aad4e0aba3e983aa8dda795ab7ab2a224d17f2ec647210286c7f91060b6bffb9bb1b8d046271b749e21cf0)
set(DULLAHAN_REF "v1.50.0-CEF_152.0.6.83")
set(DULLAHAN_SHA512 15a6e5c51e1a855a7982d2b044f1133f307a0d862582e375f9b17aa3957efb770b0554f0654f0aba718bd0e1c2bdacd2788597af092062efc90dc3ebaac24230)

# --- CEF (minimal package: headers, libcef.lib, runtime, wrapper sources) ---
string(REPLACE "+" "%2B" CEF_VERSION_URL "${CEF_VERSION}")
vcpkg_download_distfile(CEF_ARCHIVE
    URLS "https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION_URL}_windows64_minimal.tar.bz2"
    FILENAME "cef_binary_${CEF_VERSION}_windows64_minimal.tar.bz2"
    SHA512 "${CEF_SHA512}"
)
vcpkg_extract_source_archive(CEF_SOURCE_PATH ARCHIVE "${CEF_ARCHIVE}")

# --- Dullahan source ---
vcpkg_from_github(
    OUT_SOURCE_PATH DULLAHAN_SOURCE_PATH
    REPO secondlife/dullahan
    REF "${DULLAHAN_REF}"
    SHA512 "${DULLAHAN_SHA512}"
    HEAD_REF main
)

# --- 1. CEF's libcef_dll_wrapper (C++ wrapper over the C API) ---
vcpkg_cmake_configure(
    SOURCE_PATH "${CEF_SOURCE_PATH}"
    OPTIONS
        -DCEF_RUNTIME_LIBRARY_FLAG=/MD
        -DUSE_SANDBOX=OFF
)
vcpkg_cmake_build(TARGET libcef_dll_wrapper LOGFILE_BASE build-cef-wrapper)

# Dullahan's CMake looks for <wrapper dir>/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib
set(WRAPPER_OUT "${CURRENT_BUILDTREES_DIR}/wrapper-out")
file(REMOVE_RECURSE "${WRAPPER_OUT}")
file(GLOB_RECURSE WRAPPER_LIB "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libcef_dll_wrapper.lib")
list(GET WRAPPER_LIB 0 WRAPPER_LIB)
if(NOT EXISTS "${WRAPPER_LIB}")
    message(FATAL_ERROR "libcef_dll_wrapper.lib was not produced")
endif()
file(INSTALL "${WRAPPER_LIB}" DESTINATION "${WRAPPER_OUT}/libcef_dll_wrapper/Release")
file(REMOVE_RECURSE "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")

# --- 2. Dullahan library and host process ---
# A second configure is intentional (wrapper, then dullahan); clear the
# helper's called-once marker so it doesn't warn.
unset(Z_VCPKG_CMAKE_GENERATOR CACHE)
vcpkg_cmake_configure(
    SOURCE_PATH "${DULLAHAN_SOURCE_PATH}"
    OPTIONS
        -DCEF_WRAPPER_DIR=${CEF_SOURCE_PATH}
        -DCEF_WRAPPER_BUILD_DIR=${WRAPPER_OUT}
)
vcpkg_cmake_build(TARGET dullahan LOGFILE_BASE build-dullahan)
vcpkg_cmake_build(TARGET dullahan_host LOGFILE_BASE build-dullahan-host)

set(BUILD_REL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
file(GLOB_RECURSE DULLAHAN_LIB "${BUILD_REL}/dullahan.lib")
file(GLOB_RECURSE DULLAHAN_HOST "${BUILD_REL}/dullahan_host.exe")
list(GET DULLAHAN_LIB 0 DULLAHAN_LIB)
list(GET DULLAHAN_HOST 0 DULLAHAN_HOST)
if(NOT EXISTS "${DULLAHAN_LIB}" OR NOT EXISTS "${DULLAHAN_HOST}")
    message(FATAL_ERROR "dullahan.lib / dullahan_host.exe were not produced")
endif()

# --- Install ---
# include/cef/, lib/ and stage/ are placed into the repo tree by DownloadAndUnpack.ps1
file(INSTALL "${DULLAHAN_SOURCE_PATH}/src/dullahan.h" "${DULLAHAN_SOURCE_PATH}/src/dullahan_version.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/cef")

file(INSTALL "${DULLAHAN_LIB}" "${WRAPPER_OUT}/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib" "${CEF_SOURCE_PATH}/Release/libcef.lib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# CEF runtime next to dullahan_host.exe -> bin/release, resources -> resources
file(GLOB CEF_RUNTIME "${CEF_SOURCE_PATH}/Release/*")
list(FILTER CEF_RUNTIME EXCLUDE REGEX "\\.lib$")
file(INSTALL ${CEF_RUNTIME} "${DULLAHAN_HOST}" DESTINATION "${CURRENT_PACKAGES_DIR}/stage/bin/release")
file(GLOB CEF_RESOURCES "${CEF_SOURCE_PATH}/Resources/*")
file(INSTALL ${CEF_RESOURCES} DESTINATION "${CURRENT_PACKAGES_DIR}/stage/resources")

vcpkg_install_copyright(FILE_LIST
    "${DULLAHAN_SOURCE_PATH}/LICENSE.txt"
    "${DULLAHAN_SOURCE_PATH}/CEF_LICENSE.txt"
    "${CEF_SOURCE_PATH}/LICENSE.txt"
)
