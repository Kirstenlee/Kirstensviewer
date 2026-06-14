/** 
 * @file llcylinder.cpp
 * @brief Draws a cylinder using display lists for speed.
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

#include "llviewerprecompiledheaders.h"

#include "llcylinder.h"

#include "llerror.h"
#include "math.h"
#include "llmath.h"
#include "noise.h"
#include "v3math.h"
#include "llvertexbuffer.h"
#include "llgl.h"
#include "llglheaders.h"

LLCone		gCone;

//
// Cones S24 - precompute using GLM ditch older method
//

void LLCone::render(S32 sides)
{
    // Precompute constants
    const float radius = 0.5f; // Radius of the base
    const glm::vec3 tip(0.0f, 0.0f, 0.5f); // Tip of the cone
    const glm::vec3 base_center(0.0f, 0.0f, -0.5f); // Center of the base

    // Base of the cone
    gGL.begin(LLRender::TRIANGLE_FAN);
    gGL.vertex3f(base_center.x, base_center.y, base_center.z); // Center vertex

    for (S32 i = 0; i <= sides; ++i) { // Include the closing point
        float angle = (float)i / sides * glm::two_pi<float>(); // Full circle
        float x = cos(angle) * radius; // X-coordinate
        float y = sin(angle) * radius; // Y-coordinate
        gGL.vertex3f(x, y, base_center.z); // Add vertex on the base perimeter
    }
    gGL.end();

    // Sides of the cone
    gGL.begin(LLRender::TRIANGLE_FAN);
    gGL.vertex3f(tip.x, tip.y, tip.z); // Tip vertex

    for (S32 i = 0; i <= sides; ++i) { // Match the base perimeter vertices
        float angle = (float)i / sides * glm::two_pi<float>();
        float x = cos(angle) * radius;
        float y = sin(angle) * radius;
        gGL.vertex3f(x, y, base_center.z); // Add vertex on the base perimeter
    }
    gGL.end();
}


