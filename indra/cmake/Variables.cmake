include_guard()

# Definitions of variables used throughout the Kirsten's S24 build process.
# Platform: Windows x64 only

# Relative and absolute paths to subtrees
set(LIBS_CLOSED_PREFIX)
set(LIBS_OPEN_PREFIX)
set(SCRIPTS_PREFIX ../scripts)
set(VIEWER_PREFIX)
set(INCREMENTAL_LINK OFF CACHE BOOL "Use incremental linking on win32 builds (enable for faster links on some machines)")
set(ENABLE_MEDIA_PLUGINS ON CACHE BOOL "Turn off building media plugins if they are imported by third-party library mechanism")

if(LIBS_CLOSED_DIR)
	file(TO_CMAKE_PATH "${LIBS_CLOSED_DIR}" LIBS_CLOSED_DIR)
else()
	set(LIBS_CLOSED_DIR ${CMAKE_SOURCE_DIR}/${LIBS_CLOSED_PREFIX})
endif()

if(LIBS_COMMON_DIR)
	file(TO_CMAKE_PATH "${LIBS_COMMON_DIR}" LIBS_COMMON_DIR)
else()
	set(LIBS_COMMON_DIR ${CMAKE_SOURCE_DIR}/${LIBS_OPEN_PREFIX})
endif()

set(LIBS_OPEN_DIR ${LIBS_COMMON_DIR})
set(SCRIPTS_DIR ${CMAKE_SOURCE_DIR}/${SCRIPTS_PREFIX})
set(VIEWER_DIR ${CMAKE_SOURCE_DIR}/${VIEWER_PREFIX})

# S24 prebuilt libraries location
set(LIBS_PREBUILT_DIR ${CMAKE_SOURCE_DIR}/.. CACHE PATH "Location of prebuilt libraries.")

set(TEMPLATE_VERIFIER_OPTIONS "" CACHE STRING "Options for Test-MessageTemplate.ps1")
set(TEMPLATE_VERIFIER_MASTER_URL "https://github.com/secondlife/master-message-template/raw/master/message_template.msg" CACHE STRING "Location of the master message template")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type.  One of: Debug Release RelWithDebInfo" FORCE)
endif()

# Architecture detection (Windows x64 only, but keep for compatibility)
if(ADDRESS_SIZE EQUAL 32)
	set(ARCH i686)
elseif(ADDRESS_SIZE EQUAL 64)
	set(ARCH x86_64)
else()
	# Fallback: detect architecture using CMake built-in
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(ADDRESS_SIZE 64)
		set(ARCH x86_64)
	else()
		set(ADDRESS_SIZE 32)
		set(ARCH i686)
	endif()
endif()

# Windows platform (always true for S24)
set(WINDOWS ON BOOL FORCE)

# Prebuilt library type
if(ADDRESS_SIZE EQUAL 32)
	set(PREBUILT_TYPE windows)
	message(STATUS "Prebuilt Type = windows")
elseif(ADDRESS_SIZE EQUAL 64)
	set(PREBUILT_TYPE windows64)
	message(STATUS "Prebuilt Type = windows64")
endif()

# Suppress Boost global namespace deprecation warning for MSVC
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
	add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)
endif()

# Default deploy grid
set(GRID agni CACHE STRING "Target Grid")
set(VIEWER_CHANNEL "Kirstens S24" CACHE STRING "Viewer Channel Name")
set(ENABLE_SIGNING OFF CACHE BOOL "Enable signing the viewer")
set(VERSION_BUILD "0" CACHE STRING "Revision number passed in from the outside")
set(USE_PRECOMPILED_HEADERS ON CACHE BOOL "Enable use of precompiled header directives where supported.")

source_group("CMake Rules" FILES CMakeLists.txt)

