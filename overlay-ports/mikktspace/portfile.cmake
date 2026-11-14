# Header-only. Blender's intern/mikktspace headers from a pinned commit (no
# need for the multi-hundred-MB Blender source archive), plus three MSVC fixes:
#   - mikktspace.hh: add <array>, and define `uint` (Blender gets it elsewhere)
#   - mikk_atomic_hash_set.hh: drop LIKELY/UNLIKELY (Blender-internal macros)
set(MIKK_REF c928ea1f231d473afd3fbb2c33777d8ae81b4c2c)
set(MIKK_SHA512_MIKKTSPACE_HH            07c56e894f25b3e5a58ca4626dfd19d33595146f61daea0e72a7ff00fe5257f1d2e4e21eb7c734c5f16c79bc062a940a3d98855b9c6013c043bca5ecbcda465e)
set(MIKK_SHA512_MIKK_ATOMIC_HASH_SET_HH  c55763290a434c179b23d9588bf4ac7b44787b9c5e732a6f9f106153da500f24124ce001199385dc09935e8d687216990d9e62ed0a416c694c60c499ee7f0ccd)
set(MIKK_SHA512_MIKK_FLOAT3_HH           f9e9ad611ef002b8fa51c409b0a767fbf1901279561648a2590f6d5b95220ff84b9612dade05ecf61411d94feeb76833dc2e253482130f49403f1cc5b356e739)
set(MIKK_SHA512_MIKK_UTIL_HH             ce3098f8d618b8f45e46c9e2b31939125f63a7e23d4453db742892395b0d6f9209842463ebecd496c53aa2f4b8f39477a3cdcdad0d4f88e458c0cd665270f445)

set(MIKK_URL_BASE "https://raw.githubusercontent.com/blender/blender/${MIKK_REF}/intern/mikktspace")

foreach(HEADER mikktspace.hh mikk_atomic_hash_set.hh mikk_float3.hh mikk_util.hh)
    string(TOUPPER "${HEADER}" HEADER_KEY)
    string(REPLACE "." "_" HEADER_KEY "${HEADER_KEY}")
    vcpkg_download_distfile(HEADER_PATH
        URLS "${MIKK_URL_BASE}/${HEADER}"
        FILENAME "blender-${MIKK_REF}-${HEADER}"
        SHA512 "${MIKK_SHA512_${HEADER_KEY}}"
    )
    file(INSTALL "${HEADER_PATH}" DESTINATION "${CURRENT_PACKAGES_DIR}/include" RENAME "${HEADER}")
    unset(HEADER_PATH CACHE)
endforeach()

set(MIKK_INC "${CURRENT_PACKAGES_DIR}/include")
vcpkg_replace_string("${MIKK_INC}/mikktspace.hh" "#include <algorithm>" "#include <array>\n#include <algorithm>")
vcpkg_replace_string("${MIKK_INC}/mikktspace.hh" "#include \"mikk_atomic_hash_set.hh\"" "typedef uint32_t uint;\n\n#include \"mikk_atomic_hash_set.hh\"")
vcpkg_replace_string("${MIKK_INC}/mikk_atomic_hash_set.hh" "UNLIKELY(" "(")
vcpkg_replace_string("${MIKK_INC}/mikk_atomic_hash_set.hh" "LIKELY(" "(")

vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/LICENSE-Apache-2.0.txt")
