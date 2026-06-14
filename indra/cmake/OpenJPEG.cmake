# -*- cmake -*-
include_guard()
include(Prebuilt)

# Embedded OpenJPEG - no external library needed
# The ll::openjpeg interface exists for compatibility but does nothing
add_library(ll::openjpeg INTERFACE IMPORTED)
