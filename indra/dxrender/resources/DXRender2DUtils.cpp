#include "DXRender2DUtils.h"
#include "DXUIBatch.h"
#include "v2math.h"
#include <vector>
#include <cmath>

namespace
{
    constexpr F32 kTwoPi = 6.283185307179586f;

    // Mirrors llrender/llrender2dutils.h's ROUNDED_RECT_* enum values
    // exactly (LEFT=0x1, TOP=0x2, RIGHT=0x4, BOTTOM=0x8) - duplicated here
    // rather than included, since including llrender2dutils.h would create
    // a backward dependency (dxrender/ has zero llrender dependency by
    // design; llrender2dutils.cpp is the one that calls INTO dxrender/,
    // never the reverse).
    constexpr U32 kRoundedRectLeft = 0x1;
    constexpr U32 kRoundedRectTop = 0x2;
    constexpr U32 kRoundedRectRight = 0x4;
    constexpr U32 kRoundedRectBottom = 0x8;

    // Mirrors LLRender::transform(LLVector3&)'s exact formula - see
    // DXRender2DUtils.h's header comment for why this has to be replicated
    // by hand here instead of relying on gGL.vertex2i()'s automatic
    // application of it.
    inline void applyUI(F32& x, F32& y, F32& z, const LLVector3& offset, const LLVector3& scale)
    {
        x = (x + offset.mV[VX]) * scale.mV[VX];
        y = (y + offset.mV[VY]) * scale.mV[VY];
        z = (z + offset.mV[VZ]) * scale.mV[VZ];
    }

    inline DXUIVertex makeVertex(F32 x, F32 y, F32 z, const LLColor4U& color, F32 u, F32 v)
    {
        DXUIVertex vert;
        vert.pos[0] = x; vert.pos[1] = y; vert.pos[2] = z;
        vert.color[0] = color.mV[0]; vert.color[1] = color.mV[1]; vert.color[2] = color.mV[2]; vert.color[3] = color.mV[3];
        vert.uv[0] = u; vert.uv[1] = v;
        return vert;
    }

    inline DXUIVertex makeUIVertex(F32 x, F32 y, F32 z, const LLColor4U& color, F32 u, F32 v, const LLVector3& offset, const LLVector3& scale)
    {
        applyUI(x, y, z, offset, scale);
        return makeVertex(x, y, z, color, u, v);
    }
}

