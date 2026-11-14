# Vendor-prebuilt: NVIDIA publishes the headers and nvapi64.lib in its public
# repo (no source build). Pinned to a commit since the repo carries no release
# tags (this one is the R620 Developer SDK).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO NVIDIA/nvapi
    REF 51e5aae4bc40c3ccd3fb7d4dfd94256e7bfc496e
    SHA512 001a52098d1f65dbd079582ea3b76b8715a9abcba258f0f92717b48fd1f0b38f1cb6c381e9445601259e33e6230dd0c116d7ad25b65c83e00d398fa94558930f
    HEAD_REF main
)

file(GLOB NVAPI_HEADERS "${SOURCE_PATH}/*.h")
file(INSTALL ${NVAPI_HEADERS} "${SOURCE_PATH}/NvApiDriverSettings.c" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/amd64/nvapi64.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/License.txt")
