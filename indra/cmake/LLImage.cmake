# -*- cmake -*-
include_guard()
include(Jpeg)
include(Png)

set(LLIMAGE_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llimage
    ${JPEG_INCLUDE_DIRS}
    )

set(LLIMAGE_LIBRARIES llimage)
