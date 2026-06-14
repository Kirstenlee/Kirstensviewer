cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

# Set platform to x64
set(CMAKE_GENERATOR_PLATFORM x64)
set(CMAKE_GENERATOR_TOOLSET host=x64)

set(ROOT_PROJECT_NAME "Kirstens-S24" CACHE STRING "The root project name")
project(${ROOT_PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP support enabled")
endif()

include(Variables)
include(BuildVersion)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Create 'prepare' target for setup actions (must be first)
add_custom_target(prepare DEPENDS ${CMAKE_BINARY_DIR}/prepare/prebuilt)

add_subdirectory(cmake)

add_subdirectory(${LIBS_OPEN_PREFIX}llaudio)
add_subdirectory(${LIBS_OPEN_PREFIX}llappearance)
add_subdirectory(${LIBS_OPEN_PREFIX}llcharacter)
add_subdirectory(${LIBS_OPEN_PREFIX}llcommon)
add_subdirectory(${LIBS_OPEN_PREFIX}llcorehttp)
add_subdirectory(${LIBS_OPEN_PREFIX}llimage)
# S24 no KDU
add_subdirectory(${LIBS_OPEN_PREFIX}llimagej2coj)
add_subdirectory(${LIBS_OPEN_PREFIX}llinventory)
add_subdirectory(${LIBS_OPEN_PREFIX}llmath)
add_subdirectory(${LIBS_OPEN_PREFIX}llmeshoptimizer)
add_subdirectory(${LIBS_OPEN_PREFIX}llmessage)
add_subdirectory(${LIBS_OPEN_PREFIX}llconvexdecomposition)
add_subdirectory(${LIBS_OPEN_PREFIX}llprimitive)
add_subdirectory(${LIBS_OPEN_PREFIX}llrender)
add_subdirectory(dxrender)
add_subdirectory(${LIBS_OPEN_PREFIX}llfilesystem)
add_subdirectory(${LIBS_OPEN_PREFIX}llwebrtc)
add_subdirectory(${LIBS_OPEN_PREFIX}llwindow)
add_subdirectory(${LIBS_OPEN_PREFIX}llxml)

add_custom_target(viewer)
add_subdirectory(${LIBS_OPEN_PREFIX}llplugin)
add_subdirectory(${LIBS_OPEN_PREFIX}llui)
add_subdirectory(${LIBS_OPEN_PREFIX}viewer_components)

if(ENABLE_MEDIA_PLUGINS)
    add_subdirectory(${LIBS_OPEN_PREFIX}media_plugins)
endif()

add_subdirectory(${VIEWER_PREFIX}newview)
add_dependencies(viewer Kirstens-S24)

add_custom_command(
    COMMENT "Obtaining prebuilt binaries..."
    OUTPUT ${CMAKE_BINARY_DIR}/prepare/prebuilt
    COMMAND ${CMAKE_COMMAND} -E sleep 2
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/DownloadAndUnpack.ps1" -TopDir "${CMAKE_SOURCE_DIR}/.."
    DEPENDS ${CMAKE_SOURCE_DIR}/../installS24.xml
)

set(LLCONVEXDECOMPOSITION_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/${LIBS_OPEN_PREFIX}llconvexdecomposition)
set(LLCONVEXDECOMPOSITION_LIBRARIES llconvexdecomposition)

