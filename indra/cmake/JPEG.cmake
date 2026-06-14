# -*- cmake -*-
include_guard()
include(Prebuilt)
include(Linking)

add_library(ll::libjpeg INTERFACE IMPORTED)

use_prebuilt_binary(libjpeg-turbo)

if(WINDOWS)
  set(JPEG_LIBRARIES jpeg)
endif()

# Add the jpeglib-turbo directory for vcpkg layout
set(JPEG_INCLUDE_DIRS
  ${LIBS_PREBUILT_DIR}/include
  ${LIBS_PREBUILT_DIR}/include/jpeglib-turbo
)

target_link_libraries(ll::libjpeg INTERFACE ${JPEG_LIBRARIES})
target_include_directories(ll::libjpeg SYSTEM INTERFACE ${JPEG_INCLUDE_DIRS})
