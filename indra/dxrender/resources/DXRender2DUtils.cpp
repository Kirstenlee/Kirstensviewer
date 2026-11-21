/**
 * @file DXRender2DUtils.cpp
 * @brief Immediate-mode 2D drawing for the D3D11 renderer
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Portions of this file (the functions tagged [LL]) derive from
 * llrender2dutils.cpp of the Second Life Viewer and remain under the license
 * below; everything else in this file is under the MIT license above.
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
#include "llrender.h"
#include "llhlslshader.h"
#include "lldxtexture.h"
#include "llfontdx.h"
#include "lluiimage.h"
#include "DXRenderTarget.h"
#include "llvector4a.h"
#include "m3math.h"
#include "llrect.h"
#include "llgl.h"
#include "DXRender2DUtils.h"
#include "DXUIBatch.h"
#include "v2math.h"
#include <vector>
#include <cmath>

// +--------------------------------------------------------------------------+
// | DXRender2DUtils - immediate-mode 2D drawing and UI image plumbing        |
// |                                                                          |
// | Origin tags:  [LL]  derived from Linden's llrender2dutils                |
// |               [S24] S24-original                                         |
// | Sections:     HELPERS | BUILDERS | DRAWING API (state, lines, rects,     |
// |               round shapes, segmented rects, images) | DXRender2D        |
// | Flow:         dx_* function -> drawBatch() -> builder -> DXUIBatch       |
// +--------------------------------------------------------------------------+

// +--------------------------------------------------------------------------+
// | HELPERS - file-local constants, vertex packing, batch submission         |
// +--------------------------------------------------------------------------+

namespace
{

    // [S24] 2*pi
    constexpr F32 kTwoPi = 6.283185307179586f;

    // [S24] ERoundedEdge flags as U32 for mask tests
    constexpr U32 kRoundedRectLeft = ROUNDED_RECT_LEFT;
    constexpr U32 kRoundedRectTop = ROUNDED_RECT_TOP;
    constexpr U32 kRoundedRectRight = ROUNDED_RECT_RIGHT;
    constexpr U32 kRoundedRectBottom = ROUNDED_RECT_BOTTOM;

    // [S24] what a drawBatch() builder needs: the UI offset/scale and the current colour at call time
    struct UIBatchContext
    {
        LLVector3 offset;
        LLVector3 scale;
        LLColor4U color;
    };

    // [S24] LLRender::transform()'s formula applied by hand: batch vertices bypass gDX.vertex2i()
    inline void applyUI(F32& x, F32& y, F32& z, const LLVector3& offset, const LLVector3& scale)
    {
        x = (x + offset.mV[VX]) * scale.mV[VX];
        y = (y + offset.mV[VY]) * scale.mV[VY];
        z = (z + offset.mV[VZ]) * scale.mV[VZ];
    }

    // [S24] packs one UI batch vertex
    inline DXUIVertex makeVertex(F32 x, F32 y, F32 z, const LLColor4U& color, F32 u, F32 v)
    {
        DXUIVertex vert;
        vert.pos[0] = x; vert.pos[1] = y; vert.pos[2] = z;
        vert.color[0] = color.mV[0]; vert.color[1] = color.mV[1]; vert.color[2] = color.mV[2]; vert.color[3] = color.mV[3];
        vert.uv[0] = u; vert.uv[1] = v;
        return vert;
    }

    // [S24] makeVertex() with the UI offset and scale applied, explicit colour
    inline DXUIVertex makeUIVertex(F32 x, F32 y, F32 z, const LLColor4U& color, F32 u, F32 v, const LLVector3& offset, const LLVector3& scale)
    {
        applyUI(x, y, z, offset, scale);
        return makeVertex(x, y, z, color, u, v);
    }

    // [S24] makeUIVertex() for the common case: the batch's own colour, UI offset and UI scale
    inline DXUIVertex makeUIVertex(F32 x, F32 y, F32 z, F32 u, F32 v, const UIBatchContext& ctx)
    {
        applyUI(x, y, z, ctx.offset, ctx.scale);
        return makeVertex(x, y, z, ctx.color, u, v);
    }

    // [S24] drains pending geometry, lets `build` push its vertices, syncs the matrices and submits with the bound
    // shader (does nothing when no shader is bound)
    template <typename Build>
    void drawBatch(DXUITopology topology, Build&& build)
    {
        LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
        if (!shader)
        {
            return;
        }

        gDX.flush();
        gDXUIBatch.flushPending();
        build(UIBatchContext{ gDX.getUITranslation(), gDX.getUIScale(), gDX.getCurrentColor() });
        gDX.syncMatrices();
        if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
        {
            gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), topology);
        }
    }

    // [S24] unbinds texture unit 0 before untextured geometry
    inline void unbindTexture0()
    {
        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    }
}

// +--------------------------------------------------------------------------+
// | BUILDERS - fill vertex data for one primitive (called from drawBatch())  |
// +--------------------------------------------------------------------------+

namespace DXRender2DUtils
{
    // [S24] vertex data of one scaled quad (6 vertices)
    struct ScaledImageGeometry
    {
        float pos[6][3];
        float uv[6][2];
    };

    // [S24] vertex data of a nine-slice image (9 quads)
    struct NineSliceGeometry
    {
        static constexpr int NUM_VERTICES = 9 * 2 * 3;
        float pos[NUM_VERTICES][3];
        float uv[NUM_VERTICES][2];
    };

    // [S24] filled or outlined rectangle
    void rectTwoD(S32 left, S32 top, S32 right, S32 bottom, bool filled, const UIBatchContext& ctx)
    {
        if (filled)
        {
            DXUIVertex verts[6] = {
                makeUIVertex((F32)left, (F32)top, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)left, (F32)bottom, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)right, (F32)bottom, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)left, (F32)top, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)right, (F32)bottom, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)right, (F32)top, 0.f, 0.f, 0.f, ctx),
            };
            gDXUIBatch.push(verts, 6);
        }
        else
        {
            // outlines shrink top/right by 1, as upstream does
            S32 t = top - 1;
            S32 r = right - 1;
            DXUIVertex verts[5] = {
                makeUIVertex((F32)left, (F32)t, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)left, (F32)bottom, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)r, (F32)bottom, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)r, (F32)t, 0.f, 0.f, 0.f, ctx),
                makeUIVertex((F32)left, (F32)t, 0.f, 0.f, 0.f, ctx),
            };
            gDXUIBatch.push(verts, 5);
        }
    }

    // [S24] diagonal cross across a rectangle
    void drawX(const LLRect& rect, const LLColor4& color, const UIBatchContext& ctx)
    {
        LLColor4U c(color);
        DXUIVertex verts[4] = {
            makeUIVertex((F32)rect.mLeft, (F32)rect.mTop, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
            makeUIVertex((F32)rect.mRight, (F32)rect.mBottom, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
            makeUIVertex((F32)rect.mLeft, (F32)rect.mBottom, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
            makeUIVertex((F32)rect.mRight, (F32)rect.mTop, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
        };
        gDXUIBatch.push(verts, 4);
    }

    // [S24] shadow strips along the right and bottom edges, fading from start_color
    void dropShadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines, const UIBatchContext& ctx)
    {
        right--; bottom++; lines++;

        LLColor4 end_color = start_color;
        end_color.mV[VALPHA] = 0.f;
        LLColor4U sc(start_color);
        LLColor4U ec(end_color);

        std::vector<DXUIVertex> v;
        v.reserve(30);
        auto emit = [&](S32 x, S32 y, const LLColor4U& c)
        {
            v.push_back(makeUIVertex((F32)x, (F32)y, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale));
        };

        // Right edge, CCW
        emit(right, top - lines, sc);
        emit(right, bottom, sc);
        emit(right + lines, bottom, ec);
        emit(right, top - lines, sc);
        emit(right + lines, bottom, ec);
        emit(right + lines, top - lines, ec);

        // Bottom edge, CCW
        emit(right, bottom, sc);
        emit(left + lines, bottom, sc);
        emit(left + lines, bottom - lines, ec);
        emit(right, bottom, sc);
        emit(left + lines, bottom - lines, ec);
        emit(right, bottom - lines, ec);

        // bottom left corner
        emit(left + lines, bottom, sc);
        emit(left, bottom, ec);
        emit(left + 1, bottom - lines + 1, ec);
        emit(left + lines, bottom, sc);
        emit(left + 1, bottom - lines + 1, ec);
        emit(left + lines, bottom - lines, ec);

        // bottom right corner
        emit(right, bottom, sc);
        emit(right, bottom - lines, ec);
        emit(right + lines - 1, bottom - lines + 1, ec);
        emit(right, bottom, sc);
        emit(right + lines - 1, bottom - lines + 1, ec);
        emit(right + lines, bottom, ec);

        // top right corner
        emit(right, top - lines, sc);
        emit(right + lines, top - lines, ec);
        emit(right + lines - 1, top - 1, ec);
        emit(right, top - lines, sc);
        emit(right + lines - 1, top - 1, ec);
        emit(right, top, ec);

        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] one 2D line
    void lineTwoD(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4U& color, const UIBatchContext& ctx)
    {
        DXUIVertex verts[2] = {
            makeUIVertex((F32)x1, (F32)y1, 0.f, color, 0.f, 0.f, ctx.offset, ctx.scale),
            makeUIVertex((F32)x2, (F32)y2, 0.f, color, 0.f, 0.f, ctx.offset, ctx.scale),
        };
        gDXUIBatch.push(verts, 2);
    }

    // [S24] one 3D line
    void lineThreeD(const LLVector3& start, const LLVector3& end, const LLColor4& color, const UIBatchContext& ctx)
    {
        LLColor4U c(color);
        DXUIVertex verts[2] = {
            makeUIVertex(start.mV[VX], start.mV[VY], start.mV[VZ], c, 0.f, 0.f, ctx.offset, ctx.scale),
            makeUIVertex(end.mV[VX], end.mV[VY], end.mV[VZ], c, 0.f, 0.f, ctx.offset, ctx.scale),
        };
        gDXUIBatch.push(verts, 2);
    }

    // [S24] filled or outlined triangle
    void triangleTwoD(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled, const UIBatchContext& ctx)
    {
        LLColor4U c(color);
        if (filled)
        {
            DXUIVertex verts[3] = {
                makeUIVertex((F32)x1, (F32)y1, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
                makeUIVertex((F32)x2, (F32)y2, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
                makeUIVertex((F32)x3, (F32)y3, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
            };
            gDXUIBatch.push(verts, 3);
        }
        else
        {
            // closed loop: re-close to the first vertex
            DXUIVertex verts[4] = {
                makeUIVertex((F32)x1, (F32)y1, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
                makeUIVertex((F32)x2, (F32)y2, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
                makeUIVertex((F32)x3, (F32)y3, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
                makeUIVertex((F32)x1, (F32)y1, 0.f, c, 0.f, 0.f, ctx.offset, ctx.scale),
            };
            gDXUIBatch.push(verts, 4);
        }
    }

    // [S24] quad from the origin to width x height in the current colour
    void rectTwoDSimple(S32 width, S32 height, const UIBatchContext& ctx)
    {
        DXUIVertex verts[6] = {
            makeUIVertex((F32)width, (F32)height, 0.f, 0.f, 0.f, ctx),
            makeUIVertex(0.f, (F32)height, 0.f, 0.f, 0.f, ctx),
            makeUIVertex(0.f, 0.f, 0.f, 0.f, 0.f, ctx),
            makeUIVertex((F32)width, (F32)height, 0.f, 0.f, 0.f, ctx),
            makeUIVertex(0.f, 0.f, 0.f, 0.f, 0.f, ctx),
            makeUIVertex((F32)width, 0.f, 0.f, 0.f, 0.f, ctx),
        };
        gDXUIBatch.push(verts, 6);
    }

    // [S24] the same quad with texture coordinates 0..1
    void rectTwoDSimpleTex(S32 width, S32 height, const UIBatchContext& ctx)
    {
        DXUIVertex verts[6] = {
            makeUIVertex((F32)width, (F32)height, 0.f, 1.f, 1.f, ctx),
            makeUIVertex(0.f, (F32)height, 0.f, 0.f, 1.f, ctx),
            makeUIVertex(0.f, 0.f, 0.f, 0.f, 0.f, ctx),
            makeUIVertex((F32)width, (F32)height, 0.f, 1.f, 1.f, ctx),
            makeUIVertex(0.f, 0.f, 0.f, 0.f, 0.f, ctx),
            makeUIVertex((F32)width, 0.f, 0.f, 1.f, 0.f, ctx),
        };
        gDXUIBatch.push(verts, 6);
    }

    // [S24] arc or pie slice; the center is baked into each vertex instead of pushing a UI translation
    void arcTwoD(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle, const UIBatchContext& ctx)
    {
        if (end_angle < start_angle)
        {
            end_angle += kTwoPi;
        }

        F32 delta = (end_angle - start_angle) / steps;
        F32 sin_delta = sinf(delta);
        F32 cos_delta = cosf(delta);
        F32 x = cosf(start_angle) * radius;
        F32 y = sinf(start_angle) * radius;

        S32 rim_steps = filled ? steps + 1 : steps;
        std::vector<LLVector2> rim;
        rim.reserve(rim_steps);
        for (S32 i = 0; i < rim_steps; ++i)
        {
            rim.emplace_back(x, y);
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta + y * cos_delta;
            x = x_new;
        }

        std::vector<DXUIVertex> v;
        if (filled)
        {
            // triangle fan (center + rim) as a flat list: D3D11 has no fan topology
            if (rim.size() >= 2)
            {
                v.reserve((rim.size() - 1) * 3);
                for (size_t i = 0; i + 1 < rim.size(); ++i)
                {
                    v.push_back(makeUIVertex(center_x, center_y, 0.f, 0.f, 0.f, ctx));
                    v.push_back(makeUIVertex(center_x + rim[i].mV[VX], center_y + rim[i].mV[VY], 0.f, 0.f, 0.f, ctx));
                    v.push_back(makeUIVertex(center_x + rim[i + 1].mV[VX], center_y + rim[i + 1].mV[VY], 0.f, 0.f, 0.f, ctx));
                }
            }
        }
        else
        {
            v.reserve(rim.size());
            for (const LLVector2& p : rim)
            {
                v.push_back(makeUIVertex(center_x + p.mV[VX], center_y + p.mV[VY], 0.f, 0.f, 0.f, ctx));
            }
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] circle or disc
    void circleTwoD(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, const UIBatchContext& ctx)
    {
        F32 delta = kTwoPi / steps;
        F32 sin_delta = sinf(delta);
        F32 cos_delta = cosf(delta);
        F32 x = radius;
        F32 y = 0.f;

        S32 rim_steps = filled ? steps + 1 : steps;
        std::vector<LLVector2> rim;
        rim.reserve(rim_steps);
        for (S32 i = 0; i < rim_steps; ++i)
        {
            rim.emplace_back(x, y);
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta + y * cos_delta;
            x = x_new;
        }

        std::vector<DXUIVertex> v;
        if (filled)
        {
            if (rim.size() >= 2)
            {
                v.reserve((rim.size() - 1) * 3);
                for (size_t i = 0; i + 1 < rim.size(); ++i)
                {
                    v.push_back(makeUIVertex(center_x, center_y, 0.f, 0.f, 0.f, ctx));
                    v.push_back(makeUIVertex(center_x + rim[i].mV[VX], center_y + rim[i].mV[VY], 0.f, 0.f, 0.f, ctx));
                    v.push_back(makeUIVertex(center_x + rim[i + 1].mV[VX], center_y + rim[i + 1].mV[VY], 0.f, 0.f, 0.f, ctx));
                }
            }
        }
        else
        {
            v.reserve(rim.size() + 1);
            for (const LLVector2& p : rim)
            {
                v.push_back(makeUIVertex(center_x + p.mV[VX], center_y + p.mV[VY], 0.f, 0.f, 0.f, ctx));
            }
            // closed loop: duplicate the first vertex
            if (!rim.empty())
            {
                v.push_back(makeUIVertex(center_x + rim[0].mV[VX], center_y + rim[0].mV[VY], 0.f, 0.f, 0.f, ctx));
            }
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] ring with sides (tube shape)
    void deepCircle(F32 radius, F32 depth, S32 steps, const UIBatchContext& ctx)
    {
        F32 x = radius;
        F32 y = 0.f;
        F32 angle_delta = kTwoPi / (F32)steps;
        S32 count = steps + 1; // extra step closes the circle

        std::vector<DXUIVertex> v;
        v.reserve((size_t)count * 2);
        for (S32 i = 0; i < count; ++i)
        {
            v.push_back(makeUIVertex(x, y, depth, 0.f, 0.f, ctx));
            v.push_back(makeUIVertex(x, y, 0.f, 0.f, 0.f, ctx));

            F32 x_new = x * cosf(angle_delta) - y * sinf(angle_delta);
            y = x * sinf(angle_delta) + y * cosf(angle_delta);
            x = x_new;
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] area between two concentric circles, inner to outer colour
    void washerTwoD(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color, const UIBatchContext& ctx)
    {
        LLColor4U ic(inner_color), oc(outer_color);
        F32 delta = kTwoPi / steps;
        F32 sin_delta = sinf(delta);
        F32 cos_delta = cosf(delta);
        F32 x1 = outer_radius, y1 = 0.f;
        F32 x2 = inner_radius, y2 = 0.f;
        S32 count = steps + 1;

        std::vector<DXUIVertex> v;
        v.reserve((size_t)count * 2);
        for (S32 i = 0; i < count; ++i)
        {
            v.push_back(makeUIVertex(x1, y1, 0.f, oc, 0.f, 0.f, ctx.offset, ctx.scale));
            v.push_back(makeUIVertex(x2, y2, 0.f, ic, 0.f, 0.f, ctx.offset, ctx.scale));

            F32 x1_new = x1 * cos_delta - y1 * sin_delta;
            y1 = x1 * sin_delta + y1 * cos_delta;
            x1 = x1_new;

            F32 x2_new = x2 * cos_delta - y2 * sin_delta;
            y2 = x2 * sin_delta + y2 * cos_delta;
            x2 = x2_new;
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] washerTwoD() limited to an angle range
    void washerSegmentTwoD(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color, const UIBatchContext& ctx)
    {
        LLColor4U ic(inner_color), oc(outer_color);
        F32 delta = (end_radians - start_radians) / steps;
        F32 sin_delta = sinf(delta);
        F32 cos_delta = cosf(delta);
        F32 x1 = outer_radius * cosf(start_radians), y1 = outer_radius * sinf(start_radians);
        F32 x2 = inner_radius * cosf(start_radians), y2 = inner_radius * sinf(start_radians);
        S32 count = steps + 1;

        std::vector<DXUIVertex> v;
        v.reserve((size_t)count * 2);
        for (S32 i = 0; i < count; ++i)
        {
            v.push_back(makeUIVertex(x1, y1, 0.f, oc, 0.f, 0.f, ctx.offset, ctx.scale));
            v.push_back(makeUIVertex(x2, y2, 0.f, ic, 0.f, 0.f, ctx.offset, ctx.scale));

            F32 x1_new = x1 * cos_delta - y1 * sin_delta;
            y1 = x1 * sin_delta + y1 * cos_delta;
            x1 = x1_new;

            F32 x2_new = x2 * cos_delta - y2 * sin_delta;
            y2 = x2 * sin_delta + y2 * cos_delta;
            x2 = x2_new;
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] submits one scaled textured quad
    void drawScaledImage(const ScaledImageGeometry& geom, const LLColor4& color)
    {
        LLColor4U c(color);
        DXUIVertex verts[6];
        for (int i = 0; i < 6; ++i)
        {
            verts[i] = makeVertex(geom.pos[i][0], geom.pos[i][1], geom.pos[i][2], c, geom.uv[i][0], geom.uv[i][1]);
        }
        gDXUIBatch.push(verts, 6);
    }

    // [S24] submits a nine-slice textured image
    void drawScaledImageWithBorderNineSlice(const NineSliceGeometry& geom, const LLColor4& color)
    {
        LLColor4U c(color);
        DXUIVertex verts[NineSliceGeometry::NUM_VERTICES];
        for (int i = 0; i < NineSliceGeometry::NUM_VERTICES; ++i)
        {
            verts[i] = makeVertex(geom.pos[i][0], geom.pos[i][1], geom.pos[i][2], c, geom.uv[i][0], geom.uv[i][1]);
        }
        gDXUIBatch.push(verts, NineSliceGeometry::NUM_VERTICES);
    }

    // [S24] textured rounded-rectangle fragment between two fragment positions, edges chosen by mask
    void segmentedRectTwoDFragmentTex(const LLRect& rect, S32 texture_width, S32 texture_height, S32 border_size,
        F32 start_fragment, F32 end_fragment, U32 edges, const UIBatchContext& ctx)
    {
        const S32 left = rect.mLeft;
        const S32 right = rect.mRight;
        const S32 top = rect.mTop;
        const S32 bottom = rect.mBottom;
        S32 width = llabs(right - left);
        S32 height = llabs(top - bottom);

        // local origin added to every position before the UI offset/scale (upstream pushed and translated the UI
        // matrix)
        const F32 origin_x = (F32)left;
        const F32 origin_y = (F32)bottom;

        LLVector2 border_uv_scale((F32)border_size / (F32)texture_width, (F32)border_size / (F32)texture_height);
        if (border_uv_scale.mV[VX] > 0.5f) { border_uv_scale *= 0.5f / border_uv_scale.mV[VX]; }
        if (border_uv_scale.mV[VY] > 0.5f) { border_uv_scale *= 0.5f / border_uv_scale.mV[VY]; }

        F32 border_scale = llmin((F32)border_size, (F32)width * 0.5f, (F32)height * 0.5f);
        LLVector2 border_width_left = ((edges & (~kRoundedRectRight)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
        LLVector2 border_width_right = ((edges & (~kRoundedRectLeft)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
        LLVector2 border_height_bottom = ((edges & (~kRoundedRectTop)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
        LLVector2 border_height_top = ((edges & (~kRoundedRectBottom)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
        LLVector2 width_vec((F32)width, 0.f);
        LLVector2 height_vec(0.f, (F32)height);

        F32 middle_start = border_scale / (F32)width;
        F32 middle_end = 1.f - middle_start;

        std::vector<DXUIVertex> v;
        v.reserve(54);
        auto emit = [&](F32 u, F32 uv_v, const LLVector2& pos)
        {
            v.push_back(makeUIVertex(origin_x + pos.mV[VX], origin_y + pos.mV[VY], 0.f, u, uv_v, ctx));
        };

        if (start_fragment < middle_start)
        {
            F32 u_min = (start_fragment / middle_start) * border_uv_scale.mV[VX];
            F32 u_max = llmin(end_fragment / middle_start, 1.f) * border_uv_scale.mV[VX];
            LLVector2 x_min = (start_fragment / middle_start) * border_width_left;
            LLVector2 x_max = llmin(end_fragment / middle_start, 1.f) * border_width_left;

            // bottom left
            emit(u_min, 0.f, x_min);
            emit(border_uv_scale.mV[VX], 0.f, x_max);
            emit(u_max, border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(u_min, 0.f, x_min);
            emit(u_max, border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(u_min, border_uv_scale.mV[VY], x_min + border_height_bottom);

            // left
            emit(u_min, border_uv_scale.mV[VY], x_min + border_height_bottom);
            emit(u_max, border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(u_max, 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(u_min, border_uv_scale.mV[VY], x_min + border_height_bottom);
            emit(u_max, 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(u_min, 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);

            // top left
            emit(u_min, 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);
            emit(u_max, 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(u_max, 1.f, x_max + height_vec);
            emit(u_min, 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);
            emit(u_max, 1.f, x_max + height_vec);
            emit(u_min, 1.f, x_min + height_vec);
        }

        if (end_fragment > middle_start || start_fragment < middle_end)
        {
            LLVector2 x_min = border_width_left + ((llclamp(start_fragment, middle_start, middle_end) - middle_start)) * width_vec;
            LLVector2 x_max = border_width_left + ((llclamp(end_fragment, middle_start, middle_end) - middle_start)) * width_vec;

            // bottom middle
            emit(border_uv_scale.mV[VX], 0.f, x_min);
            emit(1.f - border_uv_scale.mV[VX], 0.f, x_max);
            emit(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(border_uv_scale.mV[VX], 0.f, x_min);
            emit(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(border_uv_scale.mV[VX], border_uv_scale.mV[VY], x_min + border_height_bottom);

            // middle
            emit(border_uv_scale.mV[VX], border_uv_scale.mV[VY], x_min + border_height_bottom);
            emit(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(border_uv_scale.mV[VX], border_uv_scale.mV[VY], x_min + border_height_bottom);
            emit(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);

            // top middle
            emit(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);
            emit(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(1.f - border_uv_scale.mV[VX], 1.f, x_max + height_vec);
            emit(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);
            emit(1.f - border_uv_scale.mV[VX], 1.f, x_max + height_vec);
            emit(border_uv_scale.mV[VX], 1.f, x_min + height_vec);
        }

        if (end_fragment > middle_end)
        {
            F32 u_min = 1.f - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
            F32 u_max = 1.f - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
            LLVector2 x_min = width_vec - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_width_right);
            LLVector2 x_max = width_vec - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_width_right);

            // bottom right
            emit(u_min, 0.f, x_min);
            emit(u_max, 0.f, x_max);
            emit(u_max, border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(u_min, 0.f, x_min);
            emit(u_max, border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(u_min, border_uv_scale.mV[VY], x_min + border_height_bottom);

            // right
            emit(u_min, border_uv_scale.mV[VY], x_min + border_height_bottom);
            emit(u_max, border_uv_scale.mV[VY], x_max + border_height_bottom);
            emit(u_max, 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(u_min, border_uv_scale.mV[VY], x_min + border_height_bottom);
            emit(u_max, 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(u_min, 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);

            // top right
            emit(u_min, 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);
            emit(u_max, 1.f - border_uv_scale.mV[VY], x_max + height_vec - border_height_top);
            emit(u_max, 1.f, x_max + height_vec);
            emit(u_min, 1.f - border_uv_scale.mV[VY], x_min + height_vec - border_height_top);
            emit(u_max, 1.f, x_max + height_vec);
            emit(u_min, 1.f, x_min + height_vec);
        }

        gDXUIBatch.push(v.data(), v.size());
    }

    // [S24] textured segmented rectangle in world space (the nametag panel)
    void segmentedRectThreeDTex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
        const LLVector3& width_vec, const LLVector3& height_vec, const UIBatchContext& ctx)
    {
        std::vector<DXUIVertex> v;
        v.reserve(54);
        auto emit = [&](F32 u, F32 uv_v, const LLVector3& pos)
        {
            v.push_back(makeUIVertex(pos.mV[VX], pos.mV[VY], pos.mV[VZ], u, uv_v, ctx));
        };
        const LLVector3 zero = LLVector3::zero;

        // bottom left
        emit(clip_rect.mLeft, clip_rect.mBottom, zero);
        emit(center_uv_rect.mLeft, clip_rect.mBottom, center_draw_rect.mLeft * width_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec);
        emit(clip_rect.mLeft, clip_rect.mBottom, zero);
        emit(center_uv_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec);
        emit(clip_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mBottom * height_vec);

        // bottom middle
        emit(center_uv_rect.mLeft, clip_rect.mBottom, center_draw_rect.mLeft * width_vec);
        emit(center_uv_rect.mRight, clip_rect.mBottom, center_draw_rect.mRight * width_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mBottom, center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mLeft, clip_rect.mBottom, center_draw_rect.mLeft * width_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mBottom, center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec);

        // bottom right
        emit(center_uv_rect.mRight, clip_rect.mBottom, center_draw_rect.mRight * width_vec);
        emit(clip_rect.mRight, clip_rect.mBottom, width_vec);
        emit(clip_rect.mRight, center_uv_rect.mBottom, width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mRight, clip_rect.mBottom, center_draw_rect.mRight * width_vec);
        emit(clip_rect.mRight, center_uv_rect.mBottom, width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mBottom, center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec);

        // left
        emit(clip_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec);
        emit(clip_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec);
        emit(clip_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mTop * height_vec);

        // middle
        emit(center_uv_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mBottom, center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mTop, center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mBottom, center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mTop, center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec);

        // right
        emit(center_uv_rect.mRight, center_uv_rect.mBottom, center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec);
        emit(clip_rect.mRight, center_uv_rect.mBottom, width_vec + center_draw_rect.mBottom * height_vec);
        emit(clip_rect.mRight, center_uv_rect.mTop, width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mBottom, center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec);
        emit(clip_rect.mRight, center_uv_rect.mTop, width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mTop, center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec);

        // top left
        emit(clip_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mLeft, clip_rect.mTop, center_draw_rect.mLeft * width_vec + height_vec);
        emit(clip_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mLeft, clip_rect.mTop, center_draw_rect.mLeft * width_vec + height_vec);
        emit(clip_rect.mLeft, clip_rect.mTop, height_vec);

        // top middle
        emit(center_uv_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mTop, center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mRight, clip_rect.mTop, center_draw_rect.mRight * width_vec + height_vec);
        emit(center_uv_rect.mLeft, center_uv_rect.mTop, center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec);
        emit(center_uv_rect.mRight, clip_rect.mTop, center_draw_rect.mRight * width_vec + height_vec);
        emit(center_uv_rect.mLeft, clip_rect.mTop, center_draw_rect.mLeft * width_vec + height_vec);

        // top right
        emit(center_uv_rect.mRight, center_uv_rect.mTop, center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec);
        emit(clip_rect.mRight, center_uv_rect.mTop, width_vec + center_draw_rect.mTop * height_vec);
        emit(clip_rect.mRight, clip_rect.mTop, width_vec + height_vec);
        emit(center_uv_rect.mRight, center_uv_rect.mTop, center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec);
        emit(clip_rect.mRight, clip_rect.mTop, width_vec + height_vec);
        emit(center_uv_rect.mRight, clip_rect.mTop, center_draw_rect.mRight * width_vec + height_vec);

        gDXUIBatch.push(v.data(), v.size());
    }
}

// +--------------------------------------------------------------------------+
// | DRAWING API - 2D STATE                                                   |
// +--------------------------------------------------------------------------+

// [LL] the UI colour every image defaults to: opaque white
const LLColor4 UI_VERTEX_COLOR(1.f, 1.f, 1.f, 1.f);

// [LL] sets a pixel-space orthographic projection (width x height) and an identity modelview
void dx_state_for_2d(S32 width, S32 height)
{
    const F32 w = (F32)width;
    const F32 h = (F32)height;

    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.loadIdentity();
    gDX.ortho(0.f, (w > 1.f ? w : 1.f), 0.f, (h > 1.f ? h : 1.f), -1.f, 1.f);
    gDX.matrixMode(LLRender::MM_MODELVIEW);
    gDX.loadIdentity();
}

// +--------------------------------------------------------------------------+
// | DRAWING API - LINES AND TRIANGLES                                        |
// +--------------------------------------------------------------------------+

// [S24] 2D line in the current colour
void dx_line_2d(S32 x1, S32 y1, S32 x2, S32 y2)
{
    unbindTexture0();
    drawBatch(DXUITopology::LineList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::lineTwoD(x1, y1, x2, y2, c.color, c);
    });
}

// [S24] 2D line in `color`
void dx_line_2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4& color)
{
    unbindTexture0();
    drawBatch(DXUITopology::LineList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::lineTwoD(x1, y1, x2, y2, LLColor4U(color), c);
    });
}

// [S24] filled or outlined triangle
void dx_triangle_2d(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled)
{
    unbindTexture0();
    drawBatch(filled ? DXUITopology::TriangleList : DXUITopology::LineStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::triangleTwoD(x1, y1, x2, y2, x3, y3, color, filled, c);
    });
}

// [S24] 3D line in `color`
void dx_line_3d(const LLVector3& start, const LLVector3& end, const LLColor4& color)
{
    drawBatch(DXUITopology::LineList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::lineThreeD(start, end, color, c);
    });
}

// [S24] diagonal cross across a rectangle
void dx_draw_x(const LLRect& rect, const LLColor4& color)
{
    unbindTexture0();
    drawBatch(DXUITopology::LineList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::drawX(rect, color, c);
    });
}

// +--------------------------------------------------------------------------+
// | DRAWING API - RECTANGLES                                                 |
// +--------------------------------------------------------------------------+

// [S24] filled or outlined rectangle; the colour-taking overloads set the current colour first, so drawBatch()
// reads the right one whichever entry point was used
void dx_rect_2d(S32 left, S32 top, S32 right, S32 bottom, bool filled)
{
    unbindTexture0();
    drawBatch(filled ? DXUITopology::TriangleList : DXUITopology::LineStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::rectTwoD(left, top, right, bottom, filled, c);
    });
}

// [S24] dx_rect_2d() in `color`
void dx_rect_2d(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color, bool filled)
{
    gDX.color4fv(color.mV);
    dx_rect_2d(left, top, right, bottom, filled);
}

// [S24] dx_rect_2d() of an LLRect in `color`
void dx_rect_2d(const LLRect& rect, const LLColor4& color, bool filled)
{
    gDX.color4fv(color.mV);
    dx_rect_2d(rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, filled);
}

// [LL] dx_rect_2d_offset_local() in `color`
void dx_rect_2d_offset_local(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color, S32 pixel_offset, bool filled)
{
    gDX.color4fv(color.mV);
    dx_rect_2d_offset_local(left, top, right, bottom, pixel_offset, filled);
}

// [LL] rectangle in the font origin's local space, scaled to GL pixels and grown by pixel_offset
void dx_rect_2d_offset_local(S32 left, S32 top, S32 right, S32 bottom, S32 pixel_offset, bool filled)
{
    gDX.pushUIMatrix();
    left += LLFontDX::sCurOrigin.mX;
    right += LLFontDX::sCurOrigin.mX;
    bottom += LLFontDX::sCurOrigin.mY;
    top += LLFontDX::sCurOrigin.mY;

    gDX.loadUIIdentity();
    dx_rect_2d(llfloor((F32)left * LLRender::sUIGLScaleFactor.mV[VX]) - pixel_offset,
        llfloor((F32)top * LLRender::sUIGLScaleFactor.mV[VY]) + pixel_offset,
        llfloor((F32)right * LLRender::sUIGLScaleFactor.mV[VX]) + pixel_offset,
        llfloor((F32)bottom * LLRender::sUIGLScaleFactor.mV[VY]) - pixel_offset,
        filled);
    gDX.popUIMatrix();
}

// [LL] gray and white checkerboard (the tiled "Checker" UI image) over a rectangle
void dx_rect_2d_checkerboard(const LLRect& rect, F32 alpha)
{
    LLPointer<LLUIImage> img = DXRender2D::getInstance()->getUIImage("Checker");
    gDX.getTexUnit(0)->bind(img->getImage());
    // the sampler state is read from the texture object at bind time, so set it there
    img->getImage()->setAddressMode(LLTexUnit::TAM_WRAP);
    img->getImage()->setFilteringOption(LLTexUnit::TFO_POINT);

    LLColor4 color(1.f, 1.f, 1.f, alpha);
    LLRectf uv_rect(0, 0, rect.getWidth() / 32.f, rect.getHeight() / 32.f);

    dx_draw_scaled_image(rect.mLeft, rect.mBottom, rect.getWidth(), rect.getHeight(), img->getImage(), color, uv_rect);

    gDX.flush();
}

// [LL] drop shadow outside the right and bottom edges, `lines` wide and high; the builder applies the one-pixel
// overlap adjustment
void dx_drop_shadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines)
{
    unbindTexture0();
    drawBatch(DXUITopology::TriangleList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::dropShadow(left, top, right, bottom, start_color, lines, c);
    });
}

// [S24] quad from the origin to width x height in the current colour
void dx_rect_2d_simple(S32 width, S32 height)
{
    drawBatch(DXUITopology::TriangleList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::rectTwoDSimple(width, height, c);
    });
}

// [S24] the same quad with texture coordinates 0..1
void dx_rect_2d_simple_tex(S32 width, S32 height)
{
    drawBatch(DXUITopology::TriangleList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::rectTwoDSimpleTex(width, height, c);
    });
}

// [S24] dx_rect_2d_simple() in plain gDX immediate mode, so the caller's real blend mode and colour write mask apply
// (drawBatch() paths hardcode alpha blending)
void dx_rect_2d_simple_immediate(S32 width, S32 height)
{
    gDX.begin(LLRender::TRIANGLES);
        gDX.vertex2i(width, 0);
        gDX.vertex2i(0, 0);
        gDX.vertex2i(0, height);

        gDX.vertex2i(width, 0);
        gDX.vertex2i(0, height);
        gDX.vertex2i(width, height);
    gDX.end();
    gDX.flush();
}

// [S24] dx_rect_2d_simple_tex() in plain gDX immediate mode, for the avatar-bake compositor (lltexlayer.cpp). Y positions
// (not texcoords) are swapped 0 <-> height: a literal port came out vertically mirrored, and every layer
// composites through here, so flipping it here mirrors the whole bake uniformly
void dx_rect_2d_simple_tex_immediate(S32 width, S32 height)
{
    gDX.begin(LLRender::TRIANGLES);

        gDX.texCoord2f(1.f, 1.f);
        gDX.vertex2i(width, 0);

        gDX.texCoord2f(0.f, 1.f);
        gDX.vertex2i(0, 0);

        gDX.texCoord2f(0.f, 0.f);
        gDX.vertex2i(0, height);

        gDX.texCoord2f(1.f, 1.f);
        gDX.vertex2i(width, 0);

        gDX.texCoord2f(0.f, 0.f);
        gDX.vertex2i(0, height);

        gDX.texCoord2f(1.f, 0.f);
        gDX.vertex2i(width, height);

    gDX.end();
    gDX.flush();
}

// +--------------------------------------------------------------------------+
// | DRAWING API - ROUND SHAPES                                               |
// +--------------------------------------------------------------------------+

// [S24] circle or disc
void dx_circle_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled)
{
    unbindTexture0();
    drawBatch(filled ? DXUITopology::TriangleList : DXUITopology::LineStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::circleTwoD(center_x, center_y, radius, steps, filled, c);
    });
}

// [S24] arc or pie slice; the builder bakes the center into each vertex instead of pushing a UI translation
void dx_arc_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle)
{
    drawBatch(filled ? DXUITopology::TriangleList : DXUITopology::LineStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::arcTwoD(center_x, center_y, radius, steps, filled, start_angle, end_angle, c);
    });
}

// [S24] ring with sides (tube shape) of `depth`
void dx_deep_circle(F32 radius, F32 depth, S32 steps)
{
    drawBatch(DXUITopology::TriangleStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::deepCircle(radius, depth, steps, c);
    });
}

// [LL] ring with sides (tube shape): a solid deep circle when render_center, else two washers
void dx_ring(F32 radius, F32 width, const LLColor4& center_color, const LLColor4& side_color, S32 steps, bool render_center)
{
    gDX.pushUIMatrix();
    {
        gDX.translateUI(0.f, 0.f, -width / 2);
        if (render_center)
        {
            gDX.color4fv(center_color.mV);
            gDX.diffuseColor4fv(center_color.mV);
            dx_deep_circle(radius, width, steps);
        }
        else
        {
            gDX.diffuseColor4fv(side_color.mV);
            dx_washer_2d(radius, radius - width, steps, side_color, side_color);
            gDX.translateUI(0.f, 0.f, width);
            dx_washer_2d(radius - width, radius, steps, side_color, side_color);
        }
    }
    gDX.popUIMatrix();
}

// [LL] the area between two concentric circles, like a doughnut or washer
void dx_washer_2d(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    unbindTexture0();
    drawBatch(DXUITopology::TriangleStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::washerTwoD(outer_radius, inner_radius, steps, inner_color, outer_color, c);
    });
}

// [LL] dx_washer_2d() limited to start_radians..end_radians
void dx_washer_segment_2d(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    unbindTexture0();
    drawBatch(DXUITopology::TriangleStrip, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::washerSegmentTwoD(outer_radius, inner_radius, start_radians, end_radians, steps, inner_color, outer_color, c);
    });
}

// +--------------------------------------------------------------------------+
// | DRAWING API - SEGMENTED RECTANGLES                                       |
// +--------------------------------------------------------------------------+

// [LL] textured rounded-rectangle fragment; the builder replicates the original's own UI matrix push and translate
void dx_segmented_rect_2d_fragment_tex(const LLRect& rect,
    const S32 texture_width,
    const S32 texture_height,
    const S32 border_size,
    const F32 start_fragment,
    const F32 end_fragment,
    const U32 edges)
{
    drawBatch(DXUITopology::TriangleList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::segmentedRectTwoDFragmentTex(rect, texture_width, texture_height, border_size,
            start_fragment, end_fragment, edges, c);
    });
}

// [LL] world-space segmented rectangle (the nametag panel). Depth testing is deliberately left off: nametag occlusion is
// done with occlusion queries, and passing depth through to DXUIBatch::flush() caused a regression. Untested
// live if ever re-attempted.
void dx_segmented_rect_3d_tex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
    const LLVector3& width_vec, const LLVector3& height_vec)
{
    drawBatch(DXUITopology::TriangleList, [&](const UIBatchContext& c)
    {
        DXRender2DUtils::segmentedRectThreeDTex(clip_rect, center_uv_rect, center_draw_rect, width_vec, height_vec, c);
    });
}

// +--------------------------------------------------------------------------+
// | DRAWING API - IMAGES                                                     |
// +--------------------------------------------------------------------------+

// [LL] image at x,y at its own size
void dx_draw_image(S32 x, S32 y, DXTextureBase* image, const LLColor4& color, const LLRectf& uv_rect)
{
    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }
    dx_draw_scaled_rotated_image(x, y, image->getWidth(0), image->getHeight(0), 0.f, image, color, uv_rect);
}

// [LL] render target scaled to a rectangle
void dx_draw_scaled_target(S32 x, S32 y, S32 width, S32 height, DXRenderTarget* target, const LLColor4& color, const LLRectf& uv_rect)
{
    dx_draw_scaled_rotated_image(x, y, width, height, 0.f, NULL, color, uv_rect, target);
}

// [LL] image scaled to a rectangle
void dx_draw_scaled_image(S32 x, S32 y, S32 width, S32 height, DXTextureBase* image, const LLColor4& color, const LLRectf& uv_rect)
{
    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }
    dx_draw_scaled_rotated_image(x, y, width, height, 0.f, image, color, uv_rect);
}

// [LL] scaled image with fixed-size borders and a stretched center (border width/height form)
void dx_draw_scaled_image_with_border(S32 x, S32 y, S32 border_width, S32 border_height, S32 width, S32 height, DXTextureBase* image, const LLColor4& color, bool solid_color, const LLRectf& uv_rect, bool scale_inner)
{
    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }

    // scale screen size of borders down
    F32 border_width_fraction = (F32)border_width / (F32)image->getWidth(0);
    F32 border_height_fraction = (F32)border_height / (F32)image->getHeight(0);

    LLRectf scale_rect(border_width_fraction, 1.f - border_height_fraction, 1.f - border_width_fraction, border_height_fraction);
    dx_draw_scaled_image_with_border(x, y, width, height, image, color, solid_color, uv_rect, scale_rect, scale_inner);
}

// [LL] scaled image with borders defined by a scale rect (nine-slice)
void dx_draw_scaled_image_with_border(S32 x, S32 y, S32 width, S32 height, DXTextureBase* image, const LLColor4& color, bool solid_color, const LLRectf& uv_outer_rect, const LLRectf& center_rect, bool scale_inner)
{

    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }

    if (solid_color)
    {
        gSolidColorProgram.bind();
    }

    if (center_rect.mLeft == 0.f
        && center_rect.mRight == 1.f
        && center_rect.mBottom == 0.f
        && center_rect.mTop == 1.f)
    {
        dx_draw_scaled_image(x, y, width, height, image, color, uv_outer_rect);
    }
    else
    {
        // add in offset of current image to current UI translation
        const LLVector3 ui_scale = gDX.getUIScale();
        const LLVector3 ui_translation = (gDX.getUITranslation() + LLVector3((F32)x, (F32)y, 0.f)).scaledVec(ui_scale);

        F32 uv_width = uv_outer_rect.getWidth();
        F32 uv_height = uv_outer_rect.getHeight();

        // shrink scaling region to be proportional to clipped image region
        LLRectf uv_center_rect(uv_outer_rect.mLeft + (center_rect.mLeft * uv_width),
            uv_outer_rect.mBottom + (center_rect.mTop * uv_height),
            uv_outer_rect.mLeft + (center_rect.mRight * uv_width),
            uv_outer_rect.mBottom + (center_rect.mBottom * uv_height));

        F32 image_width = (F32)image->getWidth(0);
        F32 image_height = (F32)image->getHeight(0);

        S32 image_natural_width = ll_round(image_width * uv_width);
        S32 image_natural_height = ll_round(image_height * uv_height);

        LLRectf draw_center_rect(uv_center_rect.mLeft * image_width,
            uv_center_rect.mTop * image_height,
            uv_center_rect.mRight * image_width,
            uv_center_rect.mBottom * image_height);

        if (scale_inner)
        {
            // scale center region of image to drawn region
            draw_center_rect.mRight += width - image_natural_width;
            draw_center_rect.mTop += height - image_natural_height;

            const F32 border_shrink_width = llmax(0.f, draw_center_rect.mLeft - draw_center_rect.mRight);
            const F32 border_shrink_height = llmax(0.f, draw_center_rect.mBottom - draw_center_rect.mTop);

            const F32 shrink_width_ratio = center_rect.getWidth() == 1.f ? 0.f : border_shrink_width / ((F32)image_natural_width * (1.f - center_rect.getWidth()));
            const F32 shrink_height_ratio = center_rect.getHeight() == 1.f ? 0.f : border_shrink_height / ((F32)image_natural_height * (1.f - center_rect.getHeight()));

            const F32 border_shrink_scale = 1.f - llmax(shrink_width_ratio, shrink_height_ratio);
            draw_center_rect.mLeft *= border_shrink_scale;
            draw_center_rect.mTop = lerp((F32)height, (F32)draw_center_rect.mTop, border_shrink_scale);
            draw_center_rect.mRight = lerp((F32)width, (F32)draw_center_rect.mRight, border_shrink_scale);
            draw_center_rect.mBottom *= border_shrink_scale;
        }
        else
        {
            // keep center region of image at fixed scale, but in same relative position
            F32 scale_factor = llmin((F32)width / draw_center_rect.getWidth(), (F32)height / draw_center_rect.getHeight(), 1.f);
            F32 scaled_width = draw_center_rect.getWidth() * scale_factor;
            F32 scaled_height = draw_center_rect.getHeight() * scale_factor;
            draw_center_rect.setCenterAndSize(uv_center_rect.getCenterX() * width, uv_center_rect.getCenterY() * height, scaled_width, scaled_height);
        }

        draw_center_rect.mLeft = (F32)ll_round(ui_translation.mV[VX] + (F32)draw_center_rect.mLeft * ui_scale.mV[VX]);
        draw_center_rect.mTop = (F32)ll_round(ui_translation.mV[VY] + (F32)draw_center_rect.mTop * ui_scale.mV[VY]);
        draw_center_rect.mRight = (F32)ll_round(ui_translation.mV[VX] + (F32)draw_center_rect.mRight * ui_scale.mV[VX]);
        draw_center_rect.mBottom = (F32)ll_round(ui_translation.mV[VY] + (F32)draw_center_rect.mBottom * ui_scale.mV[VY]);

        LLRectf draw_outer_rect(ui_translation.mV[VX],
            ui_translation.mV[VY] + height * ui_scale.mV[VY],
            ui_translation.mV[VX] + width * ui_scale.mV[VX],
            ui_translation.mV[VY]);

        gDX.getTexUnit(0)->bind(image, true);

        gDX.color4fv(color.mV);

        constexpr S32 NUM_VERTICES = 9 * 2 * 3; // 9 quads, 2 triangles per quad, 3 vertices per triangle
        static thread_local LLVector2 uv[NUM_VERTICES];
        static thread_local LLVector4a pos[NUM_VERTICES];

        S32 index = 0;

        gDX.begin(LLRender::TRIANGLES);
        {
            // draw bottom left triangles
            // 1
            uv[index].set(uv_outer_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            // 2
            uv[index].set(uv_outer_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            // draw bottom middle triangles
            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            // draw bottom right triangles
            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            // draw left triangles
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            // draw middle triangles
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            // draw right triangles
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            // draw top left triangles
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            // draw top middle triangles
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            // draw top right triangles
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            // recording mode feeds gDX so LLUIImage's display-list cache can capture it; otherwise the colour is
            // applied per vertex and submitted as a batch
            if (gDX.isRecording())
            {
                gDX.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
            }
            else if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
            {
                gDX.flush(); // draw order: drain queued gDX geometry before this immediate batch
                DXRender2DUtils::NineSliceGeometry geom;
                for (S32 v = 0; v < NUM_VERTICES; ++v)
                {
                    const F32* p = pos[v].getF32ptr();
                    geom.pos[v][0] = p[0]; geom.pos[v][1] = p[1]; geom.pos[v][2] = p[2];
                    geom.uv[v][0] = uv[v].mV[0]; geom.uv[v][1] = uv[v].mV[1];
                }
                gDXUIBatch.flushPending();
                DXRender2DUtils::drawScaledImageWithBorderNineSlice(geom, color);
                gDX.syncMatrices();
                if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
                {
                    gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
                }
            }
        }
        gDX.end();
    }

    if (solid_color)
    {
        gUIProgram.bind();
    }
}

// [LL] scaled image rotated by `degrees` around its center. The `target` parameter is currently unused (no caller
// passes one and its branch is empty); left in place in case it is wired in or removed later.
void dx_draw_scaled_rotated_image(S32 x, S32 y, S32 width, S32 height, F32 degrees, DXTextureBase* image, const LLColor4& color, const LLRectf& uv_rect, DXRenderTarget* target)
{
    if (!image && !target)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }

    if (image != NULL)
    {
        gDX.getTexUnit(0)->bind(image, true);
    }
    else
    {
        // shadow maps and hero-probe mirrors bind render targets this way
        gDX.getTexUnit(0)->bind(target, false, false);
    }

    gDX.color4fv(color.mV);

    if (degrees == 0.f)
    {
        constexpr S32 NUM_VERTICES = 2 * 3;
        static thread_local LLVector2 uv[NUM_VERTICES + 1];
        static thread_local LLVector4a pos[NUM_VERTICES + 1];

        gDX.begin(LLRender::TRIANGLES);
        {
            LLVector3 ui_scale = gDX.getUIScale();
            LLVector3 ui_translation = gDX.getUITranslation();
            ui_translation.mV[VX] += x;
            ui_translation.mV[VY] += y;
            ui_translation.scaleVec(ui_scale);
            S32 index = 0;
            S32 scaled_width = ll_round(width * ui_scale.mV[VX]);
            S32 scaled_height = ll_round(height * ui_scale.mV[VY]);

            uv[index].set(uv_rect.mRight, uv_rect.mTop);
            pos[index].set(ui_translation.mV[VX] + scaled_width, ui_translation.mV[VY] + scaled_height, 0.f);
            index++;

            uv[index].set(uv_rect.mLeft, uv_rect.mTop);
            pos[index].set(ui_translation.mV[VX], ui_translation.mV[VY] + scaled_height, 0.f);
            index++;

            uv[index].set(uv_rect.mLeft, uv_rect.mBottom);
            pos[index].set(ui_translation.mV[VX], ui_translation.mV[VY], 0.f);
            index++;

            uv[index].set(uv_rect.mRight, uv_rect.mTop);
            pos[index].set(ui_translation.mV[VX] + scaled_width, ui_translation.mV[VY] + scaled_height, 0.f);
            index++;

            uv[index].set(uv_rect.mLeft, uv_rect.mBottom);
            pos[index].set(ui_translation.mV[VX], ui_translation.mV[VY], 0.f);
            index++;

            uv[index].set(uv_rect.mRight, uv_rect.mBottom);
            pos[index].set(ui_translation.mV[VX] + scaled_width, ui_translation.mV[VY], 0.f);
            index++;

            // recording mode feeds gDX so LLUIImage's display-list cache can capture it; otherwise submit with the
            // bound shader (gUIProgram or gSolidColorProgram)
            if (gDX.isRecording())
            {
                gDX.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
            }
            else if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
            {
                // draw order: drain queued gDX geometry before this immediate batch
                gDX.flush();
                DXRender2DUtils::ScaledImageGeometry geom;
                for (S32 v = 0; v < NUM_VERTICES; ++v)
                {
                    const F32* p = pos[v].getF32ptr();
                    geom.pos[v][0] = p[0]; geom.pos[v][1] = p[1]; geom.pos[v][2] = p[2];
                    geom.uv[v][0] = uv[v].mV[0]; geom.uv[v][1] = uv[v].mV[1];
                }
                gDXUIBatch.flushPending();
                DXRender2DUtils::drawScaledImage(geom, color);
                gDX.syncMatrices();
                if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
                {
                    gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
                }
            }
        }
        gDX.end();
    }
    else
    {
        gDX.pushUIMatrix();
        gDX.translateUI((F32)x, (F32)y, 0.f);

        F32 offset_x = F32(width / 2);
        F32 offset_y = F32(height / 2);

        gDX.translateUI(offset_x, offset_y, 0.f);

        LLMatrix3 quat(0.f, 0.f, degrees * DEG_TO_RAD);

        if (image != NULL)
        {
            gDX.getTexUnit(0)->bind(image, true);
        }
        else
        {
            // a render-target source is not bound in the rotated variant: it draws untextured
        }

        gDX.color4fv(color.mV);

        LLVector3 rv[6];
        rv[0] = LLVector3(offset_x, offset_y, 0.f) * quat;
        rv[1] = LLVector3(-offset_x, offset_y, 0.f) * quat;
        rv[2] = LLVector3(-offset_x, -offset_y, 0.f) * quat;
        rv[3] = LLVector3(offset_x, offset_y, 0.f) * quat;
        rv[4] = LLVector3(-offset_x, -offset_y, 0.f) * quat;
        rv[5] = LLVector3(offset_x, -offset_y, 0.f) * quat;

        LLVector2 ruv[6] = {
            LLVector2(uv_rect.mRight, uv_rect.mTop),
            LLVector2(uv_rect.mLeft, uv_rect.mTop),
            LLVector2(uv_rect.mLeft, uv_rect.mBottom),
            LLVector2(uv_rect.mRight, uv_rect.mTop),
            LLVector2(uv_rect.mLeft, uv_rect.mBottom),
            LLVector2(uv_rect.mRight, uv_rect.mBottom),
        };

        // begin() flushes gDX's pending immediate geometry when switching away from a batched mode; end() itself
        // is a no-op on the UI batch path
        gDX.begin(LLRender::TRIANGLES);
        {
            // same ScaledImageGeometry submit as the unrotated branch; only the position math differs
            if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
            {
                gDX.flush();
                // rv[] is relative to the icon center: add the UI translation/scale to get the screen position
                LLVector3 ui_scale = gDX.getUIScale();
                LLVector3 ui_translation = gDX.getUITranslation();
                ui_translation.scaleVec(ui_scale);
                DXRender2DUtils::ScaledImageGeometry geom;
                for (S32 v = 0; v < 6; ++v)
                {
                    LLVector3 scaled_rv = rv[v];
                    scaled_rv.scaleVec(ui_scale);
                    geom.pos[v][0] = ui_translation.mV[VX] + scaled_rv.mV[VX];
                    geom.pos[v][1] = ui_translation.mV[VY] + scaled_rv.mV[VY];
                    geom.pos[v][2] = ui_translation.mV[VZ] + scaled_rv.mV[VZ];
                    geom.uv[v][0] = ruv[v].mV[0]; geom.uv[v][1] = ruv[v].mV[1];
                }
                gDXUIBatch.flushPending();
                DXRender2DUtils::drawScaledImage(geom, color);
                gDX.syncMatrices();
                if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
                {
                    gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
                }
            }
        }
        gDX.end();
        gDX.popUIMatrix();
    }
}

// +--------------------------------------------------------------------------+
// | DXRender2D - UI image provider registry and UI matrix wrappers           |
// +--------------------------------------------------------------------------+

// [LL] registers with the image provider so a dying provider clears our pointer
DXRender2D::DXRender2D(DXImageProviderInterface* image_provider)
{
    mImageProvider = image_provider;
    if (mImageProvider)
    {
        mImageProvider->addOnRemovalCallback(resetProvider);
    }
}

// [LL] cleans up the provider and unregisters the removal callback
DXRender2D::~DXRender2D()
{
    if (mImageProvider)
    {
        mImageProvider->cleanUp();
        mImageProvider->deleteOnRemovalCallback(resetProvider);
    }
}

// [LL] UI translation that also tracks the font origin and depth
void DXRender2D::translate(F32 x, F32 y, F32 z)
{
    gDX.translateUI(x, y, z);
    LLFontDX::sCurOrigin.mX += (S32)x;
    LLFontDX::sCurOrigin.mY += (S32)y;
    LLFontDX::sCurDepth += z;
}

// [LL] pushes the UI matrix and the font origin/depth
void DXRender2D::pushMatrix()
{
    gDX.pushUIMatrix();
    LLFontDX::sOriginStack.push_back(std::make_pair(LLFontDX::sCurOrigin, LLFontDX::sCurDepth));
}

// [LL] pops the UI matrix and restores the font origin/depth
void DXRender2D::popMatrix()
{
    gDX.popUIMatrix();
    LLFontDX::sCurOrigin = LLFontDX::sOriginStack.back().first;
    LLFontDX::sCurDepth = LLFontDX::sOriginStack.back().second;
    LLFontDX::sOriginStack.pop_back();
}

// [LL] resets the UI matrix and the font origin/depth
void DXRender2D::loadIdentity()
{
    gDX.loadUIIdentity();
    LLFontDX::sCurOrigin.mX = 0;
    LLFontDX::sCurOrigin.mY = 0;
    LLFontDX::sCurDepth = 0.f;
}

// [LL] UI image by asset id from the provider, or null
LLPointer<LLUIImage> DXRender2D::getUIImageByID(const LLUUID& image_id, S32 priority)
{
    if (mImageProvider)
    {
        return mImageProvider->getUIImageByID(image_id, priority);
    }
    else
    {
        return NULL;
    }
}

// [LL] UI image by name from the provider, or null
LLPointer<LLUIImage> DXRender2D::getUIImage(const std::string& name, S32 priority)
{
    if (!name.empty() && mImageProvider)
        return mImageProvider->getUIImage(name, priority);
    else
        return NULL;
}

// [LL] called when the provider dies: forgets it
void DXRender2D::resetProvider()
{
    if (DXRender2D::instanceExists())
    {
        DXRender2D::getInstance()->mImageProvider = NULL;
    }
}

// +--------------------------------------------------------------------------+
// | DXImageProviderInterface - removal notification                          |
// +--------------------------------------------------------------------------+

// [LL] notifies every registered holder that the provider is gone
DXImageProviderInterface::~DXImageProviderInterface()
{
    for (callback_list_t::iterator iter = mCallbackList.begin(); iter != mCallbackList.end();)
    {
        callback_list_t::iterator curiter = iter++;
        (*curiter)();
    }
}

// [LL] registers a callback to run when the provider is destroyed
void DXImageProviderInterface::addOnRemovalCallback(callback_t func)
{
    if (!func)
    {
        return;
    }
    mCallbackList.push_back(func);
}

// [LL] unregisters a removal callback
void DXImageProviderInterface::deleteOnRemovalCallback(callback_t func)
{
    callback_list_t::iterator iter = std::find(mCallbackList.begin(), mCallbackList.end(), func);
    if (iter != mCallbackList.end())
    {
        mCallbackList.erase(iter);
    }
}
