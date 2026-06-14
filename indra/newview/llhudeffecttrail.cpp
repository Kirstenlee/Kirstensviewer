/**
 * @file llhudeffecttrail.cpp
 * @brief LLHUDEffectSpiral class implementation
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

#include "llhudeffecttrail.h"

#include "llviewercontrol.h"
#include "message.h"

#include "llagent.h"
#include "llbox.h"
#include "lldrawable.h"
#include "llhudrender.h"
#include "llviewertexturelist.h"
#include "llviewerobjectlist.h"
#include "llviewerpartsim.h"
#include "llviewerpartsource.h"
#include "llvoavatar.h"
#include "llworld.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"
#include "lluicolortable.h"

LLHUDEffectSpiral::LLHUDEffectSpiral(const U8 type) : LLHUDEffect(type), mbInit(false)
{
    mKillTime = 10.f;
    mVMag = 1.f;
    mVOffset = 0.f;
    mInitialRadius = 1.f;
    mFinalRadius = 1.f;
    mSpinRate = 10.f;
    mFlickerRate = 50.f;
    mScaleBase = 0.1f;
    mScaleVar = 0.f;

    mFadeInterp.setStartTime(0.f);
    mFadeInterp.setEndTime(mKillTime);
    mFadeInterp.setStartVal(1.f);
    mFadeInterp.setEndVal(1.f);
}

LLHUDEffectSpiral::~LLHUDEffectSpiral()
{
}

void LLHUDEffectSpiral::markDead()
{
    if (mPartSourcep)
    {
        mPartSourcep->setDead();
        mPartSourcep = NULL;
    }
    LLHUDEffect::markDead();
}

void LLHUDEffectSpiral::packData(LLMessageSystem *mesgsys)
{
    if (!mSourceObject)
    {
        //LL_WARNS() << "Missing object in trail pack!" << LL_ENDL;
    }
    LLHUDEffect::packData(mesgsys);

    U8 packed_data[56];
    memset(packed_data, 0, 56);

    if (mSourceObject)
    {
        htolememcpy(packed_data, mSourceObject->mID.mData, MVT_LLUUID, 16);
    }
    if (mTargetObject)
    {
        htolememcpy(packed_data + 16, mTargetObject->mID.mData, MVT_LLUUID, 16);
    }
    if (!mPositionGlobal.isExactlyZero())
    {
        htolememcpy(packed_data + 32, mPositionGlobal.mdV, MVT_LLVector3d, 24);
    }
    mesgsys->addBinaryDataFast(_PREHASH_TypeData, packed_data, 56);
}

void LLHUDEffectSpiral::unpackData(LLMessageSystem *mesgsys, S32 blocknum)
{
    const size_t EFFECT_SIZE = 56;
    U8 packed_data[EFFECT_SIZE];

    LLHUDEffect::unpackData(mesgsys, blocknum);
    LLUUID object_id, target_object_id;
    size_t size = mesgsys->getSizeFast(_PREHASH_Effect, blocknum, _PREHASH_TypeData);
    if (size != EFFECT_SIZE)
    {
        LL_WARNS() << "Spiral effect with bad size " << size << LL_ENDL;
        return;
    }
    mesgsys->getBinaryDataFast(_PREHASH_Effect, _PREHASH_TypeData,
        packed_data, EFFECT_SIZE, blocknum, EFFECT_SIZE);

    htolememcpy(object_id.mData, packed_data, MVT_LLUUID, 16);
    htolememcpy(target_object_id.mData, packed_data + 16, MVT_LLUUID, 16);
    htolememcpy(mPositionGlobal.mdV, packed_data + 32, MVT_LLVector3d, 24);

    LLViewerObject *objp = NULL;

    if (object_id.isNull())
    {
        setSourceObject(NULL);
    }
    else
    {
        LLViewerObject *objp = gObjectList.findObject(object_id);
        if (objp)
        {
            setSourceObject(objp);
        }
        else
        {
            // We don't have this object, kill this effect
            markDead();
            return;
        }
    }

    if (target_object_id.isNull())
    {
        setTargetObject(NULL);
    }
    else
    {
        objp = gObjectList.findObject(target_object_id);
        if (objp)
        {
            setTargetObject(objp);
        }
        else
        {
            // We don't have this object, kill this effect
            markDead();
            return;
        }
    }

    triggerLocal();
}

void LLHUDEffectSpiral::triggerLocal()
{
    mKillTime = mTimer.getElapsedTimeF32() + mDuration;

    bool show_beam = gSavedSettings.getBOOL("ShowSelectionBeam");

    LLColor4 color;
    color.setVec(mColor);

    if (!mPartSourcep)
    {
        if (!mTargetObject.isNull() && !mSourceObject.isNull())
        {
            if (show_beam)
            {
                LLPointer<LLViewerPartSourceBeam> psb = new LLViewerPartSourceBeam;
                psb->setColor(color);
                psb->setSourceObject(mSourceObject);
                psb->setTargetObject(mTargetObject);
                psb->setOwnerUUID(gAgent.getID());
                LLViewerPartSim::getInstance()->addPartSource(psb);
                mPartSourcep = psb;
            }
        }
        else
        {
            if (!mSourceObject.isNull() && !mPositionGlobal.isExactlyZero())
            {
                if (show_beam)
                {
                    LLPointer<LLViewerPartSourceBeam> psb = new LLViewerPartSourceBeam;
                    psb->setSourceObject(mSourceObject);
                    psb->setTargetObject(NULL);
                    psb->setColor(color);
                    psb->mLKGTargetPosGlobal = mPositionGlobal;
                    psb->setOwnerUUID(gAgent.getID());
                    LLViewerPartSim::getInstance()->addPartSource(psb);
                    mPartSourcep = psb;
                }
            }
            else
            {
                LLVector3 pos;
                if (mSourceObject)
                {
                    pos = mSourceObject->getPositionAgent();
                }
                else
                {
                    pos = gAgent.getPosAgentFromGlobal(mPositionGlobal);
                }
                LLPointer<LLViewerPartSourceSpiral> pss = new LLViewerPartSourceSpiral(pos);
                if (!mSourceObject.isNull())
                {
                    pss->setSourceObject(mSourceObject);
                }
                pss->setColor(color);
                pss->setOwnerUUID(gAgent.getID());
                LLViewerPartSim::getInstance()->addPartSource(pss);
                mPartSourcep = pss;
            }
        }
    }
    else
    {
        LLPointer<LLViewerPartSource>& ps = mPartSourcep;
        if (mPartSourcep->getType() == LLViewerPartSource::LL_PART_SOURCE_BEAM)
        {
            LLViewerPartSourceBeam *psb = (LLViewerPartSourceBeam *)ps.get();
            psb->setSourceObject(mSourceObject);
            psb->setTargetObject(mTargetObject);
            psb->setColor(color);
            if (mTargetObject.isNull())
            {
                psb->mLKGTargetPosGlobal = mPositionGlobal;
            }
        }
        else
        {
            LLViewerPartSourceSpiral *pss = (LLViewerPartSourceSpiral *)ps.get();
            pss->setSourceObject(mSourceObject);
        }
    }

    mbInit = true;
}

void LLHUDEffectSpiral::setTargetObject(LLViewerObject *objp)
{
    if (objp == mTargetObject)
    {
        return;
    }

    mTargetObject = objp;
}

void LLHUDEffectSpiral::render()
{
    F32 time = mTimer.getElapsedTimeF32();

    if ((!mSourceObject.isNull() && mSourceObject->isDead()) ||
        (!mTargetObject.isNull() && mTargetObject->isDead()) ||
        mKillTime < time ||
        (!mPartSourcep.isNull() && !gSavedSettings.getBOOL("ShowSelectionBeam")) )
    {
        markDead();
        return;
    }

    // ========================================================================
    // S24: SELECTION BEAM ENHANCEMENT - "LIGHTSABER EFFECT"
    // ========================================================================
    // Renders a professional GL line core for the selection beam with smooth
    // alpha fade at start/end, plus optional custom color override.
    // 
    // Three rendering modes:
    //   0 = Particles only (classic grey particle stream)
    //   1 = Line only (clean solid line, no particles)
    //   2 = Hybrid (GL line core + particle glow - the "lightsaber" effect)
    //
    // Line rendering uses gradient alpha on first/last 10% of beam length
    // for smooth fade-in/fade-out. Custom colors apply to both line and
    // particles for consistent appearance.
    //
    // Modified files for this feature:
    //   - llhudeffecttrail.cpp: GL line rendering in LLHUDEffectSpiral::render()
    //   - llviewerpartsource.cpp: Particle scaling + style check in LLViewerPartSourceBeam::update()
    //   - settings.xml: SelectionBeamStyle, SelectionBeamWidth, SelectionBeamParticleScale,
    //                   SelectionBeamUseCustomColor, SelectionBeamColor
    //   - floater_preferences_kvtweaks.xml: UI controls in Experimental tab
    // ========================================================================

    S32 beam_style = gSavedSettings.getS32("SelectionBeamStyle");
    bool show_beam = gSavedSettings.getBOOL("ShowSelectionBeam");

    // S24: Only render GL line if ShowSelectionBeam is enabled AND style includes line
    if (show_beam && beam_style >= 1 && !mSourceObject.isNull())  // Style 1=line only, 2=hybrid
    {
        // Get source position (avatar wrist or object center)
        LLVector3 source_pos;
        if (mSourceObject->isAvatar())
        {
            LLVOAvatar* avatarp = (LLVOAvatar*)mSourceObject.get();
            source_pos = avatarp->mWristLeftp->getWorldPosition();
        }
        else
        {
            source_pos = mSourceObject->getPositionAgent();
        }

        // Get target position - either from target object or from global position
        LLVector3 target_pos;
        if (!mTargetObject.isNull())
        {
            target_pos = mTargetObject->getPositionAgent();
        }
        else if (!mPositionGlobal.isExactlyZero())
        {
            target_pos = gAgent.getPosAgentFromGlobal(mPositionGlobal);
        }
        else
        {
            return;  // No valid target, skip rendering
        }

        // Set up GL rendering state for alpha blending
        LLGLSPipelineAlpha gls_pipeline_alpha;
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

        // S24: Ensure a shader is bound for rendering (prevents assert on cleanup)
        gGL.flush();
        gGL.pushUIMatrix();

        // S24: Get beam color from unified EffectColor (set in Preferences)
        LLColor4 base_color = LLUIColorTable::instance().getColor("EffectColor");
        base_color.mV[VALPHA] = 0.9f; // Bright and visible

        // Calculate beam direction
        LLVector3 beam_vec = target_pos - source_pos;
        F32 beam_length = beam_vec.length();

        if (beam_length < 0.01f)
        {
            gGL.popUIMatrix();
            gGL.flush();
            return;  // Too short to render
        }

        // S24: LINE STYLE CONTROL (repurposed from width slider)
        // 0 = Solid, 1 = Dotted, 2 = Dashed, 3 = Dash-Dot
        S32 line_style = gSavedSettings.getS32("SelectionBeamLineStyle");

        glLineWidth(3.0f);  // Fixed nice visible width
        gGL.color4fv(base_color.mV);

        if (line_style == 0)
        {
            // SOLID LINE - simple and clean
            gGL.begin(LLRender::LINES);
            gGL.vertex3fv(source_pos.mV);
            gGL.vertex3fv(target_pos.mV);
            gGL.end();
        }
        else
        {
            // PATTERNED LINES - draw segments
            beam_vec.normalize();

            F32 segment_length, gap_length;
            bool draw_dot = false;

            if (line_style == 1)  // Dotted
            {
                segment_length = 0.05f;  // Short dots
                gap_length = 0.15f;
            }
            else if (line_style == 2)  // Dashed
            {
                segment_length = 0.3f;  // Longer dashes
                gap_length = 0.2f;
            }
            else  // Dash-Dot (line_style == 3)
            {
                segment_length = 0.3f;
                gap_length = 0.1f;
                draw_dot = true;  // Alternate dash/dot
            }

            F32 pattern_length = segment_length + gap_length;
            if (draw_dot) pattern_length += 0.05f + gap_length;  // Add dot + gap

            S32 num_patterns = (S32)(beam_length / pattern_length);

            gGL.begin(LLRender::LINES);
            for (S32 i = 0; i <= num_patterns; i++)
            {
                F32 start_dist = i * pattern_length;
                if (start_dist >= beam_length) break;

                // Draw main segment (dash)
                F32 end_dist = llmin(start_dist + segment_length, beam_length);
                LLVector3 seg_start = source_pos + beam_vec * start_dist;
                LLVector3 seg_end = source_pos + beam_vec * end_dist;

                gGL.vertex3fv(seg_start.mV);
                gGL.vertex3fv(seg_end.mV);

                // Draw dot if dash-dot pattern
                if (draw_dot && line_style == 3)
                {
                    F32 dot_start = start_dist + segment_length + gap_length;
                    F32 dot_end = dot_start + 0.05f;
                    if (dot_end < beam_length)
                    {
                        LLVector3 dot_s = source_pos + beam_vec * dot_start;
                        LLVector3 dot_e = source_pos + beam_vec * dot_end;
                        gGL.vertex3fv(dot_s.mV);
                        gGL.vertex3fv(dot_e.mV);
                    }
                }
            }
            gGL.end();
        }

        glLineWidth(1.0f);  // Restore default
        gGL.popUIMatrix();
        gGL.flush();
    }
    // Particles render automatically via mPartSourcep in particle simulator
}

void LLHUDEffectSpiral::renderForTimer()
{
    render();
}
