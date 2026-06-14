/**
 * @file llhudeffectbeam.cpp
 * @brief LLHUDEffectBeam class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "llviewerprecompiledheaders.h"

#include "llhudeffectbeam.h"
#include "message.h"

#include "llviewerobjectlist.h"

#include "llagent.h"
#include "lldrawable.h"
#include "llfontgl.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llhudrender.h"
#include "llrendersphere.h"
#include "llviewercamera.h"
#include "llvoavatar.h"
#include "llviewercontrol.h"
#include "lluicolortable.h"

const F32 BEAM_SPACING = 0.075f;

LLHUDEffectBeam::LLHUDEffectBeam(const U8 type) : LLHUDEffect(type)
{
    mKillTime = mDuration;

    // Initialize all of these to defaults
    S32 i;
    for (i = 0; i < NUM_POINTS; i++)
    {
        mInterp[i].setStartTime(BEAM_SPACING*i);
        mInterp[i].setEndTime(BEAM_SPACING*NUM_POINTS + BEAM_SPACING*i);
        mInterp[i].start();
        mInterpFade[i].setStartTime(BEAM_SPACING*NUM_POINTS + BEAM_SPACING*i - 0.5f*NUM_POINTS*BEAM_SPACING);
        mInterpFade[i].setEndTime(BEAM_SPACING*NUM_POINTS + BEAM_SPACING*i);
        mInterpFade[i].setStartVal(1.f);
        mInterpFade[i].setEndVal(0.f);
    }

    // Setup default timeouts and fade animations.
    F32 fade_length;
    fade_length = llmin(0.5f, mDuration);
    mFadeInterp.setStartTime(mKillTime - fade_length);
    mFadeInterp.setEndTime(mKillTime);
    mFadeInterp.setStartVal(1.f);
    mFadeInterp.setEndVal(0.f);
}

LLHUDEffectBeam::~LLHUDEffectBeam()
{
}

void LLHUDEffectBeam::packData(LLMessageSystem *mesgsys)
{
    if (!mSourceObject)
    {
        LL_WARNS() << "Missing source object!" << LL_ENDL;
    }

    // Pack the default data
    LLHUDEffect::packData(mesgsys);

    // Pack the type-specific data.  Uses a fun packed binary format.  Whee!
    // 16 + 24 + 1 = 41
    U8 packed_data[41];
    memset(packed_data, 0, 41);
    if (mSourceObject)
    {
        htolememcpy(packed_data, mSourceObject->mID.mData, MVT_LLUUID, 16);
    }

    if (mTargetObject)
    {
        packed_data[16] = 1;
    }
    else
    {
        packed_data[16] = 0;
    }

    if (mTargetObject)
    {
        htolememcpy(&(packed_data[17]), mTargetObject->mID.mData, MVT_LLUUID, 16);
    }
    else
    {
        htolememcpy(&(packed_data[17]), mTargetPos.mdV, MVT_LLVector3d, 24);
    }
    mesgsys->addBinaryDataFast(_PREHASH_TypeData, packed_data, 41);
}

void LLHUDEffectBeam::unpackData(LLMessageSystem *mesgsys, S32 blocknum)
{
    LL_ERRS() << "Got beam!" << LL_ENDL;
    bool use_target_object;
    LLVector3d new_target;
    U8 packed_data[41];

    LLHUDEffect::unpackData(mesgsys, blocknum);
    LLUUID source_id;
    LLUUID target_id;
    S32 size = mesgsys->getSizeFast(_PREHASH_Effect, blocknum, _PREHASH_TypeData);
    if (size != 41)
    {
        LL_WARNS() << "Beam effect with bad size " << size << LL_ENDL;
        return;
    }
    mesgsys->getBinaryDataFast(_PREHASH_Effect, _PREHASH_TypeData, packed_data, 41, blocknum);

    htolememcpy(source_id.mData, packed_data, MVT_LLUUID, 16);

    LLViewerObject *objp = gObjectList.findObject(source_id);
    if (objp)
    {
        setSourceObject(objp);
    }

    use_target_object = packed_data[16];

    if (use_target_object)
    {
        htolememcpy(target_id.mData, &packed_data[17], MVT_LLUUID, 16);

        LLViewerObject *objp = gObjectList.findObject(target_id);
        if (objp)
        {
            setTargetObject(objp);
        }
    }
    else
    {
        htolememcpy(new_target.mdV, &(packed_data[17]), MVT_LLVector3d, 24);
        setTargetPos(new_target);
    }

    // We've received an update for the effect, update the various timeouts
    // and fade animations.
    mKillTime = mTimer.getElapsedTimeF32() + mDuration;
    F32 fade_length;
    fade_length = llmin(0.5f, mDuration);
    mFadeInterp.setStartTime(mKillTime - fade_length);
    mFadeInterp.setEndTime(mKillTime);
    mFadeInterp.setStartVal(1.f);
    mFadeInterp.setEndVal(0.f);
}

void LLHUDEffectBeam::setSourceObject(LLViewerObject *objp)
{
    if (objp->isDead())
    {
        LL_WARNS() << "HUDEffectBeam: Source object is dead!" << LL_ENDL;
        mSourceObject = NULL;
        return;
    }

    if (mSourceObject == objp)
    {
        return;
    }

    mSourceObject = objp;
    if (mSourceObject)
    {
        S32 i;
        for (i = 0; i < NUM_POINTS; i++)
        {
            if (mSourceObject->isAvatar())
            {
                LLViewerObject *objp = mSourceObject;
                LLVOAvatar *avatarp = (LLVOAvatar *)objp;
                LLVector3d hand_pos_global = gAgent.getPosGlobalFromAgent(avatarp->mWristLeftp->getWorldPosition());
                mInterp[i].setStartVal(hand_pos_global);
                mInterp[i].start();
            }
            else
            {
                mInterp[i].setStartVal(mSourceObject->getPositionGlobal());
                mInterp[i].start();
            }
        }
    }
}


void LLHUDEffectBeam::setTargetObject(LLViewerObject *objp)
{
    if (mTargetObject->isDead())
    {
        LL_WARNS() << "HUDEffectBeam: Target object is dead!" << LL_ENDL;
    }

    mTargetObject = objp;
}

void LLHUDEffectBeam::setTargetPos(const LLVector3d &pos_global)
{
    mTargetPos = pos_global;
    mTargetObject = NULL;
}

void LLHUDEffectBeam::render()
{
    // S24: BEAM SUBSYSTEM - Modern billboard-based rendering with hybrid particle/line support

    static S32 render_counter = 0;
    if (render_counter++ % 60 == 0)
    {
        LL_WARNS() << "S24 BEAM: render() CALLED - frame " << render_counter << LL_ENDL;
    }

    // Early exit checks
    if (!mSourceObject)
    {
        markDead();
        return;
    }
    if (mSourceObject->isDead())
    {
        LL_WARNS() << "S24 BEAM: Source object is dead, marking dead" << LL_ENDL;
        markDead();
        return;
    }

    F32 time = mTimer.getElapsedTimeF32();

    // Kill us if our time is over...
    if (mKillTime < time)
    {
        markDead();
        return;
    }

    // S24: Setup GL state for alpha blending
    LLGLSPipelineAlpha gls_pipeline_alpha;
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // Interpolate the global fade alpha
    mFadeInterp.update(time);

    // Update target position
    if (mTargetObject.notNull() && mTargetObject->mDrawable.notNull())
    {
        // use viewer object position on freshly created objects
        if (mTargetObject->mDrawable->getGeneration() == -1)
        {
            mTargetPos = mTargetObject->getPositionGlobal();
        }
        // otherwise use drawable
        else
        {
            mTargetPos = gAgent.getPosGlobalFromAgent(mTargetObject->mDrawable->getPositionAgent());
        }
    }

    // S24: Read beam settings with validation
    S32 beam_style = gSavedSettings.getS32("SelectionBeamStyle");
    F32 beam_width = gSavedSettings.getF32("SelectionBeamWidth");
    F32 particle_scale = gSavedSettings.getF32("SelectionBeamParticleScale");

    // S24: Update color from UIColorTable directly (source of truth for both Prefs and KVtweaks)
    LLColor4 current_effect_color = LLUIColorTable::instance().getColor("EffectColor");

    static S32 color_log_counter = 0;
    if (color_log_counter++ % 60 == 0)
    {
        LL_WARNS() << "S24 BEAM COLOR: Reading from UIColorTable - R=" << current_effect_color.mV[0] 
                   << " G=" << current_effect_color.mV[1] 
                   << " B=" << current_effect_color.mV[2] 
                   << " A=" << current_effect_color.mV[3] << LL_ENDL;
    }

    current_effect_color.mV[VALPHA] = 1.0f; // Force full opacity
    mColor = LLColor4U(current_effect_color); // Update beam's stored color

    F32 global_alpha = mFadeInterp.getCurVal() * (mColor.mV[3] / 255.0f);

    // S24: Clamp alpha to prevent invisible beams
    if (global_alpha < 0.01f)
    {
        global_alpha = 1.0f;
        LL_WARNS() << "S24 BEAM: Alpha was too low (" << (mColor.mV[3] / 255.0f) << "), forcing to 1.0" << LL_ENDL;
    }

    // Get start and end positions for both rendering paths
    LLVector3 source_pos, target_pos;
    if (mSourceObject->isAvatar())
    {
        LLVOAvatar* avatarp = (LLVOAvatar*)mSourceObject.get();
        source_pos = gAgent.getPosAgentFromGlobal(gAgent.getPosGlobalFromAgent(avatarp->mWristLeftp->getWorldPosition()));
    }
    else
    {
        source_pos = gAgent.getPosAgentFromGlobal(mSourceObject->getPositionGlobal());
    }
    target_pos = gAgent.getPosAgentFromGlobal(mTargetPos);

    // S24: MODERN BILLBOARD LINE RENDERER (styles 1=line only, 2=hybrid)
    if (beam_style >= 1)
    {
        // Calculate line direction vector
        LLVector3 line_vec = target_pos - source_pos;
        F32 line_length = line_vec.length();

        if (line_length < 0.01f)
        {
            LL_WARNS() << "S24 BEAM: Line too short to render (" << line_length << "m)" << LL_ENDL;
        }
        else
        {
            line_vec.normalize();

            // Get camera position for billboarding calculation
            LLVector3 cam_pos = LLViewerCamera::getInstance()->getOrigin();
            LLVector3 to_camera = cam_pos - source_pos;
            to_camera.normalize();

            // Calculate perpendicular vector for line width (cross product creates billboard effect)
            LLVector3 width_vec = line_vec % to_camera;
            F32 width_len = width_vec.length();

            if (width_len < 0.001f)
            {
                // Line is pointing directly at camera, use camera up vector instead
                LL_WARNS() << "S24 BEAM: Line pointing at camera, using fallback width vector" << LL_ENDL;
                width_vec = line_vec % LLViewerCamera::getInstance()->getUpAxis();
                width_vec.normalize();
            }
            else
            {
                width_vec.normalize();
            }

            // Scale width vector by half the desired beam width (meters in world space)
            F32 half_width = beam_width * 0.01f; // Convert pixels to meters approximation
            width_vec *= half_width;

            // Build camera-facing quad vertices
            LLVector3 v1 = source_pos - width_vec;
            LLVector3 v2 = source_pos + width_vec;
            LLVector3 v3 = target_pos + width_vec;
            LLVector3 v4 = target_pos - width_vec;

            // Set line color
            LLColor4 line_color = LLColor4(mColor);
            line_color.mV[3] = global_alpha * 0.9f; // Slightly more opaque than particles
            gGL.color4fv(line_color.mV);

            // Render as triangle strip - modern, fast, driver-compatible
            gGL.begin(LLRender::TRIANGLE_STRIP);
            gGL.vertex3fv(v1.mV);
            gGL.vertex3fv(v2.mV);
            gGL.vertex3fv(v4.mV);
            gGL.vertex3fv(v3.mV);
            gGL.end();
        }
    }

    // S24: Render particle glow (styles 0=particles only, 2=hybrid)
    if (beam_style != 1)
    {

        // Init the color of the particles
        LLColor4U coloru = mColor;

        S32 particles_rendered = 0;

        // Draw the particles
        for (S32 i = 0; i < NUM_POINTS; i++)
        {
            mInterp[i].update(time);
            if (!mInterp[i].isActive())
            {
                continue;
            }
            mInterpFade[i].update(time);

            if (mInterp[i].isDone())
            {
                // Reinitialize the particle when the particle has finished its animation.
                setupParticle(i);
            }

            F32 frac = mInterp[i].getCurFrac();
            F32 scale = 0.025f + fabs(0.05f*sin(2.f*F_PI*(frac - time)));
            scale *= mInterpFade[i].getCurVal();
            scale *= particle_scale; // S24: Apply scale multiplier

            LLVector3 pos_agent = gAgent.getPosAgentFromGlobal(mInterp[i].getCurVal());

            F32 alpha = global_alpha;
            alpha *= mInterpFade[i].getCurVal();

            // S24: Ensure alpha is in valid range
            alpha = llclamp(alpha, 0.0f, 1.0f);
            coloru.mV[3] = (U8)(alpha * 255.0f);

            gGL.color4ubv(coloru.mV);

            gGL.pushMatrix();
            gGL.translatef(pos_agent.mV[0], pos_agent.mV[1], pos_agent.mV[2]);
            gGL.scalef(scale, scale, scale);
            gSphere.render();
            gGL.popMatrix();

            particles_rendered++;
        }

        LL_WARNS() << "S24 BEAM: Rendered " << particles_rendered << " particles" << LL_ENDL;
    }
}

void LLHUDEffectBeam::renderForTimer()
{
    render();
}

void LLHUDEffectBeam::setupParticle(const S32 i)
{
    LLVector3d start_pos_global;
    if (mSourceObject->getPCode() == LL_PCODE_LEGACY_AVATAR)
    {
        LLViewerObject *objp = mSourceObject;
        LLVOAvatar *avatarp = (LLVOAvatar *)objp;
        start_pos_global = gAgent.getPosGlobalFromAgent(avatarp->mWristLeftp->getWorldPosition());
    }
    else
    {
        start_pos_global = mSourceObject->getPositionGlobal();
    }

    // Generate a random offset for the target point.
    const F32 SCALE = 0.5f;
    F32 x, y, z;
    x = ll_frand(SCALE) - 0.5f*SCALE;
    y = ll_frand(SCALE) - 0.5f*SCALE;
    z = ll_frand(SCALE) - 0.5f*SCALE;

    LLVector3d target_pos_global(mTargetPos);
    target_pos_global += LLVector3d(x, y, z);

    mInterp[i].setStartTime(mInterp[i].getEndTime());
    mInterp[i].setEndTime(mInterp[i].getStartTime() + BEAM_SPACING*NUM_POINTS);
    mInterp[i].setStartVal(start_pos_global);
    mInterp[i].setEndVal(target_pos_global);
    mInterp[i].start();


    // Setup the interpolator that fades out the alpha.
    mInterpFade[i].setStartTime(mInterp[i].getStartTime() + BEAM_SPACING*NUM_POINTS - 0.5f*NUM_POINTS*BEAM_SPACING);
    mInterpFade[i].setEndTime(mInterp[i].getStartTime() + BEAM_SPACING*NUM_POINTS - 0.05f);
    mInterpFade[i].start();
}
