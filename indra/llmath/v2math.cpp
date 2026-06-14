/**
 * @file v2math.cpp
 * @brief LLVector2 class implementation.
 *
 * Converted to DirectXmath by Kirstenlee 2025
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

#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "m4math.h"
#include "m3math.h"
#include "llquaternion.h"

#include "DirectXMath.h" // S24
using namespace DirectX;

// LLVector2

LLVector2 LLVector2::zero(0, 0);

bool LLVector2::abs()
{
	// Load the vector components into an XMVECTOR.
	// We set z and w to 0 since this is a 2D vector.
	XMVECTOR vec = XMVectorSet(mV[VX], mV[VY], 0.f, 0.f);

	// Compute the absolute values using the intrinsic function.
	XMVECTOR absVec = XMVectorAbs(vec);

	// Store the result back into an XMFLOAT4 for extraction.
	XMFLOAT4 absFloat;
	XMStoreFloat4(&absFloat, absVec);

	bool ret = false;
	// Check if values have changed and assign.
	if (mV[VX] != absFloat.x) { mV[VX] = absFloat.x; ret = true; }
	if (mV[VY] != absFloat.y) { mV[VY] = absFloat.y; ret = true; }

	return ret;
}

F32 angle_between(const LLVector2& a, const LLVector2& b)
{
	// Convert LLVector2 components to an XMVECTOR.
	// Assuming mV[0] and mV[1] represent x and y respectively.
	XMVECTOR va = XMVectorSet(a.mV[0], a.mV[1], 0.0f, 0.0f);
	XMVECTOR vb = XMVectorSet(b.mV[0], b.mV[1], 0.0f, 0.0f);

	// Normalize both vectors
	XMVECTOR normA = XMVector2Normalize(va);
	XMVECTOR normB = XMVector2Normalize(vb);

	// Calculate the dot product.
	XMVECTOR dotResult = XMVector2Dot(normA, normB);
	float cosine;
	XMStoreFloat(&cosine, dotResult);

	// Clamp the cosine value and compute the angle.
	// Assuming DirectX::XM_PI is defined as the value of ?.
	F32 angle = (cosine >= 1.0f) ? 0.0f :
		(cosine <= -1.0f) ? DirectX::XM_PI :
		acosf(cosine);

	return angle;
}

F32 signed_angle_between(const LLVector2& a, const LLVector2& b)
{
	// Compute the angle between the vectors (using our DirectXMath-based angle_between).
	F32 angle = angle_between(a, b);

	// Convert the 2D vectors to 3D by setting z = 0 (w can be set to 0 as well).
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);

	// Compute the cross product using 3D cross product.
	XMVECTOR crossVec = XMVector3Cross(va, vb);

	// The resulting crossVec will have its z-component equal to (a.x * b.y - a.y * b.x).
	// We can extract the z-component with XMVectorGetZ.
	float cross = XMVectorGetZ(crossVec);

	// If the cross product is negative, the angle should be negative.
	return (cross < 0.f) ? -angle : angle;
}

bool are_parallel(const LLVector2& a, const LLVector2& b, float epsilon)
{
	XMVECTOR vecA = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vecB = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);

	XMVECTOR normA = XMVector2Normalize(vecA);
	XMVECTOR normB = XMVector2Normalize(vecB);

	XMVECTOR dotProduct = XMVector2Dot(normA, normB);
	float dot;
	XMStoreFloat(&dot, dotProduct);

	return (1.0f - fabs(dot)) < epsilon;
}

F32 dist_vec(const LLVector2& a, const LLVector2& b)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);
	XMVECTOR diff = XMVectorSubtract(va, vb);
	XMVECTOR lengthVec = XMVector2Length(diff);
	float length;
	XMStoreFloat(&length, lengthVec);
	return length;
}

F32 dist_vec_squared(const LLVector2& a, const LLVector2& b)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);
	XMVECTOR diff = XMVectorSubtract(va, vb);
	XMVECTOR lenSq = XMVector2LengthSq(diff);
	float result;
	XMStoreFloat(&result, lenSq);
	return result;
}

F32 dist_vec_squared2D(const LLVector2& a, const LLVector2& b)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);
	XMVECTOR diff = XMVectorSubtract(va, vb);
	XMVECTOR lenSq = XMVector2LengthSq(diff);
	float result;
	XMStoreFloat(&result, lenSq);
	return result;
}

LLVector2 lerp(const LLVector2& a, const LLVector2& b, F32 u)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);
	XMVECTOR lerpVec = XMVectorLerp(va, vb, u);
	XMFLOAT2 result;
	XMStoreFloat2(&result, lerpVec);
	return LLVector2(result.x, result.y);
}

LLSD LLVector2::getValue() const
{
	LLSD ret;
	ret[VX] = mV[VX];
	ret[VY] = mV[VY];
	return ret;
}

void LLVector2::setValue(const LLSD& sd)
{
	mV[VX] = (F32)sd[0].asReal();
	mV[VY] = (F32)sd[1].asReal();
}