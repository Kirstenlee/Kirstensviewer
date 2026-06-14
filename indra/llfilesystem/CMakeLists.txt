# -*- cmake -*-

project(llfilesystem)

include(00-Common)
include(LLCommon)

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    )

set(llfilesystem_SOURCE_FILES
    lldir.cpp
    lldiriterator.cpp
    lllfsthread.cpp
    lldiskcache.cpp
    llfilesystem.cpp
    )

set(llfilesystem_HEADER_FILES
    CMakeLists.txt
    lldir.h
    lldirguard.h
    lldiriterator.h
    lllfsthread.h
    lldiskcache.h
    llfilesystem.h
    )


if (WINDOWS)
  LIST(APPEND llfilesystem_SOURCE_FILES lldir_win32.cpp)
  LIST(APPEND llfilesystem_HEADER_FILES lldir_win32.h)
endif (WINDOWS)

set_source_files_properties(${llfilesystem_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llfilesystem_SOURCE_FILES ${llfilesystem_HEADER_FILES})

add_library (llfilesystem ${llfilesystem_SOURCE_FILES})

set(cache_BOOST_LIBRARIES
    ${BOOST_FILESYSTEM_LIBRARY}
    ${BOOST_SYSTEM_LIBRARY}
    )

target_link_libraries(llfilesystem
    ${LLCOMMON_LIBRARIES}
    ${cache_BOOST_LIBRARIES}
    )

target_compile_options(llfilesystem PRIVATE "/Zc:wchar_t-")



