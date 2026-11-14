# Vendored sources (BSD-3-Clause, 3Dconnexion 2007) from secondlife/3p-libndofdev
# commit a0b7d990096f36d9e32c67d09a94fa5a0fff6401, Windows files only. The library is
# frozen, so it lives in-tree and the build never touches the network for it.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_cmake_configure(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}")
vcpkg_cmake_build()
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/COPYING")
