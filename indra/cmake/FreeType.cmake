# -*- cmake -*-
include_guard()
include(Prebuilt)
include(Linking)

add_library(ll::freetype INTERFACE IMPORTED)

use_prebuilt_binary(freetype)
set(FREETYPE_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/freetype/)

# Link the Brotli pieces freetype needs (encoder, decoder, common).
# Update names if your prebuilt libs use different names (e.g. libbrotlidec).
set(BROTLI_LIBRARIES brotlidec brotlienc brotlicommon)
set(BROTLI_LIBRARY_DIRS ${LIBS_PREBUILT_DIR}/lib/release)

# Expose both freetype and brotli to consumers of ll::freetype
set(FREETYPE_LIBRARIES freetype ${BROTLI_LIBRARIES})

# Make include dirs and link libraries available on the INTERFACE target
target_include_directories(ll::freetype SYSTEM INTERFACE ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(ll::freetype INTERFACE ${FREETYPE_LIBRARIES})

# Make sure the linker can find both freetype and brotli libs
link_directories(${FREETYPE_LIBRARY_DIRS} ${BROTLI_LIBRARY_DIRS})
