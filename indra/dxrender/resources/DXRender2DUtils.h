#pragma once
#include "v4color.h"
#include "v4coloru.h"
#include "v3math.h"
#include "llrect.h"

// Native D3D11 implementations of llrender/llrender2dutils.cpp's GL
// immediate-mode 2D/UI primitives (gl_rect_2d, gl_line_2d, gl_circle_2d,
// ...). See the session plan file's "consolidate llrender2dutils.cpp into
// a real DXRender2DUtils" section for the full rationale - in short:
// llrender2dutils.cpp kept growing large inline `#ifdef DX_RENDER` blocks
// function-by-function as each one got chased down as a rendering gap;
// this file is the dedicated home those blocks move into (and new
// conversions land in), matching the existing dxrender/resources/
// convention (DXUIBatch, DXTexture, DXSampler, ...).
//
// Every function here is stateless - builds a local CPU-side vertex array
// mirroring the GL function's own geometry math exactly, then ENQUEUES it
// via gDXUIBatch.push() only. It deliberately does NOT call
// gDXUIBatch.flush() itself: flush() needs the bound shader's VS bytecode
// and a prior gGL.syncMatrices() call, both of which require llrender
// (LLGLSLShader::sCurBoundShaderPtr, gGL) - and dxrender/ has zero llrender
// dependency by design (see DXUIBatch.h/DXVertexLayout.h's own comments).
// The llrender2dutils.cpp fence that calls into this file does the actual
// gGL.syncMatrices()/gDXUIBatch.flush(...) sequence itself immediately
// after, exactly like every DXUIBatch caller before this file existed.
// Textures/samplers/shaders are the same story - the fence has already
// bound whatever it needs (gGL.getTexUnit(0)->bind()/gUIProgram.bind()/
// gSolidColorProgram.bind()) before calling in here. Every function here
// takes its color explicitly rather than reading GL's ambient "current
// color" state; callers that don't have an explicit color of their own
// use LLRender::getCurrentColor() (llrender.h) to capture it at the fence,
// the one point that still has access to it.
//
// GL_LINE_LOOP and GL_TRIANGLE_FAN have no native D3D11 topology
// equivalent (removed after D3D9) - functions using them expand to
// LineStrip (loop: duplicate the first vertex onto the end) or
// TriangleList (fan: emit independent triangles (v0, vi, vi+1)) here,
// rather than teaching DXUIBatch itself about GL-specific quirks.
//
// `ui_offset`/`ui_scale`: EVERY function below that takes raw pixel
// coordinates needs these. Under GL, gGL.vertex2i()/vertex2f()/vertex3f()
// (which every one of these GL functions' bodies calls) silently applies
// LLRender's CPU-side "UI offset/scale" stack to every vertex before it
// ever reaches the GPU - confirmed by reading LLRender::transform():
// `vert += mUIOffset.back(); vert *= mUIScale.back();`, and confirmed that
// pushUIMatrix()/translateUI()/scaleUI()/popUIMatrix() (called throughout
// this codebase, including LLViewerWindow::draw()'s own top-level
// gGL.scaleUI(mDisplayScale...) for DPI scaling) manipulate that exact
// same mUIOffset/mUIScale stack - NOT the real GPU matrix stack
// syncMatrices() uploads, which is an entirely separate, unrelated
// mechanism. Since these functions bypass gGL.vertex2i() entirely (they
// build DXUIVertex arrays directly), that automatic per-vertex transform
// has to be replicated by hand: `final = (raw + ui_offset); final *=
// ui_scale` (component-wise), applied to every position, exactly
// mirroring LLRender::transform()'s own formula. The llrender2dutils.cpp
// fence passes `gGL.getUITranslation()`/`gGL.getUIScale()` in (empty-stack-
// safe: they return zero/one when no UI frame is pushed, confirmed by
// reading their implementation - always safe to apply unconditionally).
namespace DXRender2DUtils
{
    // gl_rect_2d(left, top, right, bottom, filled) - both the plain and
    // color-parameterized GL overloads land here (see llrender2dutils.cpp's
    // fence - the color-taking wrappers already call gGL.color4fv() before
    // reaching the base overload, so capturing gGL.getCurrentColor() at the
    // base overload's own fence handles every entry point uniformly).
    // `filled`: two-triangle quad. Unfilled: 5-vertex closed LineStrip
    // outline, matching GL's LINE_STRIP outline (last vertex re-closes to
    // the first).
    void glRectTwoD(S32 left, S32 top, S32 right, S32 bottom, const LLColor4U& color, bool filled, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glDrawX(const LLRect& rect, const LLColor4& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glDropShadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glLineTwoD(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glLineThreeD(const LLVector3& start, const LLVector3& end, const LLColor4& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glTriangleTwoD(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glCornersTwoD(S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glRectTwoDSimple(S32 width, S32 height, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glRectTwoDSimpleTex(S32 width, S32 height, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    // gl_arc_2d - filled: TriangleFan (expanded to TriangleList here).
    // Unfilled: LineStrip (open, matches GL - callers that want a closed
    // ring use the full-circle end_angle/start_angle wraparound already
    // handled by the GL math this mirrors, not a LineLoop close). Center
    // (center_x, center_y) is baked in directly here rather than via a
    // pushUIMatrix()/translateUI() layer (same net effect, one fewer
    // stack push - the fence would otherwise need to push/pop around the
    // flush() call to keep the translation active through syncMatrices()).
    void glArcTwoD(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    // gl_circle_2d - filled: TriangleFan (expanded). Unfilled: LineLoop
    // (expanded to a closed LineStrip - duplicate the first vertex onto
    // the end).
    void glCircleTwoD(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    // Native TriangleStrip - no expansion needed. 3D (radius/depth along Z).
    void glDeepCircle(F32 radius, F32 depth, S32 steps, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glWasherTwoD(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glWasherSegmentTwoD(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    // gl_draw_scaled_rotated_image()'s degrees==0.f case - relocated from
    // its original inline fence for consistency with every other function
    // in this file (not because it was broken). Takes already-fully-
    // resolved absolute screen positions (the existing fence already
    // computes these via getUITranslation()/getUIScale() before this was
    // extracted - relocation preserves that exact math unchanged).
    struct ScaledImageGeometry
    {
        float pos[6][3];
        float uv[6][2];
    };
    void glDrawScaledImage(const ScaledImageGeometry& geom, const LLColor4& color);

    // gl_draw_scaled_image_with_border()'s 9-slice branch - relocated from
    // its original inline fence, same non-broken relocation as above.
    struct NineSliceGeometry
    {
        static constexpr int NUM_VERTICES = 9 * 2 * 3;
        float pos[NUM_VERTICES][3];
        float uv[NUM_VERTICES][2];
    };
    void glDrawScaledImageWithBorderNineSlice(const NineSliceGeometry& geom, const LLColor4& color);

    void glSegmentedRectTwoDFragmentTex(const LLRect& rect, S32 texture_width, S32 texture_height, S32 border_size,
        F32 start_fragment, F32 end_fragment, U32 edges, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);

    void glSegmentedRectThreeDTex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
        const LLVector3& width_vec, const LLVector3& height_vec, const LLColor4U& color, const LLVector3& ui_offset, const LLVector3& ui_scale);
}
