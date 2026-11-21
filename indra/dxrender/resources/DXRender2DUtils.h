/**
 * @file DXRender2DUtils.h
 * @brief Immediate-mode 2D drawing and UI image plumbing for the D3D11 renderer
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

#pragma once

#include "v4color.h"
#include "v4coloru.h"
#include "v3math.h"
#include "llpointer.h"      // LLPointer<>
#include "llrect.h"
#include "llsingleton.h"
#include "llhlslshader.h"

class LLColor4;
class LLVector3;
class LLVector2;
class LLUIImage;
class LLUUID;

// +--------------------------------------------------------------------------+
// | DXRender2DUtils - immediate-mode 2D drawing and UI image plumbing        |
// |                                                                          |
// | Origin tags:  [LL]  derived from Linden's llrender2dutils                |
// |               [S24] S24-original                                         |
// | Sections:     STATE | LINES | RECTANGLES | ROUND SHAPES | SEGMENTED |     |
// |               IMAGES | DXRender2D | DXImageProviderInterface             |
// | Flow:         dx_* function -> drawBatch() -> builder -> DXUIBatch       |
// | Implementation: DXRender2DUtils.cpp                                      |
// +--------------------------------------------------------------------------+

// +--------------------------------------------------------------------------+
// | STATE                                                                    |
// +--------------------------------------------------------------------------+

// [LL] the UI colour every image defaults to: opaque white
extern const LLColor4 UI_VERTEX_COLOR;

// [LL] sets a pixel-space orthographic projection (width x height) and an identity modelview
void dx_state_for_2d(S32 width, S32 height);

// +--------------------------------------------------------------------------+
// | LINES AND TRIANGLES                                                      |
// +--------------------------------------------------------------------------+

// [S24] 2D line in the current colour / in `color`
void dx_line_2d(S32 x1, S32 y1, S32 x2, S32 y2);
void dx_line_2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4& color);

// [S24] filled or outlined triangle
void dx_triangle_2d(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled);

// [S24] 3D line in `color`
void dx_line_3d(const LLVector3& start, const LLVector3& end, const LLColor4& color);

// [S24] diagonal cross across a rectangle
void dx_draw_x(const LLRect& rect, const LLColor4& color);

// +--------------------------------------------------------------------------+
// | RECTANGLES                                                               |
// +--------------------------------------------------------------------------+

// [S24] filled or outlined rectangle, in the current colour or in `color`
void dx_rect_2d(S32 left, S32 top, S32 right, S32 bottom, bool filled = true);
void dx_rect_2d(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color, bool filled = true);
void dx_rect_2d(const LLRect& rect, bool filled = true);
void dx_rect_2d(const LLRect& rect, const LLColor4& color, bool filled = true);

// [LL] rectangle in the font origin's local space, scaled to GL pixels and grown by pixel_offset
void dx_rect_2d_offset_local(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color, S32 pixel_offset = 0, bool filled = true);
void dx_rect_2d_offset_local(S32 left, S32 top, S32 right, S32 bottom, S32 pixel_offset = 0, bool filled = true);

// [LL] gray and white checkerboard over a rectangle
void dx_rect_2d_checkerboard(const LLRect& rect, F32 alpha = 1.0f);

// [LL] drop shadow outside the right and bottom edges, `lines` wide and high
void dx_drop_shadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines);

// [S24] quad from the origin to width x height in the current colour; the _tex form adds texture coordinates 0..1
void dx_rect_2d_simple(S32 width, S32 height);
void dx_rect_2d_simple_tex(S32 width, S32 height);

// [S24] the same quads in plain gDX immediate mode. The batched versions above go through gDXUIBatch, whose
// drawAndPop() hardcodes SRC_ALPHA/INV_SRC_ALPHA blending with a full colour-write mask and ignores whatever
// the caller set via gDX.setSceneBlendType()/setColorWriteMask()/blendFunc(). Callers compositing into an
// off-screen buffer (the avatar-bake compositor in llappearance/lltexlayer.cpp) need these, which honour the
// caller's real state, as upstream's gl_rect_2d_simple*() do.
void dx_rect_2d_simple_immediate(S32 width, S32 height);
void dx_rect_2d_simple_tex_immediate(S32 width, S32 height);

inline void dx_rect_2d(const LLRect& rect, bool filled)
{
    dx_rect_2d(rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, filled);
}

inline void dx_rect_2d_offset_local(const LLRect& rect, S32 pixel_offset, bool filled)
{
    dx_rect_2d_offset_local(rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, pixel_offset, filled);
}

// +--------------------------------------------------------------------------+
// | ROUND SHAPES                                                             |
// +--------------------------------------------------------------------------+

// [S24] circle / arc (pie slice) / ring with sides (tube shape)
void dx_circle_2d(F32 x, F32 y, F32 radius, S32 steps, bool filled);
void dx_arc_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle);
void dx_deep_circle(F32 radius, F32 depth, S32 steps);

// [LL] ring with sides (tube shape)
void dx_ring(F32 radius, F32 width, const LLColor4& center_color, const LLColor4& side_color, S32 steps, bool render_center);

// [LL] the area between two concentric circles (doughnut / washer), optionally limited to an angle range
void dx_washer_2d(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color);
void dx_washer_segment_2d(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color);

// +--------------------------------------------------------------------------+
// | SEGMENTED RECTANGLES                                                     |
// +--------------------------------------------------------------------------+

/*
   TL |______TOP_________| TR
     /|                  |\
   _/_|__________________|_\_
   L| |    MIDDLE        | |R
   _|_|__________________|_|_
    \ |    BOTTOM        | /
   BL\|__________________|/ BR
      |                  |
*/

