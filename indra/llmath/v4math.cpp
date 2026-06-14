/**
 * @file v4math.cpp
 * @brief LLVector4 class implementation.
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
#include "v4math.h"
#include "m4math.h"
#include "m3math.h"
#include "llquaternion.h"

 // S24 includes
#include <immintrin.h>
#include "DirectXmath.h"
using namespace DirectX;

// S24 LLVector4

const LLVector4& LLVector4::rotVec(const LLMatrix4& mat)
{
	// Load the vector and matrix into DirectXMath structures
	XMVECTOR this_vec = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(mV));
	XMMATRIX matrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&mat));

	// Apply matrix transformation
	XMVECTOR result = XMVector4Transform(this_vec, matrix);

	// Store the result back into mV
	XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(mV), result);

	return *this;
}

const LLVector4& LLVector4::rotVec(const LLQuaternion& q)
{
	// Load vector and quaternion into DirectXMath structures
	XMVECTOR this_vec = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(mV));
	XMVECTOR quat = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(&q));

	// Apply quaternion rotation
	XMVECTOR result = XMVector3Rotate(this_vec, quat);

	// Store the result back into mV
	XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(mV), result);

	return *this;
}

// S24 DX12
const LLVector4& LLVector4::scaleVec(const LLVector4& vec) {
	// Load both vectors into DirectXMath SIMD registers
	XMVECTOR this_vec = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(mV));
	XMVECTOR other_vec = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(vec.mV));

	// Perform element-wise multiplication
	XMVECTOR result = XMVectorMultiply(this_vec, other_vec);

	// Store the result back to mV
	XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(mV), result);

	return *this;
}

// S24 DX12
bool LLVector4::abs() {
	// Load the 4 elements of mV into a DirectXMath SIMD register
	XMVECTOR vec = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(mV));

	// Compute absolute values using DirectXMath
	XMVECTOR abs_vec = XMVectorAbs(vec);

	// Store the result back into mV
	XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(mV), abs_vec);

	// Check if any value was originally negative
	return !XMVector4Equal(vec, abs_vec);
}

std::ostream& operator<<(std::ostream& s, const LLVector4& a)
{
	s << "{ " << a.mV[VX] << ", " << a.mV[VY] << ", " << a.mV[VZ] << ", " << a.mV[VW] << " }";
	return s;
}

// NON-Member Functions
//
// S24 DX12
F32 angle_between(const LLVector4& a, const LLVector4& b)
{
	// Load vectors into DirectXMath structures
	XMVECTOR an = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(a.mV));
	XMVECTOR bn = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(b.mV));

	// Normalize both vectors
	an = XMVector3Normalize(an);
	bn = XMVector3Normalize(bn);

	// Compute the dot product
	F32 cosine = XMVectorGetX(XMVector3Dot(an, bn));

	// Compute the angle using acos while handling edge cases
	F32 angle = (cosine >= 1.0f) ? 0.0f :
		(cosine <= -1.0f) ? XM_PI :
		acosf(cosine);

	return angle;
}

// S24 DX12
bool are_parallel(const LLVector4& a, const LLVector4& b, F32 epsilon)
{
	// Load vectors into DirectXMath structures
	XMVECTOR an = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(a.mV));
	XMVECTOR bn = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(b.mV));

	// Normalize both vectors
	an = XMVector3Normalize(an);
	bn = XMVector3Normalize(bn);

	// Compute the dot product
	F32 dot = XMVectorGetX(XMVector3Dot(an, bn));

	// Check if the vectors are parallel within the given epsilon tolerance
	return (1.0f - fabsf(dot)) < epsilon;
}

LLVector3 vec4to3(const LLVector4& vec)
{
	return LLVector3(vec.mV[VX], vec.mV[VY], vec.mV[VZ]);
}

LLVector4 vec3to4(const LLVector3& vec)
{
	return LLVector4(vec.mV[VX], vec.mV[VY], vec.mV[VZ]);
}