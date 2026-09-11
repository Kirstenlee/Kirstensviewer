/**
 * @file llglsandbox.cpp
 * @brief GL functionality access
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

/**
 * Contains ALL methods which directly access GL functionality
 * except for core rendering engine functionality.
 */

#include "llviewerprecompiledheaders.h"

#include "llviewercontrol.h"

#include "llgl.h"
#include "llrender.h"
#include "llglheaders.h"
#include "llparcel.h"
#include "llui.h"

#include "lldrawable.h"
#include "lltextureentry.h"
#include "llviewercamera.h"

#include "llvoavatarself.h"
#include "llsky.h"
#include "llagent.h"
#include "lltoolmgr.h"
#include "llselectmgr.h"
#include "llhudmanager.h"
#include "llhudtext.h"
#include "llrendersphere.h"
#include "llviewerobjectlist.h"
#include "lltoolselectrect.h"
#include "llviewerwindow.h"
#include "llsurface.h"
#include "llwind.h"
#include "llworld.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llpreviewtexture.h"
#include "llresmgr.h"
#include "pipeline.h"
#include "llspatialpartition.h"
#include "llviewershadermgr.h"
#include "lldxlinewidth.h"

#include <vector>

// Height of the yellow selection highlight posts for land
constexpr F32 PARCEL_POST_HEIGHT = 0.666f;

// Returns true if you got at least one object
void LLToolSelectRect::handleRectangleSelection(S32 x, S32 y, MASK mask)
{
    LLVector3 av_pos = gAgent.getPositionAgent();
    F32 select_dist_squared = gSavedSettings.getF32("MaxSelectDistance");
    select_dist_squared = select_dist_squared * select_dist_squared;

    bool deselect = (mask == MASK_CONTROL);
    S32 left =  llmin(x, mDragStartX);
    S32 right = llmax(x, mDragStartX);
    S32 top =   llmax(y, mDragStartY);
    S32 bottom =llmin(y, mDragStartY);

    left = ll_round((F32) left * LLUI::getScaleFactor().mV[VX]);
    right = ll_round((F32) right * LLUI::getScaleFactor().mV[VX]);
    top = ll_round((F32) top * LLUI::getScaleFactor().mV[VY]);
    bottom = ll_round((F32) bottom * LLUI::getScaleFactor().mV[VY]);

    F32 old_far_plane = LLViewerCamera::getInstance()->getFar();
    F32 old_near_plane = LLViewerCamera::getInstance()->getNear();

    S32 width = right - left + 1;
    S32 height = top - bottom + 1;

    bool grow_selection = false;
    bool shrink_selection = false;

    if (height > mDragLastHeight || width > mDragLastWidth)
    {
        grow_selection = true;
    }
    if (height < mDragLastHeight || width < mDragLastWidth)
    {
        shrink_selection = true;
    }

    if (!grow_selection && !shrink_selection)
    {
        // nothing to do
        return;
    }

    mDragLastHeight = height;
    mDragLastWidth = width;

    S32 center_x = (left + right) / 2;
    S32 center_y = (top + bottom) / 2;

    // save drawing mode
    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.pushMatrix();

    bool limit_select_distance = gSavedSettings.getBOOL("LimitSelectDistance");
    if (limit_select_distance)
    {
        // ...select distance from control
        LLVector3 relative_av_pos = av_pos;
        relative_av_pos -= LLViewerCamera::getInstance()->getOrigin();

        F32 new_far = relative_av_pos * LLViewerCamera::getInstance()->getAtAxis() + gSavedSettings.getF32("MaxSelectDistance");
        F32 new_near = relative_av_pos * LLViewerCamera::getInstance()->getAtAxis() - gSavedSettings.getF32("MaxSelectDistance");

        new_near = llmax(new_near, 0.1f);

        LLViewerCamera::getInstance()->setFar(new_far);
        LLViewerCamera::getInstance()->setNear(new_near);
    }
    LLViewerCamera::getInstance()->setPerspective(FOR_SELECTION,
                            center_x-width/2, center_y-height/2, width, height,
                            limit_select_distance);

    if (shrink_selection)
    {
        struct f : public LLSelectedObjectFunctor
        {
            virtual bool apply(LLViewerObject* vobjp)
            {
                LLDrawable* drawable = vobjp->mDrawable;
                if (!drawable || vobjp->getPCode() != LL_PCODE_VOLUME || vobjp->isAttachment())
                {
                    return true;
                }
                S32 result = LLViewerCamera::getInstance()->sphereInFrustum(drawable->getPositionAgent(), drawable->getRadius());
                switch (result)
                {
                  case 0:
                    LLSelectMgr::getInstance()->unhighlightObjectOnly(vobjp);
                    break;
                  case 1:
                    // check vertices
                    if (!LLViewerCamera::getInstance()->areVertsVisible(vobjp, LLSelectMgr::sRectSelectInclusive))
                    {
                        LLSelectMgr::getInstance()->unhighlightObjectOnly(vobjp);
                    }
                    break;
                  default:
                    break;
                }
                return true;
            }
        } func;
        LLSelectMgr::getInstance()->getHighlightedObjects()->applyToObjects(&func);
    }

    if (grow_selection)
    {
        std::vector<LLDrawable*> potentials;

        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
            {
                if (LLSpatialPartition* part = region->getSpatialPartition(i))
                {
                    part->cull(*LLViewerCamera::getInstance(), &potentials, true);
                }
            }
        }

        for (LLDrawable* drawable : potentials)
        {
            if (!drawable)
            {
                continue;
            }

            LLViewerObject* vobjp = drawable->getVObj();

            if (!vobjp ||
                vobjp->getPCode() != LL_PCODE_VOLUME ||
                vobjp->isAttachment() ||
                (deselect && !vobjp->isSelected()))
            {
                continue;
            }

            if (limit_select_distance && dist_vec_squared(drawable->getWorldPosition(), av_pos) > select_dist_squared)
            {
                continue;
            }

            S32 result = LLViewerCamera::getInstance()->sphereInFrustum(drawable->getPositionAgent(), drawable->getRadius());
            if (result)
            {
                switch (result)
                {
                case 1:
                    // check vertices
                    if (LLViewerCamera::getInstance()->areVertsVisible(vobjp, LLSelectMgr::sRectSelectInclusive))
                    {
                        LLSelectMgr::getInstance()->highlightObjectOnly(vobjp);
                    }
                    break;
                case 2:
                    LLSelectMgr::getInstance()->highlightObjectOnly(vobjp);
                    break;
                default:
                    break;
                }
            }
        }
    }

    // restore drawing mode
    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.popMatrix();
    gDX.matrixMode(LLRender::MM_MODELVIEW);

    // restore camera
    LLViewerCamera::getInstance()->setFar(old_far_plane);
    LLViewerCamera::getInstance()->setNear(old_near_plane);
    gViewerWindow->setup3DRender();
}

