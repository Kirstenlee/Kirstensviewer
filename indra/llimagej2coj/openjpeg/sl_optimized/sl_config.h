/*
 * Second Life Optimized Configuration
 * 
 * This file contains SL-specific preprocessor definitions and feature flags
 */

#ifndef SL_CONFIG_H
#define SL_CONFIG_H

#include "../opj_config.h"

#if SL_EMBEDDED_OPENJPEG

/* ============================================================================
 * Feature Flags (TODO: Implement these optimizations)
 * ============================================================================ */

// Phase 1: Basic integration (IMPLEMENTED)
#define SL_FEATURE_EMBEDDED_CODE        1   // Using embedded OpenJPEG code

// Phase 2: Dead code elimination (TODO)
#define SL_FEATURE_NO_JP2               1   // Remove JP2 file format support
#define SL_FEATURE_NO_FILE_IO           1   // Remove file I/O (memory only)

// Phase 3: DWT optimizations (TODO - YOUR EXPERTISE!)
#define SL_FEATURE_CUSTOM_DWT           0   // Custom DWT for power-of-2 sizes
#define SL_FEATURE_DWT_AVX2             0   // AVX2 SIMD wavelet transform
#define SL_FEATURE_DWT_UNROLLED         0   // Loop unrolling for 64x64 tiles

// Phase 4: Memory optimizations (TODO)
#define SL_FEATURE_MEMORY_POOLS         0   // Pre-allocated texture pools
#define SL_FEATURE_ZERO_COPY            0   // Zero-copy decode to output buffer

// Phase 5: Tile optimizations (TODO)
#define SL_FEATURE_TILE_64X64_FAST      0   // Hardcoded 64x64 tile decoder
#define SL_FEATURE_TILE_UNROLLED        0   // Unrolled tile processing loops

// Phase 6: Advanced optimizations (TODO)
#define SL_FEATURE_PROFILE_GUIDED       0   // Profile-Guided Optimization enabled

/* ============================================================================
 * Runtime Feature Detection
 * ============================================================================ */

#ifdef OPJ_COMPILER_MSVC
    #include <intrin.h>

    // CPU feature detection for SIMD
    static inline bool sl_cpu_has_avx2(void) {
        int cpuInfo[4];
        __cpuid(cpuInfo, 0);
        if (cpuInfo[0] >= 7) {
            __cpuidex(cpuInfo, 7, 0);
            return (cpuInfo[1] & (1 << 5)) != 0;  // Check AVX2 bit
        }
        return false;
    }

    static inline bool sl_cpu_has_sse42(void) {
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[2] & (1 << 20)) != 0;  // Check SSE4.2 bit
    }
#else
    // TODO: GCC/Clang CPU detection
    static inline bool sl_cpu_has_avx2(void) { return false; }
    static inline bool sl_cpu_has_sse42(void) { return false; }
#endif

/* ============================================================================
 * Debug/Logging Configuration
 * ============================================================================ */

#ifdef OPJ_DEBUG_BUILD
    #define SL_VERBOSE_LOGGING          1   // Enable detailed logging
    #define SL_PERFORMANCE_PROFILING    1   // Enable performance timers
#else
    #define SL_VERBOSE_LOGGING          0
    #define SL_PERFORMANCE_PROFILING    0
#endif

/* ============================================================================
 * Optimization Hints
 * ============================================================================ */

// Compiler optimization hints
#ifdef OPJ_COMPILER_MSVC
    #define SL_LIKELY(x)    (x)
    #define SL_UNLIKELY(x)  (x)
    #define SL_RESTRICT     __restrict
    #define SL_ASSUME(x)    __assume(x)
#elif defined(OPJ_COMPILER_GCC)
    #define SL_LIKELY(x)    __builtin_expect(!!(x), 1)
    #define SL_UNLIKELY(x)  __builtin_expect(!!(x), 0)
    #define SL_RESTRICT     __restrict__
    #define SL_ASSUME(x)    do { if (!(x)) __builtin_unreachable(); } while(0)
#else
    #define SL_LIKELY(x)    (x)
    #define SL_UNLIKELY(x)  (x)
    #define SL_RESTRICT
    #define SL_ASSUME(x)
#endif

#endif // SL_EMBEDDED_OPENJPEG

#endif // SL_CONFIG_H
