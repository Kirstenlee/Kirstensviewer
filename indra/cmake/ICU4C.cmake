# -*- cmake -*-
include_guard()
include(Prebuilt)

add_library(ll::icu4c INTERFACE IMPORTED)

use_prebuilt_binary(icu4c)
use_prebuilt_binary(dictionaries)

target_link_libraries(ll::icu4c INTERFACE icuuc)
target_include_directories(ll::icu4c SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/unicode)
