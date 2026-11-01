# -*- cmake -*-
include_guard()
include(Prebuilt)
include(Linking)
include(ZLIB)

add_library(ll::libcurl INTERFACE IMPORTED)

use_prebuilt_binary(curl)

# Required: tell curl headers we're linking statically (not via DLL)
# Without this, curl.h uses __declspec(dllimport) which breaks
# internal feature initialization (content decoders, etc.)
add_definitions(-DCURL_STATICLIB)

# Static curl with OpenSSL/Schannel requires additional Windows libraries
# Static curl with zlib requires linking zlib explicitly
set(CURL_LIBRARIES
    debug libcurl-d.lib
    optimized libcurl.lib
    secur32.lib      # Windows SSPI support
    crypt32.lib      # Windows certificate store
    ws2_32.lib       # S24: curl 8.x local-address-binding (peer.c), unused by the old 7.54.1 build
    iphlpapi.lib     # S24: if_nametoindex()/if_indextoname() live here, confirmed via dumpbin /exports
    brotlicommon.lib # S24: brotli content-decoding (CURLOPT_ENCODING "")
    brotlidec.lib
    brotlienc.lib
    zstd.lib         # S24: zstd content-decoding (CURLOPT_ENCODING "")
)

set(CURL_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

# Configure the imported target
target_link_libraries(ll::libcurl INTERFACE ${CURL_LIBRARIES} ll::zlib)
target_include_directories(ll::libcurl SYSTEM INTERFACE ${CURL_INCLUDE_DIRS})