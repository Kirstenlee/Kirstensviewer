/**
 * @file v3math.cpp
 * @brief LLVector3 class implementation.
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

#include "v3math.h"

#include "v2math.h"
#include "v4math.h"
#include "m4math.h"
#include "m3math.h"
#include "llquaternion.h"
#include "llquantize.h"
#include "v3dmath.h"

// S24 includes
#include <immintrin.h> // For avx
#include <algorithm>
#include <cmath>       // For powf
#include "DirectXMath.h"
using namespace DirectX;


// LLVector3
// WARNING: Don't use these for global const definitions!
// For example:
//      const LLQuaternion(0.5f * DirectX::XM_PI, LLVector3::zero);
// at the top of a *.cpp file might not give you what you think.
const LLVector3 LLVector3::zero(0,0,0);
const LLVector3 LLVector3::x_axis(1.f, 0, 0);
const LLVector3 LLVector3::y_axis(0, 1.f, 0);
const LLVector3 LLVector3::z_axis(0, 0, 1.f);
const LLVector3 LLVector3::x_axis_neg(-1.f, 0, 0);
const LLVector3 LLVector3::y_axis_neg(0, -1.f, 0);
const LLVector3 LLVector3::z_axis_neg(0, 0, -1.f);
const LLVector3 LLVector3::all_one(1.f,1.f,1.f);


// S24 DX12
bool LLVector3::clamp(F32 min, F32 max)
{
    bool ret = false;

    // Load the vector components into an XMVECTOR.
    // The 4th component is set to 0 (unused).
    XMVECTOR vec = XMVectorSet(mV[0], mV[1], mV[2], 0.f);

    // Create vectors for the min and max bounds.
    XMVECTOR minVec = XMVectorReplicate(min);
    XMVECTOR maxVec = XMVectorReplicate(max);

    // Clamp the vector.
    XMVECTOR clampedVec = XMVectorClamp(vec, minVec, maxVec);

    // Extract the original and clamped values into XMFLOAT3 structs.
    XMFLOAT3 orig, clamped;
    XMStoreFloat3(&orig, vec);
    XMStoreFloat3(&clamped, clampedVec);

    // Check if any component has changed.
    if (orig.x != clamped.x || orig.y != clamped.y || orig.z != clamped.z)
    {
        ret = true;
    }

    // Store the clamped values back into mV.
    mV[0] = clamped.x;
    mV[1] = clamped.y;
    mV[2] = clamped.z;

    return ret;
}

// S24 DX12
bool LLVector3::clampLength(F32 length_limit)
{
    bool changed = false;
    F32 len = length();  // Assume this uses your existing method (possibly refactored as well)

    if (std::isfinite(len))
    {
        if (len > length_limit)
        {
            // Load mV into an XMVECTOR (w set to 0)
            XMVECTOR vec = XMVectorSet(mV[0], mV[1], mV[2], 0.f);
            // Normalize the vector
            XMVECTOR normVec = XMVector3Normalize(vec);
            // Ensure we don't use a negative length_limit
            F32 safe_length_limit = (length_limit < 0.f) ? 0.f : length_limit;
            // Scale the normalized vector
            XMVECTOR scaledVec = XMVectorScale(normVec, safe_length_limit);
            // Store back into a temporary XMFLOAT3 and then into mV[]
            XMFLOAT3 result;
            XMStoreFloat3(&result, scaledVec);
            mV[0] = result.x;
            mV[1] = result.y;
            mV[2] = result.z;
            changed = true;
        }
    }
    else
    {
        // Handle non-finite length: rescale components relative to the maximum absolute component.
        F32 abs_mV0 = fabsf(mV[0]);
        F32 abs_mV1 = fabsf(mV[1]);
        F32 abs_mV2 = fabsf(mV[2]);

        F32 max_abs_component = std::isfinite(abs_mV0) ? abs_mV0 : 0.f;
        if (std::isfinite(abs_mV1) && abs_mV1 > max_abs_component)
            max_abs_component = abs_mV1;
        if (std::isfinite(abs_mV2) && abs_mV2 > max_abs_component)
            max_abs_component = abs_mV2;

        if (max_abs_component > 0.f)
        {
            mV[0] = std::isfinite(mV[0]) ? (mV[0] / max_abs_component) : 0.f;
            mV[1] = std::isfinite(mV[1]) ? (mV[1] / max_abs_component) : 0.f;
            mV[2] = std::isfinite(mV[2]) ? (mV[2] / max_abs_component) : 0.f;

            // Normalize the adjusted vector via DirectXMath.
            XMVECTOR vec = XMVectorSet(mV[0], mV[1], mV[2], 0.f);
            XMVECTOR normVec = XMVector3Normalize(vec);
            XMFLOAT3 normResult;
            XMStoreFloat3(&normResult, normVec);
            mV[0] = normResult.x;
            mV[1] = normResult.y;
            mV[2] = normResult.z;

            F32 safe_length_limit = (length_limit < 0.f) ? 0.f : length_limit;
            XMVECTOR scaledVec = XMVectorScale(normVec, safe_length_limit);
            XMStoreFloat3(&normResult, scaledVec);
            mV[0] = normResult.x;
            mV[1] = normResult.y;
            mV[2] = normResult.z;
            // (Following the original code, we do not mark changed if max_abs_component > 0)
        }
        else
        {
            clear();
            changed = true;
        }
    }

    return changed;
}

// S24 DX12
bool LLVector3::clamp(const LLVector3& min_vec, const LLVector3& max_vec)
{
    // Load the vectors using DirectXMath.
    // Assumes that mV is stored as at least three floats.
    XMVECTOR v = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(mV));
    XMVECTOR v_min = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(min_vec.mV));
    XMVECTOR v_max = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(max_vec.mV));

    // Clamp the vector between v_min and v_max.
    XMVECTOR clamped = XMVectorClamp(v, v_min, v_max);

    // Compare original and clamped values.
    XMFLOAT3 orig, clampedResult;
    XMStoreFloat3(&orig, v);
    XMStoreFloat3(&clampedResult, clamped);

    bool ret = (orig.x != clampedResult.x) ||
        (orig.y != clampedResult.y) ||
        (orig.z != clampedResult.z);

    // Store the clamped values back into mV.
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(mV), clamped);

    return ret;
}

// S24 DX12
bool LLVector3::abs()
{
    constexpr uint32_t mask = 0x7fffffff;

    uint32_t* px = reinterpret_cast<uint32_t*>(&mV[VX]);
    uint32_t* py = reinterpret_cast<uint32_t*>(&mV[VY]);
    uint32_t* pz = reinterpret_cast<uint32_t*>(&mV[VZ]);

    uint32_t ax = *px & mask;
    uint32_t ay = *py & mask;
    uint32_t az = *pz & mask;

    bool ret = false;

    if (*px != ax) { *px = ax; ret = true; }
    if (*py != ay) { *py = ay; ret = true; }
    if (*pz != az) { *pz = az; ret = true; }

    return ret;
}


// S24 DX12
void LLVector3::quantize16(F32 lowerxy, F32 upperxy, F32 lowerz, F32 upperz)
{
    // Manually extract vector components from mV into an XMFLOAT3.
    XMFLOAT3 f;
    f.x = mV[VX];
    f.y = mV[VY];
    f.z = mV[VZ];

    // Calculate scale factors for x and y.
    const float scaleXY = 65535.0f / (upperxy - lowerxy);
    const float invScaleXY = (upperxy - lowerxy) / 65535.0f;

    // Quantize the x component.
    float quantX = floorf((f.x - lowerxy) * scaleXY + 0.5f);
    quantX = quantX * invScaleXY + lowerxy;

    // Quantize the y component.
    float quantY = floorf((f.y - lowerxy) * scaleXY + 0.5f);
    quantY = quantY * invScaleXY + lowerxy;

    // Calculate scale factors for z.
    const float scaleZ = 65535.0f / (upperz - lowerz);
    const float invScaleZ = (upperz - lowerz) / 65535.0f;

    // Quantize the z component.
    float quantZ = floorf((f.z - lowerz) * scaleZ + 0.5f);
    quantZ = quantZ * invScaleZ + lowerz;

    // Update XMFLOAT3 with quantized values.
    f.x = quantX;
    f.y = quantY;
    f.z = quantZ;

    // Write the new values back into mV manually.
    mV[VX] = f.x;
    mV[VY] = f.y;
    mV[VZ] = f.z;
}

// S24 DX12
void LLVector3::quantize8(F32 lowerxy, F32 upperxy, F32 lowerz, F32 upperz)
{
    // Manually extract the components from mV into an XMFLOAT3.
    XMFLOAT3 f;
    f.x = mV[VX];
    f.y = mV[VY];
    f.z = mV[VZ];

    const float scaleXY = 255.0f / (upperxy - lowerxy);
    const float invScaleXY = (upperxy - lowerxy) / 255.0f;

    // Quantize and dequantize the x-component.
    f.x = floorf((f.x - lowerxy) * scaleXY + 0.5f) * invScaleXY + lowerxy;

    // Quantize and dequantize the y-component.
    f.y = floorf((f.y - lowerxy) * scaleXY + 0.5f) * invScaleXY + lowerxy;

    const float scaleZ = 255.0f / (upperz - lowerz);
    const float invScaleZ = (upperz - lowerz) / 255.0f;

    // Quantize and dequantize the z-component.
    f.z = floorf((f.z - lowerz) * scaleZ + 0.5f) * invScaleZ + lowerz;

    // Write the quantized values back into mV manually.
    mV[VX] = f.x;
    mV[VY] = f.y;
    mV[VZ] = f.z;
}

// S24 DX12
void LLVector3::snap(S32 sig_digits)
{
    // Manually load the vector components from mV into a temporary XMFLOAT3.
    XMFLOAT3 f;
    f.x = mV[VX];
    f.y = mV[VY];
    f.z = mV[VZ];

    // Load the XMFLOAT3 into an XMVECTOR.
    XMVECTOR vec = XMLoadFloat3(&f);

    // Compute the scaling factor: 10^sig_digits.
    const float scale_factor = powf(10.0f, static_cast<float>(sig_digits));
    XMVECTOR scale = XMVectorReplicate(scale_factor);

    // Scale the vector.
    XMVECTOR scaled_vec = XMVectorMultiply(vec, scale);

    // Round the scaled values to nearest integer.
    XMVECTOR rounded_vec = XMVectorRound(scaled_vec);

    // Compute the inverse scale and scale the vector back to the original range.
    XMVECTOR inv_scale = XMVectorReplicate(1.0f / scale_factor);
    XMVECTOR snapped_vec = XMVectorMultiply(rounded_vec, inv_scale);

    // Store the snapped vector values back into our temporary XMFLOAT3.
    XMStoreFloat3(&f, snapped_vec);

    // Write the results back into the mV array.
    mV[VX] = f.x;
    mV[VY] = f.y;
    mV[VZ] = f.z;
}


const LLVector3&    LLVector3::rotVec(const LLMatrix3 &mat)
{
    *this = *this * mat;
    return *this;
}

const LLVector3&    LLVector3::rotVec(const LLQuaternion &q)
{
    *this = *this * q;
    return *this;
}

// S24 DX12
const LLVector3& LLVector3::transVec(const LLMatrix4& mat)
{
    // Load vector components manually into an XMFLOAT4 (w=1 for affine transform)
    XMFLOAT4 vecF;
    vecF.x = mV[VX];
    vecF.y = mV[VY];
    vecF.z = mV[VZ];
    vecF.w = 1.0f;
    XMVECTOR vec = XMLoadFloat4(&vecF);

    // Manually construct an XMFLOAT4X4 from the LLMatrix4.
    // Assuming LLMatrix4 stores its matrix as a 4x4 array of floats in row-major order.
    XMFLOAT4X4 xmMat;
    for (int i = 0; i < 4; i++)
    {
        xmMat.m[i][0] = mat.mMatrix[i][0];
        xmMat.m[i][1] = mat.mMatrix[i][1];
        xmMat.m[i][2] = mat.mMatrix[i][2];
        xmMat.m[i][3] = mat.mMatrix[i][3];
    }
    XMMATRIX matrix = XMLoadFloat4x4(&xmMat);

    // Transform the vector using DirectXMath.
    XMVECTOR result = XMVector4Transform(vec, matrix);

    // Store the transformed result back into the temporary XMFLOAT4.
    XMStoreFloat4(&vecF, result);

    // Update the LLVector3 with the new x, y, z values.
    setVec(vecF.x, vecF.y, vecF.z);

    return *this;
}



const LLVector3&    LLVector3::rotVec(F32 angle, const LLVector3 &vec)
{
    if ( !vec.isExactlyZero() && angle )
    {
        *this = *this * LLQuaternion(angle, vec);
    }
    return *this;
}

const LLVector3&    LLVector3::rotVec(F32 angle, F32 x, F32 y, F32 z)
{
    LLVector3 vec(x, y, z);
    if ( !vec.isExactlyZero() && angle )
    {
        *this = *this * LLQuaternion(angle, vec);
    }
    return *this;
}

const LLVector3&    LLVector3::scaleVec(const LLVector3& vec)
{
    mV[VX] *= vec.mV[VX];
    mV[VY] *= vec.mV[VY];
    mV[VZ] *= vec.mV[VZ];

    return *this;
}

LLVector3           LLVector3::scaledVec(const LLVector3& vec) const
{
    LLVector3 ret = LLVector3(*this);
    ret.scaleVec(vec);
    return ret;
}

const LLVector3&    LLVector3::set(const LLVector3d &vec)
{
    mV[VX] = (F32)vec.mdV[VX];
    mV[VY] = (F32)vec.mdV[VY];
    mV[VZ] = (F32)vec.mdV[VZ];
    return (*this);
}

const LLVector3&    LLVector3::set(const LLVector4 &vec)
{
    mV[VX] = vec.mV[VX];
    mV[VY] = vec.mV[VY];
    mV[VZ] = vec.mV[VZ];
    return (*this);
}

const LLVector3&    LLVector3::setVec(const LLVector3d &vec)
{
    mV[VX] = (F32)vec.mdV[0];
    mV[VY] = (F32)vec.mdV[1];
    mV[VZ] = (F32)vec.mdV[2];
    return (*this);
}

const LLVector3&    LLVector3::setVec(const LLVector4 &vec)
{
    mV[VX] = vec.mV[VX];
    mV[VY] = vec.mV[VY];
    mV[VZ] = vec.mV[VZ];
    return (*this);
}

// S24 DX12
LLVector3::LLVector3(const LLVector2& vec)
{
    // Create an XMVECTOR with the LLVector2's x and y, and a 0 for z.
    XMVECTOR v = XMVectorSet(vec.mV[VX], vec.mV[VY], 0.0f, 0.0f);

    // Store the XMVECTOR into an XMFLOAT3.
    XMFLOAT3 result;
    XMStoreFloat3(&result, v);

    // Initialize our LLVector3 components from the XMFLOAT3.
    mV[VX] = result.x;
    mV[VY] = result.y;
    mV[VZ] = result.z;
}

// S24 DX12
LLVector3::LLVector3(const LLVector3d& vec)
{
    // Convert each of the three double-precision components to float
    XMVECTOR v = XMVectorSet(
        static_cast<float>(vec.mdV[0]),
        static_cast<float>(vec.mdV[1]),
        static_cast<float>(vec.mdV[2]),
        0.0f); // Set the fourth element to 0.0f

    // Store the result into an XMFLOAT3 (this stores the lower 3 components)
    XMFLOAT3 f;
    XMStoreFloat3(&f, v);

    // Initialize this vector's internal storage from the XMFLOAT3.
    mV[VX] = f.x;
    mV[VY] = f.y;
    mV[VZ] = f.z;
}

// S24 DX12
LLVector3::LLVector3(const LLVector4& vec)
{
    // Manually extract components from the LLVector4.
    XMFLOAT4 temp;
    temp.x = vec.mV[0];
    temp.y = vec.mV[1];
    temp.z = vec.mV[2];
    temp.w = vec.mV[3];

    // If you need to use the XMVECTOR path, load the XMFLOAT4.
    XMVECTOR v = XMLoadFloat4(&temp);

    // Store the lower three components into an XMFLOAT3.
    XMFLOAT3 result;
    XMStoreFloat3(&result, v);

    // Initialize the vector; only the first three components are relevant.
    mV[VX] = result.x;
    mV[VY] = result.y;
    mV[VZ] = result.z;
}

LLVector3::LLVector3(const LLVector4a& vec)
    : LLVector3(vec.getF32ptr())
{
}

LLVector3::LLVector3(const LLSD& sd)
{
    setValue(sd);
}

LLSD LLVector3::getValue() const
{
    LLSD ret;
    ret[VX] = mV[VX];
    ret[VY] = mV[VY];
    ret[VZ] = mV[VZ];
    return ret;
}

void LLVector3::setValue(const LLSD& sd)
{
    mV[VX] = (F32) sd[VX].asReal();
    mV[VY] = (F32) sd[VY].asReal();
    mV[VZ] = (F32) sd[VZ].asReal();
}

// S24 DX12
const LLVector3& operator*=(LLVector3& a, const LLQuaternion& rot)
{
    // Load the vector from 'a' into an XMVECTOR.
    XMVECTOR vec = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(a.mV));

    // Load the quaternion from 'rot'. Note: Ensure that the quaternion
    // is stored as (x, y, z, w) in LLQuaternion.
    XMVECTOR quaternion = XMVectorSet(rot.mQ[VX], rot.mQ[VY], rot.mQ[VZ], rot.mQ[VW]);

    // Rotate the vector using DirectXMath's XMVector3Rotate.
    XMVECTOR rotatedVec = XMVector3Rotate(vec, quaternion);

    // Store the result back.
    XMFLOAT3 result;
    XMStoreFloat3(&result, rotatedVec);
    a.mV[VX] = result.x;
    a.mV[VY] = result.y;
    a.mV[VZ] = result.z;

    return a;
}

// S24 PERF
// static
bool LLVector3::parseVector3(const std::string& buf, LLVector3* value)
{
    if (buf.empty() || value == nullptr)
    {
        return false; // Early exit for invalid input
    }

    // Only initialize `v` if the parsing succeeds
    if (sscanf(buf.c_str(), "%f %f %f", &value->mV[VX], &value->mV[VY], &value->mV[VZ]) == 3)
    {
        return true;
    }

    return false; // Parsing failed
}

// S24 DX12
LLVector3 point_to_box_offset(LLVector3& pos, const LLVector3* box)
{
    // Manually load pos and box components into XMFLOAT3 structures.
    XMFLOAT3 posF = { pos.mV[0], pos.mV[1], pos.mV[2] };
    XMFLOAT3 boxMinF = { box[0].mV[0], box[0].mV[1], box[0].mV[2] };
    XMFLOAT3 boxMaxF = { box[1].mV[0], box[1].mV[1], box[1].mV[2] };

    // Load the XMFLOAT3s into XMVECTORs.
    XMVECTOR posV = XMLoadFloat3(&posF);
    XMVECTOR minV = XMLoadFloat3(&boxMinF);
    XMVECTOR maxV = XMLoadFloat3(&boxMaxF);

    // Clamp pos to the range [minV, maxV]. For components where pos is inside the range
    // the clamped result is the same as pos.
    XMVECTOR clampedV = XMVectorClamp(posV, minV, maxV);

    // The offset from pos to its nearest neighbor on the box is simply:
    // offset = pos - clamped_pos
    XMVECTOR offsetV = XMVectorSubtract(posV, clampedV);

    // Store the result back into an XMFLOAT3.
    XMFLOAT3 offsetF;
    XMStoreFloat3(&offsetF, offsetV);

    // Construct a new LLVector3 from the computed offset.
    return LLVector3(offsetF.x, offsetF.y, offsetF.z);
}

// S24 DX12
bool box_valid_and_non_zero(const LLVector3* box)
{
    // Manually construct XMFLOAT3 representations from the two box corners.
    XMFLOAT3 v0 = { box[0].mV[0], box[0].mV[1], box[0].mV[2] };
    XMFLOAT3 v1 = { box[1].mV[0], box[1].mV[1], box[1].mV[2] };

    // Check if all components are finite.
    if (!std::isfinite(v0.x) || !std::isfinite(v0.y) || !std::isfinite(v0.z) ||
        !std::isfinite(v1.x) || !std::isfinite(v1.y) || !std::isfinite(v1.z))
    {
        return false;
    }

    // Load the floats into XMVECTORs.
    XMVECTOR vec0 = XMLoadFloat3(&v0);
    XMVECTOR vec1 = XMLoadFloat3(&v1);

    // Compute the squared lengths; a zero vector will have a squared length of 0.
    float lenSq0 = XMVectorGetX(XMVector3LengthSq(vec0));
    float lenSq1 = XMVectorGetX(XMVector3LengthSq(vec1));

    // Return true if either vector is nonzero.
    return (lenSq0 > 0.f) || (lenSq1 > 0.f);
}

