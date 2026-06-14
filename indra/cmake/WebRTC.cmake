# -*- cmake -*-
include_guard()
include(Linking)
include(Prebuilt)

# Define paths
set(WEBRTC_DIR "${LIBS_PREBUILT_DIR}/include/webrtc")
set(ABSEIL_DIR "${LIBS_PREBUILT_DIR}/include/webrtc/third_party/abseil-cpp")

# Create directories if they do not exist
file(MAKE_DIRECTORY "${WEBRTC_DIR}")
file(MAKE_DIRECTORY "${ABSEIL_DIR}")

add_library(ll::webrtc INTERFACE IMPORTED)

use_prebuilt_binary(webrtc)

target_include_directories(ll::webrtc SYSTEM INTERFACE "${WEBRTC_DIR}" "${ABSEIL_DIR}")

if(WINDOWS)
    target_link_libraries(ll::webrtc INTERFACE webrtc.lib)
endif()


