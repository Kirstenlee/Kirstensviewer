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

#include <DirectXMath.h>
using namespace DirectX;

// LLVector2

LLVector2 LLVector2::zero(0, 0);

// S24 PERF
bool LLVector2::abs()
{
	constexpr uint32_t mask = 0x7fffffff;

	uint32_t* px = reinterpret_cast<uint32_t*>(&mV[VX]);
	uint32_t* py = reinterpret_cast<uint32_t*>(&mV[VY]);

	uint32_t ax = *px & mask;
	uint32_t ay = *py & mask;

	bool ret = false;

	if (*px != ax) { *px = ax; ret = true; }
	if (*py != ay) { *py = ay; ret = true; }

	return ret;
}

// S24 PERF
F32 angle_between(const LLVector2& a, const LLVector2& b)
{
    XMVECTOR va = XMVectorSet(a.mV[0], a.mV[1], 0.0f, 0.0f);
    XMVECTOR vb = XMVectorSet(b.mV[0], b.mV[1], 0.0f, 0.0f);

    XMVECTOR normA = XMVector2Normalize(va);
    XMVECTOR normB = XMVector2Normalize(vb);

    float cosine;
    XMStoreFloat(&cosine, XMVector2Dot(normA, normB));
    return acosf(fmaxf(-1.0f, fminf(1.0f, cosine)));
}

// S24 PERF
F32 signed_angle_between(const LLVector2& a, const LLVector2& b)
{
	F32 angle = angle_between(a, b);
	// Scalar cross.z instead of full 3D vector cross.
	float cross = a.mV[VX] * b.mV[VY] - a.mV[VY] * b.mV[VX];
	return (cross < 0.f) ? -angle : angle;
}

bool are_parallel(const LLVector2& a, const LLVector2& b, float epsilon)
{
	// S24 PERF: normalize vectors once, then compute dot product for parallel check.
	XMVECTOR vecA = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vecB = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.f);

	XMVECTOR normA = XMVector2Normalize(vecA);
	XMVECTOR normB = XMVector2Normalize(vecB);

	float dot;
	XMStoreFloat(&dot, XMVector2Dot(normA, normB)); // dot of normalized vectors == cos(theta)

	// Parallel if cos(theta) ≈ 1.0f (within epsilon).
	return (1.0f - fabs(dot)) < epsilon;
}

F32 dist_vec(const LLVector2& a, const LLVector2& b)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.0f);
	XMVECTOR diff = XMVectorSubtract(va, vb);
	XMVECTOR lengthVec = XMVector2Length(diff);
	float length;
	XMStoreFloat(&length, lengthVec);
	return length;
}

F32 dist_vec_squared(const LLVector2& a, const LLVector2& b)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.0f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.0f);
	XMVECTOR diff = XMVectorSubtract(va, vb);
	XMVECTOR lenSq = XMVector2LengthSq(diff);
	float result;
	XMStoreFloat(&result, lenSq);
	return result;
}

F32 dist_vec_squared2D(const LLVector2& a, const LLVector2& b)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.0f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.0f);
	XMVECTOR diff = XMVectorSubtract(va, vb);
	XMVECTOR lenSq = XMVector2LengthSq(diff);
	float result;
	XMStoreFloat(&result, lenSq);
	return result;
}

LLVector2 lerp(const LLVector2& a, const LLVector2& b, F32 u)
{
	XMVECTOR va = XMVectorSet(a.mV[VX], a.mV[VY], 0.f, 0.0f);
	XMVECTOR vb = XMVectorSet(b.mV[VX], b.mV[VY], 0.f, 0.0f);
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
