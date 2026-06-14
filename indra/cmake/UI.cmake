# -*- cmake -*-
include(Prebuilt)
include(FreeType)

if (USESYSTEMLIBS)
  include(FindPkgConfig)
    
  foreach(pkg ${PKGCONFIG_PACKAGES})
    pkg_check_modules(${pkg} REQUIRED ${pkg})
    include_directories(${${pkg}_INCLUDE_DIRS})
    link_directories(${${pkg}_LIBRARY_DIRS})
    list(APPEND UI_LIBRARIES ${${pkg}_LIBRARIES})
    add_definitions(${${pkg}_CFLAGS_OTHERS})
  endforeach(pkg)
  
endif (USESYSTEMLIBS)

