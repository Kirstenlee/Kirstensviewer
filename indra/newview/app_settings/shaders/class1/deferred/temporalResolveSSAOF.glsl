/**
 * @file temporalResolveSSAOF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

/*[EXTRA_CODE_HERE]*/

// S24 (2026-08-23, task #190 temporal-SSAO follow-up, DX_RENDER): kept
// structurally parallel to temporalResolveSSAOF.hlsl (this project's
// standing convention) but not part of the DX_RENDER port itself - written
// for source-parity, not GL-tested. See the .hlsl file for full context.

out vec4 frag_color;

uniform sampler2D lightMap;
uniform sampler2D historyMap;

uniform mat4 inv_modelview_delta;
uniform mat4 last_projection_matrix;

in vec2 vary_fragcoord;

vec4 getPosition(vec2 pos_screen);

// Fixed per-frame history weight - see .hlsl sibling for why this is not
// dt-based.
const float kHistoryBlend = 0.85;

void main()
{
    vec2 tc = vary_fragcoord.xy;

    vec4 current = texture(lightMap, tc);

    vec3 pos_cur_eye = getPosition(tc).xyz;
    vec4 pos_last_eye = inv_modelview_delta * vec4(pos_cur_eye, 1.0);
    vec4 clip_last = last_projection_matrix * pos_last_eye;

    float ao_final = current.g;

    if (clip_last.w > 0.0)
    {
        vec2 ndc_last = clip_last.xy / clip_last.w;
        vec2 uv_last = ndc_last * 0.5 + 0.5;

        if (uv_last.x >= 0.0 && uv_last.x <= 1.0 && uv_last.y >= 0.0 && uv_last.y <= 1.0)
        {
            float history_ao = texture(historyMap, uv_last).g;
            ao_final = mix(current.g, history_ao, kHistoryBlend);
        }
    }

    frag_color = vec4(current.r, ao_final, current.b, current.a);
}