constexpr F32 WIND_RELATIVE_ALTITUDE = 25.f;

void LLWind::renderVectors()
{
    // Renders the wind as vectors (used for debug)
    S32 i,j;
    F32 x,y;

    F32 region_width_meters = LLWorld::getInstance()->getRegionWidthInMeters();

    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gDX.pushMatrix();
    LLVector3 origin_agent;
    origin_agent = gAgent.getPosAgentFromGlobal(mOriginGlobal);
    gDX.translatef(origin_agent.mV[VX], origin_agent.mV[VY], gAgent.getPositionAgent().mV[VZ] + WIND_RELATIVE_ALTITUDE);
    for (j = 0; j < mSize; j++)
    {
        for (i = 0; i < mSize; i++)
        {
            x = mVelX[i + j*mSize] * WIND_SCALE_HACK;
            y = mVelY[i + j*mSize] * WIND_SCALE_HACK;
            gDX.pushMatrix();
            gDX.translatef((F32)i * region_width_meters/mSize, (F32)j * region_width_meters/mSize, 0.f);
            gDX.color3f(0.f, 1.f, 0.f);
            gDX.begin(LLRender::POINTS);
                gDX.vertex3f(0.f, 0.f, 0.f);
            gDX.end();
            gDX.color3f(1.f, 0.f, 0.f);
            gDX.begin(LLRender::LINES);
                gDX.vertex3f(x * 0.1f, y * 0.1f, 0.f);
                gDX.vertex3f(x, y, 0.f);
            gDX.end();
            gDX.popMatrix();
        }
    }
    gDX.popMatrix();
}