namespace DXRender2DUtils
{
    void glRectTwoD(S32 left, S32 top, S32 right, S32 bottom, const LLColor4U& color, bool filled, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        if (filled)
        {
            DXUIVertex verts[6] = {
                makeUIVertex((F32)left, (F32)top, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)left, (F32)bottom, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)right, (F32)bottom, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)left, (F32)top, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)right, (F32)bottom, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)right, (F32)top, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            };
            gDXUIBatch.push(verts, 6);
        }
        else
        {
            // GL's outline case decrements top/right by 1 before drawing -
            // mirrored exactly here.
            S32 t = top - 1;
            S32 r = right - 1;
            DXUIVertex verts[5] = {
                makeUIVertex((F32)left, (F32)t, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)left, (F32)bottom, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)r, (F32)bottom, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)r, (F32)t, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)left, (F32)t, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            };
            gDXUIBatch.push(verts, 5);
        }
    }

    void glDrawX(const LLRect& rect, const LLColor4& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        LLColor4U c(color);
        DXUIVertex verts[4] = {
            makeUIVertex((F32)rect.mLeft, (F32)rect.mTop, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)rect.mRight, (F32)rect.mBottom, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)rect.mLeft, (F32)rect.mBottom, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)rect.mRight, (F32)rect.mTop, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
        };
        gDXUIBatch.push(verts, 4);
    }

    void glDropShadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines, const LLVector3& ui_offset, const LLVector3& ui_scale)
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
            v.push_back(makeUIVertex((F32)x, (F32)y, 0.f, c, 0.f, 0.f, ui_offset, ui_scale));
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

    void glLineTwoD(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        DXUIVertex verts[2] = {
            makeUIVertex((F32)x1, (F32)y1, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)x2, (F32)y2, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
        };
        gDXUIBatch.push(verts, 2);
    }

    void glLineThreeD(const LLVector3& start, const LLVector3& end, const LLColor4& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        LLColor4U c(color);
        DXUIVertex verts[2] = {
            makeUIVertex(start.mV[VX], start.mV[VY], start.mV[VZ], c, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex(end.mV[VX], end.mV[VY], end.mV[VZ], c, 0.f, 0.f, ui_offset, ui_scale),
        };
        gDXUIBatch.push(verts, 2);
    }

    void glTriangleTwoD(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        LLColor4U c(color);
        if (filled)
        {
            DXUIVertex verts[3] = {
                makeUIVertex((F32)x1, (F32)y1, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)x2, (F32)y2, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)x3, (F32)y3, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
            };
            gDXUIBatch.push(verts, 3);
        }
        else
        {
            // GL_LINE_LOOP -> closed LineStrip (re-close to the first vertex).
            DXUIVertex verts[4] = {
                makeUIVertex((F32)x1, (F32)y1, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)x2, (F32)y2, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)x3, (F32)y3, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
                makeUIVertex((F32)x1, (F32)y1, 0.f, c, 0.f, 0.f, ui_offset, ui_scale),
            };
            gDXUIBatch.push(verts, 4);
        }
    }

    void glCornersTwoD(S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        S32 max1 = (S32)(max_frac * (right - left));
        S32 max2 = (S32)(max_frac * (top - bottom));
        if (length > max1) length = max1;
        if (length > max2) length = max2;

        std::vector<DXUIVertex> v;
        v.reserve(16);
        auto emit = [&](S32 x, S32 y)
        {
            v.push_back(makeUIVertex((F32)x, (F32)y, 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
        };

        emit(left, top);    emit(left + length, top);
        emit(left, top);    emit(left, top - length);
        emit(left, bottom); emit(left + length, bottom);
        emit(left, bottom); emit(left, bottom + length);
        emit(right, top);   emit(right - length, top);
        emit(right, top);   emit(right, top - length);
        emit(right, bottom); emit(right - length, bottom);
        emit(right, bottom); emit(right, bottom + length);

        gDXUIBatch.push(v.data(), v.size());
    }

    void glRectTwoDSimple(S32 width, S32 height, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        DXUIVertex verts[6] = {
            makeUIVertex((F32)width, (F32)height, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex(0.f, (F32)height, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex(0.f, 0.f, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)width, (F32)height, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex(0.f, 0.f, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)width, 0.f, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
        };
        gDXUIBatch.push(verts, 6);
    }

    void glRectTwoDSimpleTex(S32 width, S32 height, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        DXUIVertex verts[6] = {
            makeUIVertex((F32)width, (F32)height, 0.f, color, 1.f, 1.f, ui_offset, ui_scale),
            makeUIVertex(0.f, (F32)height, 0.f, color, 0.f, 1.f, ui_offset, ui_scale),
            makeUIVertex(0.f, 0.f, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)width, (F32)height, 0.f, color, 1.f, 1.f, ui_offset, ui_scale),
            makeUIVertex(0.f, 0.f, 0.f, color, 0.f, 0.f, ui_offset, ui_scale),
            makeUIVertex((F32)width, 0.f, 0.f, color, 1.f, 0.f, ui_offset, ui_scale),
        };
        gDXUIBatch.push(verts, 6);
    }

    void glArcTwoD(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
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
            // Triangle fan (center + rim) expanded to a flat triangle list -
            // D3D11 has no native fan topology.
            if (rim.size() >= 2)
            {
                v.reserve((rim.size() - 1) * 3);
                for (size_t i = 0; i + 1 < rim.size(); ++i)
                {
                    v.push_back(makeUIVertex(center_x, center_y, 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
                    v.push_back(makeUIVertex(center_x + rim[i].mV[VX], center_y + rim[i].mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
                    v.push_back(makeUIVertex(center_x + rim[i + 1].mV[VX], center_y + rim[i + 1].mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
                }
            }
        }
        else
        {
            v.reserve(rim.size());
            for (const LLVector2& p : rim)
            {
                v.push_back(makeUIVertex(center_x + p.mV[VX], center_y + p.mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
            }
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    void glCircleTwoD(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
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
                    v.push_back(makeUIVertex(center_x, center_y, 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
                    v.push_back(makeUIVertex(center_x + rim[i].mV[VX], center_y + rim[i].mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
                    v.push_back(makeUIVertex(center_x + rim[i + 1].mV[VX], center_y + rim[i + 1].mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
                }
            }
        }
        else
        {
            v.reserve(rim.size() + 1);
            for (const LLVector2& p : rim)
            {
                v.push_back(makeUIVertex(center_x + p.mV[VX], center_y + p.mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
            }
            // GL_LINE_LOOP -> closed LineStrip: duplicate the first vertex.
            if (!rim.empty())
            {
                v.push_back(makeUIVertex(center_x + rim[0].mV[VX], center_y + rim[0].mV[VY], 0.f, color, 0.f, 0.f, ui_offset, ui_scale));
            }
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    void glDeepCircle(F32 radius, F32 depth, S32 steps, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        F32 x = radius;
        F32 y = 0.f;
        F32 angle_delta = kTwoPi / (F32)steps;
        S32 count = steps + 1; // extra step to close the circle, matches GL

        std::vector<DXUIVertex> v;
        v.reserve((size_t)count * 2);
        for (S32 i = 0; i < count; ++i)
        {
            v.push_back(makeUIVertex(x, y, depth, color, 0.f, 0.f, ui_offset, ui_scale));
            v.push_back(makeUIVertex(x, y, 0.f, color, 0.f, 0.f, ui_offset, ui_scale));

            F32 x_new = x * cosf(angle_delta) - y * sinf(angle_delta);
            y = x * sinf(angle_delta) + y * cosf(angle_delta);
            x = x_new;
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    void glWasherTwoD(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color, const LLVector3& ui_offset, const LLVector3& ui_scale)
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
            v.push_back(makeUIVertex(x1, y1, 0.f, oc, 0.f, 0.f, ui_offset, ui_scale));
            v.push_back(makeUIVertex(x2, y2, 0.f, ic, 0.f, 0.f, ui_offset, ui_scale));

            F32 x1_new = x1 * cos_delta - y1 * sin_delta;
            y1 = x1 * sin_delta + y1 * cos_delta;
            x1 = x1_new;

            F32 x2_new = x2 * cos_delta - y2 * sin_delta;
            y2 = x2 * sin_delta + y2 * cos_delta;
            x2 = x2_new;
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    void glWasherSegmentTwoD(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color, const LLVector3& ui_offset, const LLVector3& ui_scale)
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
            v.push_back(makeUIVertex(x1, y1, 0.f, oc, 0.f, 0.f, ui_offset, ui_scale));
            v.push_back(makeUIVertex(x2, y2, 0.f, ic, 0.f, 0.f, ui_offset, ui_scale));

            F32 x1_new = x1 * cos_delta - y1 * sin_delta;
            y1 = x1 * sin_delta + y1 * cos_delta;
            x1 = x1_new;

            F32 x2_new = x2 * cos_delta - y2 * sin_delta;
            y2 = x2 * sin_delta + y2 * cos_delta;
            x2 = x2_new;
        }
        gDXUIBatch.push(v.data(), v.size());
    }

    void glDrawScaledImage(const ScaledImageGeometry& geom, const LLColor4& color)
    {
        LLColor4U c(color);
        DXUIVertex verts[6];
        for (int i = 0; i < 6; ++i)
        {
            verts[i] = makeVertex(geom.pos[i][0], geom.pos[i][1], geom.pos[i][2], c, geom.uv[i][0], geom.uv[i][1]);
        }
        gDXUIBatch.push(verts, 6);
    }

    void glDrawScaledImageWithBorderNineSlice(const NineSliceGeometry& geom, const LLColor4& color)
    {
        LLColor4U c(color);
        DXUIVertex verts[NineSliceGeometry::NUM_VERTICES];
        for (int i = 0; i < NineSliceGeometry::NUM_VERTICES; ++i)
        {
            verts[i] = makeVertex(geom.pos[i][0], geom.pos[i][1], geom.pos[i][2], c, geom.uv[i][0], geom.uv[i][1]);
        }
        gDXUIBatch.push(verts, NineSliceGeometry::NUM_VERTICES);
    }

    void glSegmentedRectTwoDFragmentTex(const LLRect& rect, S32 texture_width, S32 texture_height, S32 border_size,
        F32 start_fragment, F32 end_fragment, U32 edges, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        const S32 left = rect.mLeft;
        const S32 right = rect.mRight;
        const S32 top = rect.mTop;
        const S32 bottom = rect.mBottom;
        S32 width = llabs(right - left);
        S32 height = llabs(top - bottom);

        // GL body does gGL.pushUIMatrix()/translateUI((F32)left,(F32)bottom,0.f)
        // before emitting any vertices - replicated here as a local origin
        // added to every position before applying ui_offset/ui_scale (same
        // net transform, no nested push/pop needed).
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
            v.push_back(makeUIVertex(origin_x + pos.mV[VX], origin_y + pos.mV[VY], 0.f, color, u, uv_v, ui_offset, ui_scale));
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

    void glSegmentedRectThreeDTex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
        const LLVector3& width_vec, const LLVector3& height_vec, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale)
    {
        std::vector<DXUIVertex> v;
        v.reserve(54);
        auto emit = [&](F32 u, F32 uv_v, const LLVector3& pos)
        {
            v.push_back(makeUIVertex(pos.mV[VX], pos.mV[VY], pos.mV[VZ], color, u, uv_v, ui_offset, ui_scale));
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
