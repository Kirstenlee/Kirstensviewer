# -*- cmake -*-
include_guard()

# Minimal, safe modernization for vcpkg-style layout.
if(NOT TARGET ll::openssl)
  add_library(ll::openssl INTERFACE IMPORTED GLOBAL)
endif()

set(_openssl_inc "${LIBS_PREBUILT_DIR}/include")
# Keep Crypt32 linkage only on Windows using a generator expression.
set(_openssl_libs "libssl.lib;libcrypto.lib;$<$<PLATFORM_ID:Windows>:Crypt32.lib>")

if(EXISTS "${_openssl_inc}")
  target_include_directories(ll::openssl SYSTEM INTERFACE "${_openssl_inc}")
endif()

target_link_libraries(ll::openssl INTERFACE ${_openssl_libs})

# Backwards-compatible cache variables (internal)
set(OPENSSL_INCLUDE_DIRS "${_openssl_inc}" CACHE INTERNAL "OpenSSL include dir (generated)")
set(OPENSSL_LIBRARIES ${_openssl_libs} CACHE INTERNAL "OpenSSL link libraries (generated)")

