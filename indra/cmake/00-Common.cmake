include_guard()

include(Variables)
include(00-BuildOptions)

# Global C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configuration types
set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo;Release" CACHE STRING "Supported build types" FORCE)
set(BUILD_SHARED_LIBS OFF)

# Build type definitions
add_compile_definitions(
    $<$<CONFIG:Release>:LL_RELEASE=1>
    $<$<CONFIG:Release>:LL_RELEASE_FOR_DOWNLOAD=1>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:LL_RELEASE=1>
    $<$<CONFIG:RelWithDebInfo>:LL_RELEASE_WITH_DEBUG_INFO=1>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
    ADDRESS_SIZE=${ADDRESS_SIZE}
)

# Windows platform definitions
add_compile_definitions(
    LL_WINDOWS=1
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES=1
    GLM_FORCE_SSE2=1
    GLM_ENABLE_EXPERIMENTAL=1
)

# MSVC compile options
add_compile_options(
    /W3
    /permissive-
    /Zc:forScope
    /MP
    # FP options are set due to some serious regressions in the 14.51 toolchain
    /fp:precise
    #/fp:contract
    #/fp:except-
    #/d2ftz

    /Oy-
    /GS
    /Gw
    $<$<CONFIG:RelWithDebInfo>:/Od>
    $<$<CONFIG:RelWithDebInfo>:/MD>
    $<$<CONFIG:RelWithDebInfo>:/Zi>
    $<$<CONFIG:RelWithDebInfo>:/Zo>
    $<$<CONFIG:RelWithDebInfo>:/Ob0>
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Ot>
    $<$<CONFIG:Release>:/Oi>
    $<$<CONFIG:Release>:/Gy>
    $<$<CONFIG:Release>:/MD>
    $<$<CONFIG:Release>:/Zi>
    $<$<CONFIG:Release>:/Zo>
    $<$<CONFIG:Release>:/Ob2>
    $<$<CONFIG:Release>:/GL>
    $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:/arch:AVX2>
    $<$<NOT:$<BOOL:${VS_DISABLE_FATAL_WARNINGS}>>:/WX->
)

# MSVC linker options
add_link_options(
    /OPT:REF
    /OPT:ICF
    /IGNORE:4099
    /IGNORE:4217
    /IGNORE:4286
    /LARGEADDRESSAWARE
    $<$<CONFIG:Release>:/LTCG>
)

# Compiler cache support (ccache/sccache)
# Use /Z7 for embedded debug info instead of /Zi (external PDB) for better caching
if(CMAKE_CXX_COMPILER_LAUNCHER MATCHES "cache")
    add_compile_options(/Z7)
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
endif()