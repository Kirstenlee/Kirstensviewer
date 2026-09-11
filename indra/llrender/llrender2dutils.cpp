/**
 * @file llrender2dutils.cpp
 * @brief GL function implementations for immediate-mode gl drawing.
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

 // Linden library includes
#include "v2math.h"
#include "m3math.h"
#include "v4color.h"
#include "llfontgl.h"
#include "llrender.h"
#include "llrect.h"
#include "llgl.h"
#include "lltexture.h"
#include "llfasttimer.h"

// Project includes
#include "llrender2dutils.h"
#include "lluiimage.h"

#ifdef DX_RENDER
#include "DXUIBatch.h"
#include "DXRender2DUtils.h"
// S24 (2026-08-16): every DX_RENDER function below that pushes into
// gDXUIBatch now calls gDXUIBatch.flushPending() immediately before doing
// so (matching the same fix applied to LLFontGL::submitGlyphBatch()/
// submitUnderline() in llfontgl.cpp). Root cause: DXUIBatch's batching key
// is (shader, topology, alpha_blend, depth) only - it has no idea the MVP
// is about to change, so two shapes/text sharing that same key (extremely
// common - most 2D UI and world-space HUD content alike uses gUIProgram/
// TriangleList/alpha-blend/no-depth) could get silently merged into ONE
// pending batch even though each was positioned assuming its OWN,
// different transform (gDX.syncMatrices(), called AFTER push() at every
// one of these call sites, only takes effect for whichever call happens to
// trigger the eventual real Draw() - anything merged in from an earlier
// call under a different matrix draws wrong). The existing pass-boundary
// flushPending() hooks (llviewerdisplay.cpp, llhudobject.cpp) only guard
// specific, coarse call-site boundaries and don't reach every case -
// draining any stale pending batch immediately before each shape's own
// push() guarantees it always draws with its own correct, freshly-synced
// matrix regardless of what any particular caller elsewhere remembers to
// flush. Narrows batching granularity to "one draw per shape" instead of
// "one draw per matching-state run", still far fewer draws than the
// pre-batching one-draw-per-primitive baseline task #211 fixed.
#endif

//
// Globals
//
const LLColor4 UI_VERTEX_COLOR(1.f, 1.f, 1.f, 1.f);

//
// Functions
//

bool ui_point_in_rect(S32 x, S32 y, S32 left, S32 top, S32 right, S32 bottom)
{
	if (x < left || right < x) return false;
	if (y < bottom || top < y) return false;
	return true;
}

// Puts GL into 2D drawing mode by turning off lighting, setting to an
// orthographic projection, etc.
[[nodiscard]] void gl_state_for_2d(S32 width, S32 height)
{
	const F32 w = (F32)width;
	const F32 h = (F32)height;

	gDX.matrixMode(LLRender::MM_PROJECTION);
	gDX.loadIdentity();
	gDX.ortho(0.f, (w > 1.f ? w : 1.f), 0.f, (h > 1.f ? h : 1.f), -1.f, 1.f);
	gDX.matrixMode(LLRender::MM_MODELVIEW);
	gDX.loadIdentity();
}

void gl_draw_x(const LLRect& rect, const LLColor4& color)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glDrawX(rect, color, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::LineList);
		}
	}
	return;
#else
	gDX.color4fv(color.mV);

	gDX.begin(LLRender::LINES);
	gDX.vertex2i(rect.mLeft, rect.mTop);
	gDX.vertex2i(rect.mRight, rect.mBottom);
	gDX.vertex2i(rect.mLeft, rect.mBottom);
	gDX.vertex2i(rect.mRight, rect.mTop);
	gDX.end();
#endif // DX_RENDER
}

void gl_rect_2d_offset_local(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color, S32 pixel_offset, bool filled)
{
	gDX.color4fv(color.mV);
	gl_rect_2d_offset_local(left, top, right, bottom, pixel_offset, filled);
}

void gl_rect_2d_offset_local(S32 left, S32 top, S32 right, S32 bottom, S32 pixel_offset, bool filled)
{
	gDX.pushUIMatrix();
	left += LLFontGL::sCurOrigin.mX;
	right += LLFontGL::sCurOrigin.mX;
	bottom += LLFontGL::sCurOrigin.mY;
	top += LLFontGL::sCurOrigin.mY;

	gDX.loadUIIdentity();
	gl_rect_2d(llfloor((F32)left * LLRender::sUIGLScaleFactor.mV[VX]) - pixel_offset,
		llfloor((F32)top * LLRender::sUIGLScaleFactor.mV[VY]) + pixel_offset,
		llfloor((F32)right * LLRender::sUIGLScaleFactor.mV[VX]) + pixel_offset,
		llfloor((F32)bottom * LLRender::sUIGLScaleFactor.mV[VY]) - pixel_offset,
		filled);
	gDX.popUIMatrix();
}

void gl_rect_2d(S32 left, S32 top, S32 right, S32 bottom, bool filled)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	// S24 (DXRender2DUtils, 2026-07-25): see llrender2dutils.cpp's file-level
	// DX_RENDER include comment / the session plan's "consolidate
	// llrender2dutils.cpp" section - this is one of the ~20 functions moved
	// out of large inline fences into dxrender/resources/DXRender2DUtils.
	// Both color-taking wrappers (gl_rect_2d(...,color,filled) and the
	// LLRect overload) already call gDX.color4fv(color) before reaching this
	// base overload, so gDX.getCurrentColor() here picks up the right value
	// regardless of which entry point was used - see LLRender::
	// getCurrentColor()'s own comment for why this exists at all (GL's
	// ambient "current color" has no DX_RENDER equivalent to read from
	// dxrender/, which has zero llrender dependency by design).
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		// S24 (2026-09-02): a 2026-07-28 diagnostic removed here (confirmed
		// real UI solid-rect draws route through gUIProgram with correct
		// color data, per its own comment - that question's been answered
		// for over a month, and this was left logging on every one of a
		// session's first 20 gl_rect_2d() calls since).
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glRectTwoD(left, top, right, bottom, gDX.getCurrentColor(), filled, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(),
				filled ? DXUITopology::TriangleList : DXUITopology::LineStrip);
		}
	}
	return;
#else
	// Counterclockwise quad will face the viewer
	if (filled)
	{
		gDX.begin(LLRender::TRIANGLES);
		gDX.vertex2i(left, top);
		gDX.vertex2i(left, bottom);
		gDX.vertex2i(right, bottom);

		gDX.vertex2i(left, top);
		gDX.vertex2i(right, bottom);
		gDX.vertex2i(right, top);
		gDX.end();
	}
	else
	{
		top--;
		right--;
		gDX.begin(LLRender::LINE_STRIP);
		gDX.vertex2i(left, top);
		gDX.vertex2i(left, bottom);
		gDX.vertex2i(right, bottom);
		gDX.vertex2i(right, top);
		gDX.vertex2i(left, top);
		gDX.end();
	}
#endif // DX_RENDER
}

void gl_rect_2d(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color, bool filled)
{
	gDX.color4fv(color.mV);
	gl_rect_2d(left, top, right, bottom, filled);
}

void gl_rect_2d(const LLRect& rect, const LLColor4& color, bool filled)
{
	gDX.color4fv(color.mV);
	gl_rect_2d(rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, filled);
}

// Given a rectangle on the screen, draws a drop shadow _outside_
// the right and bottom edges of it.  Along the right it has width "lines"
// and along the bottom it has height "lines".
void gl_drop_shadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	// DXRender2DUtils::glDropShadow() applies the same right--/bottom++/
	// lines++ "overlap by a single pixel" adjustment internally - pass the
	// original, unmodified values.
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glDropShadow(left, top, right, bottom, start_color, lines, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
		}
	}
	return;
#else
	// HACK: Overlap with the rectangle by a single pixel.
	right--;
	bottom++;
	lines++;

	LLColor4 end_color = start_color;
	end_color.mV[VALPHA] = 0.f;

	gDX.begin(LLRender::TRIANGLES);

	// Right edge, CCW faces screen
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, top - lines);
	gDX.vertex2i(right, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(right + lines, bottom);
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, top - lines);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(right + lines, bottom);
	gDX.vertex2i(right + lines, top - lines);

	// Bottom edge, CCW faces screen
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, bottom);
	gDX.vertex2i(left + lines, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(left + lines, bottom - lines);
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(left + lines, bottom - lines);
	gDX.vertex2i(right, bottom - lines);

	// bottom left Corner
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(left + lines, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(left, bottom);
	// make the bottom left corner not sharp
	gDX.vertex2i(left + 1, bottom - lines + 1);
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(left + lines, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(left + 1, bottom - lines + 1);
	gDX.vertex2i(left + lines, bottom - lines);

	// bottom right corner
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(right, bottom - lines);
	// make the rightmost corner not sharp
	gDX.vertex2i(right + lines - 1, bottom - lines + 1);
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, bottom);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(right + lines - 1, bottom - lines + 1);
	gDX.vertex2i(right + lines, bottom);

	// top right corner
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, top - lines);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(right + lines, top - lines);
	// make the corner not sharp
	gDX.vertex2i(right + lines - 1, top - 1);
	gDX.color4fv(start_color.mV);
	gDX.vertex2i(right, top - lines);
	gDX.color4fv(end_color.mV);
	gDX.vertex2i(right + lines - 1, top - 1);
	gDX.vertex2i(right, top);

	gDX.end();
	stop_glerror();
#endif // DX_RENDER
}

void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		// S24 (2026-09-02): a 2026-08-01 diagnostic removed here (confirmed
		// LLMenuItemSeparatorGL's separator line does reach a real Draw()
		// call with correct color data, per its own comment - answered for
		// over a month, left logging on every one of a session's first 20
		// gl_line_2d() calls since).
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glLineTwoD(x1, y1, x2, y2, gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::LineList);
		}
	}
	return;
#else
	gDX.begin(LLRender::LINES);
	gDX.vertex2i(x1, y1);
	gDX.vertex2i(x2, y2);
	gDX.end();
#endif // DX_RENDER
}

void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4& color)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glLineTwoD(x1, y1, x2, y2, LLColor4U(color), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::LineList);
		}
	}
	return;
#else
	gDX.color4fv(color.mV);

	gDX.begin(LLRender::LINES);
	gDX.vertex2i(x1, y1);
	gDX.vertex2i(x2, y2);
	gDX.end();
#endif // DX_RENDER
}

void gl_triangle_2d(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glTriangleTwoD(x1, y1, x2, y2, x3, y3, color, filled, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(),
				filled ? DXUITopology::TriangleList : DXUITopology::LineStrip);
		}
	}
	return;
#else
	gDX.color4fv(color.mV);

	if (filled)
	{
		gDX.begin(LLRender::TRIANGLES);
	}
	else
	{
		gDX.begin(LLRender::LINE_LOOP);
	}
	gDX.vertex2i(x1, y1);
	gDX.vertex2i(x2, y2);
	gDX.vertex2i(x3, y3);
	gDX.end();
#endif // DX_RENDER
}

void gl_corners_2d(S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	// DXRender2DUtils::glCornersTwoD() applies the same length-clamping
	// internally - pass the original, unclamped length.
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glCornersTwoD(left, top, right, bottom, length, max_frac, gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::LineList);
		}
	}
	return;
#else
	length = llmin((S32)(max_frac * (right - left)), length);
	length = llmin((S32)(max_frac * (top - bottom)), length);
	gDX.begin(LLRender::LINES);
	gDX.vertex2i(left, top);
	gDX.vertex2i(left + length, top);

	gDX.vertex2i(left, top);
	gDX.vertex2i(left, top - length);

	gDX.vertex2i(left, bottom);
	gDX.vertex2i(left + length, bottom);

	gDX.vertex2i(left, bottom);
	gDX.vertex2i(left, bottom + length);

	gDX.vertex2i(right, top);
	gDX.vertex2i(right - length, top);

	gDX.vertex2i(right, top);
	gDX.vertex2i(right, top - length);

	gDX.vertex2i(right, bottom);
	gDX.vertex2i(right - length, bottom);

	gDX.vertex2i(right, bottom);
	gDX.vertex2i(right, bottom + length);
	gDX.end();
#endif // DX_RENDER
}

void gl_draw_image(S32 x, S32 y, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect)
{
	if (NULL == image)
	{
		LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
		return;
	}
	gl_draw_scaled_rotated_image(x, y, image->getWidth(0), image->getHeight(0), 0.f, image, color, uv_rect);
}

void gl_draw_scaled_target(S32 x, S32 y, S32 width, S32 height, LLRenderTarget* target, const LLColor4& color, const LLRectf& uv_rect)
{
	gl_draw_scaled_rotated_image(x, y, width, height, 0.f, NULL, color, uv_rect, target);
}

void gl_draw_scaled_image(S32 x, S32 y, S32 width, S32 height, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect)
{
	if (NULL == image)
	{
		LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
		return;
	}
	gl_draw_scaled_rotated_image(x, y, width, height, 0.f, image, color, uv_rect);
}

void gl_draw_scaled_image_with_border(S32 x, S32 y, S32 border_width, S32 border_height, S32 width, S32 height, LLTexture* image, const LLColor4& color, bool solid_color, const LLRectf& uv_rect, bool scale_inner)
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
	gl_draw_scaled_image_with_border(x, y, width, height, image, color, solid_color, uv_rect, scale_rect, scale_inner);
}

void gl_draw_scaled_image_with_border(S32 x, S32 y, S32 width, S32 height, LLTexture* image, const LLColor4& color, bool solid_color, const LLRectf& uv_outer_rect, const LLRectf& center_rect, bool scale_inner)
{
	stop_glerror();

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
		gl_draw_scaled_image(x, y, width, height, image, color, uv_outer_rect);
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

#ifdef DX_RENDER
			// S24 (DXRender2DUtils, 2026-07-25): relocated from an inline
			// fence into DXRender2DUtils::glDrawScaledImageWithBorderNineSlice()
			// for consistency with the rest of llrender2dutils.cpp's
			// DX_RENDER conversions (not because this was broken). No
			// explicit color array in the GL path above (color is carried
			// via gDX.color4fv()'s "current color" state) - applied
			// explicitly per-vertex by the relocated function instead, same
			// as before.
			// S24 (2026-08-17, task #54): recording-mode fallback, same
			// pattern/reasoning as gl_draw_scaled_rotated_image()'s matching
			// fix just above in this file.
			if (gDX.isRecording())
			{
				gDX.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
			}
			else if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
			{
				gDX.flush(); // draw-order fix, same rationale as the simple-quad case
				DXRender2DUtils::NineSliceGeometry geom;
				for (S32 v = 0; v < NUM_VERTICES; ++v)
				{
					const F32* p = pos[v].getF32ptr();
					geom.pos[v][0] = p[0]; geom.pos[v][1] = p[1]; geom.pos[v][2] = p[2];
					geom.uv[v][0] = uv[v].mV[0]; geom.uv[v][1] = uv[v].mV[1];
				}
				gDXUIBatch.flushPending();
				DXRender2DUtils::glDrawScaledImageWithBorderNineSlice(geom, color);
				gDX.syncMatrices();
				if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
				{
					gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
				}
			}
#else
			gDX.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
#endif
		}
		gDX.end();
	}

	if (solid_color)
	{
		gUIProgram.bind();
	}
}

void gl_draw_rotated_image(S32 x, S32 y, F32 degrees, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect)
{
	gl_draw_scaled_rotated_image(x, y, image->getWidth(0), image->getHeight(0), degrees, image, color, uv_rect);
}

void gl_draw_scaled_rotated_image(S32 x, S32 y, S32 width, S32 height, F32 degrees, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect, LLRenderTarget* target)
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
#ifndef DX_RENDER
		// S24 (DX_RENDER): LLTexUnit::bind(LLRenderTarget*, bool) has no
		// DX_RENDER handling yet (only caller is LLSceneMonitor's frame-diff
		// debug tool, off by default) - skip the bind rather than reach the
		// unconverted overload with a hardcoded unit; draws untextured under
		// DX_RENDER until that overload gets real handling.
		gDX.getTexUnit(0)->bind(target);
#endif
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

#ifdef DX_RENDER
			// S24 (DXRender2DUtils, 2026-07-25): relocated from an inline
			// fence into DXRender2DUtils::glDrawScaledImage() for
			// consistency with the rest of llrender2dutils.cpp's DX_RENDER
			// conversions (not because this was broken - it was the
			// original, proven pattern every later conversion copied). The
			// pos[]/uv[] math above is unchanged; only the final GPU
			// submission moved. Texture/sampler already bound via the
			// bind() call above; uses whatever shader is CURRENTLY bound
			// (gUIProgram or gSolidColorProgram - color4fv() above already
			// routes DIFFUSE_COLOR correctly either way).
			// S24 (2026-08-17, task #54): recording-mode fallback, same
			// pattern as LLFontGL::submitGlyphBatch()'s matching fix (see its
			// comment) - lets LLUIImage's display-list cache actually capture
			// something via LLRender::flush()'s existing sBufferDataList
			// logic instead of always taking the DXUIBatch fast path, which
			// never touches it.
			if (gDX.isRecording())
			{
				gDX.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
			}
			else if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
			{
				// Flush any still-queued gDX geometry before this
				// function's own immediate DXUIBatch draw - draw-order fix,
				// see LLFontGL::beginTextRender()'s matching comment.
				gDX.flush();
				DXRender2DUtils::ScaledImageGeometry geom;
				for (S32 v = 0; v < NUM_VERTICES; ++v)
				{
					const F32* p = pos[v].getF32ptr();
					geom.pos[v][0] = p[0]; geom.pos[v][1] = p[1]; geom.pos[v][2] = p[2];
					geom.uv[v][0] = uv[v].mV[0]; geom.uv[v][1] = uv[v].mV[1];
				}
				gDXUIBatch.flushPending();
				DXRender2DUtils::glDrawScaledImage(geom, color);
				gDX.syncMatrices();
				if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
				{
					gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
				}
			}
#else
			gDX.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
#endif
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
#ifndef DX_RENDER
			// S24 (DX_RENDER): see the matching comment in the degrees==0.f
			// branch above - same unconverted LLTexUnit::bind(LLRenderTarget*)
			// overload, same single debug-tool caller.
			gDX.getTexUnit(0)->bind(target);
#endif
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

		// S24 (2026-09-09, task TBD): this branch (degrees != 0.f) was
		// missing the gDX.begin(TRIANGLES)/gDX.end() wrapping that the
		// degrees==0.f branch above always had around its own DX_RENDER
		// gDXUIBatch submission - LLRender::begin() has a real, needed side
		// effect (llrender.cpp: if switching away from a LINES/TRIANGLES/
		// POINTS mode, it flushes gDX's own pending immediate-mode buffer
		// FIRST, before this batch starts) that nothing else in this
		// function was providing for the rotated case. end() itself is a
		// no-op here (mCount stays 0 on the gDXUIBatch path), but begin()'s
		// flush-barrier isn't - matches the proven-working structure now.
		gDX.begin(LLRender::TRIANGLES);
		{
#ifdef DX_RENDER
			// S24 (2026-08-16): this branch (degrees != 0.f) previously had no
			// DX_RENDER handling at all - always fell through to raw gDX
			// immediate mode below even under DX_RENDER, unlike the degrees==0.f
			// branch above. Same DXRender2DUtils::glDrawScaledImage()/
			// ScaledImageGeometry reuse as that branch - the 6-vertex triangle-
			// list shape is identical, only the position math (rotated vs
			// axis-aligned) differs.
			if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
			{
				gDX.flush();
				// S24 (2026-09-09, task TBD): THE actual bug - rv[] (built
				// above) is only the rotated corner offset RELATIVE TO THE
				// ICON'S OWN CENTER (matching the degrees==0.f branch's
				// per-vertex position BEFORE it adds ui_translation). This
				// code was pushing rv[] into geom.pos[] directly, with the
				// screen-space offset (x, y, and the center offset already
				// accumulated into gDX's UI-matrix stack via the two
				// translateUI() calls above) never added in at all - drawing
				// at the wrong location (near the tiny local rv[] values)
				// instead of the folder row's actual position, not
				// invisibly. Matches the degrees==0.f branch's own
				// ui_translation/ui_scale handling (llrender2dutils.cpp,
				// same function, ~40 lines up).
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
				DXRender2DUtils::glDrawScaledImage(geom, color);
				gDX.syncMatrices();
				if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
				{
					gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
				}
			}
#else
			for (S32 v = 0; v < 6; ++v)
			{
				gDX.texCoord2f(ruv[v].mV[0], ruv[v].mV[1]);
				gDX.vertex2f(rv[v].mV[0], rv[v].mV[1]);
			}
#endif
		}
		gDX.end();
		gDX.popUIMatrix();
	}
}

void gl_line_3d(const LLVector3& start, const LLVector3& end, const LLColor4& color)
{
	gDX.flush();
#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDXUIBatch.flushPending();
		DXRender2DUtils::glLineThreeD(start, end, color, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::LineList);
		}
	}
#else
	gDX.color4f(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE], color.mV[VALPHA]);
	glLineWidth(2.5f);

	gDX.begin(LLRender::LINES);
	{
		gDX.vertex3fv(start.mV);
		gDX.vertex3fv(end.mV);
	}
	gDX.end();
#endif // DX_RENDER

	LLRender2D::setLineWidth(1.f);
}

void gl_arc_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle)
{
#ifdef DX_RENDER
	// DXRender2DUtils::glArcTwoD() bakes (center_x, center_y) directly into
	// each vertex instead of via a pushUIMatrix()/translateUI() layer (see
	// its own header comment) - mathematically equivalent (translateUI only
	// ever touches offset, never scale, and addition is commutative), one
	// fewer stack push/pop.
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glArcTwoD(center_x, center_y, radius, steps, filled, start_angle, end_angle,
			gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(),
				filled ? DXUITopology::TriangleList : DXUITopology::LineStrip);
		}
	}
	return;
#else
	if (end_angle < start_angle)
	{
		end_angle += F_TWO_PI;
	}

	gDX.pushUIMatrix();
	{
		gDX.translateUI(center_x, center_y, 0.f);

		// Inexact, but reasonably fast.
		F32 delta = (end_angle - start_angle) / steps;
		F32 sin_delta = sin(delta);
		F32 cos_delta = cos(delta);
		F32 x = cosf(start_angle) * radius;
		F32 y = sinf(start_angle) * radius;

		if (filled)
		{
			gDX.begin(LLRender::TRIANGLE_FAN);
			gDX.vertex2f(0.f, 0.f);
			// make sure circle is complete
			steps += 1;
		}
		else
		{
			gDX.begin(LLRender::LINE_STRIP);
		}

		while (steps--)
		{
			// Successive rotations
			gDX.vertex2f(x, y);
			F32 x_new = x * cos_delta - y * sin_delta;
			y = x * sin_delta + y * cos_delta;
			x = x_new;
		}
		gDX.end();
	}
	gDX.popUIMatrix();
#endif // DX_RENDER
}

void gl_circle_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glCircleTwoD(center_x, center_y, radius, steps, filled,
			gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(),
				filled ? DXUITopology::TriangleList : DXUITopology::LineStrip);
		}
	}
	return;
#else
	gDX.pushUIMatrix();
	{
		gDX.translateUI(center_x, center_y, 0.f);

		// Inexact, but reasonably fast.
		F32 delta = F_TWO_PI / steps;
		F32 sin_delta = sin(delta);
		F32 cos_delta = cos(delta);
		F32 x = radius;
		F32 y = 0.f;

		if (filled)
		{
			gDX.begin(LLRender::TRIANGLE_FAN);
			gDX.vertex2f(0.f, 0.f);
			// make sure circle is complete
			steps += 1;
		}
		else
		{
			gDX.begin(LLRender::LINE_LOOP);
		}

		while (steps--)
		{
			// Successive rotations
			gDX.vertex2f(x, y);
			F32 x_new = x * cos_delta - y * sin_delta;
			y = x * sin_delta + y * cos_delta;
			x = x_new;
		}
		gDX.end();
	}
	gDX.popUIMatrix();
#endif // DX_RENDER
}

// Renders a ring with sides (tube shape)
void gl_deep_circle(F32 radius, F32 depth, S32 steps)
{
#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glDeepCircle(radius, depth, steps, gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::TriangleStrip);
		}
	}
	return;
#else
	F32 x = radius;
	F32 y = 0.f;
	F32 angle_delta = F_TWO_PI / (F32)steps;
	gDX.begin(LLRender::TRIANGLE_STRIP);
	{
		S32 step = steps + 1; // An extra step to close the circle.
		while (step--)
		{
			gDX.vertex3f(x, y, depth);
			gDX.vertex3f(x, y, 0.f);

			F32 x_new = x * cosf(angle_delta) - y * sinf(angle_delta);
			y = x * sinf(angle_delta) + y * cosf(angle_delta);
			x = x_new;
		}
	}
	gDX.end();
#endif // DX_RENDER
}

void gl_ring(F32 radius, F32 width, const LLColor4& center_color, const LLColor4& side_color, S32 steps, bool render_center)
{
	gDX.pushUIMatrix();
	{
		gDX.translateUI(0.f, 0.f, -width / 2);
		if (render_center)
		{
			gDX.color4fv(center_color.mV);
			gDX.diffuseColor4fv(center_color.mV);
			gl_deep_circle(radius, width, steps);
		}
		else
		{
			gDX.diffuseColor4fv(side_color.mV);
			gl_washer_2d(radius, radius - width, steps, side_color, side_color);
			gDX.translateUI(0.f, 0.f, width);
			gl_washer_2d(radius - width, radius, steps, side_color, side_color);
		}
	}
	gDX.popUIMatrix();
}

// Draw gray and white checkerboard with black border
void gl_rect_2d_checkerboard(const LLRect& rect, F32 alpha)
{
	//polygon stipple is deprecated, use "Checker" texture
	LLPointer<LLUIImage> img = LLRender2D::getInstance()->getUIImage("Checker");
	gDX.getTexUnit(0)->bind(img->getImage());
	gDX.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
	gDX.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);

	LLColor4 color(1.f, 1.f, 1.f, alpha);
	LLRectf uv_rect(0, 0, rect.getWidth() / 32.f, rect.getHeight() / 32.f);

	gl_draw_scaled_image(rect.mLeft, rect.mBottom, rect.getWidth(), rect.getHeight(), img->getImage(), color, uv_rect);

	gDX.flush();
}

// Draws the area between two concentric circles, like
// a doughnut or washer.
void gl_washer_2d(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glWasherTwoD(outer_radius, inner_radius, steps, inner_color, outer_color, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::TriangleStrip);
		}
	}
	return;
#else
	const F32 DELTA = F_TWO_PI / steps;
	const F32 SIN_DELTA = sin(DELTA);
	const F32 COS_DELTA = cos(DELTA);

	F32 x1 = outer_radius;
	F32 y1 = 0.f;
	F32 x2 = inner_radius;
	F32 y2 = 0.f;

	gDX.begin(LLRender::TRIANGLE_STRIP);
	{
		steps += 1; // An extra step to close the circle.
		while (steps--)
		{
			gDX.color4fv(outer_color.mV);
			gDX.vertex2f(x1, y1);
			gDX.color4fv(inner_color.mV);
			gDX.vertex2f(x2, y2);

			F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
			y1 = x1 * SIN_DELTA + y1 * COS_DELTA;
			x1 = x1_new;

			F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
			y2 = x2 * SIN_DELTA + y2 * COS_DELTA;
			x2 = x2_new;
		}
	}
	gDX.end();
#endif // DX_RENDER
}

// Draws the area between two concentric circles, like
// a doughnut or washer.
void gl_washer_segment_2d(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
	gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glWasherSegmentTwoD(outer_radius, inner_radius, start_radians, end_radians, steps,
			inner_color, outer_color, gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str(), DXUITopology::TriangleStrip);
		}
	}
	return;
#else
	const F32 DELTA = (end_radians - start_radians) / steps;
	const F32 SIN_DELTA = sin(DELTA);
	const F32 COS_DELTA = cos(DELTA);

	F32 x1 = outer_radius * cos(start_radians);
	F32 y1 = outer_radius * sin(start_radians);
	F32 x2 = inner_radius * cos(start_radians);
	F32 y2 = inner_radius * sin(start_radians);

	gDX.begin(LLRender::TRIANGLE_STRIP);
	{
		steps += 1; // An extra step to close the circle.
		while (steps--)
		{
			gDX.color4fv(outer_color.mV);
			gDX.vertex2f(x1, y1);
			gDX.color4fv(inner_color.mV);
			gDX.vertex2f(x2, y2);

			F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
			y1 = x1 * SIN_DELTA + y1 * COS_DELTA;
			x1 = x1_new;

			F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
			y2 = x2 * SIN_DELTA + y2 * COS_DELTA;
			x2 = x2_new;
		}
	}
	gDX.end();
#endif // DX_RENDER
}

void gl_rect_2d_simple_tex(S32 width, S32 height)
{
#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glRectTwoDSimpleTex(width, height, gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
		}
	}
	return;
#else
	gDX.begin(LLRender::TRIANGLES);

	gDX.texCoord2f(1.f, 1.f);
	gDX.vertex2i(width, height);

	gDX.texCoord2f(0.f, 1.f);
	gDX.vertex2i(0, height);

	gDX.texCoord2f(0.f, 0.f);
	gDX.vertex2i(0, 0);

	gDX.texCoord2f(1.f, 1.f);
	gDX.vertex2i(width, height);

	gDX.texCoord2f(0.f, 0.f);
	gDX.vertex2i(0, 0);

	gDX.texCoord2f(1.f, 0.f);
	gDX.vertex2i(width, 0);

	gDX.end();
#endif // DX_RENDER
}

void gl_rect_2d_simple(S32 width, S32 height)
{
#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glRectTwoDSimple(width, height, gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
		}
	}
	return;
#else
	gDX.begin(LLRender::TRIANGLES);
	gDX.vertex2i(width, height);
	gDX.vertex2i(0, height);
	gDX.vertex2i(0, 0);

	gDX.vertex2i(width, height);
	gDX.vertex2i(0, 0);
	gDX.vertex2i(width, 0);
	gDX.end();
#endif // DX_RENDER
}

void gl_segmented_rect_2d_tex(const S32 left,
	const S32 top,
	const S32 right,
	const S32 bottom,
	const S32 texture_width,
	const S32 texture_height,
	const S32 border_size,
	const U32 edges)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

	S32 width = llabs(right - left);
	S32 height = llabs(top - bottom);

	gDX.pushUIMatrix();

	gDX.translateUI((F32)left, (F32)bottom, 0.f);
	LLVector2 border_uv_scale((F32)border_size / (F32)texture_width, (F32)border_size / (F32)texture_height);

	if (border_uv_scale.mV[VX] > 0.5f)
	{
		border_uv_scale *= 0.5f / border_uv_scale.mV[VX];
	}
	if (border_uv_scale.mV[VY] > 0.5f)
	{
		border_uv_scale *= 0.5f / border_uv_scale.mV[VY];
	}

	F32 border_scale = llmin((F32)border_size, (F32)width * 0.5f, (F32)height * 0.5f);
	LLVector2 border_width_left = ((edges & (~(U32)ROUNDED_RECT_RIGHT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
	LLVector2 border_width_right = ((edges & (~(U32)ROUNDED_RECT_LEFT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
	LLVector2 border_height_bottom = ((edges & (~(U32)ROUNDED_RECT_TOP)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
	LLVector2 border_height_top = ((edges & (~(U32)ROUNDED_RECT_BOTTOM)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
	LLVector2 width_vec((F32)width, 0.f);
	LLVector2 height_vec(0.f, (F32)height);

	gDX.begin(LLRender::TRIANGLES);
	{
		// draw bottom left
		gDX.texCoord2f(0.f, 0.f);
		gDX.vertex2f(0.f, 0.f);

		gDX.texCoord2f(border_uv_scale.mV[VX], 0.f);
		gDX.vertex2fv(border_width_left.mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + border_height_bottom).mV);

		gDX.texCoord2f(0.f, 0.f);
		gDX.vertex2f(0.f, 0.f);

		gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + border_height_bottom).mV);

		gDX.texCoord2f(0.f, border_uv_scale.mV[VY]);
		gDX.vertex2fv(border_height_bottom.mV);

		// draw bottom middle
		gDX.texCoord2f(border_uv_scale.mV[VX], 0.f);
		gDX.vertex2fv(border_width_left.mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
		gDX.vertex2fv((width_vec - border_width_right).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 0.f);
		gDX.vertex2fv(border_width_left.mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + border_height_bottom).mV);

		// draw bottom right
		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
		gDX.vertex2fv((width_vec - border_width_right).mV);

		gDX.texCoord2f(1.f, 0.f);
		gDX.vertex2fv(width_vec.mV);

		gDX.texCoord2f(1.f, border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec + border_height_bottom).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
		gDX.vertex2fv((width_vec - border_width_right).mV);

		gDX.texCoord2f(1.f, border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec + border_height_bottom).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

		// draw left
		gDX.texCoord2f(0.f, border_uv_scale.mV[VY]);
		gDX.vertex2fv(border_height_bottom.mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + border_height_bottom).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + height_vec - border_height_top).mV);

		gDX.texCoord2f(0.f, border_uv_scale.mV[VY]);
		gDX.vertex2fv(border_height_bottom.mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + height_vec - border_height_top).mV);

		gDX.texCoord2f(0.f, 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((height_vec - border_height_top).mV);

		// draw middle
		gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + border_height_bottom).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + border_height_bottom).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + height_vec - border_height_top).mV);

		// draw right
		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

		gDX.texCoord2f(1.f, border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec + border_height_bottom).mV);

		gDX.texCoord2f(1.f, 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

		gDX.texCoord2f(1.f, 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

		// draw top left
		gDX.texCoord2f(0.f, 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((height_vec - border_height_top).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + height_vec - border_height_top).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f);
		gDX.vertex2fv((border_width_left + height_vec).mV);

		gDX.texCoord2f(0.f, 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((height_vec - border_height_top).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f);
		gDX.vertex2fv((border_width_left + height_vec).mV);

		gDX.texCoord2f(0.f, 1.f);
		gDX.vertex2fv((height_vec).mV);

		// draw top middle
		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
		gDX.vertex2fv((width_vec - border_width_right + height_vec).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((border_width_left + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
		gDX.vertex2fv((width_vec - border_width_right + height_vec).mV);

		gDX.texCoord2f(border_uv_scale.mV[VX], 1.f);
		gDX.vertex2fv((border_width_left + height_vec).mV);

		// draw top right
		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f, 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f, 1.f);
		gDX.vertex2fv((width_vec + height_vec).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
		gDX.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

		gDX.texCoord2f(1.f, 1.f);
		gDX.vertex2fv((width_vec + height_vec).mV);

		gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
		gDX.vertex2fv((width_vec - border_width_right + height_vec).mV);
	}
	gDX.end();

	gDX.popUIMatrix();
}

void gl_segmented_rect_2d_fragment_tex(const LLRect& rect,
	const S32 texture_width,
	const S32 texture_height,
	const S32 border_size,
	const F32 start_fragment,
	const F32 end_fragment,
	const U32 edges)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
#ifdef DX_RENDER
	// S24 (DXRender2DUtils, 2026-07-25): DXRender2DUtils::glSegmentedRectTwoDFragmentTex()
	// takes the same raw parameters and replicates this function's own
	// gDX.pushUIMatrix()/translateUI((F32)left,(F32)bottom,0.f) internally
	// (see its own comment) - so the DX_RENDER path bypasses all of the
	// shared setup math below entirely rather than duplicating it partially.
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glSegmentedRectTwoDFragmentTex(rect, texture_width, texture_height, border_size,
			start_fragment, end_fragment, edges, gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
		}
	}
	return;
#else
	const S32 left = rect.mLeft;
	const S32 right = rect.mRight;
	const S32 top = rect.mTop;
	const S32 bottom = rect.mBottom;
	S32 width = llabs(right - left);
	S32 height = llabs(top - bottom);

	gDX.pushUIMatrix();

	gDX.translateUI((F32)left, (F32)bottom, 0.f);
	LLVector2 border_uv_scale((F32)border_size / (F32)texture_width, (F32)border_size / (F32)texture_height);

	if (border_uv_scale.mV[VX] > 0.5f)
	{
		border_uv_scale *= 0.5f / border_uv_scale.mV[VX];
	}
	if (border_uv_scale.mV[VY] > 0.5f)
	{
		border_uv_scale *= 0.5f / border_uv_scale.mV[VY];
	}

	F32 border_scale = llmin((F32)border_size, (F32)width * 0.5f, (F32)height * 0.5f);
	LLVector2 border_width_left = ((edges & (~(U32)ROUNDED_RECT_RIGHT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
	LLVector2 border_width_right = ((edges & (~(U32)ROUNDED_RECT_LEFT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
	LLVector2 border_height_bottom = ((edges & (~(U32)ROUNDED_RECT_TOP)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
	LLVector2 border_height_top = ((edges & (~(U32)ROUNDED_RECT_BOTTOM)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
	LLVector2 width_vec((F32)width, 0.f);
	LLVector2 height_vec(0.f, (F32)height);

	F32 middle_start = border_scale / (F32)width;
	F32 middle_end = 1.f - middle_start;

	F32 u_min;
	F32 u_max;
	LLVector2 x_min;
	LLVector2 x_max;

	gDX.begin(LLRender::TRIANGLES);
	{
		if (start_fragment < middle_start)
		{
			u_min = (start_fragment / middle_start) * border_uv_scale.mV[VX];
			u_max = llmin(end_fragment / middle_start, 1.f) * border_uv_scale.mV[VX];
			x_min = (start_fragment / middle_start) * border_width_left;
			x_max = llmin(end_fragment / middle_start, 1.f) * border_width_left;

			// draw bottom left
			gDX.texCoord2f(u_min, 0.f);
			gDX.vertex2fv(x_min.mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], 0.f);
			gDX.vertex2fv(x_max.mV);

			gDX.texCoord2f(u_max, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(u_min, 0.f);
			gDX.vertex2fv(x_min.mV);

			gDX.texCoord2f(u_max, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(u_min, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			// draw left
			gDX.texCoord2f(u_min, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			gDX.texCoord2f(u_max, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_min, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			gDX.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			// draw top left
			gDX.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_max, 1.f);
			gDX.vertex2fv((x_max + height_vec).mV);

			gDX.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_max, 1.f);
			gDX.vertex2fv((x_max + height_vec).mV);

			gDX.texCoord2f(u_min, 1.f);
			gDX.vertex2fv((x_min + height_vec).mV);
		}

		if (end_fragment > middle_start || start_fragment < middle_end)
		{
			x_min = border_width_left + ((llclamp(start_fragment, middle_start, middle_end) - middle_start)) * width_vec;
			x_max = border_width_left + ((llclamp(end_fragment, middle_start, middle_end) - middle_start)) * width_vec;

			// draw bottom middle
			gDX.texCoord2f(border_uv_scale.mV[VX], 0.f);
			gDX.vertex2fv(x_min.mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
			gDX.vertex2fv((x_max).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], 0.f);
			gDX.vertex2fv(x_min.mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			// draw middle
			gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			// draw top middle
			gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
			gDX.vertex2fv((x_max + height_vec).mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			gDX.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
			gDX.vertex2fv((x_max + height_vec).mV);

			gDX.texCoord2f(border_uv_scale.mV[VX], 1.f);
			gDX.vertex2fv((x_min + height_vec).mV);
		}

		if (end_fragment > middle_end)
		{
			u_min = 1.f - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
			u_max = 1.f - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
			x_min = width_vec - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_width_right);
			x_max = width_vec - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_width_right);

			// draw bottom right
			gDX.texCoord2f(u_min, 0.f);
			gDX.vertex2fv((x_min).mV);

			gDX.texCoord2f(u_max, 0.f);
			gDX.vertex2fv(x_max.mV);

			gDX.texCoord2f(u_max, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(u_min, 0.f);
			gDX.vertex2fv((x_min).mV);

			gDX.texCoord2f(u_max, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(u_min, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			// draw right
			gDX.texCoord2f(u_min, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			gDX.texCoord2f(u_max, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + border_height_bottom).mV);

			gDX.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_min, border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + border_height_bottom).mV);

			gDX.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			// draw top right
			gDX.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_max + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_max, 1.f);
			gDX.vertex2fv((x_max + height_vec).mV);

			gDX.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
			gDX.vertex2fv((x_min + height_vec - border_height_top).mV);

			gDX.texCoord2f(u_max, 1.f);
			gDX.vertex2fv((x_max + height_vec).mV);

			gDX.texCoord2f(u_min, 1.f);
			gDX.vertex2fv((x_min + height_vec).mV);
		}
	}
	gDX.end();

	gDX.popUIMatrix();
#endif // DX_RENDER
}

void gl_segmented_rect_3d_tex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
	const LLVector3& width_vec, const LLVector3& height_vec)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

#ifdef DX_RENDER
	if (LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		DXRender2DUtils::glSegmentedRectThreeDTex(clip_rect, center_uv_rect, center_draw_rect, width_vec, height_vec,
			gDX.getCurrentColor(), gDX.getUITranslation(), gDX.getUIScale());
		gDX.syncMatrices();
		if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
		{
			// S24 (2026-08-11, task #191): this is the one gl_segmented_rect_*
			// caller that draws genuine 3D world-space content (LLHUDNameTag's
			// nametag panel, via LLUIImage::draw3D()) rather than 2D screen-
			// space UI - its caller wraps it in LLGLDepthTest(GL_TRUE,
			// GL_FALSE), wanting real depth-test occlusion against world
			// geometry. Tried passing that through to DXUIBatch::flush() at
			// the time - caused a regression (panel disappeared entirely):
			// the swap chain's depth-stencil genuinely had no real scene
			// depth in it yet back then, only a far-clear (task #193).
			//
			// S24 (2026-08-31, task #193 follow-up): that root gap is gone
			// now, just never reconnected back to this comment - task #139
			// (2026-08-17, six days after the regression above) made
			// DXPipeline::presentFinal() unconditionally write real scene
			// depth into the swap chain's depth-stencil via SV_Depth
			// (postDeferredNoDoFF.hlsl, the single "put this on screen"
			// chokepoint every post-fx path funnels through, GL_ALWAYS/
			// depth-write-on) - confirmed via fresh read of dxpipeline.cpp,
			// not assumed. presentFinal() runs at the tail of
			// presentDeferredScreen(), which LLPipeline::renderFinalize()
			// calls strictly before the UI/HUD pass - real depth should be
			// present by the time this draws. Also found+fixed a genuinely
			// separate, currently-live bug while checking: DXUIBatch::
			// flush()'s own depth_func default was still D3D11_COMPARISON_
			// LESS_EQUAL, correct under the old near=0/far=1 convention but
			// backwards since the reversed-Z conversion (task #289) -
			// fixed to GREATER_EQUAL (DXUIBatch.h). depth_test is
			// deliberately left false here regardless - real nametag
			// occlusion is already solved a different, working way (task
			// #272, occlusion queries, r3686) and there's no reason to risk
			// re-touching that path just to prove this one out. If this
			// exact regression is ever re-attempted, the infrastructure
			// should now be correct - but genuinely untested live, verify
			// before trusting it for anything that matters.
			gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
		}
	}
	return;
#else
	gDX.begin(LLRender::TRIANGLES);
	{
		// draw bottom left
		gDX.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
		gDX.vertex3f(0.f, 0.f, 0.f);

		gDX.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
		gDX.vertex3f(0.f, 0.f, 0.f);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

		// draw bottom middle
		gDX.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

		// draw bottom right
		gDX.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, clip_rect.mBottom);
		gDX.vertex3fv(width_vec.mV);

		gDX.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

		// draw left
		gDX.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mTop * height_vec).mV);

		// draw middle
		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

		// draw right
		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

		// draw top left
		gDX.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

		gDX.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

		gDX.texCoord2f(clip_rect.mLeft, clip_rect.mTop);
		gDX.vertex3fv((height_vec).mV);

		// draw top middle
		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

		// draw top right
		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, clip_rect.mTop);
		gDX.vertex3fv((width_vec + height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

		gDX.texCoord2f(clip_rect.mRight, clip_rect.mTop);
		gDX.vertex3fv((width_vec + height_vec).mV);

		gDX.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
		gDX.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);
	}
	gDX.end();
#endif // DX_RENDER
}

LLRender2D::LLRender2D(LLImageProviderInterface* image_provider)
{
	mImageProvider = image_provider;
	if (mImageProvider)
	{
		mImageProvider->addOnRemovalCallback(resetProvider);
	}
}

LLRender2D::~LLRender2D()
{
	if (mImageProvider)
	{
		mImageProvider->cleanUp();
		mImageProvider->deleteOnRemovalCallback(resetProvider);
	}
}

// static
void LLRender2D::translate(F32 x, F32 y, F32 z)
{
	gDX.translateUI(x, y, z);
	LLFontGL::sCurOrigin.mX += (S32)x;
	LLFontGL::sCurOrigin.mY += (S32)y;
	LLFontGL::sCurDepth += z;
}

// static
void LLRender2D::pushMatrix()
{
	gDX.pushUIMatrix();
	LLFontGL::sOriginStack.push_back(std::make_pair(LLFontGL::sCurOrigin, LLFontGL::sCurDepth));
}

// static
void LLRender2D::popMatrix()
{
	gDX.popUIMatrix();
	LLFontGL::sCurOrigin = LLFontGL::sOriginStack.back().first;
	LLFontGL::sCurDepth = LLFontGL::sOriginStack.back().second;
	LLFontGL::sOriginStack.pop_back();
}

// static
void LLRender2D::loadIdentity()
{
	gDX.loadUIIdentity();
	LLFontGL::sCurOrigin.mX = 0;
	LLFontGL::sCurOrigin.mY = 0;
	LLFontGL::sCurDepth = 0.f;
}

// static
void LLRender2D::setLineWidth(F32 width)
{
	gDX.flush();
#ifndef DX_RENDER
	// If outside the allowed range, glLineWidth fails with "invalid value".
	// On Darwin, the range is [1, 1].
	static F32 range[2]{ 0.0 };
	if (range[1] == 0)
	{
		glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, range);
	}
	width *= lerp(LLRender::sUIGLScaleFactor.mV[VX], LLRender::sUIGLScaleFactor.mV[VY], 0.5f);
	glLineWidth(llclamp(width, range[0], range[1]));
#endif
	// S24 (DX_RENDER): no per-draw line-width equivalent in D3D11 - lines
	// always render at width 1 under DX_RENDER, matching the existing
	// "no depth-bias/polygon-offset equivalent" class of visual-quality gap.
}

LLPointer<LLUIImage> LLRender2D::getUIImageByID(const LLUUID& image_id, S32 priority)
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

LLPointer<LLUIImage> LLRender2D::getUIImage(const std::string& name, S32 priority)
{
	if (!name.empty() && mImageProvider)
		return mImageProvider->getUIImage(name, priority);
	else
		return NULL;
}

// static
void LLRender2D::resetProvider()
{
	if (LLRender2D::instanceExists())
	{
		LLRender2D::getInstance()->mImageProvider = NULL;
	}
}

// class LLImageProviderInterface

LLImageProviderInterface::~LLImageProviderInterface()
{
	for (callback_list_t::iterator iter = mCallbackList.begin(); iter != mCallbackList.end();)
	{
		callback_list_t::iterator curiter = iter++;
		(*curiter)();
	}
}

void LLImageProviderInterface::addOnRemovalCallback(callback_t func)
{
	if (!func)
	{
		return;
	}
	mCallbackList.push_back(func);
}

void LLImageProviderInterface::deleteOnRemovalCallback(callback_t func)
{
	callback_list_t::iterator iter = std::find(mCallbackList.begin(), mCallbackList.end(), func);
	if (iter != mCallbackList.end())
	{
		mCallbackList.erase(iter);
	}
}
