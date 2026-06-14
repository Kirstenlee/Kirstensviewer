/**
 * @file llfloaterobjectweights.cpp
 * @brief Object weights advanced view floater
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

#include "llfloaterobjectweights.h"

#include "llparcel.h"

#include "llfloaterreg.h"
#include "lltextbox.h"

#include "llagent.h"
#include "llcallbacklist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llmeshrepository.h"

static const std::string lod_strings[4] =
{
    "lowest_lod",
    "low_lod",
    "medium_lod",
    "high_lod",
};

// virtual
bool LLCrossParcelFunctor::apply(LLViewerObject* obj)
{
    // Add the root object box.
    mBoundingBox.addBBoxAgent(LLBBox(obj->getPositionRegion(), obj->getRotationRegion(), obj->getScale() * -0.5f, obj->getScale() * 0.5f).getAxisAligned());

    // Extend the bounding box across all the children.
    LLViewerObject::const_child_list_t children = obj->getChildren();
    for (LLViewerObject::const_child_list_t::const_iterator iter = children.begin();
         iter != children.end(); iter++)
    {
        LLViewerObject* child = *iter;
        mBoundingBox.addBBoxAgent(LLBBox(child->getPositionRegion(), child->getRotationRegion(), child->getScale() * -0.5f, child->getScale() * 0.5f).getAxisAligned());
    }

    bool result = false;

    LLViewerRegion* region = obj->getRegion();
    if (region)
    {
        std::vector<LLBBox> boxes;
        boxes.push_back(mBoundingBox);
        result = region->objectsCrossParcel(boxes);
    }

    return result;
}

LLFloaterObjectWeights::LLFloaterObjectWeights(const LLSD& key)
:   LLFloater(key),
    mWeightsDirty(false),
    mSelectionDirty(true),
    mSelectionMeshDirty(true),
    mSelectionLastLOD(-1),
    mSelectionLastTris(0),
    mSelectionLastArea(0.f),
    mLastActiveLODRequests(0),
    mSelectionRefreshTime(0.),
    mSelectedObjects(NULL),
    mSelectedPrims(NULL),
    mSelectedDownloadWeight(NULL),
    mSelectedPhysicsWeight(NULL),
    mSelectedServerWeight(NULL),
    mSelectedDisplayWeight(NULL),
    mSelectedOnLand(NULL),
    mRezzedOnLand(NULL),
    mRemainingCapacity(NULL),
    mTotalCapacity(NULL),
    mLodLevel(nullptr),
    mTrianglesShown(nullptr),
    mPixelArea(nullptr),
    mHighLodTris(nullptr),
    mMediumLodTris(nullptr),
    mLowLodTris(nullptr),
    mLowestLodTris(nullptr)
{
    // S24_IMPROVEMENT: Lambda-based selection callback (cleaner than global functor)
    mSelectionConnection = LLSelectMgr::getInstance()->mUpdateSignal.connect(
        [this]()
        {
            mSelectionDirty = true;
            mSelectionMeshDirty = true; // assume that mesh data will arrive with a delay.
        }
    );
}

LLFloaterObjectWeights::~LLFloaterObjectWeights()
{
}

// virtual
bool LLFloaterObjectWeights::postBuild()
{
    mSelectedObjects = getChild<LLTextBox>("objects");
    mSelectedPrims = getChild<LLTextBox>("prims");

    mSelectedDownloadWeight = getChild<LLTextBox>("download");
    mSelectedPhysicsWeight = getChild<LLTextBox>("physics");
    mSelectedServerWeight = getChild<LLTextBox>("server");
    mSelectedDisplayWeight = getChild<LLTextBox>("display");

    mSelectedOnLand = getChild<LLTextBox>("selected");
    mRezzedOnLand = getChild<LLTextBox>("rezzed_on_land");
    mRemainingCapacity = getChild<LLTextBox>("remaining_capacity");
    mTotalCapacity = getChild<LLTextBox>("total_capacity");

    mLodLevel = getChild<LLTextBox>("lod_level");
    mTrianglesShown = getChild<LLTextBox>("triangles_shown");
    mPixelArea = getChild<LLTextBox>("pixel_area");

    mHighLodTris = getChild<LLTextBox>("high_lod_tris");
    mMediumLodTris = getChild<LLTextBox>("medium_lod_tris");
    mLowLodTris = getChild<LLTextBox>("low_lod_tris");
    mLowestLodTris = getChild<LLTextBox>("lowest_lod_tris");

    return true;
}

// virtual
void LLFloaterObjectWeights::onOpen(const LLSD& key)
{
    refresh();
    updateLandImpacts(LLViewerParcelMgr::getInstance()->getFloatingParcelSelection()->getParcel());

    // S24_OPTIMIZATION: Immediate refresh on open, then register idle callback for updates
    onIdleRefresh(this);
    gIdleCallbacks.addFunction(onIdleRefresh, this);
}

// S24_IMPROVEMENT: Proper cleanup on close to avoid idle callback leaks
void LLFloaterObjectWeights::onClose(bool app_quitting)
{
    gIdleCallbacks.deleteFunction(onIdleRefresh, this);
}

// virtual
void LLFloaterObjectWeights::onWeightsUpdate(const SelectionCost& selection_cost)
{
    mSelectedDownloadWeight->setText(llformat("%.1f", selection_cost.mNetworkCost));
    mSelectedPhysicsWeight->setText(llformat("%.1f", selection_cost.mPhysicsCost));
    mSelectedServerWeight->setText(llformat("%.1f", selection_cost.mSimulationCost));

    // S24_DEFENSIVE: onWeightsUpdate comes from a coroutine.
    // Postpone LLSelectMgr operations as getSelectedObjectRenderCost is not coroutine-safe
    mWeightsDirty = true;

    toggleWeightsLoadingIndicators(false);
}

//virtual
void LLFloaterObjectWeights::setErrorStatus(S32 status, const std::string& reason)
{
    const std::string text = getString("nothing_selected");

    mSelectedDownloadWeight->setText(text);
    mSelectedPhysicsWeight->setText(text);
    mSelectedServerWeight->setText(text);
    mSelectedDisplayWeight->setText(text);

    toggleWeightsLoadingIndicators(false);
}

void LLFloaterObjectWeights::draw()
{
    // S24_OPTIMIZATION: Avoid heavy computation in draw() - use cached dirty flags
    // This logic detects changes and marks dirty flags; actual refresh happens in onIdleRefresh()
    // Having callbacks like 'tris changed' in every selected object would be more impactful on performance.
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (!selection->isEmpty() && !mSelectionDirty)
    {
        S32 object_lod = -1;
        bool multiple_lods = false;
        S32 total_tris = 0;
        F32 pixel_area = 0.f;
        for (LLObjectSelection::valid_root_iterator iter = selection->valid_root_begin();
            iter != selection->valid_root_end(); ++iter)
        {
            LLViewerObject* object = (*iter)->getObject();
            S32 lod = object->getLOD();
            if (object_lod < 0)
            {
                object_lod = lod;
            }
            else if (object_lod != lod)
            {
                multiple_lods = true;
            }

            if (object->isRootEdit())
            {
                total_tris += object->recursiveGetTriangleCount();
                pixel_area += object->getPixelArea();
            }
        }

        // S24_IMPROVEMENT: Compare cached state to detect changes without full refresh
        if (mSelectionLastTris != total_tris)
        {
            mSelectionDirty = true;
        }
        else if (mSelectionLastArea != pixel_area)
        {
            mSelectionDirty = true;
        }
        else if (multiple_lods && mSelectionLastLOD >= 0)
        {
            mSelectionDirty = true;
        }
        else if (!multiple_lods && mSelectionLastLOD != object_lod)
        {
            mSelectionDirty = true;
        }
    }

    LLFloater::draw();
}

void LLFloaterObjectWeights::updateLandImpacts(const LLParcel* parcel)
{
    if (!parcel || LLSelectMgr::getInstance()->getSelection()->isEmpty())
    {
        updateIfNothingSelected();
    }
    else
    {
        S32 rezzed_prims = parcel->getSimWidePrimCount();
        S32 total_capacity = parcel->getSimWideMaxPrimCapacity();
        // Can't have more than region max tasks, regardless of parcel
        // object bonus factor.
        LLViewerRegion* region = LLViewerParcelMgr::getInstance()->getSelectionRegion();
        if (region)
        {
            S32 max_tasks_per_region = (S32)region->getMaxTasks();
            total_capacity = llmin(total_capacity, max_tasks_per_region);
        }

        mRezzedOnLand->setText(llformat("%d", rezzed_prims));
        mRemainingCapacity->setText(llformat("%d", total_capacity - rezzed_prims));
        mTotalCapacity->setText(llformat("%d", total_capacity));

        toggleLandImpactsLoadingIndicators(false);
    }
}

void LLFloaterObjectWeights::refresh()
{
    LLSelectMgr* sel_mgr = LLSelectMgr::getInstance();

    if (sel_mgr->getSelection()->isEmpty())
    {
        updateIfNothingSelected();
    }
    else
    {
        S32 prim_count = sel_mgr->getSelection()->getObjectCount();
        S32 link_count = sel_mgr->getSelection()->getRootObjectCount();
        F32 prim_equiv = sel_mgr->getSelection()->getSelectedLinksetCost();

        mSelectedObjects->setText(llformat("%d", link_count));
        mSelectedPrims->setText(llformat("%d", prim_count));
        mSelectedOnLand->setText(llformat("%.1d", (S32)prim_equiv));

        LLCrossParcelFunctor func;
        if (sel_mgr->getSelection()->applyToRootObjects(&func, true))
        {
            // Some of the selected objects cross parcel bounds.
            // We don't display object weights and land impacts in this case.
            const std::string text = getString("nothing_selected");

            mRezzedOnLand->setText(text);
            mRemainingCapacity->setText(text);
            mTotalCapacity->setText(text);

            toggleLandImpactsLoadingIndicators(false);
        }

        LLViewerRegion* region = gAgent.getRegion();
        if (region && region->capabilitiesReceived())
        {
            for (LLObjectSelection::valid_root_iterator iter = sel_mgr->getSelection()->valid_root_begin();
                    iter != sel_mgr->getSelection()->valid_root_end(); ++iter)
            {
                LLAccountingCostManager::getInstance()->addObject((*iter)->getObject()->getID());
            }

            std::string url = region->getCapability("ResourceCostSelected");
            if (!url.empty())
            {
                // Update the transaction id before the new fetch request
                generateTransactionID();

                LLAccountingCostManager::getInstance()->fetchCosts(Roots, url, getObserverHandle());
                toggleWeightsLoadingIndicators(true);
            }
        }
        else
        {
            LL_WARNS() << "Failed to get region capabilities" << LL_ENDL;
        }
    }
}

// S24_OPTIMIZATION: Idle-based refresh with throttling to avoid excessive updates
//static
void LLFloaterObjectWeights::onIdleRefresh(void* user_data)
{
    LLFloaterObjectWeights* self = (LLFloaterObjectWeights*)user_data;

    F64 current_time = LLTimer::getTotalSeconds();
    S32 current_lod_requests = LLMeshRepoThread::sActiveLODRequests.load();

    // S24_IMPROVEMENT: Track mesh loading completion without polling every frame
    // Can't look for a specific mesh (no callback mechanics and we need multiple ones),
    // so just update periodically if something loads
    self->mSelectionMeshDirty |= (self->mLastActiveLODRequests > current_lod_requests);
    bool throttle_elapsed = (current_time - self->mSelectionRefreshTime) >= 2.0;

    if (self->mLastActiveLODRequests < current_lod_requests)
    {
        // We are interested in finished downloads,
        // so don't refresh if more requests are getting added.
        self->mLastActiveLODRequests = current_lod_requests;
    }

    if (self->mSelectionDirty)
    {
        // Always refresh on selection changes
        self->refreshDataFromSelection();
        self->mSelectionDirty = false;
        self->mSelectionRefreshTime = current_time;
        // refreshDataFromSelection could have indirectly initiated more requests
        self->mLastActiveLODRequests = LLMeshRepoThread::sActiveLODRequests.load();
    }
    else if (self->mSelectionMeshDirty && throttle_elapsed)
    {
        // S24_OPTIMIZATION: 2-second throttle on mesh updates to avoid excessive recalculation
        // LOD requests count changed or mesh needs lod data reload
        self->refreshDataFromSelection();
        self->mSelectionRefreshTime = current_time;
        self->mSelectionMeshDirty = false;
        // refreshDataFromSelection could have indirectly initiated more requests
        self->mLastActiveLODRequests = LLMeshRepoThread::sActiveLODRequests.load();
    }
    else if (self->mWeightsDirty) // also done in refreshDataFromSelection
    {
        // S24_DEFENSIVE: Safe main-thread update deferred from coroutine callback
        LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
        S32 render_cost = selection->getSelectedObjectRenderCost();
        self->mSelectedDisplayWeight->setText(llformat("%d", render_cost));
        self->mWeightsDirty = false;
    }
}

// S24_OPTIMIZATION: Consolidated refresh logic extracted from draw() for reuse
void LLFloaterObjectWeights::refreshDataFromSelection()
{
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (selection->isEmpty())
    {
        const std::string text = getString("nothing_selected");
        mLodLevel->setText(text);
        mTrianglesShown->setText(text);
        mPixelArea->setText(text);

        mHighLodTris->setText(text);
        mMediumLodTris->setText(text);
        mLowLodTris->setText(text);
        mLowestLodTris->setText(text);

        toggleRenderLoadingIndicators(false);
        toggleLODLoadingIndicators(false);
    }
    else
    {
        S32 object_lod = -1;
        bool multiple_lods = false;
        S32 total_tris = 0;
        F32 pixel_area = 0.f;
        S32 high_tris = 0;
        S32 medium_tris = 0;
        S32 low_tris = 0;
        S32 lowest_tris = 0;

        // S24_OPTIMIZATION: Single iteration to gather all metrics
        for (LLObjectSelection::valid_root_iterator iter = selection->valid_root_begin();
            iter != selection->valid_root_end(); ++iter)
        {
            LLViewerObject* object = (*iter)->getObject();
            S32 lod = object->getLOD();
            if (object_lod < 0)
            {
                object_lod = lod;
            }
            else if (object_lod != lod)
            {
                multiple_lods = true;
            }

            if (object->isRootEdit())
            {
                total_tris += object->recursiveGetTriangleCount();
                pixel_area += object->getPixelArea();
                // S24_IMPROVEMENT: Single call for all LOD levels (avoids 4 separate traversals)
                object->recursiveGetLODTriangleCount(high_tris, medium_tris, low_tris, lowest_tris);
            }
        }

        // S24_DEFENSIVE: Cache state for change detection in draw()
        mSelectionLastTris = total_tris;
        mSelectionLastArea = pixel_area;
        if (multiple_lods)
        {
            mSelectionLastLOD = -1;
        }
        else
        {
            mSelectionLastLOD = object_lod;
        }

        if (multiple_lods)
        {
            mLodLevel->setText(getString("multiple_lods"));
            toggleRenderLoadingIndicators(false);
            toggleLODLoadingIndicators(false);
        }
        else if (object_lod < 0)
        {
            // S24_NOTE: Nodes are waiting for data
            toggleRenderLoadingIndicators(true);
            toggleLODLoadingIndicators(true);
        }
        else
        {
            mLodLevel->setText(getString(lod_strings[object_lod]));
            toggleRenderLoadingIndicators(false);
            toggleLODLoadingIndicators(false);
        }

        mTrianglesShown->setText(llformat("%d", total_tris));
        mPixelArea->setText(llformat("%ld", (S64)pixel_area)); // value capped at 10M
        mHighLodTris->setText(llformat("%d", high_tris));
        mMediumLodTris->setText(llformat("%d", medium_tris));
        mLowLodTris->setText(llformat("%d", low_tris));
        mLowestLodTris->setText(llformat("%d", lowest_tris));
    }

    // S24_DEFENSIVE: Safe deferred weight update from coroutine callback
    if (mWeightsDirty)
    {
        S32 render_cost = selection->getSelectedObjectRenderCost();
        mSelectedDisplayWeight->setText(llformat("%d", render_cost));
        mWeightsDirty = false;
    }
}

// virtual
void LLFloaterObjectWeights::generateTransactionID()
{
    mTransactionID.generate();
}

void LLFloaterObjectWeights::toggleWeightsLoadingIndicators(bool visible)
{
    childSetVisible("download_loading_indicator", visible);
    childSetVisible("physics_loading_indicator", visible);
    childSetVisible("server_loading_indicator", visible);
    childSetVisible("display_loading_indicator", visible);

    mSelectedDownloadWeight->setVisible(!visible);
    mSelectedPhysicsWeight->setVisible(!visible);
    mSelectedServerWeight->setVisible(!visible);
    mSelectedDisplayWeight->setVisible(!visible);
}

void LLFloaterObjectWeights::toggleLandImpactsLoadingIndicators(bool visible)
{
    childSetVisible("selected_loading_indicator", visible);
    childSetVisible("rezzed_on_land_loading_indicator", visible);
    childSetVisible("remaining_capacity_loading_indicator", visible);
    childSetVisible("total_capacity_loading_indicator", visible);

    mSelectedOnLand->setVisible(!visible);
    mRezzedOnLand->setVisible(!visible);
    mRemainingCapacity->setVisible(!visible);
    mTotalCapacity->setVisible(!visible);
}

void LLFloaterObjectWeights::toggleRenderLoadingIndicators(bool visible)
{
    childSetVisible("lod_level_loading_indicator", visible);
    childSetVisible("triangles_shown_loading_indicator", visible);
    childSetVisible("pixel_area_loading_indicator", visible);

    mLodLevel->setVisible(!visible);
    mTrianglesShown->setVisible(!visible);
    mPixelArea->setVisible(!visible);
}

void LLFloaterObjectWeights::toggleLODLoadingIndicators(bool visible)
{
    childSetVisible("high_lod_tris_loading_indicator", visible);
    childSetVisible("medium_lod_tris_loading_indicator", visible);
    childSetVisible("low_lod_tris_loading_indicator", visible);
    childSetVisible("lowest_lod_tris_loading_indicator", visible);

    mHighLodTris->setVisible(!visible);
    mMediumLodTris->setVisible(!visible);
    mLowLodTris->setVisible(!visible);
    mLowestLodTris->setVisible(!visible);
}

void LLFloaterObjectWeights::updateIfNothingSelected()
{
    const std::string text = getString("nothing_selected");

    mSelectedObjects->setText(text);
    mSelectedPrims->setText(text);

    mSelectedDownloadWeight->setText(text);
    mSelectedPhysicsWeight->setText(text);
    mSelectedServerWeight->setText(text);
    mSelectedDisplayWeight->setText(text);

    mSelectedOnLand->setText(text);
    mRezzedOnLand->setText(text);
    mRemainingCapacity->setText(text);
    mTotalCapacity->setText(text);

    mLodLevel->setText(text);
    mTrianglesShown->setText(text);
    mPixelArea->setText(text);

    mHighLodTris->setText(text);
    mMediumLodTris->setText(text);
    mLowLodTris->setText(text);
    mLowestLodTris->setText(text);

    toggleWeightsLoadingIndicators(false);
    toggleLandImpactsLoadingIndicators(false);
    toggleRenderLoadingIndicators(false);
    toggleLODLoadingIndicators(false);
}
