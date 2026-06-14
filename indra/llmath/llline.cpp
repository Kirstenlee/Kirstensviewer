/**
 * @file llline.cpp
 * @author Andrew Meadows
 * @brief Simple line class that can compute nearest approach between two lines
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#include "llline.h"
#include "llrand.h"

const F32 SOME_VERY_SMALL_NUMBER = 1.0e-8f;

LLLine::LLLine()
	: mPoint(0.f, 0.f, 0.f),
	mDirection(1.f, 0.f, 0.f)
{
}

LLLine::LLLine(const LLVector3& first_point, const LLVector3& second_point)
{
	setPoints(first_point, second_point);
}

void LLLine::setPoints(const LLVector3& first_point, const LLVector3& second_point)
{
	mPoint = first_point;
	mDirection = second_point - first_point;
	mDirection.normalize();
}

void LLLine::setPointDirection(const LLVector3& first_point, const LLVector3& second_point)
{
	setPoints(first_point, first_point + second_point);
}

bool LLLine::intersects(const LLVector3& point, F32 radius) const
{
	LLVector3 other_direction = point - mPoint;
	LLVector3 nearest_point = mPoint + mDirection * (other_direction * mDirection);
	F32 nearest_approach = (nearest_point - point).length();
	return (nearest_approach <= radius);
}

// returns the point on this line that is closest to some_point
LLVector3 LLLine::nearestApproach(const LLVector3& some_point) const
{
	return (mPoint + mDirection * ((some_point - mPoint) * mDirection));
}

// the accuracy of this method sucks when you give it two nearly
// parallel lines, so you should probably check for parallelism
// before you call this
//
// returns the point on this line that is closest to other_line
LLVector3 LLLine::nearestApproach(const LLLine& other_line) const
{
	LLVector3 between_points = other_line.mPoint - mPoint;
	F32 dir_dot_dir = mDirection * other_line.mDirection;
	F32 one_minus_dir_dot_dir = 1.0f - fabs(dir_dot_dir);
	if (one_minus_dir_dot_dir < SOME_VERY_SMALL_NUMBER)
	{
#ifdef LL_DEBUG
		LL_WARNS() << "LLLine::nearestApproach() was given two very "
			<< "nearly parallel lines dir1 = " << mDirection
			<< " dir2 = " << other_line.mDirection << " with 1-dot_product = "
			<< one_minus_dir_dot_dir << LL_ENDL;
#endif
		// the lines are approximately parallel
		// We shouldn't fall in here because this check should have been made
		// BEFORE this function was called.  We dare not continue with the
		// computations for fear of division by zero, but we have to return
		// something so we return a bogus point -- caller beware.
		return 0.5f * (mPoint + other_line.mPoint);
	}

	F32 odir_dot_bp = other_line.mDirection * between_points;

	F32 numerator = 0;
	F32 denominator = 0;
	for (S32 i = 0; i < 3; i++)
	{
		F32 factor = dir_dot_dir * other_line.mDirection.mV[i] - mDirection.mV[i];
		numerator += (between_points.mV[i] - odir_dot_bp * other_line.mDirection.mV[i]) * factor;
		denominator -= factor * factor;
	}

	F32 length_to_nearest_approach = numerator / denominator;

	return mPoint + length_to_nearest_approach * mDirection;
}

std::ostream& operator<<(std::ostream& output_stream, const LLLine& line)
{
	output_stream << "{point=" << line.mPoint << "," << "dir=" << line.mDirection << "}";
	return output_stream;
}

F32 ALMOST_PARALLEL = 0.99f;
F32 TOO_SMALL_FOR_DIVISION = 0.0001f;

// returns 'true' if this line intersects the plane
// on success stores the intersection point in 'result'
// S24 ENHANCED
bool LLLine::intersectsPlane(LLVector3& result, const LLLine& plane) const
{
	// Cache the plane's normal vector.
	const LLVector3& planeNormal = plane.mDirection;

	// Compute the dot product between the plane's normal and this line's direction.
	const F32 dot = planeNormal * mDirection;

	// If the dot product is nearly zero, the line is parallel to the plane.
	if (fabs(dot) < TOO_SMALL_FOR_DIVISION)
	{
		return false;
	}

	// Compute the plane constant:
	// Equation of the plane: (planeNormal • p) = D, where D = planeNormal • plane.mPoint.
	const F32 D = planeNormal * plane.mPoint;

	// Calculate the parameter 't' along the line at which the intersection occurs:
	// From the line equation p = mPoint + t*mDirection and substituting into the plane equation:
	// t = (D - planeNormal • mPoint) / dot
	const F32 t = (D - (planeNormal * mPoint)) / dot;

	// Compute the intersection point.
	result = mPoint + t * mDirection;
	return true;
}
//static
// S24 ENHANCED
// returns 'true' if planes intersect, and stores the result
// x₀ = ((d₁ * n₂ − d₂ * n₁) × d) / d²
// This formulation is well known(see, for example, “Real - Time Collision Detection” by Christer Ericson)
// and avoids the extra “helper” line construction and iterative intersection call.
// Includes Safety for nonzero or degenerate situations
bool LLLine::getIntersectionBetweenTwoPlanes(LLLine& result,
	const LLLine& first_plane,
	const LLLine& second_plane)
{
	// Retrieve plane normals (assumed to be stored in mDirection).
	const LLVector3& n1 = first_plane.mDirection;
	const LLVector3& n2 = second_plane.mDirection;

	// Safety check: Ensure that both normals are non-zero.
	if (n1.magVecSquared() < TOO_SMALL_FOR_DIVISION ||
		n2.magVecSquared() < TOO_SMALL_FOR_DIVISION)
	{
		// One or both normals are too small to define a valid plane.
		return false;
	}

	// Check if the planes are nearly parallel.
	if (fabs(n1 * n2) > ALMOST_PARALLEL)
	{
		return false;
	}

	// Compute the line direction as the cross product of the plane normals.
	LLVector3 lineDir = n1 % n2;
	// Check against degenerate cross product (nearly zero length):
	F32 denom = lineDir.magVecSquared();
	if (denom < TOO_SMALL_FOR_DIVISION * TOO_SMALL_FOR_DIVISION)
	{
		// The cross product's magnitude is too small – numerical degeneracy.
		return false;
	}

	// Compute the plane constants:
	// For a plane defined by n · x = D, compute D as (n dot a point on the plane).
	F32 d1 = n1 * first_plane.mPoint;
	F32 d2 = n2 * second_plane.mPoint;

	// Compute an intersection point using the analytic formula:
	// x0 = ((d1 * n2 - d2 * n1) × (n1 × n2)) / |n1 × n2|².
	LLVector3 intersectionPoint = ((d1 * n2) - (d2 * n1)) % lineDir;
	intersectionPoint /= denom;

	result.mPoint = intersectionPoint;
	result.mDirection = lineDir;
	result.mDirection.normalize(); // Normalize the result for consistency.

	return true;
}