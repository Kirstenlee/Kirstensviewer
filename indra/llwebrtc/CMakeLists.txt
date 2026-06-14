# -*- cmake -*-

# Project setup
project(llwebrtc LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includes
include(00-Common)
include(Linking)
include(WebRTC)

# Source and header files
set(llwebrtc_SOURCE_FILES
    llwebrtc.cpp
    llwebrtc.h
    llwebrtc_impl.h
)

# Create shared library
add_library(llwebrtc SHARED ${llwebrtc_SOURCE_FILES})
set_target_properties(llwebrtc PROPERTIES PUBLIC_HEADER llwebrtc.h)

# Include directories
target_include_directories(llwebrtc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Platform-specific settings
if (WIN32)
    cmake_policy(SET CMP0091 NEW)

    # Force Release runtime for consistency with WebRTC binaries
    set_property(TARGET llwebrtc PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

    # Link flags and libraries
    set_target_properties(llwebrtc PROPERTIES
        LINK_FLAGS "/release /LARGEADDRESSAWARE"
    )

    target_compile_options(llwebrtc PRIVATE "/MT" "/Zc:wchar_t")

    target_link_libraries(llwebrtc PRIVATE
        ll::webrtc
        secur32
        winmm
        dmoguids
        wmcodecdspuuid
        msdmo
        strmiids
        iphlpapi
        libcmt
        Advapi32.lib
    )

    # Inject macro to override legacy wchar_t detection
    target_compile_definitions(llwebrtc PRIVATE LL_WCHAR_T_NATIVE)
endif()