// [LL] which edges of a segmented rectangle are rounded
typedef enum e_rounded_edge
{
    ROUNDED_RECT_LEFT = 0x1,
    ROUNDED_RECT_TOP = 0x2,
    ROUNDED_RECT_RIGHT = 0x4,
    ROUNDED_RECT_BOTTOM = 0x8,
    ROUNDED_RECT_ALL = 0xf
}ERoundedEdge;

// [LL] textured rounded-rectangle fragment, and the world-space (nametag panel) segmented rectangle
void dx_segmented_rect_2d_fragment_tex(const LLRect& rect, const S32 texture_width, const S32 texture_height, const S32 border_size, const F32 start_fragment, const F32 end_fragment, const U32 edges = ROUNDED_RECT_ALL);
void dx_segmented_rect_3d_tex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect, const LLVector3& width_vec, const LLVector3& height_vec);

// +--------------------------------------------------------------------------+
// | IMAGES                                                                   |
// +--------------------------------------------------------------------------+

// [LL] image at its own size / render target scaled / image scaled / scaled with borders (two forms) / rotated.
// dx_draw_scaled_rotated_image()'s `target` parameter is currently unused; kept in case it is wired in or removed later.
void dx_draw_image(S32 x, S32 y, DXTextureBase* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void dx_draw_scaled_target(S32 x, S32 y, S32 width, S32 height, DXRenderTarget* target, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void dx_draw_scaled_image(S32 x, S32 y, S32 width, S32 height, DXTextureBase* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void dx_draw_scaled_rotated_image(S32 x, S32 y, S32 width, S32 height, F32 degrees, DXTextureBase* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f), DXRenderTarget* target = NULL);
void dx_draw_scaled_image_with_border(S32 x, S32 y, S32 border_width, S32 border_height, S32 width, S32 height, DXTextureBase* image, const LLColor4& color, bool solid_color = false, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f), bool scale_inner = true);
void dx_draw_scaled_image_with_border(S32 x, S32 y, S32 width, S32 height, DXTextureBase* image, const LLColor4& color, bool solid_color = false, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f), const LLRectf& scale_rect = LLRectf(0.f, 1.f, 1.f, 0.f), bool scale_inner = true);

// +--------------------------------------------------------------------------+
// | DXRender2D - UI image provider registry and UI matrix wrappers           |
// +--------------------------------------------------------------------------+

class DXImageProviderInterface;

// [LL] singleton holding the image provider; its static matrix wrappers also track the font origin and depth
class DXRender2D : public LLSimpleton<DXRender2D>
{
    LOG_CLASS(DXRender2D);
public:
    DXRender2D(DXImageProviderInterface* image_provider);
    ~DXRender2D();

    static void pushMatrix();
    static void popMatrix();
    static void loadIdentity();
    static void translate(F32 x, F32 y, F32 z = 0.0f);

    LLPointer<LLUIImage> getUIImageByID(const LLUUID& image_id, S32 priority = 0);
    LLPointer<LLUIImage> getUIImage(const std::string& name, S32 priority = 0);

protected:
    // called when the provider dies so DXRender2D drops its pointer
    static void resetProvider();

private:
    DXImageProviderInterface* mImageProvider;
};

// [LL] image source for DXRender2D; notifies registered holders when it is destroyed
class DXImageProviderInterface
{
protected:
    DXImageProviderInterface() {};
    virtual ~DXImageProviderInterface();
public:
    virtual LLPointer<LLUIImage> getUIImage(const std::string& name, S32 priority) = 0;
    virtual LLPointer<LLUIImage> getUIImageByID(const LLUUID& id, S32 priority) = 0;
    virtual void cleanUp() = 0;

    typedef void(*callback_t)();
    void addOnRemovalCallback(callback_t func);
    void deleteOnRemovalCallback(callback_t func);

private:
    typedef std::list< callback_t > callback_list_t;
    callback_list_t mCallbackList;
};