// Used by lltoolselectland
void LLViewerParcelMgr::renderRect(const LLVector3d &west_south_bottom_global,
                                   const LLVector3d &east_north_top_global )
{
    LLGLSUIDefault gls_ui;
    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    LLGLDepthTest gls_depth(GL_TRUE);

    LLVector3 west_south_bottom_agent = gAgent.getPosAgentFromGlobal(west_south_bottom_global);
    F32 west    = west_south_bottom_agent.mV[VX];
    F32 south   = west_south_bottom_agent.mV[VY];
//  F32 bottom  = west_south_bottom_agent.mV[VZ] - 1.f;

    LLVector3 east_north_top_agent = gAgent.getPosAgentFromGlobal(east_north_top_global);
    F32 east    = east_north_top_agent.mV[VX];
    F32 north   = east_north_top_agent.mV[VY];
//  F32 top     = east_north_top_agent.mV[VZ] + 1.f;

    // HACK: At edge of last region of world, we need to make sure the region
    // resolves correctly so we can get a height value.
    const F32 FUDGE = 0.01f;

    F32 sw_bottom = LLWorld::getInstance()->resolveLandHeightAgent( LLVector3( west, south, 0.f ) );
    F32 se_bottom = LLWorld::getInstance()->resolveLandHeightAgent( LLVector3( east-FUDGE, south, 0.f ) );
    F32 ne_bottom = LLWorld::getInstance()->resolveLandHeightAgent( LLVector3( east-FUDGE, north-FUDGE, 0.f ) );
    F32 nw_bottom = LLWorld::getInstance()->resolveLandHeightAgent( LLVector3( west, north-FUDGE, 0.f ) );

    F32 sw_top = sw_bottom + PARCEL_POST_HEIGHT;
    F32 se_top = se_bottom + PARCEL_POST_HEIGHT;
    F32 ne_top = ne_bottom + PARCEL_POST_HEIGHT;
    F32 nw_top = nw_bottom + PARCEL_POST_HEIGHT;

    // S24 (2026-09-05, task #309): LLUI::setLineWidth(2.f) is a no-op under
    // DX_RENDER (D3D11 has no per-draw line-width control at all) - these 4
    // posts are real, simple world-space segments with no local transform
    // in play, so they're a direct dxLineWidth() candidate like the beam/
    // beacon pillar before them.
    LLColor4 post_color(1.f, 1.f, 0.f, 1.f);
    gDX.color4fv(post_color.mV);

    // Cheat and give this the same pick-name as land
    gDX.begin(LLRender::TRIANGLES);

    dxLineWidth(LLVector3(west, north, nw_bottom), LLVector3(west, north, nw_top), 1.f, post_color);
    dxLineWidth(LLVector3(east, north, ne_bottom), LLVector3(east, north, ne_top), 1.f, post_color);
    dxLineWidth(LLVector3(east, south, se_bottom), LLVector3(east, south, se_top), 1.f, post_color);
    dxLineWidth(LLVector3(west, south, sw_bottom), LLVector3(west, south, sw_top), 1.f, post_color);

    gDX.end();

    gDX.color4f(1.f, 1.f, 0.f, 0.2f);
    gDX.begin(LLRender::TRIANGLE_STRIP);
    {
        gDX.vertex3f(west, north, nw_bottom);
        gDX.vertex3f(west, north, nw_top);
        gDX.vertex3f(east, north, ne_bottom);
        gDX.vertex3f(east, north, ne_top);
        gDX.vertex3f(east, south, se_bottom);
        gDX.vertex3f(east, south, se_top);
        gDX.vertex3f(west, south, sw_top);
        gDX.vertex3f(west, south, sw_bottom);
        gDX.vertex3f(west, north, nw_top);
        gDX.vertex3f(west, north, nw_bottom);
    }
    gDX.end();
}


// north = a wall going north/south.  Need that info to set up texture
// coordinates correctly.
void LLViewerParcelMgr::renderOneSegment(F32 x1, F32 y1, F32 x2, F32 y2, F32 height, U8 direction, LLViewerRegion* regionp)
{
    // HACK: At edge of last region of world, we need to make sure the region
    // resolves correctly so we can get a height value.
    const F32 BORDER = REGION_WIDTH_METERS - 0.1f;

    F32 clamped_x1 = x1;
    F32 clamped_y1 = y1;
    F32 clamped_x2 = x2;
    F32 clamped_y2 = y2;

    if (clamped_x1 > BORDER) clamped_x1 = BORDER;
    if (clamped_y1 > BORDER) clamped_y1 = BORDER;
    if (clamped_x2 > BORDER) clamped_x2 = BORDER;
    if (clamped_y2 > BORDER) clamped_y2 = BORDER;

    F32 z;
    F32 z1;
    F32 z2;

    z1 = regionp->getLand().resolveHeightRegion( LLVector3( clamped_x1, clamped_y1, 0.f ) );
    z2 = regionp->getLand().resolveHeightRegion( LLVector3( clamped_x2, clamped_y2, 0.f ) );

    // Convert x1 and x2 from region-local to agent coords.
    LLVector3 origin = regionp->getOriginAgent();
    x1 += origin.mV[VX];
    x2 += origin.mV[VX];
    y1 += origin.mV[VY];
    y2 += origin.mV[VY];

    if (height < 1.f)
    {
        z = z1+height;
        gDX.vertex3f(x1, y1, z);

        gDX.vertex3f(x1, y1, z1);

        gDX.vertex3f(x2, y2, z2);

        gDX.vertex3f(x1, y1, z);

        gDX.vertex3f(x2, y2, z2);

        z = z2+height;
        gDX.vertex3f(x2, y2, z);
    }
    else
    {
        F32 tex_coord1;
        F32 tex_coord2;

        if (WEST_MASK == direction)
        {
            tex_coord1 = y1;
            tex_coord2 = y2;
        }
        else if (SOUTH_MASK == direction)
        {
            tex_coord1 = x1;
            tex_coord2 = x2;
        }
        else if (EAST_MASK == direction)
        {
            tex_coord1 = y2;
            tex_coord2 = y1;
        }
        else /* (NORTH_MASK == direction) */
        {
            tex_coord1 = x2;
            tex_coord2 = x1;
        }


        gDX.texCoord2f(tex_coord1*0.5f+0.5f, z1*0.5f);
        gDX.vertex3f(x1, y1, z1);

        gDX.texCoord2f(tex_coord2*0.5f+0.5f, z2*0.5f);
        gDX.vertex3f(x2, y2, z2);

        // top edge stairsteps
        z = llmax(z2 + height, z1 + height);
        gDX.texCoord2f(tex_coord2 * 0.5f + 0.5f, z * 0.5f);
        gDX.vertex3f(x2, y2, z);

        gDX.texCoord2f(tex_coord1 * 0.5f + 0.5f, z1 * 0.5f);
        gDX.vertex3f(x1, y1, z1);

        gDX.texCoord2f(tex_coord2 * 0.5f + 0.5f, z * 0.5f);
        gDX.vertex3f(x2, y2, z);

        gDX.texCoord2f(tex_coord1 * 0.5f + 0.5f, z * 0.5f);
        gDX.vertex3f(x1, y1, z);
    }
}


