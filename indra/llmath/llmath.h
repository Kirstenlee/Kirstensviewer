/**
 * @file llmath.h
 * @brief Useful math constants and macros.
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

#ifndef LLMATH_H
#define LLMATH_H

#include <cstdlib>
#include <vector>
#include <limits>
#include "lldefs.h"

#include "is_approx_equal_fraction.h"

#pragma once
#include <cmath>
#include<bit>
#include <immintrin.h> // For AVX intrinsics
#include <DirectXMath.h>

 // Single Precision Floating Point Routines

constexpr float GRAVITY = -9.8f;

// S24 mathematical constants (double precision)
constexpr double D_PI        = 3.141592653589793238462643383279502884;
constexpr double D_TWO_PI    = 6.283185307179586476925286766559005768;
constexpr double D_PI_BY_TWO = 1.570796326794896619231321691639751442;
// S24 mathematical constants (single precision)
constexpr float F_PI = 3.14159274f;
constexpr float F_TWO_PI = 6.28318548f;
constexpr float F_PI_BY_TWO = 1.57079637f;

// Root Constants
const float F_SQRT2 = std::sqrt(2.0f);
const float F_SQRT3 = std::sqrt(3.0f);
const float F_SQRT_TWO_PI = std::sqrt(2.0f * F_PI);

// Reciprocal Root Constants
const float OO_SQRT2 = 1.0f / F_SQRT2;
const float OO_SQRT3 = 1.0f / F_SQRT3;

// Euler’s Number
const float F_E = std::exp(1.0f);

// S24 Angular Conversions
constexpr float DEG_TO_RAD = F_PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / F_PI;

// Precision Thresholds
constexpr float F_APPROXIMATELY_ZERO = 1e-5f;
constexpr float F_ALMOST_ZERO = F_APPROXIMATELY_ZERO;
constexpr float F_ALMOST_ONE = 1.0f - F_APPROXIMATELY_ZERO;

// S24 Logarithmic Constants
const float F_LN10 = std::log(10.0f);
const float OO_LN10 = 1.0f / F_LN10;
const float F_LN2 = std::log(2.0f);
const float OO_LN2 = 1.0f / F_LN2;

// Application-Specific Constants
constexpr float GIMBAL_THRESHOLD = 0.000436f; // Gimbal lock threshold, sin(DEG_TO_RAD * gimbal_threshold_angle)

// FP_MAG_THRESHOLD: Minimum vector magnitude for safe quaternion computation
constexpr float FP_MAG_THRESHOLD = 1e-7f;

inline bool is_approx_zero(float f) {
	return std::abs(f) < F_APPROXIMATELY_ZERO;
}

// These functions work by interpreting sign+exp+mantissa as an unsigned
// integer.
// For example:
// x = <sign>1 <exponent>00000010 <mantissa>00000000000000000000000
// y = <sign>1 <exponent>00000001 <mantissa>11111111111111111111111
//
// interpreted as ints =
// x = 10000001000000000000000000000000
// y = 10000000111111111111111111111111
// which is clearly a different of 1 in the least significant bit
// Values with the same exponent can be trivially shown to work.
//
// WARNING: Denormals of opposite sign do not work
// x = <sign>1 <exponent>00000000 <mantissa>00000000000000000000001
// y = <sign>0 <exponent>00000000 <mantissa>00000000000000000000001
// Although these values differ by 2 in the LSB, the sign bit makes
// the int comparison fail.
//
// WARNING: NaNs can compare equal
// There is no special treatment of exceptional values like NaNs
//
// WARNING: Infinity is comparable with F32_MAX and negative
// infinity is comparable with F32_MIN

// S24 For std::isnan and std::isfinite

inline bool llisnan(float val) {
	return std::isnan(val);
}

inline bool llfinite(float val) {
	return std::isfinite(val);
}

// S24: handles negative and positive zeros using std::bit_cast (C++20)
inline bool is_zero(float x) noexcept
{
	return (std::bit_cast<U32>(x) & 0x7FFFFFFF) == 0;
}

// S24: ULP-based approximate equality using std::bit_cast (C++20)
inline bool is_approx_equal(float x, float y) noexcept
{
	constexpr S32 COMPARE_MANTISSA_UP_TO_BIT = 0x02;
	U32 x_bits = std::bit_cast<U32>(x);
	U32 y_bits = std::bit_cast<U32>(y);
	return std::abs(static_cast<S32>(x_bits - y_bits)) < COMPARE_MANTISSA_UP_TO_BIT;
}

inline bool is_approx_equal(F64 x, F64 y) noexcept
{
	constexpr S64 COMPARE_MANTISSA_UP_TO_BIT = 0x02;
	U64 x_bits = std::bit_cast<U64>(x);
	U64 y_bits = std::bit_cast<U64>(y);
	return std::abs(static_cast<S64>(x_bits - y_bits)) < COMPARE_MANTISSA_UP_TO_BIT;
}

// S24 MODERNIZATION: llabs() replaced with std::abs() - C++ standard library
// std::abs is compiler-intrinsic optimized, constexpr, and type-generic
// Keeping thin wrappers temporarily for compatibility - will remove in future
// TODO: Search codebase for llabs() usage and replace with std::abs()
inline S32 llabs(const S32 a) noexcept {
	return std::abs(a);
}

inline float llabs(const float a) noexcept {
	return std::abs(a);
}

inline F64 llabs(const F64 a) noexcept {
	return std::abs(a);
}

// S24: Use std::trunc (compiler intrinsic, faster than narrow cast)
inline S32 lltrunc(float f) noexcept
{
	return static_cast<S32>(std::trunc(f));
}

inline S32 lltrunc(F64 f) noexcept
{
	return static_cast<S32>(std::trunc(f));
}

// S24: Use std::floor/ceil (compiler intrinsics, SSE4.1 roundss instruction)
inline S32 llfloor(float f) noexcept
{
	return static_cast<S32>(std::floor(f));
}

inline S32 llceil(float f) noexcept
{
	return static_cast<S32>(std::ceil(f));
}

// S24: Use std::lround for arithmetic rounding (compiler intrinsic, SSE4.1 optimized)
// std::lround rounds 0.5 away from zero (proper arithmetic rounding)
inline S32 ll_round(const float val) noexcept
{
	return static_cast<S32>(std::lround(val));
}

inline F64 ll_round(const F64 val) noexcept
{
	return std::round(val);
}

// S24: Round to nearest multiple using std::round (faster than floor-based approach)
inline float ll_round(float val, float nearest) noexcept
{
	return std::round(val / nearest) * nearest;
}

inline F64 ll_round(F64 val, F64 nearest) noexcept
{
	return std::round(val / nearest) * nearest;
}

// these provide minimum peak error
//
// avg  error = -0.013049
// peak error = -31.4 dB
// RMS  error = -28.1 dB

constexpr float FAST_MAG_ALPHA = 0.960433870103f;
constexpr float FAST_MAG_BETA = 0.397824734759f;

// these provide minimum RMS error
//
// avg  error = 0.000003
// peak error = -32.6 dB
// RMS  error = -25.7 dB
//
//const float FAST_MAG_ALPHA = 0.948059448969f;
//const float FAST_MAG_BETA = 0.392699081699f;

constexpr float bitwise_abs(float x) {
	return std::bit_cast<float>(std::bit_cast<uint32_t>(x) & 0x7FFFFFFF);
}
// S24 branchless, bitwise fastMagnitude
constexpr float fastMagnitude(float a, float b)
{
	a = bitwise_abs(a);
	b = bitwise_abs(b);
	return FAST_MAG_ALPHA * llmax(a, b) + FAST_MAG_BETA * llmin(a, b);
}

////////////////////
//
// Fast float/S32 conversions

constexpr F64 LL_DOUBLE_TO_FIX_MAGIC = 68719476736.0 * 1.5;     //2^36 * 1.5,  (52-_shiftamt=36) uses limited precisicion to floor
constexpr S32 LL_SHIFT_AMOUNT = 16;                    //16.16 fixed point representation,

// S24 DEPRECATED: Little-endian architecture assumed (Windows x64 only)
// These macros are legacy cruft - modern code should use std::endian (C++20)
[[deprecated("Use std::endian::native or direct bit manipulation")]]
constexpr S32 LL_EXP_INDEX = 1;
[[deprecated("Use std::endian::native or direct bit manipulation")]]
constexpr S32 LL_MAN_INDEX = 0;

////////////////////////////////////////////////
//
// S24 MODERNIZED: Fast exp approximation
// Based on Nicol N. Schraudolph's paper: http://www.inf.ethz.ch/~schraudo/pubs/exp.pdf
// Replaced union type-punning with std::bit_cast (C++20 standard, zero overhead)
//

// S24: Converted macros to const for type safety (OO_LN2 is runtime-computed)
const float LL_EXP_A = 1048576.0f * OO_LN2;  // use 1512775 for integer
constexpr S32   LL_EXP_C = 60801;            // this value of C good for -4 < y < 4

// S24: Replaced macro + union hack with inline function + std::bit_cast
// std::bit_cast is compiler-intrinsic (zero overhead) and avoids undefined behavior
inline double LL_FAST_EXP(float y) noexcept
{
	S32 magic_bits = ll_round(LL_EXP_A * y) + (1072693248 - LL_EXP_C);

	// Construct IEEE 754 double by manipulating exponent bits (little-endian assumption)
	struct DoubleBits { S32 lo, hi; };
	DoubleBits bits{ 0, magic_bits };

	return std::bit_cast<double>(bits);
}

// S24: Fast power approximation using modernized LL_FAST_EXP
// Note: Accuracy limited to ~4 decimal places, use std::pow for precision-critical code
inline float llfastpow(const float x, const float y) noexcept
{
	return static_cast<float>(LL_FAST_EXP(y * std::log(x)));
}

// S24: Snap to significant figures using std::pow and std::copysign (compiler intrinsics)
inline float snap_to_sig_figs(float foo, S32 sig_figs) noexcept
{
	float multiplier = std::pow(10.0f, static_cast<float>(sig_figs));
	float sign = std::copysign(1.0f, foo);
	return static_cast<float>(static_cast<S64>(std::fma(foo, multiplier, sign * 0.5f))) / multiplier;
}

inline float lerp(float a, float b, float u) {
	return std::fma(u, b - a, a); // S24 Use FMA for fused multiply-add
}

inline float lerp2d(float x00, float x01, float x10, float x11, float u, float v) {
	// S24 Fused Multiply-Add for precision and speed
	float a = std::fma(u, x01 - x00, x00); // a = x00 + (x01 - x00) * u
	float b = std::fma(u, x11 - x10, x10); // b = x10 + (x11 - x10) * u
	return std::fma(v, b - a, a);        // r = a + (b - a) * v
}

constexpr float ramp(float x, float a, float b)
{
	// Handle the special case where a == b upfront to avoid unnecessary computation
	if (a == b) return 0.0f;

	// S24 Use FMA for the core operation: (a - x) / (a - b)
	float denominator = a - b;
	return std::fma(-1.0f / denominator, x, a / denominator); // Efficient computation of (a - x) / (a - b)
}
// S24 DO not Constexpr as we use FMA for certain functions!
inline float rescale(float x, float x1, float x2, float y1, float y2)
{
	return lerp(y1, y2, ramp(x, x1, x2));
}

constexpr float clamp_rescale(float x, float x1, float x2, float y1, float y2)
{
	if (y1 < y2)
	{
		return llclamp(rescale(x, x1, x2, y1, y2), y1, y2);
	}
	else
	{
		return llclamp(rescale(x, x1, x2, y1, y2), y2, y1);
	}
}

// S24 FMA
constexpr float cubic_step(float x, float x0, float x1, float s0, float s1)
{
	// Early exits for edge cases
	if (x <= x0) return s0;
	if (x >= x1) return s1;

	// Compute normalized factor in [0, 1]
	float f = (x - x0) / (x1 - x0);

	// Compute cubic interpolation
	float f2 = f * f;                  // Precompute f^2
	float f3 = f2 * (3.0f - 2.0f * f); // Evaluate cubic polynomial

	return s0 + (s1 - s0) * f3;
}

constexpr float cubic_step(float x)
{
	x = llclampf(x);

	return	(x * x) * (3.0f - 2.0f * x);
}

constexpr float quadratic_step(float x, float x0, float x1, float s0, float s1)
{
	if (x <= x0)
		return s0;

	if (x >= x1)
		return s1;

	float f = (x - x0) / (x1 - x0);
	float f_squared = f * f;

	return	(s0 * (1.f - f_squared)) + ((s1 - s0) * f_squared);
}

constexpr float llsimple_angle(float angle)
{
	while (angle <= -F_PI)
		angle += F_TWO_PI;
	while (angle > F_PI)
		angle -= F_TWO_PI;
	return angle;
}

//SDK - Renamed this to get_lower_power_two, since this is what this actually does.
constexpr U32 get_lower_power_two(U32 val, U32 max_power_two)
{
	if (!max_power_two)
	{
		max_power_two = 1 << 31;
	}
	if (max_power_two & (max_power_two - 1))
	{
		return 0;
	}

	for (; val < max_power_two; max_power_two >>= 1);

	return max_power_two;
}

// calculate next highest power of two, limited by max_power_two
// This is taken from a brilliant little code snipped on http://acius2.blogspot.com/2007/11/calculating-next-power-of-2.html
// Basically we convert the binary to a solid string of 1's with the same
// number of digits, then add one.  We subtract 1 initially to handle
// the case where the number passed in is actually a power of two.
// WARNING: this only works with 32 bit ints.
constexpr U32 get_next_power_two(U32 val, U32 max_power_two)
{
	if (!max_power_two)
	{
		max_power_two = 1 << 31;
	}

	if (val >= max_power_two)
	{
		return max_power_two;
	}

	val--;
	val = (val >> 1) | val;
	val = (val >> 2) | val;
	val = (val >> 4) | val;
	val = (val >> 8) | val;
	val = (val >> 16) | val;
	val++;

	return val;
}

// S24: Gaussian distribution using std::exp (compiler intrinsic, vectorizable)
inline float llgaussian(float x, float o) noexcept
{
	float inv_norm = 1.0f / (F_SQRT_TWO_PI * o);
	float o_squared = o * o;
	float exponent = -(x * x) / (2.0f * o_squared);
	return inv_norm * std::exp(exponent);
}

//helper function for removing outliers
template <class VEC_TYPE>
inline void ll_remove_outliers(std::vector<VEC_TYPE>& data, float k)
{
	if (data.size() < 100)
	{ //not enough samples
		return;
	}

	VEC_TYPE Q1 = data[data.size() / 4];
	VEC_TYPE Q3 = data[data.size() - data.size() / 4 - 1];

	if ((float)(Q3 - Q1) < 1.f)
	{
		// not enough variation to detect outliers
		return;
	}

	VEC_TYPE min = (VEC_TYPE)((float)Q1 - k * (float)(Q3 - Q1));
	VEC_TYPE max = (VEC_TYPE)((float)Q3 + k * (float)(Q3 - Q1));

	U32 i = 0;
	while (i < data.size() && data[i] < min)
	{
		i++;
	}

	size_t j = data.size() - 1;
	while (j > 0 && data[j] > max)
	{
		j--;
	}

	if (j < data.size() - 1)
	{
		data.erase(data.begin() + j, data.end());
	}

	if (i > 0)
	{
		data.erase(data.begin(), data.begin() + i);
	}
}

// S24: sRGB gamma correction using std::pow (compiler intrinsic, auto-vectorizable)
// Converts linear RGB [0..1] to gamma-corrected sRGB [0..1]
// Note: Values labeled as sRGB are ALWAYS gamma corrected linear values
// Note: DO NOT cache - slower due to cache misses, use inline conversion
constexpr float SRGB_LINEAR_THRESHOLD = 0.0031308f;
constexpr float SRGB_LINEAR_SCALE = 12.92f;
constexpr float SRGB_GAMMA_SCALE = 1.055f;
constexpr float SRGB_GAMMA_OFFSET = 0.055f;
constexpr float SRGB_GAMMA_POWER = 1.0f / 2.4f;

inline float linearTosRGB(const float val) noexcept
{
	if (val < SRGB_LINEAR_THRESHOLD) {
		return val * SRGB_LINEAR_SCALE;
	}
	return std::fma(SRGB_GAMMA_SCALE, std::pow(val, SRGB_GAMMA_POWER), -SRGB_GAMMA_OFFSET);
}

// S24: sRGB to linear conversion using std::pow (compiler intrinsic, auto-vectorizable)
// Converts gamma-corrected sRGB [0..1] to linear RGB [0..1]
constexpr float SRGB_DECODE_THRESHOLD = 0.04045f;
constexpr float SRGB_DECODE_SCALE = 1.0f / 12.92f;
constexpr float SRGB_DECODE_OFFSET_SCALE = 1.0f / 1.055f;
constexpr float SRGB_DECODE_POWER = 2.4f;

inline float sRGBtoLinear(const float val) noexcept
{
	if (val < SRGB_DECODE_THRESHOLD) {
		return val * SRGB_DECODE_SCALE;
	}
	return std::pow((val + SRGB_GAMMA_OFFSET) * SRGB_DECODE_OFFSET_SCALE, SRGB_DECODE_POWER);
}

// Include simd math header
#include "llsimdmath.h"

#endif
