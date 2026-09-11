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
    # S24 (2026-09-10): this was missing entirely - llviewerjoystick.cpp/.h's
    # SpaceNavigator AND generic-controller (DirectInput DI8DEVCLASS_GAMECTRL
    # enumeration, same code path Xbox-style controllers go through) support
    # is entirely gated behind #if LIB_NDOF. With no add_definitions() call
    # anywhere in the tree to actually define that macro, every #if LIB_NDOF
    # block evaluated to 0 (undefined-in-#if == 0) and got compiled out as
    # dead code - confirmed via the real generated .vcxproj's
    # PreprocessorDefinitions list, which had no LIB_NDOF entry despite
    # NDOF:BOOL=ON in CMakeCache.txt. The joystick/controller subsystem has
    # been silently a no-op the whole time, matching user reports that these
    # devices "ceased working".
    add_definitions(-DLIB_NDOF=1)
endif (NDOF)