void LLViewerParcelMgr::renderHighlightSegments(const U8* segments, LLViewerRegion* regionp)
{
    S32 x, y;
    F32 x1, y1; // start point
    F32 x2, y2; // end point
    bool has_segments = false;

    LLGLSUIDefault gls_ui;
    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    LLGLDepthTest gls_depth(GL_TRUE);

    gDX.color4f(1.f, 1.f, 0.f, 0.2f);

    const S32 STRIDE = (mParcelsPerEdge+1);

    // Cheat and give this the same pick-name as land


    for (y = 0; y < STRIDE; y++)
    {
        for (x = 0; x < STRIDE; x++)
        {
            U8 segment_mask = segments[x + y*STRIDE];

            if (segment_mask & SOUTH_MASK)
            {
                x1 = x * PARCEL_GRID_STEP_METERS;
                y1 = y * PARCEL_GRID_STEP_METERS;

                x2 = x1 + PARCEL_GRID_STEP_METERS;
                y2 = y1;

                if (!has_segments)
                {
                    has_segments = true;
                    gDX.begin(LLRender::TRIANGLES);
                }
                renderOneSegment(x1, y1, x2, y2, PARCEL_POST_HEIGHT, SOUTH_MASK, regionp);
            }

            if (segment_mask & WEST_MASK)
            {
                x1 = x * PARCEL_GRID_STEP_METERS;
                y1 = y * PARCEL_GRID_STEP_METERS;

                x2 = x1;
                y2 = y1 + PARCEL_GRID_STEP_METERS;

                if (!has_segments)
                {
                    has_segments = true;
                    gDX.begin(LLRender::TRIANGLES);
                }
                renderOneSegment(x1, y1, x2, y2, PARCEL_POST_HEIGHT, WEST_MASK, regionp);
            }
        }
    }

    if (has_segments)
    {
        gDX.end();
    }
}


void LLViewerParcelMgr::renderCollisionSegments(U8* segments, bool use_pass, LLViewerRegion* regionp)
{

    S32 x, y;
    F32 x1, y1; // start point
    F32 x2, y2; // end point
    F32 alpha = 0;
    F32 dist = 0;
    F32 dx, dy;
    F32 collision_height;

    const S32 STRIDE = (mParcelsPerEdge+1);

    LLVector3 pos = gAgent.getPositionAgent();

    F32 pos_x = pos.mV[VX];
    F32 pos_y = pos.mV[VY];

    LLGLSUIDefault gls_ui;
    LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);

    if (mCollisionBanned == BA_BANNED ||
        regionp->getRegionFlag(REGION_FLAGS_BLOCK_FLYOVER))
    {
        collision_height = BAN_HEIGHT;
    }
    else
    {
        collision_height = PARCEL_HEIGHT;
    }


    if (use_pass && (mCollisionBanned == BA_NOT_ON_LIST))
    {
        gDX.getTexUnit(0)->bind(mPassImage);
    }
    else
    {
        gDX.getTexUnit(0)->bind(mBlockedImage);
    }

    gDX.begin(LLRender::TRIANGLES);

    for (y = 0; y < STRIDE; y++)
    {
        for (x = 0; x < STRIDE; x++)
        {
            U8 segment_mask = segments[x + y*STRIDE];
            U8 direction;
            const F32 MAX_ALPHA = 0.95f;
            const S32 DIST_OFFSET = 5;
            const S32 MIN_DIST_SQ = DIST_OFFSET*DIST_OFFSET;
            const S32 MAX_DIST_SQ = 169;

            if (segment_mask & SOUTH_MASK)
            {
                x1 = x * PARCEL_GRID_STEP_METERS;
                y1 = y * PARCEL_GRID_STEP_METERS;

                x2 = x1 + PARCEL_GRID_STEP_METERS;
                y2 = y1;

                dy = (pos_y - y1) + DIST_OFFSET;

                if (pos_x < x1)
                    dx = pos_x - x1;
                else if (pos_x > x2)
                    dx = pos_x - x2;
                else
                    dx = 0;

                dist = dx*dx+dy*dy;

                if (dist < MIN_DIST_SQ)
                    alpha = MAX_ALPHA;
                else if (dist > MAX_DIST_SQ)
                    alpha = 0.0f;
                else
                    alpha = 30/dist;

                alpha = llclamp(alpha, 0.0f, MAX_ALPHA);

                gDX.color4f(1.f, 1.f, 1.f, alpha);

                if ((pos_y - y1) < 0) direction = SOUTH_MASK;
                else        direction = NORTH_MASK;

                // avoid Z fighting
                renderOneSegment(x1+0.1f, y1+0.1f, x2+0.1f, y2+0.1f, collision_height, direction, regionp);

            }

            if (segment_mask & WEST_MASK)
            {
                x1 = x * PARCEL_GRID_STEP_METERS;
                y1 = y * PARCEL_GRID_STEP_METERS;

                x2 = x1;
                y2 = y1 + PARCEL_GRID_STEP_METERS;

                dx = (pos_x - x1) + DIST_OFFSET;

                if (pos_y < y1)
                    dy = pos_y - y1;
                else if (pos_y > y2)
                    dy = pos_y - y2;
                else
                    dy = 0;

                dist = dx*dx+dy*dy;

                if (dist < MIN_DIST_SQ)
                    alpha = MAX_ALPHA;
                else if (dist > MAX_DIST_SQ)
                    alpha = 0.0f;
                else
                    alpha = 30/dist;

                alpha = llclamp(alpha, 0.0f, MAX_ALPHA);

                gDX.color4f(1.f, 1.f, 1.f, alpha);

                if ((pos_x - x1) > 0) direction = WEST_MASK;
                else        direction = EAST_MASK;

                // avoid Z fighting
                renderOneSegment(x1+0.1f, y1+0.1f, x2+0.1f, y2+0.1f, collision_height, direction, regionp);

            }
        }
    }

    gDX.end();
}

