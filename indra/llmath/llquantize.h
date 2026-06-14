/** 
 * @file llquantize.h
 * @brief useful routines for quantizing floats to various length ints
 * and back out again
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

// S24 Includes
#include <immintrin.h>
#include <cstdint>
#include <cmath>

#ifndef LL_LLQUANTIZE_H
#define LL_LLQUANTIZE_H

const U16 U16MAX = 65535;
const F32 OOU16MAX = 1.f/(F32)(U16MAX);
const U8 U8MAX = 255;
const F32 OOU8MAX = 1.f/(F32)(U8MAX);

__declspec(align(16)) const F32 F_U16MAX_4A[4] = { 65535.f, 65535.f, 65535.f, 65535.f };
__declspec(align(16)) const F32 F_OOU16MAX_4A[4] = { OOU16MAX, OOU16MAX, OOU16MAX, OOU16MAX };
__declspec(align(16)) const F32 F_U8MAX_4A[4] = { 255.f, 255.f, 255.f, 255.f };
__declspec(align(16)) const F32 F_OOU8MAX_4A[4] = { OOU8MAX, OOU8MAX, OOU8MAX, OOU8MAX };

const U8 FIRSTVALIDCHAR = 54;
const U8 MAXSTRINGVAL = U8MAX - FIRSTVALIDCHAR; //we don't allow newline or null 


// S24 - convert F32 to U16 with rounding using AVX intrinsics
inline U16 F32_to_U16_ROUND(F32 val, F32 lower, F32 upper) {
	const F32 U16MAX = 65535.0f; // Maximum value for uint16_t

	// Load constants into SIMD registers
	__m128 val_vec = _mm_set_ss(val);        // Load the input value into a vector
	__m128 lower_vec = _mm_set_ss(lower);    // Load the lower bound
	__m128 upper_vec = _mm_set_ss(upper);    // Load the upper bound
	__m128 max_u16_vec = _mm_set_ss(U16MAX); // Load 65535 as the maximum U16 value

	// Clamp the value: val = llclamp(val, lower, upper)
	val_vec = _mm_max_ss(lower_vec, _mm_min_ss(val_vec, upper_vec)); // Equivalent to clamping

	// Normalize the value: val = (val - lower) / (upper - lower)
	__m128 delta_vec = _mm_sub_ss(upper_vec, lower_vec);             // delta = upper - lower
	__m128 norm_vec = _mm_div_ss(_mm_sub_ss(val_vec, lower_vec), delta_vec); // Normalize to <0, 1>

	// Scale to U16 range and round it: (U16)(ll_round(val * U16MAX))
	__m128 scaled_vec = _mm_mul_ss(norm_vec, max_u16_vec);
	__m128 rounded_vec = _mm_round_ss(scaled_vec, scaled_vec, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC); // Round to nearest

	// Extract the result and convert to U16
	return static_cast<U16>(_mm_cvtss_f32(rounded_vec));
}



// S24 - convert F32 to U16 using AVX intrinsics
inline U16 F32_to_U16(F32 val, F32 lower, F32 upper) {
	const F32 U16MAX = 65535.0f; // Maximum value for uint16_t

	// Load constants and input into SIMD registers
	__m128 val_vec = _mm_set_ss(val);        // Load the input value into a vector
	__m128 lower_vec = _mm_set_ss(lower);    // Load the lower bound
	__m128 upper_vec = _mm_set_ss(upper);    // Load the upper bound
	__m128 max_u16_vec = _mm_set_ss(U16MAX); // Load 65535 as the maximum U16 value

	// Clamp the value: val = llclamp(val, lower, upper)
	val_vec = _mm_max_ss(lower_vec, _mm_min_ss(val_vec, upper_vec)); // Equivalent to clamping

	// Normalize the value: val = (val - lower) / (upper - lower)
	__m128 delta_vec = _mm_sub_ss(upper_vec, lower_vec);             // delta = upper - lower
	__m128 norm_vec = _mm_div_ss(_mm_sub_ss(val_vec, lower_vec), delta_vec); // Normalize to <0, 1>

	// Scale to U16 range and floor it: (U16)(llfloor(val * U16MAX))
	__m128 scaled_vec = _mm_mul_ss(norm_vec, max_u16_vec);
	scaled_vec = _mm_floor_ss(scaled_vec, scaled_vec); // Apply floor operation

	// Extract the result and convert to U16
	return static_cast<U16>(_mm_cvtss_f32(scaled_vec));
}



// S24 - convert U16 to F32 using AVX intrinsics
inline F32 U16_to_F32(U16 ival, F32 lower, F32 upper) {
	const F32 OOU16MAX = 1.0f / 65535.0f; // Precomputed constant for normalization
	const F32 delta = upper - lower;

	// Load constants and input into SIMD registers
	__m128 ival_vec = _mm_set_ss(static_cast<F32>(ival)); // Convert U16 to single precision float
	__m128 lower_vec = _mm_set_ss(lower);                // Load lower bound
	__m128 delta_vec = _mm_set_ss(delta);                // Load delta
	__m128 scale_vec = _mm_set_ss(OOU16MAX);             // Load scaling factor
	__m128 max_error_vec = _mm_set_ss(delta * OOU16MAX); // Calculate max error

	// Perform computations
	__m128 normalized_val = _mm_mul_ss(ival_vec, scale_vec);      // Normalize: ival * OOU16MAX
	__m128 scaled_val = _mm_mul_ss(normalized_val, delta_vec);    // Scale by delta
	__m128 result = _mm_add_ss(scaled_val, lower_vec);            // Add lower bound

	// Apply correction for near-zero values
	__m128 abs_result = _mm_andnot_ps(_mm_set1_ps(-0.0f), result); // Absolute value: fabsf(result)
	__m128 zero_mask = _mm_cmplt_ss(abs_result, max_error_vec);    // Check if |result| < max_error
	result = _mm_blendv_ps(result, _mm_setzero_ps(), zero_mask);   // If true, set to zero

	// Extract the final scalar result
	return _mm_cvtss_f32(result);
}

// S24 perf code
inline uint8_t F32_to_U8_ROUND(float val, float lower, float upper) {
	// Load scalar values into 128-bit AVX registers
	__m128 val_ps = _mm_set_ss(val);
	__m128 lower_ps = _mm_set_ss(lower);
	__m128 upper_ps = _mm_set_ss(upper);
	__m128 range_ps = _mm_set_ss(upper - lower);
	__m128 multiplier_ps = _mm_set_ss(255.0f); // U8MAX

	// Clamp value between lower and upper
	val_ps = _mm_min_ss(_mm_max_ss(val_ps, lower_ps), upper_ps);

	// Normalize to range <0, 1>
	val_ps = _mm_sub_ss(val_ps, lower_ps);
	val_ps = _mm_div_ss(val_ps, range_ps);

	// Multiply by U8MAX and round to the nearest integer
	__m128i result = _mm_cvtps_epi32(_mm_mul_ss(val_ps, multiplier_ps));

	// Extract the rounded result as a uint8_t value
	return (uint8_t)_mm_cvtsi128_si32(result);
}



// S24 -  Scalar AVX-Optimized Implementation
inline U8 F32_to_U8(F32 val, F32 lower, F32 upper) {
	const F32 U8MAX = 255.0f; // Maximum value for uint8_t

	// Load constants into SIMD registers
	__m128 val_vec = _mm_set_ss(val);        // Load input value into vector
	__m128 lower_vec = _mm_set_ss(lower);    // Load lower bound
	__m128 upper_vec = _mm_set_ss(upper);    // Load upper bound
	__m128 max_u8_vec = _mm_set_ss(U8MAX);   // Load 255 as the maximum U8 value

	// Clamp the value: val = llclamp(val, lower, upper)
	val_vec = _mm_max_ss(lower_vec, _mm_min_ss(val_vec, upper_vec)); // Equivalent to clamping

	// Normalize the value: val = (val - lower) / (upper - lower)
	__m128 delta_vec = _mm_sub_ss(upper_vec, lower_vec); // delta = upper - lower
	__m128 norm_vec = _mm_div_ss(_mm_sub_ss(val_vec, lower_vec), delta_vec); // Normalize to <0, 1>

	// Scale to U8 range and floor it: (U8)(llfloor(val * U8MAX))
	__m128 scaled_vec = _mm_mul_ss(norm_vec, max_u8_vec);
	scaled_vec = _mm_floor_ss(scaled_vec, scaled_vec); // Apply floor operation

	// Extract the final result as a scalar U8
	return static_cast<U8>(_mm_cvtss_f32(scaled_vec));
}

// S24 perf code
inline F32 U8_to_F32(U8 ival, F32 lower, F32 upper) {
	const F32 OOU8MAX = 1.0f / 255.0f; // Precompute OOU8MAX for clarity
	const F32 delta = upper - lower;

	// Load constants into AVX registers
	__m128 ival_vec = _mm_set_ss(static_cast<F32>(ival)); // Load the U8 value as a single float
	__m128 lower_vec = _mm_set_ss(lower);                // Set lower bound
	__m128 delta_vec = _mm_set_ss(delta);                // Set delta
	__m128 max_error_vec = _mm_set_ss(delta * OOU8MAX);  // Set max error threshold
	__m128 scale_vec = _mm_set_ss(OOU8MAX);              // Set scaling factor

	// Perform calculations using AVX
	__m128 scaled_val = _mm_mul_ss(ival_vec, scale_vec);      // scaled_val = ival * OOU8MAX
	__m128 delta_scaled = _mm_mul_ss(scaled_val, delta_vec); // delta_scaled = scaled_val * delta
	__m128 result = _mm_add_ss(delta_scaled, lower_vec);      // result = delta_scaled + lower

	// Handle near-zero correction
	__m128 abs_result = _mm_andnot_ps(_mm_set1_ps(-0.0f), result); // Absolute value: abs(result)
	__m128 zero_mask = _mm_cmplt_ss(abs_result, max_error_vec);    // Compare: abs(result) < max_error
	result = _mm_blendv_ps(result, _mm_setzero_ps(), zero_mask);   // If true, set to zero

	// Extract the result back into a scalar float
	return _mm_cvtss_f32(result);
}

inline U8 F32_TO_STRING(F32 val, F32 lower, F32 upper)
{
	val = llclamp(val, lower, upper); //[lower, upper]
	// make sure that the value is positive and normalized to <0, 1>
	val -= lower;					//[0, upper-lower]
	val /= (upper - lower);			//[0,1]
	val = val * MAXSTRINGVAL;		//[0, MAXSTRINGVAL]
	val = floor(val + 0.5f);		//[0, MAXSTRINGVAL]

	U8 stringVal = (U8)(val) + FIRSTVALIDCHAR;			//[FIRSTVALIDCHAR, MAXSTRINGVAL + FIRSTVALIDCHAR]
	return stringVal;
}

inline F32 STRING_TO_F32(U8 ival, F32 lower, F32 upper)
{
	// remove empty space left for NULL, newline, etc.
	ival -= FIRSTVALIDCHAR;								//[0, MAXSTRINGVAL]

	F32 val = (F32)ival * (1.f / (F32)MAXSTRINGVAL);	//[0, 1]
	F32 delta = (upper - lower);
	val *= delta;										//[0, upper - lower]
	val += lower;										//[lower, upper]

	return val;
}

#endif
