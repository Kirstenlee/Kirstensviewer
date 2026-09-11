/**
 * @file lldxlinewidth.cpp
 * @brief Screen-space-constant-width "thick line" billboard quad - the
 * standard replacement for D3D11's missing per-draw line-width control.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Kirstens Viewer Source Code
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lldxlinewidth.h"

#include "llrender.h"
#include "llviewercamera.h"

void dxLineWidth(const LLVector3& start, const LLVector3& end,
                  F32 half_pixel_width,
                  const LLColor4& start_color, const LLColor4& end_color)
{
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    F32 pixel_meter_ratio = camera->getPixelMeterRatio();
    LLVector3 cam_origin = camera->getOrigin();

    LLVector3 line_vec = end - start;
    if (line_vec.lengthSquared() < 0.000001f)
    {
        return;
    }
    line_vec.normalize();

    LLVector3 to_camera = cam_origin - start;
    to_camera.normalize();

    LLVector3 width_dir = line_vec % to_camera;
    if (width_dir.lengthSquared() < 0.000001f)
    {
        // Line points directly at the camera - fall back to the camera's
        // up axis so the quad doesn't degenerate to zero width.
        width_dir = line_vec % camera->getUpAxis();
    }
    width_dir.normalize();

    auto half_width_at = [pixel_meter_ratio, cam_origin, half_pixel_width](const LLVector3& p) -> F32
    {
        F32 dist = (cam_origin - p).length();
        return half_pixel_width * dist / pixel_meter_ratio;
    };

    LLVector3 wa = width_dir * half_width_at(start);
    LLVector3 wb = width_dir * half_width_at(end);

    LLVector3 v1 = start - wa;
    LLVector3 v2 = start + wa;
    LLVector3 v3 = end + wb;
    LLVector3 v4 = end - wb;

    gDX.color4fv(start_color.mV);
    gDX.vertex3fv(v1.mV);
    gDX.vertex3fv(v2.mV);
    gDX.color4fv(end_color.mV);
    gDX.vertex3fv(v3.mV);

    gDX.color4fv(start_color.mV);
    gDX.vertex3fv(v1.mV);
    gDX.color4fv(end_color.mV);
    gDX.vertex3fv(v3.mV);
    gDX.vertex3fv(v4.mV);
}
