/**
 * @file llsphere.cpp
 * @author Andrew Meadows / refactored and Re-Written by Kirstenlee Cinquetti
 * @brief Simple line class that can compute nearest approach between two lines
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "llsphere.h"

LLSphere::LLSphere()
	: mCenter(0.f, 0.f, 0.f),
	mRadius(0.f)
{ }

LLSphere::LLSphere(const LLVector3& center, F32 radius)
{
	set(center, radius);
}

void LLSphere::set(const LLVector3& center, F32 radius)
{
	mCenter = center;
	setRadius(radius);
}

void LLSphere::setCenter(const LLVector3& center)
{
	mCenter = center;
}

void LLSphere::setRadius(F32 radius)
{
	if (radius < 0.f)
	{
		radius = -radius;
	}
	mRadius = radius;
}

const LLVector3& LLSphere::getCenter() const
{
	return mCenter;
}

F32 LLSphere::getRadius() const
{
	return mRadius;
}

// returns 'true' if this sphere completely contains other_sphere
bool LLSphere::contains(const LLSphere& other_sphere) const
{
	F32 separation = (mCenter - other_sphere.mCenter).length();
	return mRadius >= separation + other_sphere.mRadius;
}

// returns 'true' if this sphere completely contains other_sphere
bool LLSphere::overlaps(const LLSphere& other_sphere) const
{
	F32 separation = (mCenter - other_sphere.mCenter).length();
	return mRadius >= separation - other_sphere.mRadius;
}

// returns overlap
// negative overlap is closest approach
F32 LLSphere::getOverlap(const LLSphere& other_sphere) const
{
	// separation is distance from other_sphere's edge and this center
	return (mCenter - other_sphere.mCenter).length() - mRadius - other_sphere.mRadius;
}

bool LLSphere::operator==(const LLSphere& rhs) const
{
	return fabs(mRadius - rhs.mRadius) <= FLT_EPSILON &&
		(mCenter - rhs.mCenter).length() <= FLT_EPSILON;
}

std::ostream& operator<<(std::ostream& output_stream, const LLSphere& sphere)
{
	output_stream << "{center=" << sphere.mCenter << "," << "radius=" << sphere.mRadius << "}";
	return output_stream;
}

// static
// S24 ENHANCED : removes any spheres that are contained in others
void LLSphere::collapse(std::vector<LLSphere>& sphere_list)
{
	const size_t sphere_count = sphere_list.size();
	if (sphere_count < 2)
	{
		// Nothing to collapse if there are less than 2 spheres.
		return;
	}

	// Mark spheres that are contained by another.
	std::vector<bool> to_remove(sphere_count, false);
	for (size_t i = 0; i < sphere_count; ++i)
	{
		for (size_t j = i + 1; j < sphere_count; ++j)
		{
			if (sphere_list[j].contains(sphere_list[i]))
			{
				// If sphere[j] contains sphere[i], then mark sphere[i] for removal.
				to_remove[i] = true;
			}
			else if (sphere_list[i].contains(sphere_list[j]))
			{
				// Otherwise, if sphere[i] contains sphere[j], mark sphere[j] for removal.
				to_remove[j] = true;
			}
		}
	}

	// Create a new list with only the spheres that were not marked for removal.
	std::vector<LLSphere> collapsed_list;
	collapsed_list.reserve(sphere_count);
	for (size_t i = 0; i < sphere_count; ++i)
	{
		if (!to_remove[i])
		{
			collapsed_list.push_back(sphere_list[i]);
		}
	}

	// Replace the original sphere_list with the collapsed list.
	sphere_list.swap(collapsed_list);
}

// static
// S24 ENHANCED : returns the bounding sphere that contains both spheres
LLSphere LLSphere::getBoundingSphere(const LLSphere& first_sphere, const LLSphere& second_sphere)
{
	// Define a small slop to offset potential floating point errors.
	const F32 slop = 0.0005f;

	// Compute the directional vector from first_sphere to second_sphere.
	LLVector3 direction = second_sphere.mCenter - first_sphere.mCenter;
	F32 distance = direction.length();

	// If both centers are extremely close, we choose an arbitrary direction.
	if (distance < 1e-6f)
	{
		direction.setVec(1.f, 0.f, 0.f);
		// Early return: choose the maximum radius (with slop added to second_sphere)
		// since the spheres are nearly coincident.
		F32 radius = llmax(first_sphere.getRadius(), second_sphere.getRadius() + slop);
		return LLSphere(first_sphere.mCenter, radius);
	}
	else
	{
		direction.normVec();
	}

	// Define the extents along the line from first_sphere's center.
	// For first_sphere, its range is from -radius to +radius.
	const F32 firstMin = -first_sphere.getRadius();
	const F32 firstMax = first_sphere.getRadius();

	// For second_sphere, its "projected" min and max along the direction are computed
	// considering its distance and radius, while also subtracting/adding the slop.
	const F32 secondMin = distance - second_sphere.getRadius() - slop;
	const F32 secondMax = distance + second_sphere.getRadius() + slop;

	// Compute the overall minimum and maximum extents along this axis.
	F32 overallMin = llmin(firstMin, secondMin);
	F32 overallMax = llmax(firstMax, secondMax);

	// The new bounding sphere's radius is half the total span.
	F32 radius = 0.5f * (overallMax - overallMin);
	// Its center is offset from first_sphere's center by the average of the extents.
	F32 offset = 0.5f * (overallMax + overallMin);
	LLVector3 center = first_sphere.mCenter + (offset * direction);

	return LLSphere(center, radius);
}

// static
// S24 ENHANCED : returns the bounding sphere that contains an arbitrary set of spheres
LLSphere LLSphere::getBoundingSphere(const std::vector<LLSphere>& sphere_list)
{
	size_t sphere_count = sphere_list.size();

	// Handle trivial cases.
	if (sphere_count == 0)
	{
		return LLSphere(LLVector3(0.f, 0.f, 0.f), 0.f);
	}
	else if (sphere_count == 1)
	{
		return sphere_list.front();
	}
	else if (sphere_count == 2)
	{
		return LLSphere::getBoundingSphere(sphere_list[0], sphere_list[1]);
	}

	// Compute the AABB for the set of spheres.
	LLVector3 min_corner, max_corner;
	{
		// Use the first sphere as the starting point.
		const LLSphere& s = sphere_list.front();
		min_corner = s.getCenter() - s.getRadius() * LLVector3(1.f, 1.f, 1.f);
		max_corner = s.getCenter() + s.getRadius() * LLVector3(1.f, 1.f, 1.f);

		// Expand the AABB for each sphere.
		for (const auto& sphere : sphere_list)
		{
			const LLVector3& center = sphere.getCenter();
			F32 radius = sphere.getRadius();

			min_corner.mV[VX] = std::min(min_corner.mV[VX], center.mV[VX] - radius);
			min_corner.mV[VY] = std::min(min_corner.mV[VY], center.mV[VY] - radius);
			min_corner.mV[VZ] = std::min(min_corner.mV[VZ], center.mV[VZ] - radius);

			max_corner.mV[VX] = std::max(max_corner.mV[VX], center.mV[VX] + radius);
			max_corner.mV[VY] = std::max(max_corner.mV[VY], center.mV[VY] + radius);
			max_corner.mV[VZ] = std::max(max_corner.mV[VZ], center.mV[VZ] + radius);
		}
	}

	// Initial bounding sphere from the AABB.
	LLVector3 bounding_center = 0.5f * (max_corner + min_corner);
	LLVector3 diagonal = max_corner - min_corner;
	F32 bounding_radius = 0.5f * diagonal.length();

	// Define a small tolerance matching half a millimeter.
	const F32 half_millimeter = 0.0005f;

	// Define a step size based on the diagonal of the AABB.
	F32 minimum_dimension = std::min({ diagonal.mV[VX], diagonal.mV[VY], diagonal.mV[VZ] });
	F32 minimum_radius = 0.5f * minimum_dimension;
	F32 step_length = bounding_radius - minimum_radius;

	// We'll keep track of the last perturbation direction using a simple structure.
	struct IntVector3 { int dx, dy, dz; };
	IntVector3 last_dir = { 2, 2, 2 };  // Out-of-bound marker

	// Lambda that calculates the required radius when using a candidate center.
	auto computeRequiredRadius = [&](const LLVector3& center) -> F32 {
		F32 max_radius = 0.f;
		for (const auto& sphere : sphere_list)
		{
			F32 curRadius = (sphere.getCenter() - center).length() + sphere.getRadius();
			max_radius = std::max(max_radius, curRadius);
		}
		return max_radius;
		};

	// Iteratively adjust the center to reduce the bounding sphere radius.
	while (step_length > half_millimeter)
	{
		bool improved = false;
		IntVector3 best_dir = { 0, 0, 0 };

		// Sample neighboring directions.
		for (int dx = -1; dx <= 1; ++dx)
		{
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dz = -1; dz <= 1; ++dz)
				{
					// Skip the current center.
					if (dx == 0 && dy == 0 && dz == 0)
						continue;

					// If two components match the last direction, skip this candidate.
					int match_count = ((dx == last_dir.dx) ? 1 : 0) +
						((dy == last_dir.dy) ? 1 : 0) +
						((dz == last_dir.dz) ? 1 : 0);
					if (match_count == 2)
						continue;

					LLVector3 candidate_center = bounding_center;
					candidate_center.mV[VX] += dx * step_length;
					candidate_center.mV[VY] += dy * step_length;
					candidate_center.mV[VZ] += dz * step_length;

					F32 candidate_radius = computeRequiredRadius(candidate_center);
					if (candidate_radius < bounding_radius)
					{
						best_dir = { dx, dy, dz };
						bounding_center = candidate_center;
						bounding_radius = candidate_radius;
						improved = true;
					}
				}
			}
		}

		if (improved)
		{
			// Remember the inverse of the direction in which improvement was found.
			last_dir.dx = -best_dir.dx;
			last_dir.dy = -best_dir.dy;
			last_dir.dz = -best_dir.dz;
		}
		else
		{
			// No improvement: reduce the step size.
			step_length *= 0.5f;
			last_dir = { 2, 2, 2 };
		}
	}

	// Account for any residual floating-point error.
	bounding_radius += half_millimeter;

	LLSphere bounding_sphere;
	bounding_sphere.set(bounding_center, bounding_radius);
	return bounding_sphere;
}