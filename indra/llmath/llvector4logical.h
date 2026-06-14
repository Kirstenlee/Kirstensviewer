/** 
 * @file llvector4logical.h
 * @brief LLVector4Logical class header file - Companion class to LLVector4a for logical and bit-twiddling operations
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#ifndef	LL_VECTOR4LOGICAL_H
#define	LL_VECTOR4LOGICAL_H

#include "llmemory.h"

////////////////////////////
// LLVector4Logical
////////////////////////////

// S24 includes
#include <array>
#include <immintrin.h>
#include <cstdint>
#include <iostream>

class LLVector4Logical
{
public:
    // Masks as constexpr values for compile-time optimization
    static constexpr uint32_t MASK_X = 1;
    static constexpr uint32_t MASK_Y = 1 << 1;
    static constexpr uint32_t MASK_Z = 1 << 2;
    static constexpr uint32_t MASK_W = 1 << 3;
    static constexpr uint32_t MASK_XYZ = MASK_X | MASK_Y | MASK_Z;
    static constexpr uint32_t MASK_XYZW = MASK_XYZ | MASK_W;

    // Default constructor
    LLVector4Logical() : mQ(_mm_setzero_ps()) {}

    // Constructor accepting a quad (SIMD register)
    LLVector4Logical(const __m128& quad) : mQ(quad) {}

    // Gather bits from the lowest order of each element
    inline uint32_t getGatheredBits() const {
        return _mm_movemask_ps(mQ);
    }

    // Invert the mask
    inline LLVector4Logical& invert() {
        // All-ones constant
        static constexpr alignas(16) std::array<uint32_t, 4> allOnes = {
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        };
        mQ = _mm_andnot_ps(mQ, _mm_load_ps(reinterpret_cast<const float*>(allOnes.data())));
        return *this;
    }

    inline LLBool32 areAllSet( U32 mask ) const
    {
        return (getGatheredBits() & mask) == mask;
    }

    inline LLBool32 areAllSet() const
    {
        return areAllSet(MASK_XYZW);
    }

    inline LLBool32 areAnySet( U32 mask ) const
    {
        return getGatheredBits() & mask;
    }

    inline LLBool32 areAnySet() const
    {
        return areAnySet(MASK_XYZW);
    }

    // Conversion operator to LLQuad (SIMD register)
    inline operator __m128() const {
        return mQ;
    }

    inline void clear()
    {
        mQ = _mm_setzero_ps();
    }

    // Set a specific element using a mask
    template<int N>
    void setElement() {
        constexpr size_t maskIndex = 4 * N;
        alignas(16) static constexpr std::array<uint32_t, 16> S_V4LOGICAL_MASK_TABLE = {
            0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF
        };
        mQ = _mm_or_ps(mQ, _mm_load_ps(reinterpret_cast<const float*>(S_V4LOGICAL_MASK_TABLE.data() + maskIndex)));
    }

private:
    __m128 mQ; // SIMD register for vectorized operations
};

#endif // LL_VECTOR4_LOGICAL_H

