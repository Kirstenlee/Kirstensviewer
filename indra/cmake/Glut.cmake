include_guard()

include(Linking)
include(Prebuilt)

use_prebuilt_binary(freeglut)
set(GLUT_LIBRARY
    debug freeglut_static.lib
    optimized freeglut_static.lib)

