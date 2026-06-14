/**
 * @file llperlin.cpp
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

#include "linden_common.h"
#include "llmath.h"

#include "llperlin.h"
#include <random>

#define B 0x100
#define BM 0xff
#define N 0x1000
#define NF32 (4096.f)
#define NP 12   /* 2^N */
#define NM 0xfff

static S32 p[B + B + 2];
static F32 g3[B + B + 2][3];
static F32 g2[B + B + 2][2];
static F32 g1[B + B + 2];

bool LLPerlinNoise::sInitialized = 0;

static void normalize2(F32 v[2])
{
	F32 mag = v[0] * v[0] + v[1] * v[1];
	if (mag > 0.f)
	{
		F32 s = 1.f / (F32)sqrt(mag);
		v[0] *= s;
		v[1] *= s;
	}
}

static void normalize3(F32 v[3])
{
	F32 mag = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (mag > 0.f)
	{
		F32 s = 1.f / (F32)sqrt(mag);
		v[0] *= s;
		v[1] *= s;
		v[2] *= s;
	}
}

static void fast_setup(F32 vec, U8& b0, U8& b1, F32& r0, F32& r1)
{
	S32 t_S32 = lltrunc(vec + NF32);
	b0 = (U8)(t_S32 & BM);  // Ensure modular wrapping with BM
	b1 = (b0 + 1) & BM;
	r0 = (vec + NF32) - t_S32;
	r1 = r0 - 1.f;
}

void LLPerlinNoise::init(void)
{
	static std::mt19937 rng(42); // Seed for consistent randomization
	std::uniform_real_distribution<F32> dist(-1.f, 1.f);

	for (int i = 0; i < B; i++)
	{
		p[i] = i;
		g1[i] = dist(rng);

		for (int j = 0; j < 2; j++)
			g2[i][j] = dist(rng);
		normalize2(g2[i]);

		for (int j = 0; j < 3; j++)
			g3[i][j] = dist(rng);
		normalize3(g3[i]);
	}

	// Shuffle permutation table using Fisher-Yates algorithm
	for (int i = B - 1; i > 0; --i)
	{
		int j = rng() % B;
		std::swap(p[i], p[j]);
	}

	// Extend arrays for seamless wrapping
	for (int i = 0; i < B + 2; i++)
	{
		p[B + i] = p[i];
		g1[B + i] = g1[i];
		for (int j = 0; j < 2; j++)
			g2[B + i][j] = g2[i][j];
		for (int j = 0; j < 3; j++)
			g3[B + i][j] = g3[i][j];
	}

	sInitialized = true;
}

//============================================================================
// Noise functions

#define s_curve(t) ( t * t * (3.f - 2.f * t) )

#define lerp_m(t, a, b) ( a + t * (b - a) )

F32 LLPerlinNoise::noise1(F32 x)
{
	int bx0, bx1;
	F32 rx0, rx1, sx, t, u, v;

	if (!sInitialized)
		init(); // Ensure initialization only once

	// Calculate grid cell and fractional offset
	t = x + N;
	bx0 = lltrunc(t) & BM;
	bx1 = (bx0 + 1) & BM;
	rx0 = t - lltrunc(t);
	rx1 = rx0 - 1.f;

	// Apply smoothing
	sx = s_curve(rx0);

	// Compute contributions from the grid points
	u = rx0 * g1[p[bx0]];
	v = rx1 * g1[p[bx1]];

	// Interpolate for final noise value
	return lerp_m(sx, u, v);
}

static F32 fast_at2(F32 rx, F32 ry, F32* q)
{
	return rx * q[0] + ry * q[1];
}

F32 LLPerlinNoise::noise2(F32 x, F32 y)
{
	U8 bx0, bx1, by0, by1;
	U32 b00, b10, b01, b11;
	F32 rx0, rx1, ry0, ry1, sx, sy, a, b, u, v;

	if (!sInitialized)
		init(); // Ensure initialization only once

	// Grid setup
	fast_setup(x, bx0, bx1, rx0, rx1);
	fast_setup(y, by0, by1, ry0, ry1);

	// Permutation indices
	U32 i = p[bx0];
	U32 j = p[bx1];

	b00 = p[i + by0];
	b10 = p[j + by0];
	b01 = p[i + by1];
	b11 = p[j + by1];

	// Smoothing curves
	sx = s_curve(rx0);
	sy = s_curve(ry0);

	// Bottom row interpolation
	u = fast_at2(rx0, ry0, g2[b00]);
	v = fast_at2(rx1, ry0, g2[b10]);
	a = lerp_m(sx, u, v);

	// Top row interpolation
	u = fast_at2(rx0, ry1, g2[b01]);
	v = fast_at2(rx1, ry1, g2[b11]);
	b = lerp_m(sx, u, v);

	// Final vertical interpolation
	return lerp_m(sy, a, b);
}

static F32 fast_at3(F32 rx, F32 ry, F32 rz, F32* q)
{
	return rx * q[0] + ry * q[1] + rz * q[2];
}

