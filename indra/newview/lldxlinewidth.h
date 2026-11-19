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

// D3D11's rasterizer has no per-draw line-width control (glLineWidth(N>1) is
// a silent 1px no-op under DX_RENDER). Draws the line as a camera-facing
// billboard quad instead, sized in world-space meters per endpoint so it
// holds constant on-screen width regardless of camera distance. Caller must
// already be inside a gDX.begin(LLRender::TRIANGLES)/end() block; this only
// emits vertices (6 per call, two triangles).
//
// GOTCHA: width interpolates linearly between the two endpoints only -
// breaks down when the line's closest approach to the camera falls in the
// MIDDLE of the span. Split the line at the near point instead.
//
// GOTCHA: draws TRIANGLES (not GL_LINES), so it can batch-merge with
// adjacent TRIANGLES draws. If per-draw color comes from a shader uniform
// (diffuseColor4ub/f) rather than per-vertex color, a merged batch keeps
// only the LAST uniform value set - add an explicit
// gDXUIBatch.flushPending() at that boundary if this causes color bleed.
void dxLineWidth(const LLVector3& start, const LLVector3& end,
                  F32 half_pixel_width,
                  const LLColor4& start_color, const LLColor4& end_color);

inline void dxLineWidth(const LLVector3& start, const LLVector3& end,
                         F32 half_pixel_width, const LLColor4& color)
{
    dxLineWidth(start, end, half_pixel_width, color, color);
}

#endif // LL_LLDXLINEWIDTH_H
