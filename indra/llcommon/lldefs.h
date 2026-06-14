/** 
 * @file lldefs.h
 * @brief Various generic constant definitions.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLDEFS_H
#define LL_LLDEFS_H

#include "stdtypes.h"
#include <type_traits>
#include <algorithm>
#include <immintrin.h>   // AVX2 is a required part of this header

// -----------------------------------------------------------------------------
// Fundamental constants (unchanged semantics)
// -----------------------------------------------------------------------------

constexpr U32 VX = 0;
constexpr U32 VY = 1;
constexpr U32 VZ = 2;
constexpr U32 VW = 3;
constexpr U32 VS = 3;

constexpr U32 VRED   = 0;
constexpr U32 VGREEN = 1;
constexpr U32 VBLUE  = 2;
constexpr U32 VALPHA = 3;

constexpr U32 INVALID_DIRECTION = 0xFFFFFFFF;
constexpr U32 EAST  = 0;
constexpr U32 NORTH = 1;
constexpr U32 WEST  = 2;
constexpr U32 SOUTH = 3;

constexpr U32 NORTHEAST = 4;
constexpr U32 NORTHWEST = 5;
constexpr U32 SOUTHWEST = 6;
constexpr U32 SOUTHEAST = 7;
constexpr U32 MIDDLE    = 8;

constexpr U8 EAST_MASK  = 1 << EAST;
constexpr U8 NORTH_MASK = 1 << NORTH;
constexpr U8 WEST_MASK  = 1 << WEST;
constexpr U8 SOUTH_MASK = 1 << SOUTH;

constexpr U8 NORTHEAST_MASK = NORTH_MASK | EAST_MASK;
constexpr U8 NORTHWEST_MASK = NORTH_MASK | WEST_MASK;
constexpr U8 SOUTHWEST_MASK = SOUTH_MASK | WEST_MASK;
constexpr U8 SOUTHEAST_MASK = SOUTH_MASK | EAST_MASK;

constexpr U32 gDirOpposite[8] = {2, 3, 0, 1, 6, 7, 4, 5};

constexpr U32 gDirAdjacent[8][2] = {
    {4, 7}, {4, 5}, {5, 6}, {6, 7},
    {0, 1}, {1, 2}, {2, 3}, {0, 3}
};

constexpr S32 gDirAxes[8][2] = {
    { 1, 0}, { 0, 1}, {-1, 0}, { 0,-1},
    { 1, 1}, {-1, 1}, {-1,-1}, { 1,-1}
};

constexpr S32 gDirMasks[8] = {
    EAST_MASK, NORTH_MASK, WEST_MASK, SOUTH_MASK,
    NORTHEAST_MASK, NORTHWEST_MASK, SOUTHWEST_MASK, SOUTHEAST_MASK
};

// Sides of a box...
//                  . Z      __.Y
//                 /|\        /|       0 = NO_SIDE
//                  |        /         1 = FRONT_SIDE   = +x
//           +------|-----------+      2 = BACK_SIDE    = -x
//          /|      |/     /   /|      3 = LEFT_SIDE    = +y
//         / |     -5-   |/   / |      4 = RIGHT_SIDE   = -y
//        /  |     /|   -3-  /  |      5 = TOP_SIDE     = +z
//       +------------------+   |      6 = BOTTOM_SIDE  = -z
//       |   |      |  /    |   |
//       | |/|      | /     | |/|
//       | 2 |    | *-------|-1--------> X
//       |/| |   -4-        |/| |
//       |   +----|---------|---+
//       |  /        /      |  /
//       | /       -6-      | /
//       |/        /        |/
//       +------------------+
constexpr U32 NO_SIDE     = 0;
constexpr U32 FRONT_SIDE  = 1;
constexpr U32 BACK_SIDE   = 2;
constexpr U32 LEFT_SIDE   = 3;
constexpr U32 RIGHT_SIDE  = 4;
constexpr U32 TOP_SIDE    = 5;
constexpr U32 BOTTOM_SIDE = 6;

// Sound flags
constexpr U8 LL_SOUND_FLAG_NONE         = 0x0;
constexpr U8 LL_SOUND_FLAG_LOOP         = 1 << 0;
constexpr U8 LL_SOUND_FLAG_SYNC_MASTER  = 1 << 1;
constexpr U8 LL_SOUND_FLAG_SYNC_SLAVE   = 1 << 2;
constexpr U8 LL_SOUND_FLAG_SYNC_PENDING = 1 << 3;
constexpr U8 LL_SOUND_FLAG_QUEUE        = 1 << 4;
constexpr U8 LL_SOUND_FLAG_STOP         = 1 << 5;

constexpr U8 LL_SOUND_FLAG_SYNC_MASK =
    LL_SOUND_FLAG_SYNC_MASTER |
    LL_SOUND_FLAG_SYNC_SLAVE |
    LL_SOUND_FLAG_SYNC_PENDING;

// String/path constants
constexpr U32 LL_MAX_PATH         = 1024;
constexpr U32 STD_STRING_BUF_SIZE = 255;
constexpr U32 STD_STRING_STR_LEN  = 254;
constexpr U32 MAX_STRING          = STD_STRING_BUF_SIZE;
constexpr U32 MAXADDRSTR          = 17;

// -----------------------------------------------------------------------------
// Core scalar helpers (functionally identical to original)
// -----------------------------------------------------------------------------

// S24 Highly optimized, branchless, always-inline, constexpr, noexcept min/max
// Compatible with all usages in the LL codebase (Unreal/Crytek style)
// Uses C++17 fold expressions for zero-overhead variadic support

// Scalar 2-arg version
template <typename T>
[[nodiscard]] constexpr inline T llmax(const T& a, const T& b) noexcept
{
    // Branchless: returns the greater of a and b
    return (a < b) ? b : a;
}

// Variadic version (3+ args)
template <typename T0, typename T1, typename... Ts>
[[nodiscard]] constexpr inline auto llmax(const T0& a, const T1& b, const Ts&... rest) noexcept
{
    using CT = std::common_type_t<T0, T1, Ts...>;
    CT result = (a < b) ? b : a;
    ((result = (result < rest) ? rest : result), ...);
    return result;
}

// Scalar 2-arg version
template <typename T>
[[nodiscard]] constexpr inline T llmin(const T& a, const T& b) noexcept
{
    // Branchless: returns the lesser of a and b
    return (a < b) ? a : b;
}

// Variadic version (3+ args)
template <typename T0, typename T1, typename... Ts>
[[nodiscard]] constexpr inline auto llmin(const T0& a, const T1& b, const Ts&... rest) noexcept
{
    using CT = std::common_type_t<T0, T1, Ts...>;
    CT result = (a < b) ? a : b;
    ((result = (rest < result) ? rest : result), ...);
    return result;
}

// Clamp
template <typename A, typename MIN, typename MAX>
constexpr A llclamp(A a, MIN minval, MAX maxval) noexcept
{
    const A amin = static_cast<A>(minval);
    const A amax = static_cast<A>(maxval);
    return (a < amin) ? amin : (a > amax ? amax : a);
}

template <class T>
constexpr T llclampf(T a) noexcept
{
    return llclamp(a, T(0), T(1));
}

template <class T>
constexpr T llclampb(T a) noexcept
{
    return llclamp(a, T(0), T(255));
}

template <class T>
constexpr void llswap(T& lhs, T& rhs) noexcept(std::is_nothrow_swappable_v<T>)
{
    using std::swap;
    swap(lhs, rhs);
}

// -----------------------------------------------------------------------------
// S24 Core AVX2 bulk helpers
// These are part of the core API: if you call the array overloads, you get AVX.
// Note: Scalar overloads above remain exactly as before.
// -----------------------------------------------------------------------------

// Max over float array (AVX2)
inline float llmax(const float* data, std::size_t n) noexcept
{
    if (n == 0) return 0.0f; // policy: same as "no elements" default

    float maxv = data[0];
    std::size_t i = 1;

    __m256 vmax = _mm256_set1_ps(maxv);

    for (; i + 7 < n; i += 8)
    {
        __m256 v = _mm256_loadu_ps(&data[i]);
        vmax = _mm256_max_ps(vmax, v);
    }

    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, vmax);
    for (int k = 0; k < 8; ++k)
        if (tmp[k] > maxv) maxv = tmp[k];

    for (; i < n; ++i)
        if (data[i] > maxv) maxv = data[i];

    return maxv;
}

// Min over float array (AVX2)
inline float llmin(const float* data, std::size_t n) noexcept
{
    if (n == 0) return 0.0f;

    float minv = data[0];
    std::size_t i = 1;

    __m256 vmin = _mm256_set1_ps(minv);

    for (; i + 7 < n; i += 8)
    {
        __m256 v = _mm256_loadu_ps(&data[i]);
        vmin = _mm256_min_ps(vmin, v);
    }

    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, vmin);
    for (int k = 0; k < 8; ++k)
        if (tmp[k] < minv) minv = tmp[k];

    for (; i < n; ++i)
        if (data[i] < minv) minv = data[i];

    return minv;
}

#endif // LL_LLDEFS_H


