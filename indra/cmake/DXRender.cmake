# -*- cmake -*-
if (DXRENDER_CMAKE_INCLUDED)
  return()
endif (DXRENDER_CMAKE_INCLUDED)
set (DXRENDER_CMAKE_INCLUDED TRUE)

include(Variables)

# DX11 does not need GLH or FreeType here
# Add any DX-specific includes later if needed

set(DXRENDER_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/dxrender/core
    ${LIBS_OPEN_DIR}/dxrender/resources
    )

set(DXRENDER_LIBRARIES
    dxrender
    )