void LLViewerParcelMgr::resetCollisionTimer()
{
    mCollisionTimer.reset();
    mRenderCollision = true;
}

void draw_line_cube(F32 width, const LLVector3& center)
{
    width = 0.5f * width;
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);

    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);

    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    gDX.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
}

void draw_cross_lines(const LLVector3& center, F32 dx, F32 dy, F32 dz)
{
    gDX.vertex3f(center.mV[VX] - dx, center.mV[VY], center.mV[VZ]);
    gDX.vertex3f(center.mV[VX] + dx, center.mV[VY], center.mV[VZ]);
    gDX.vertex3f(center.mV[VX], center.mV[VY] - dy, center.mV[VZ]);
    gDX.vertex3f(center.mV[VX], center.mV[VY] + dy, center.mV[VZ]);
    gDX.vertex3f(center.mV[VX], center.mV[VY], center.mV[VZ] - dz);
    gDX.vertex3f(center.mV[VX], center.mV[VY], center.mV[VZ] + dz);
}

// S24 (2026-09-04): the tall "sky pillar" beacon (draw_cross_lines() at
// dz=50) used to rely on glLineWidth() for visual weight - D3D11's
// rasterizer has no line-width control at all (a real cross-API gap, not a
// port gap - see DXStateCache.h's own comment on this), so under DX_RENDER
// it was permanently a 1px, alpha-0.25 hairline spanning 100 world units -
// effectively invisible at any real distance, exactly the "GL artifact,
// no longer works" the beacon system was reported as. Replaced with a real
// camera-facing billboard quad, same technique llhudeffecttrail.cpp's beam
// already established for this identical problem: constant SCREEN-space
// pixel width via getPixelMeterRatio() (stays visible at any distance,
// doesn't blow up to a giant slab up close), fading from the base color to
// fully transparent at the top so it reads as a beam of light rather than a
// fence post, plus a gentle pulse so it's not a static/flat GL-era leftover.
// S24 (2026-09-04): takes an explicit `top` rather than a straight-up
// height so the same billboard technique also covers renderSunMoonBeacons()
// below (an arbitrary-direction beam toward the sun/moon, not a vertical
// pillar) - see that function's own comment, same dead-glLineWidth bug.
//
// S24 (2026-09-05, task #309): the billboard-quad geometry itself (was
// duplicated here and in llhudeffecttrail.cpp's selection beam) is now
// shared via dxLineWidth() (lldxlinewidth.h) - this function only keeps its
// own beacon-specific behavior: the pulse animation and the fade-to-
// transparent-at-top look.
void draw_beacon_pillar(const LLVector3& base, const LLVector3& top, F32 half_pixel_width, const LLColor4& color)
{
    // S24 (2026-09-04, user feedback): widened swing (was 0.8+/-0.2) so the
    // bright half of the pulse genuinely punches past 1.0 - combined with
    // BT_ADD_WITH_ALPHA (see renderObjectBeacons()) that overshoot is what
    // actually reads as "brighter than the sky" rather than just less
    // transparent.
    F32 pulse = 1.0f + 0.5f * sinf((F32)LLFrameTimer::getElapsedSeconds() * 3.0f);

    LLColor4 base_color = color;
    base_color.mV[VALPHA] *= pulse;
    LLColor4 top_color = base_color;
    top_color.mV[VALPHA] = 0.f;

    dxLineWidth(base, top, half_pixel_width, base_color, top_color);
}

