/*
 * Second Life Optimized Types and Constants
 * 
 * Defines SL-specific types, constants, and helper macros for optimized OpenJPEG integration
 */

#ifndef SL_TYPES_H
#define SL_TYPES_H

#include "../opj_config.h"

#if SL_EMBEDDED_OPENJPEG

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Second Life Texture Constants
 * ============================================================================ */

// Common SL texture dimensions (always power-of-2)
#define SL_TEX_SIZE_64      64
#define SL_TEX_SIZE_128     128
#define SL_TEX_SIZE_256     256
#define SL_TEX_SIZE_512     512
#define SL_TEX_SIZE_1024    1024
#define SL_TEX_SIZE_2048    2048

// Component counts
#define SL_COMPONENTS_GRAY  1   // Grayscale
#define SL_COMPONENTS_RGB   3   // RGB
#define SL_COMPONENTS_RGBA  4   // RGBA

// Tile size (hardcoded for SL)
#define SL_TILE_WIDTH       64
#define SL_TILE_HEIGHT      64
#define SL_TILE_PIXELS      (SL_TILE_WIDTH * SL_TILE_HEIGHT)  // 4096

// Discard levels (mipmap levels)
#define SL_DISCARD_NONE     0   // Full resolution
#define SL_DISCARD_1        1   // Half resolution
#define SL_DISCARD_2        2   // Quarter resolution
#define SL_DISCARD_3        3   // Eighth resolution
#define SL_DISCARD_4        4   // Sixteenth resolution
#define SL_DISCARD_MAX      SL_DISCARD_4

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

// Check if dimension is power of 2
#define SL_IS_POW2(x) (((x) > 0) && (((x) & ((x) - 1)) == 0))

// Fast power-of-2 operations (no division)
#define SL_DIV_POW2(val, shift) ((val) >> (shift))
#define SL_MUL_POW2(val, shift) ((val) << (shift))

// Alignment macros
#define SL_ALIGN_16(x) (((x) + 15) & ~15)
#define SL_ALIGN_32(x) (((x) + 31) & ~31)

// Min/max macros
#ifndef SL_MIN
#define SL_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef SL_MAX
#define SL_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

// Clamp value between min and max
#define SL_CLAMP(val, min_val, max_val) \
    SL_MAX(min_val, SL_MIN(max_val, val))

/* ============================================================================
 * Memory Pool Size Hints
 * ============================================================================ */

// Pre-allocated pool sizes for common texture sizes
#define SL_POOL_SIZE_256    (256  * 256  * 4)  // 256KB
#define SL_POOL_SIZE_512    (512  * 512  * 4)  // 1MB
#define SL_POOL_SIZE_1024   (1024 * 1024 * 4)  // 4MB
#define SL_POOL_SIZE_2048   (2048 * 2048 * 4)  // 16MB

/* ============================================================================
 * Fast Path Detection
 * ============================================================================ */

// Inline function to detect if we can use SL-optimized fast paths
static OPJ_INLINE bool sl_can_use_fast_path(
    uint32_t width,
    uint32_t height,
    uint32_t tile_width,
    uint32_t tile_height,
    uint32_t num_components)
{
    // Requirements for fast path:
    // 1. Power-of-2 dimensions
    // 2. 64x64 tiles
    // 3. RGB or RGBA
    // 4. Dimensions <= 2048

    return SL_IS_POW2(width) &&
           SL_IS_POW2(height) &&
           (tile_width == SL_TILE_WIDTH) &&
           (tile_height == SL_TILE_HEIGHT) &&
           (num_components == SL_COMPONENTS_RGB || num_components == SL_COMPONENTS_RGBA) &&
           (width <= SL_MAX_TEXTURE_DIM) &&
           (height <= SL_MAX_TEXTURE_DIM);
}

/* ============================================================================
 * SL Texture Metadata
 * ============================================================================ */

typedef struct sl_texture_info {
    uint32_t width;
    uint32_t height;
    uint32_t num_components;    // 3=RGB, 4=RGBA
    uint32_t discard_level;     // 0-4
    uint32_t tile_width;
    uint32_t tile_height;
    bool     is_power_of_2;
    bool     can_use_fast_path;
} sl_texture_info_t;

/* ============================================================================
 * Performance Profiling Macros (Debug builds)
 * ============================================================================ */

#ifdef OPJ_DEBUG_BUILD
    #include <stdio.h>
    #include <time.h>

    #define SL_PROFILE_START(name) \
        clock_t _sl_profile_##name = clock();

    #define SL_PROFILE_END(name) \
        do { \
            clock_t _sl_profile_end = clock(); \
            double elapsed = ((double)(_sl_profile_end - _sl_profile_##name)) / CLOCKS_PER_SEC; \
            printf("[SL-OPJ] %s: %.3f ms\n", #name, elapsed * 1000.0); \
        } while(0)
#else
    #define SL_PROFILE_START(name)
    #define SL_PROFILE_END(name)
#endif

#endif // SL_EMBEDDED_OPENJPEG

#endif // SL_TYPES_H
