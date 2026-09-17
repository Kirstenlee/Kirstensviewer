/** 
 * @file v3color.cpp
 * @brief LLColor3 class implementation.
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

#include "v3color.h"
#include "v4color.h"
#include "v4math.h"

 // S24 includes
#include <immintrin.h> 
#include <algorithm>  // For std::min and std::max
#include "DirectXMath.h"
using namespace DirectX;

LLColor3 LLColor3::white(1.0f, 1.0f, 1.0f);
LLColor3 LLColor3::black(0.0f, 0.0f, 0.0f);
LLColor3 LLColor3::grey (0.5f, 0.5f, 0.5f);

LLColor3::LLColor3(const LLColor4 &a)
{
    mV[VRED]   = a.mV[VRED];
    mV[VGREEN] = a.mV[VGREEN];
    mV[VBLUE]  = a.mV[VBLUE];
}

LLColor3::LLColor3(const LLVector4 &a)
{
    mV[VRED]   = a.mV[VRED];
    mV[VGREEN] = a.mV[VGREEN];
    mV[VBLUE]  = a.mV[VBLUE];
}

LLColor3::LLColor3(const LLSD &sd)
{
	setValue(sd);
}

const LLColor3& LLColor3::operator=(const LLColor4 &a) 
{
    mV[VRED]   = a.mV[VRED];
    mV[VGREEN] = a.mV[VGREEN];
    mV[VBLUE]  = a.mV[VBLUE];
	return (*this);
}

std::ostream& operator<<(std::ostream& s, const LLColor3 &a) 
{
    s << "{ " << a.mV[VRED] << ", " << a.mV[VGREEN] << ", " << a.mV[VBLUE] << " }";
	return s;
}

static F32 hueToRgb(F32 val1In, F32 val2In, F32 valHueIn)
{
	// S24: was an 8-wide AVX implementation of what is fundamentally scalar math (one float in,
	// one float out) - live-tested (lluihueshift.cpp) and hand-verified against the standard HSL
	// formula to produce genuinely out-of-range results (e.g. hue=1/6 exactly landing on a channel
	// that should evaluate to ~1.0 instead came out as 2.4) - a real bug in the intrinsics, not
	// speculative. Replaced with the textbook scalar algorithm; this runs at most a few hundred
	// times per user-driven color change, never a per-frame hot path, so there's no performance
	// case for SIMD here even if it had been correct.
	valHueIn = fmodf(valHueIn + 1.0f, 1.0f);
	if (valHueIn < 0.0f)
	{
		valHueIn += 1.0f;
	}

	if (valHueIn < 1.0f / 6.0f)
	{
		return val1In + (val2In - val1In) * 6.0f * valHueIn;
	}
	if (valHueIn < 1.0f / 2.0f)
	{
		return val2In;
	}
	if (valHueIn < 2.0f / 3.0f)
	{
		return val1In + (val2In - val1In) * (2.0f / 3.0f - valHueIn) * 6.0f;
	}
	return val1In;
}

void LLColor3::setHSL(F32 hValIn, F32 sValIn, F32 lValIn)
{
	if (std::fabsf(sValIn) < FLT_EPSILON)  // More robust near-zero saturation check
	{
		XMVECTOR gray = XMVectorReplicate(lValIn); // Set all channels to lightness
		XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(mV), gray);
	}
	else
	{
		F32 interVal1;
		F32 interVal2;

		if (lValIn < 0.5f)
			interVal2 = lValIn * (1.0f + sValIn);
		else
			interVal2 = (lValIn + sValIn) - (sValIn * lValIn);

		interVal1 = 2.0f * lValIn - interVal2;

		// Convert to XMVECTOR for SIMD processing
		XMVECTOR inter1 = XMVectorReplicate(interVal1);
		XMVECTOR inter2 = XMVectorReplicate(interVal2);
		XMVECTOR hueOffsets = XMVectorSet(hValIn + (1.0f / 3.0f), hValIn, hValIn - (1.0f / 3.0f), 0.0f);

		// Apply DirectXMath hueToRgb equivalent in parallel
		XMVECTOR rgb = XMVectorSet(
			hueToRgb(interVal1, interVal2, hValIn + (1.0f / 3.0f)),
			hueToRgb(interVal1, interVal2, hValIn),
			hueToRgb(interVal1, interVal2, hValIn - (1.0f / 3.0f)),
			1.0f // Alpha channel placeholder
		);

		// Store result in mV
		XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(mV), rgb);
	}
}

void LLColor3::calcHSL(F32* hue, F32* saturation, F32* luminance) const
{
	F32 var_R = mV[VRED];
	F32 var_G = mV[VGREEN];
	F32 var_B = mV[VBLUE];

	// Compute min and max using std::min and std::max
	F32 var_Min = std::min({ var_R, var_G, var_B });
	F32 var_Max = std::max({ var_R, var_G, var_B });
	F32 del_Max = var_Max - var_Min;

	// Compute luminance
	F32 L = (var_Max + var_Min) * 0.5f;
	F32 H = 0.0f, S = 0.0f;

	if (std::fabsf(del_Max) < FLT_EPSILON) // Avoid floating-point inaccuracies
	{
		H = 0.0f;
		S = 0.0f;
	}
	else
	{
		S = (L < 0.5f) ? del_Max / (var_Max + var_Min) : del_Max / (2.0f - var_Max - var_Min);

		F32 del_R = ((var_Max - var_R) * (1.0f / 6.0f) + (del_Max * 0.5f)) / del_Max;
		F32 del_G = ((var_Max - var_G) * (1.0f / 6.0f) + (del_Max * 0.5f)) / del_Max;
		F32 del_B = ((var_Max - var_B) * (1.0f / 6.0f) + (del_Max * 0.5f)) / del_Max;

		// Determine hue based on dominant color
		if (var_R == var_Max)
			H = del_B - del_G;
		else if (var_G == var_Max)
			H = (1.0f / 3.0f) + del_R - del_B;
		else
			H = (2.0f / 3.0f) + del_G - del_R;

		// Wrap hue within range [0,1]
		H = fmodf(H + 1.0f, 1.0f);
	}

	// Store values safely
	if (hue) *hue = H;
	if (saturation) *saturation = S;
	if (luminance) *luminance = L;
}
