# -*- cmake -*-
include_guard()
include(Prebuilt)

use_prebuilt_binary(tinygltf)

set(TINYGLTF_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/tinygltf)

