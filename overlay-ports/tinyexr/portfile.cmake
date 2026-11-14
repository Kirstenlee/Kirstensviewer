# Header-only. The upstream vcpkg port installs only tinyexr.h and builds a
# miniz-backed library; since 3.x, tinyexr.h also needs exr_reader.hh and
# streamreader.hh next to it when TINYEXR_IMPLEMENTATION is defined, so install
# all three and skip the library.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO syoyo/tinyexr
    REF "v${VERSION}"
    SHA512 01b666903719db3ade3363dc477abbecb9d8a3b4bcb6b6a4496c76b79e67efe21cbf9cffce939cc4dda3d9656809e2365d04e17d2b70053614ae4da426ce35f0
    HEAD_REF master
)

file(INSTALL
        "${SOURCE_PATH}/tinyexr.h"
        "${SOURCE_PATH}/exr_reader.hh"
        "${SOURCE_PATH}/streamreader.hh"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/NOTICE")
