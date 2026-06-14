# -*- cmake -*-

project(llimagej2coj)

include(00-Common)
include(LLCommon)
include(LLImage)

include(OpenJPEG)

# ============================================================================
# Embedded OpenJPEG - DEFAULT MODE (optimized static build)
# ============================================================================
# The viewer now uses embedded OpenJPEG by default for better performance.
# vcpkg can still download openjpeg (harmless) but it won't be used.
# ============================================================================

# Always use embedded OpenJPEG - no external library option
add_compile_definitions(OPJ_STATIC)
message(STATUS "llimagej2coj: Using EMBEDDED OpenJPEG (optimized)")

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}           # For openjpeg/ includes
    ${CMAKE_CURRENT_SOURCE_DIR}/openjpeg/core
    ${CMAKE_CURRENT_SOURCE_DIR}/openjpeg/utils
    )

# ============================================================================
# Source Files
# ============================================================================

set(llimagej2coj_SOURCE_FILES
    llimagej2coj.cpp
    )

set(llimagej2coj_HEADER_FILES
    CMakeLists.txt
    llimagej2coj.h
    )

# ============================================================================
# Embedded OpenJPEG Files
# ============================================================================

# Configuration headers
list(APPEND llimagej2coj_HEADER_FILES
    openjpeg/opj_config.h
    openjpeg/sl_optimized/sl_config.h
    openjpeg/sl_optimized/sl_types.h
)

# Core codec source files (Phase 1 - OpenJPEG 2.5.4)
list(APPEND llimagej2coj_SOURCE_FILES
    
    openjpeg/core/openjpeg.c
    openjpeg/core/j2k.c
    openjpeg/core/jp2.c
    openjpeg/core/tcd.c
    openjpeg/core/dwt.c
    openjpeg/core/ht_dec.c
    openjpeg/core/t1.c
    openjpeg/core/t2.c
    openjpeg/core/mct.c
    openjpeg/core/mqc.c
    openjpeg/core/bio.c
    openjpeg/core/pi.c
    openjpeg/core/tgt.c
    openjpeg/utils/opj_malloc.c
    openjpeg/utils/image.c
    openjpeg/utils/event.c
    openjpeg/utils/sparse_array.c
    openjpeg/utils/thread.c
    openjpeg/utils/invert.c
    openjpeg/utils/opj_clock.c
    openjpeg/utils/cio.c
    openjpeg/utils/function_list.c
)

list(APPEND llimagej2coj_HEADER_FILES
    openjpeg/core/openjpeg.h
    openjpeg/core/opj_codec.h
    openjpeg/core/opj_common.h
    openjpeg/core/opj_includes.h
    openjpeg/core/opj_intmath.h
    openjpeg/core/j2k.h
    openjpeg/core/jp2.h
    openjpeg/core/tcd.h
    openjpeg/core/dwt.h
    openjpeg/core/t1.h
    openjpeg/core/t1_luts.h
    openjpeg/core/t2.h
    openjpeg/core/mct.h
    openjpeg/core/mqc.h
    openjpeg/core/mqc_inl.h
    openjpeg/core/bio.h
    openjpeg/core/pi.h
    openjpeg/core/tgt.h
    openjpeg/utils/opj_malloc.h
    openjpeg/utils/image.h
    openjpeg/utils/event.h
    openjpeg/utils/sparse_array.h
    openjpeg/utils/thread.h
    openjpeg/utils/tls_keys.h
    openjpeg/utils/invert.h
    openjpeg/utils/opj_clock.h
    openjpeg/utils/cio.h
    openjpeg/utils/function_list.h
)

message(STATUS "  -> Embedded OpenJPEG 2.5.4: 48 core files integrated")
message(STATUS "  -> DWT optimization target: openjpeg/core/dwt.c")

set_source_files_properties(${llimagej2coj_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llimagej2coj_SOURCE_FILES ${llimagej2coj_HEADER_FILES})

# ============================================================================
# Build Library
# ============================================================================

add_library (llimagej2coj ${llimagej2coj_SOURCE_FILES})

# ============================================================================
# Link Libraries
# ============================================================================

# Embedded version: No external OpenJPEG library needed
target_link_libraries(llimagej2coj
        ${LLCOMMON_LIBRARIES}
    ${LLIMAGE_LIBRARIES}
)

# ============================================================================
# Compiler Optimizations (Embedded OpenJPEG)
# ============================================================================

if(MSVC)
    # Aggressive optimization for Release builds
    target_compile_options(llimagej2coj PRIVATE
        $<$<CONFIG:Release>:/O2 /Oi /arch:AVX2>
    )

    # Link-Time Code Generation
    target_link_options(llimagej2coj PRIVATE
        
    )

    message(STATUS "  -> Embedded OpenJPEG: MSVC optimizations enabled (Release)")

elseif(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # GCC/Clang optimizations
    target_compile_options(llimagej2coj PRIVATE
        $<$<CONFIG:Release>:-O3 -flto -march=native>
    )

    target_link_options(llimagej2coj PRIVATE
        $<$<CONFIG:Release>:-flto>
    )

    message(STATUS "  -> Embedded OpenJPEG: GCC/Clang optimizations enabled (Release)")
endif()






