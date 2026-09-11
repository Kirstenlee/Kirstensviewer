/**
 * @file lldxlinewidth.h
 * @brief Screen-space-constant-width "thick line" billboard quad - the
 * standard replacement for D3D11's missing per-draw line-width control.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Kirstens Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_LLDXLINEWIDTH_H
#define LL_LLDXLINEWIDTH_H

#include "v3math.h"
#include "v4color.h"

// S24 (2026-09-05, task #309): D3D11's rasterizer has no per-draw
// line-width control at all - a real cross-API gap, not a DX_RENDER port
// gap (see dxrender/core/DXStateCache.h's own comment on this). Every
// glLineWidth(N>1)-dependent GL_LINES draw is silently a 1px no-op under
// DX_RENDER. The proven replacement, established independently for the
// selection beam (llhudeffecttrail.cpp, 2026-08-16) and the beacon pillar
// (llglsandbox.cpp, 2026-09-04) before being unified here: draw the line as
// a real camera-facing billboard quad, sized in world-space meters PER
// ENDPOINT so it holds a constant on-screen pixel width regardless of how
// close the camera gets to either end.
//
// Caller must already be inside a gDX.begin(LLRender::TRIANGLES)/end()
// block bracketing one or more calls to this - it only emits vertices
// (6 per call, two triangles), the same convention the two sites above
// already used before this was factored out.
//
// GOTCHA (found 2026-09-05 fixing LLManip::renderGuidelines() - see task
// #309): width is computed ONLY at start/end and linearly interpolated
// across the quad in between - there is no per-fragment or intermediate-
// vertex distance correction. This is fine whenever the line's closest
// approach to the camera is at (or near) one of its two endpoints, as it
// is for a beam/beacon/post. It breaks down for a long line whose closest
// approach to the camera falls somewhere in the MIDDLE of the span (e.g. a
// line drawn symmetrically through a nearby point out to two far ends) -
// neither real vertex is actually close to the camera, so the visible
// near-camera portion gets dragged toward the far, wide interpolated width
// instead of its own true (small) one. Split the line at the near point
// (call this function once per half, both anchored there) instead of
// spanning straight across it.
//
// SECOND GOTCHA (same investigation): this draws LLRender::TRIANGLES,
// where the old GL_LINES call it's replacing did not - DXUIBatch's
// batching key includes topology, so a GL_LINES draw could never merge
// with an adjacent TRIANGLES draw. Converting to this function removes
// that accidental separation. If per-draw color is set via
// diffuseColor4ub()/diffuseColor4f() (a shader UNIFORM, used whenever the
// bound shader lacks the MAP_COLOR vertex attribute) rather than true
// per-vertex color, a merged batch only keeps the LAST uniform value set -
// so a differently-colored TRIANGLES draw immediately before or after a
// dxLineWidth() call can visually take on ITS color instead of its own.
// Add an explicit gDXUIBatch.flushPending() (guarded #ifdef DX_RENDER) at
// that boundary if this shows up - see LLManip::renderGuidelines()'s own
// fix for a worked example.
void dxLineWidth(const LLVector3& start, const LLVector3& end,
                  F32 half_pixel_width,
                  const LLColor4& start_color, const LLColor4& end_color);

inline void dxLineWidth(const LLVector3& start, const LLVector3& end,
                         F32 half_pixel_width, const LLColor4& color)
{
    dxLineWidth(start, end, half_pixel_width, color, color);
}

#endif // LL_LLDXLINEWIDTH_H
