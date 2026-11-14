# Vendor-prebuilt: AMD ships the static libraries in the SDK repo (no source
# build). Install only what the viewer links: the header and the VS2026 /MD
# static library (the viewer's Release config is /MD).
# The viewer is /MD while our triplets are static-CRT; the /MD lib is deliberate.
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO GPUOpen-LibrariesAndSDKs/AGS_SDK
    REF "v${VERSION}"
    SHA512 042469d4a442d837595351d136e932fb8055f2c227a906991cb3e66488a1b0dfce4c554cf0f19e6ac2a8d084a4c5b8ae742d5f77dd616665cbaa7f818bca44da
    HEAD_REF master
)

file(INSTALL "${SOURCE_PATH}/ags_lib/inc/amd_ags.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/ags_lib/lib/amd_ags_x64_2026_MD.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt" "${SOURCE_PATH}/ags_lib/LICENSE.txt")