F32 LLPerlinNoise::noise3(F32 x, F32 y, F32 z)
{
	U8 bx0, bx1, by0, by1, bz0, bz1;
	S32 b00, b10, b01, b11;
	F32 rx0, rx1, ry0, ry1, rz0, rz1, t, sy, sz, a, b, c, d, u, v;
	S32 i, j;

	if (!sInitialized)
		init(); // Ensure initialization only once.

	// Setup the grid cell and fractional components for x, y, z
	fast_setup(x, bx0, bx1, rx0, rx1);
	fast_setup(y, by0, by1, ry0, ry1);
	fast_setup(z, bz0, bz1, rz0, rz1);

	// Calculate permutation values
	i = p[bx0];
	j = p[bx1];

	// Gradient indices for each corner
	b00 = p[i + by0];
	b10 = p[j + by0];
	b01 = p[i + by1];
	b11 = p[j + by1];

	// Smoothing curves
	t = s_curve(rx0);
	sy = s_curve(ry0);
	sz = s_curve(rz0);

	// Compute noise contributions
	// Bottom layer
	u = fast_at3(rx0, ry0, rz0, g3[b00 + bz0]);
	v = fast_at3(rx1, ry0, rz0, g3[b10 + bz0]);
	a = lerp_m(t, u, v);

	u = fast_at3(rx0, ry1, rz0, g3[b01 + bz0]);
	v = fast_at3(rx1, ry1, rz0, g3[b11 + bz0]);
	b = lerp_m(t, u, v);

	c = lerp_m(sy, a, b);

	// Top layer
	u = fast_at3(rx0, ry0, rz1, g3[b00 + bz1]);
	v = fast_at3(rx1, ry0, rz1, g3[b10 + bz1]);
	a = lerp_m(t, u, v);

	u = fast_at3(rx0, ry1, rz1, g3[b01 + bz1]);
	v = fast_at3(rx1, ry1, rz1, g3[b11 + bz1]);
	b = lerp_m(t, u, v);

	d = lerp_m(sy, a, b);

	// Final interpolation
	return lerp_m(sz, c, d);
}

F32 LLPerlinNoise::turbulence2(F32 x, F32 y, F32 freq)
{
	F32 t = 0.f;          // Accumulator for turbulence intensity
	F32 amplitude = 1.f;  // Starting amplitude for base layer
	F32 persistence = 0.7f; // Controls amplitude decay across octaves

	while (freq >= 1.f)
	{
		// Scale coordinates for current octave
		F32 lx = freq * x;
		F32 ly = freq * y;

		// Get noise value for current frequency
		F32 noise_val = noise2(lx, ly);

		// Use absolute value for turbulence and blend layers
		t += amplitude * fabs(noise_val);

		// Reduce frequency and amplitude for finer details
		freq *= 0.5f;
		amplitude *= persistence; // Persistence controls contribution of each layer
	}

	// Optional: Add ambient offset for base turbulence
	t += 0.1f;

	return t;
}

F32 LLPerlinNoise::turbulence3(F32 x, F32 y, F32 z, F32 freq)
{
	F32 t = 0.f;          // Accumulator for turbulence intensity
	F32 amplitude = 1.f;  // Starting amplitude for the base layer
	F32 persistence = 0.6f; // Controls amplitude decay for finer details

	while (freq >= 1.f)
	{
		// Scale coordinates for the current octave
		F32 lx = freq * x;
		F32 ly = freq * y;
		F32 lz = freq * z;

		// Fetch noise value for the current frequency
		F32 noise_val = noise3(lx, ly, lz);

		// Accumulate with an enhanced blending method
		t += amplitude * fabs(noise_val); // Using absolute value for turbulence effect

		// Reduce frequency and amplitude for the next octave
		freq *= 0.5f;
		amplitude *= persistence; // Persistence controls contribution of finer layers
	}

	return t;
}

F32 LLPerlinNoise::clouds3(F32 x, F32 y, F32 z, F32 freq)
{
	F32 t = 0.f; // Accumulator for the cloud density
	F32 amplitude = 1.f; // Amplitude scaling for finer details
	F32 persistence = 0.5f; // Controls the decrease in amplitude per octave

	while (freq >= 1.f)
	{
		// Scale the coordinates based on the current frequency
		F32 lx = freq * x;
		F32 ly = freq * y;
		F32 lz = freq * z;

		// Calculate noise value for the current layer
		F32 noise_val = noise3(lx, ly, lz);

		// Accumulate the results using an improved blending technique
		// Here, we use absolute value with a power-based enhancement for better realism
		t += amplitude * pow(fabs(noise_val), 2.f) / freq;

		// Reduce frequency and amplitude for the next octave
		freq *= 0.5f;
		amplitude *= persistence;
	}

	// Add a subtle offset for ambient density (optional, for soft haze-like effects)
	t += 0.15f;

	return t;
}