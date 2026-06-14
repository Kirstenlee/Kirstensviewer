# -*- cmake -*-
include(Prebuilt)

set(NDOF ON CACHE BOOL "Use NDOF space navigator joystick library.")

include_guard()
add_library( ll::ndof INTERFACE IMPORTED )
if (NDOF)
    use_prebuilt_binary(libndofdev)
    set(NDOF_LIBRARY libndofdev)
    set(NDOF_INCLUDE_DIR ${ARCH_PREBUILT_DIRS}/include/ndofdev)
    set(NDOF_FOUND 1)
endif (NDOF)


