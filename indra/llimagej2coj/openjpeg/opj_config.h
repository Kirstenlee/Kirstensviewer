/*
 * OpenJPEG Configuration - Second Life Optimized Build
 * 
 * This file is part of the embedded OpenJPEG integration for Kirstens S24 viewer.
 * Copyright notice and license from original OpenJPEG project applies.
 * 
 * Original OpenJPEG: Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * BSD 2-Clause License
 */

#ifndef OPJ_CONFIG_SL_H
#define OPJ_CONFIG_SL_H

/* ============================================================================
 * KILL SWITCH: Set to 1 to enable embedded OpenJPEG, 0 to use vcpkg library
 * ============================================================================
 * 
 * When set to 0: Uses external openjp2.dll from vcpkg (current behavior)
 * When set to 1: Uses embedded, optimized OpenJPEG code in this directory
 * 
 * This allows safe development and testing without breaking existing functionality
 */
#ifndef SL_EMBEDDED_OPENJPEG
    #define SL_EMBEDDED_OPENJPEG 1  // Default: ON (use embedded library)
#endif

/* ============================================================================
 * Second Life Specific Configuration
 * ============================================================================ */

// Always embedded - no external OpenJPEG option

// Disable unused OpenJPEG features (massive code size reduction)
#define OPJ_NO_JP2          1   // No JP2 file format (only J2K codestream)
#define OPJ_NO_MJ2          1   // No Motion JPEG2000
#define OPJ_NO_JPT          1   // No JPIP Tile-part
#define OPJ_NO_JPIP         1   // No JPIP protocol

// Second Life texture constants (enables aggressive optimizations)
#define SL_TILE_SIZE        64  // SL always uses 64x64 tiles
#define SL_MAX_TEXTURE_DIM  2048 // Largest SL texture dimension (2048x2048)
#define SL_MAX_COMPONENTS   4   // RGBA maximum
#define SL_MAX_DISCARD      5   // Discard levels 0-4 (plus full resolution)

// Performance optimizations
#define OPJ_USE_FAST_MATH   1   // Enable fast floating point math
#define OPJ_USE_AVX2        1   // Enable AVX2 SIMD instructions
#define OPJ_USE_SSE42       1   // Enable SSE4.2 for older CPUs (fallback)

// Threading configuration - Conservative tuning for large textures only
// STRATEGY: OpenJPEG threading ONLY enabled for large textures (1024x1024+)
//   - Small/Medium (<=512x512): Single-threaded (overhead > benefit)
//   - Large (1024x1024+): 2 threads for wavelet parallelism
// RATIONALE: Viewer already has 8-32 ImageDecode threads handling different textures
//   Adding selective threading for large textures provides real benefit without over-subscription
// MATH: ~5 large textures × 2 threads = 10 extra threads (acceptable overhead)
// The OPJ_NUM_THREADS environment variable can override for testing

// Memory optimization
#define OPJ_USE_CUSTOM_ALLOCATOR 1  // Use SL texture memory pools

// End of SL configuration

/* ============================================================================
 * Standard OpenJPEG Build Configuration (from vcpkg)
 * ============================================================================ */

// Standard types
#define OPJ_HAVE_STDINT_H   1
#define OPJ_HAVE_INTTYPES_H 1

// Package information (from vcpkg openjpeg 2.5.4)
#define OPENJPEG_VERSION_MAJOR 2
#define OPENJPEG_VERSION_MINOR 5
#define OPENJPEG_VERSION_BUILD 4

#define OPJ_PACKAGE_VERSION OPENJPEG_VERSION
#define OPENJPEG_VERSION "2.5.4"

// Static vs Dynamic - Always static for embedded OpenJPEG
#ifndef OPJ_STATIC
#define OPJ_STATIC  // Building as static library
#endif

// Platform: Windows x64 with Visual Studio (only supported target)
#define OPJ_WINDOWS
#define OPJ_COMPILER_MSVC
#define OPJ_INLINE __forceinline

/* ============================================================================
 * Debug/Release Configuration
 * ============================================================================ */

#ifdef NDEBUG
    #define OPJ_RELEASE_BUILD 1
#else
    #define OPJ_DEBUG_BUILD 1
#endif

#endif // OPJ_CONFIG_SL_H


