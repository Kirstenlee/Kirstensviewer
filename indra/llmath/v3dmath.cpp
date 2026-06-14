/**
 * @file v3dmath.cpp
 * @brief LLVector3d class implementation.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#include "linden_common.h"

//#include <sstream>    // gcc 2.95.2 doesn't support sstream

#include "v3dmath.h"

#include "v4math.h"
#include "m4math.h"
#include "m3math.h"
#include "llquaternion.h"
#include "llquantize.h"

// S24 includes
#include <immintrin.h>
#include <sstream>

// LLVector3d
// WARNING: Don't use these for global const definitions!
// For example:
//      const LLQuaternion(0.5f * DirectX::XM_PI, LLVector3d::zero);
// at the top of a *.cpp file might not give you what you think.
const LLVector3d LLVector3d::zero(0,0,0);
const LLVector3d LLVector3d::x_axis(1, 0, 0);
const LLVector3d LLVector3d::y_axis(0, 1, 0);
const LLVector3d LLVector3d::z_axis(0, 0, 1);
const LLVector3d LLVector3d::x_axis_neg(-1, 0, 0);
const LLVector3d LLVector3d::y_axis_neg(0, -1, 0);
const LLVector3d LLVector3d::z_axis_neg(0, 0, -1);


// Clamps each values to range (min,max).
// Returns true if data changed.
// S24 - AVX
bool LLVector3d::clamp(F64 min, F64 max)
{
    bool ret = false;

    // Load the values into an AVX2 register
    __m256d values = _mm256_loadu_pd(mdV); // Load the 3 elements + 1 padding

    // Create AVX2 registers for min and max
    __m256d min_val = _mm256_set1_pd(min);
    __m256d max_val = _mm256_set1_pd(max);

    // Clamp the values
    __m256d clamped = _mm256_max_pd(_mm256_min_pd(values, max_val), min_val);

    // Check if any values changed
    __m256d diff = _mm256_cmp_pd(values, clamped, _CMP_NEQ_OQ); // Compare for inequality
    ret = (_mm256_movemask_pd(diff) != 0);

    // Store the clamped values back
    _mm256_storeu_pd(mdV, clamped);

    return ret;
}

// S24 AVX
// Sets all values to absolute value of their original values
// Returns true if data changed
bool LLVector3d::abs()
{
    bool ret = false;

    // Load the values into an AVX2 register
    __m256d values = _mm256_loadu_pd(mdV); // Load the 3 elements + 1 padding (double-precision)

    // Create a mask for values less than 0
    __m256d zero = _mm256_set1_pd(0.0);
    __m256d neg_mask = _mm256_cmp_pd(values, zero, _CMP_LT_OQ); // Mask for negative values

    // Take absolute values using bitwise AND with inverted sign bit
    __m256d abs_values = _mm256_andnot_pd(neg_mask, values);

    // Store the result back
    _mm256_storeu_pd(mdV, abs_values);

    // Check if any value was modified (ret = true if any element was negative)
    ret = (_mm256_movemask_pd(neg_mask) != 0);

    return ret;
}

std::ostream& operator<<(std::ostream& s, const LLVector3d &a)
{
    s << "{ " << a.mdV[VX] << ", " << a.mdV[VY] << ", " << a.mdV[VZ] << " }";
    return s;
}

const LLVector3d& LLVector3d::operator=(const LLVector4 &a)
{
    mdV[VX] = a.mV[VX];
    mdV[VY] = a.mV[VY];
    mdV[VZ] = a.mV[VZ];
    return *this;
}

const LLVector3d&   LLVector3d::rotVec(const LLMatrix3 &mat)
{
    *this = *this * mat;
    return *this;
}

const LLVector3d&   LLVector3d::rotVec(const LLQuaternion &q)
{
    *this = *this * q;
    return *this;
}

const LLVector3d&   LLVector3d::rotVec(F64 angle, const LLVector3d &vec)
{
    if ( !vec.isExactlyZero() && angle )
    {
        *this = *this * LLMatrix3((F32)angle, vec);
    }
    return *this;
}

const LLVector3d&   LLVector3d::rotVec(F64 angle, F64 x, F64 y, F64 z)
{
    LLVector3d vec(x, y, z);
    if ( !vec.isExactlyZero() && angle )
    {
        *this = *this * LLMatrix3((F32)angle, vec);
    }
    return *this;
}


bool LLVector3d::parseVector3d(const std::string& buf, LLVector3d* value)
{
    if (buf.empty() || value == nullptr)
    {
        return false;
    }

    LLVector3d v;
    S32 count = sscanf(buf.c_str(), "%lf %lf %lf", v.mdV + VX, v.mdV + VY, v.mdV + VZ);
    if (3 == count)
    {
        value->setVec(v);
        return true;
    }

    return false;
}
