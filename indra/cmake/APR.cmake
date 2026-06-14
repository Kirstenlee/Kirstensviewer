# -*- cmake -*-
include_guard()
include(Linking)
include(Prebuilt)

add_library(ll::apr INTERFACE IMPORTED)

use_prebuilt_binary(apr_suite)

# Use apr-iconv for APR 1.7
set(APR_LIBRARIES
  ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}apr-1.lib
)
set(APRUTIL_LIBRARIES
  ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}aprutil-1.lib
  ${APRICONV_LIBRARIES}
)

set(APR_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include/apr-util)

target_link_libraries(ll::apr INTERFACE ${APR_LIBRARIES} ${APRUTIL_LIBRARIES})
target_include_directories(ll::apr SYSTEM INTERFACE ${APR_INCLUDE_DIR})