// S24 (2026-09-04): replaces the small close-range draw_cross_lines()
// (dz=0.5) - same dead-glLineWidth reasoning as draw_beacon_pillar() above.
// A solid downward-pointing pyramid reads unambiguously as "the source is
// HERE" from any horizontal viewing angle without needing to be
// camera-billboarded (unlike the pillar, it has real extent on every axis).
void draw_beacon_arrow(const LLVector3& target, F32 size, const LLColor4& color)
{
    LLVector3 base_center = target;
    base_center.mV[VZ] += size * 0.7f;

    LLVector3 p0 = base_center + LLVector3(-size * 0.5f, -size * 0.5f, 0.f);
    LLVector3 p1 = base_center + LLVector3(size * 0.5f, -size * 0.5f, 0.f);
    LLVector3 p2 = base_center + LLVector3(size * 0.5f, size * 0.5f, 0.f);
    LLVector3 p3 = base_center + LLVector3(-size * 0.5f, size * 0.5f, 0.f);

    gDX.color4fv(color.mV);
    gDX.vertex3fv(p0.mV); gDX.vertex3fv(p1.mV); gDX.vertex3fv(target.mV);
    gDX.vertex3fv(p1.mV); gDX.vertex3fv(p2.mV); gDX.vertex3fv(target.mV);
    gDX.vertex3fv(p2.mV); gDX.vertex3fv(p3.mV); gDX.vertex3fv(target.mV);
    gDX.vertex3fv(p3.mV); gDX.vertex3fv(p0.mV); gDX.vertex3fv(target.mV);
}

void LLViewerObjectList::renderObjectBeacons()
{
    if (mDebugBeacons.empty())
    {
        return;
    }

    LLGLSUIDefault gls_ui;

    gUIProgram.bind();

    // S24 (2026-09-04): mLineWidth/glLineWidth() dropped entirely (not just
    // gated) - both draw calls below are real triangle geometry now, so
    // there's no per-width GL state left to batch around. See
    // draw_beacon_pillar()/draw_beacon_arrow()'s own comments.
    {
        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

        // S24 (2026-09-04, user feedback: "quick difficult to pick up
        // against a bright background"): normal alpha blend gets visually
        // diluted the brighter whatever's behind it is - a plain
        // alpha-blended beam over a bright midday sky ends up barely
        // tinting it. Switched to additive-with-alpha (same technique the
        // night-sky stars/shooting-stars already use) so the pillar ADDS
        // light instead of blending toward it - stays punchy against any
        // background, bright sky included. Reset back to plain alpha right
        // after so the depth-tested cube/arrow below aren't affected.
        gDX.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

        gDX.begin(LLRender::TRIANGLES);
        for (std::vector<LLDebugBeacon>::iterator iter = mDebugBeacons.begin(); iter != mDebugBeacons.end(); ++iter)
        {
            const LLDebugBeacon &debug_beacon = *iter;
            LLColor4 color = debug_beacon.mColor;
            color.mV[3] *= 0.45f;
            // S24 (2026-09-04): DebugBeaconLineWidth (mLineWidth) used to
            // feed glLineWidth() directly - restore it as a real, visible
            // tunable rather than letting it go dead alongside that API
            // under DX_RENDER (1.25px half-width per setting unit, so the
            // default of 1 gives a readable ~2.5px-wide beam).
            F32 half_pixel_width = (F32)debug_beacon.mLineWidth * 1.25f;
            LLVector3 top = debug_beacon.mPositionAgent;
            top.mV[VZ] += 50.f;
            draw_beacon_pillar(debug_beacon.mPositionAgent, top, half_pixel_width, linearColor4(color));
        }
        gDX.end();

        gDX.setSceneBlendType(LLRender::BT_ALPHA);

        gDX.begin(LLRender::LINES);
        for (std::vector<LLDebugBeacon>::iterator iter = mDebugBeacons.begin(); iter != mDebugBeacons.end(); ++iter)
        {
            const LLDebugBeacon &debug_beacon = *iter;
            LLColor4 color = debug_beacon.mColor;
            color.mV[3] *= 0.25f;
            gDX.color4fv(linearColor4(color).mV);
            draw_line_cube(0.10f, debug_beacon.mPositionAgent);
        }
        gDX.end();
    }

    {
        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        LLGLDepthTest gls_depth(GL_TRUE);

        gDX.begin(LLRender::TRIANGLES);
        for (std::vector<LLDebugBeacon>::iterator iter = mDebugBeacons.begin(); iter != mDebugBeacons.end(); ++iter)
        {
            const LLDebugBeacon &debug_beacon = *iter;
            draw_beacon_arrow(debug_beacon.mPositionAgent, 1.0f, linearColor4(debug_beacon.mColor));
        }
        gDX.end();

        gDX.begin(LLRender::LINES);
        for (std::vector<LLDebugBeacon>::iterator iter = mDebugBeacons.begin(); iter != mDebugBeacons.end(); ++iter)
        {
            const LLDebugBeacon &debug_beacon = *iter;
            gDX.color4fv(linearColor4(debug_beacon.mColor).mV);
            draw_line_cube(0.10f, debug_beacon.mPositionAgent);
        }
        gDX.end();

        for (std::vector<LLDebugBeacon>::iterator iter = mDebugBeacons.begin(); iter != mDebugBeacons.end(); ++iter)
        {
            LLDebugBeacon &debug_beacon = *iter;
            if (debug_beacon.mString == "")
            {
                continue;
            }
            LLHUDText *hud_textp = (LLHUDText *)LLHUDObject::addHUDObject(LLHUDObject::LL_HUD_TEXT);

            hud_textp->setZCompare(false);
            LLColor4 color;
            color = debug_beacon.mTextColor;
            color.mV[3] *= 1.f;

            hud_textp->setString(debug_beacon.mString);
            hud_textp->setColor(color);
            hud_textp->setPositionAgent(debug_beacon.mPositionAgent);
            debug_beacon.mHUDObject = hud_textp;
        }
    }
}

