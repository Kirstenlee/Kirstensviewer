# -*- cmake -*-
if (KRLV_CMAKE_INCLUDED)
  return()
endif (KRLV_CMAKE_INCLUDED)
set (KRLV_CMAKE_INCLUDED TRUE)

include(Variables)

set(KRLV_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/krlv/core
    ${LIBS_OPEN_DIR}/krlv/behaviours
    )

set(KRLV_LIBRARIES
    krlv
    )