void LLSky::renderSunMoonBeacons(const LLVector3& pos_agent, const LLVector3& direction, LLColor4 color)
{
    LLGLSUIDefault gls_ui;
    gUIProgram.bind();
    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    LLVector3 pos_end;
    for (S32 i = 0; i < 3; ++i)
    {
        pos_end.mV[i] = pos_agent.mV[i] + (50 * direction.mV[i]);
    }

    // S24 (2026-09-04): same dead-glLineWidth bug as renderObjectBeacons()'s
    // old tall pillar (see draw_beacon_pillar()'s own comment, and the small
    // draw_cross_lines() caps this used to draw at each end had the exact
    // same problem) - D3D11 has no line-width control at all, so this
    // sun/moon direction beam was a permanent 1px hairline under DX_RENDER.
    // Reuses the same billboard-beam technique; additive blend so it stays
    // visible against the bright sky it's usually pointing across.
    color.mV[3] *= 0.5f;
    gDX.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);
    gDX.begin(LLRender::TRIANGLES);
    draw_beacon_pillar(pos_agent, pos_end, (F32)LLPipeline::DebugBeaconLineWidth * 1.25f, color);
    gDX.end();
    gDX.setSceneBlendType(LLRender::BT_ALPHA);

}

//-----------------------------------------------------------------------------
// gpu_benchmark() helper classes
//-----------------------------------------------------------------------------

// This struct is used to ensure that once we call initProfile(), it will
// definitely be matched by a corresponding call to finishProfile(). It's
// a struct rather than a class simply because every member is public.
struct ShaderProfileHelper
{
    ShaderProfileHelper()
    {
        LLHLSLShader::initProfile();
    }
    ~ShaderProfileHelper()
    {
        LLHLSLShader::finishProfile();
    }
};

// This helper class is used to ensure that each generateTextures() call
// is matched by a corresponding deleteTextures() call. It also handles
// the bindManual() calls using those textures.
class TextureHolder
{
public:
    TextureHolder(U32 unit, U32 size) :
        texUnit(gDX.getTexUnit(unit)),
        source(size)            // preallocate vector
    {
        // takes (count, pointer)
        // &vector[0] gets pointer to contiguous array
        LLImageGL::generateTextures(static_cast<S32>(source.size()), &source[0]);
    }

    ~TextureHolder()
    {
        // unbind
        if (texUnit)
        {
                texUnit->unbind(LLTexUnit::TT_TEXTURE);
        }
        // ensure that we delete these textures regardless of how we exit
        LLImageGL::deleteTextures(static_cast<S32>(source.size()), &source[0]);
    }

    bool bind(U32 index)
    {
        if (texUnit) // should always be there with dummy (-1), but just in case
        {
            return texUnit->bindManual(LLTexUnit::TT_TEXTURE, source[index]);
        }
        return false;
    }

private:
    // capture which LLTexUnit we're going to use
    LLTexUnit* texUnit;

    // use std::vector for implicit resource management
    std::vector<U32> source;
};

class ShaderBinder
{
public:
    ShaderBinder(LLHLSLShader& shader) :
        mShader(shader)
    {
        mShader.bind();
    }
    ~ShaderBinder()
    {
        mShader.unbind();
    }

private:
    LLHLSLShader& mShader;
};


F32 shader_timer_benchmark(std::vector<LLRenderTarget> & dest, TextureHolder & texHolder, U32 textures_count, LLVertexBuffer * buff, F32 &seconds)
{
    // run GPU timer benchmark

    //number of samples to take
    const S32 samples = 64;

    {
        ShaderProfileHelper initProfile;
        dest[0].bindTarget();
        gBenchmarkProgram.bind();
        for (S32 c = 0; c < samples; ++c)
        {
            for (U32 i = 0; i < textures_count; ++i)
            {
                texHolder.bind(i);
                buff->setBuffer();
                buff->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
        }
        gBenchmarkProgram.unbind();
        dest[0].flush();
    }

    F32 ms = gBenchmarkProgram.mTimeElapsed / 1000000.f;
    seconds = ms / 1000.f;

    F64 samples_drawn = (F64)gBenchmarkProgram.mSamplesDrawn;
    F64 gpixels_drawn = samples_drawn / 1000000000.0;
    F32 samples_sec = (F32)(gpixels_drawn / seconds);
    return samples_sec * 4;  // 4 bytes per sample
}

//-----------------------------------------------------------------------------
// gpu_benchmark()
//  returns measured memory bandwidth of GPU in gigabytes per second
//-----------------------------------------------------------------------------
F32 gpu_benchmark()
{
#ifdef DX_RENDER
    // S24 (2026-08-05): GL-only (compiles a GLSL/HLSL benchmark shader,
    // times it via GL_TIMER queries) and never audited for DX_RENDER -
    // menu-triggerable independently of LLFeatureManager::loadGPUClass()
    // (llviewermenu.cpp), so guard here too rather than only at that one
    // call site. See loadGPUClass()'s own DX_RENDER comment for the real
    // GPU classification path used instead.
    return -1.f;
#endif

    if (gGLManager.mGLVersion < 3.3f)
    { // don't bother benchmarking venerable drivers which don't support accurate timing anyway
        return -1.f;
    }

    if (gBenchmarkProgram.mProgramObject == 0)
    {
        LLViewerShaderMgr::instance()->initAttribsAndUniforms();

        gBenchmarkProgram.mName = "Benchmark Shader";
        gBenchmarkProgram.mFeatures.attachNothing = true;
        gBenchmarkProgram.mShaderFiles.clear();
        gBenchmarkProgram.mShaderFiles.push_back(std::make_pair("interface/benchmarkV.glsl", GL_VERTEX_SHADER));
        gBenchmarkProgram.mShaderFiles.push_back(std::make_pair("interface/benchmarkF.glsl", GL_FRAGMENT_SHADER));
        gBenchmarkProgram.mShaderLevel = 1;
        if (!gBenchmarkProgram.createShader())
        {
            return -1.f;
        }
    }

    LLGLDisable blend(GL_BLEND);

    //measure memory bandwidth by:
    // - allocating a batch of textures and render targets
    // - rendering those textures to those render targets
    // - recording time taken
    // - taking the median time for a given number of samples

    //resolution of textures/render targets
    const U32 res = 1024;

    //number of textures
    const U32 count = 32;

    //time limit, allocation operations shouldn't take longer then 30 seconds, same for actual benchmark.
    const F32 time_limit = 30;

    std::vector<LLRenderTarget> dest(count);
    TextureHolder texHolder(0, count);
    std::vector<F32> results;

    //build a random texture
    U8* pixels = new U8[res*res*4];

    for (U32 i = 0; i < res*res*4; ++i)
    {
        pixels[i] = (U8) ll_rand(255);
    }

    gDX.setColorMask(true, true);
    LLGLDepthTest depth(GL_FALSE);

    LLTimer alloc_timer;
    alloc_timer.start();
    for (U32 i = 0; i < count; ++i)
    {
        //allocate render targets and textures
        if (!dest[i].allocate(res, res, GL_RGBA))
        {
            LL_WARNS("Benchmark") << "Failed to allocate render target." << LL_ENDL;
            // abandon the benchmark test
            delete[] pixels;
            return -1.f;
        }
        dest[i].bindTarget();
        dest[i].clear();
        dest[i].flush();

        if (!texHolder.bind(i))
        {
            // can use a dummy value mDummyTexUnit = new LLTexUnit(-1);
            LL_WARNS("Benchmark") << "Failed to bind tex unit." << LL_ENDL;
            // abandon the benchmark test
            delete[] pixels;
            return -1.f;
        }
        LLImageGL::setManualImage(GL_TEXTURE_2D, 0, GL_RGBA, res,res,GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        // disable mipmaps and use point filtering to cause cache misses
        gDX.getTexUnit(0)->setHasMipMaps(false);
        gDX.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);

        if (alloc_timer.getElapsedTimeF32() > time_limit)
        {
            // abandon the benchmark test
            LL_WARNS("Benchmark") << "Allocation operation took longer then 30 seconds, stopping." << LL_ENDL;
            delete[] pixels;
            return -1.f;
        }
    }

    delete [] pixels;

    //make a dummy triangle to draw with
    LLPointer<LLVertexBuffer> buff = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);

    if (!buff->allocateBuffer(3, 0))
    {
        LL_WARNS("Benchmark") << "Failed to allocate buffer during benchmark." << LL_ENDL;
        // abandon the benchmark test
        return -1.f;
    }

    LLStrider<LLVector3> v;

    if (! buff->getVertexStrider(v))
    {
        LL_WARNS("Benchmark") << "GL LLVertexBuffer::getVertexStrider() returned false, "
                   << "buff->getMappedData() is"
                   << (buff->getMappedData()? " not" : "")
                   << " NULL" << LL_ENDL;
        // abandon the benchmark test
        return -1.f;
    }

    // generate dummy triangle
    v[0].set(-1, 1, 0);
    v[1].set(-1, -3, 0);
    v[2].set(3, 1, 0);

    buff->unmapBuffer();

    LLHLSLShader::unbind();

    // run GPU timer benchmark twice
    F32 seconds = 0;
    F32 gbps = shader_timer_benchmark(dest, texHolder, count, buff.get(), seconds);

    LL_INFOS("Benchmark") << "Memory bandwidth, 1st run is " << llformat("%.3f", gbps) << " GB/sec according to ARB_timer_query, total time " << seconds << " seconds" << LL_ENDL;

    gbps = shader_timer_benchmark(dest, texHolder, count, buff.get(), seconds);

    LL_INFOS("Benchmark") << "Memory bandwidth, final run is " << llformat("%.3f", gbps) << " GB/sec according to ARB_timer_query, total time " << seconds << " seconds" << LL_ENDL;

    return gbps;
}
