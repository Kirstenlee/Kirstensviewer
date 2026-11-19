/**
* @file llskinningutil.cpp
* @brief  Functions for mesh object skinning
* @author vir@lindenlab.com
*
* $LicenseInfo:firstyear=2015&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2015, Linden Research, Inc.
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

#include "llskinningutil.h"
#include "llvoavatar.h"
#include "llviewercontrol.h"
#include "llmeshrepository.h"
#include "llvolume.h"
#include "llrigginginfo.h"

S32 LLSkinningUtil::getMaxJointCount()
{
    return (S32)LL_MAX_JOINTS_PER_MESH_OBJECT;
}

U32 LLSkinningUtil::getMeshJointCount(const LLMeshSkinInfo *skin)
{
    return llmin((U32)getMaxJointCount(), (U32)skin->mJointNames.size());
}

S32 LLSkinningUtil::getMaxGLTFJointCount()
{
    // this is the maximum number of 3x4 matrices than can fit in a UBO
    return gDXDeviceInfo.mMaxUniformBlockSize / 48;
}

void LLSkinningUtil::scrubInvalidJoints(LLVOAvatar *avatar, LLMeshSkinInfo* skin)
{
    if (skin->mInvalidJointsScrubbed)
    {
        return;
    }
    for (U32 j = 0; j < skin->mJointNames.size(); ++j)
    {
        // Fix invalid names to "mPelvis". Currently meshes with
        // invalid names will be blocked on upload, so this is just
        // needed for handling of any legacy bad data.
        if (!avatar->getJoint(skin->mJointNames[j]))
        {
            if (avatar->isSelf())
            {
                LL_WARNS_ONCE("Avatar") << "A mesh on own avatar is rigged to an invalid joint" << skin->mJointNames[j] << LL_ENDL;
            }
            else
            {
                LL_DEBUGS("Avatar") << avatar->getDebugName() << " mesh rigged to invalid joint " << skin->mJointNames[j] << LL_ENDL;
            }
            skin->mJointNames[j] = "mPelvis";
            skin->mJointNumsInitialized = false; // force update after names change.
        }
    }
    skin->mInvalidJointsScrubbed = true;
}

void LLSkinningUtil::initSkinningMatrixPalette(
    LLMatrix4a* mat,
    S32 count,
    const LLMeshSkinInfo* skin,
    LLVOAvatar *avatar)
{

    initJointNums(const_cast<LLMeshSkinInfo*>(skin), avatar);

    if (skin->mInvBindMatrix.size() < count )
    {
        // faulty model? mInvBindMatrix.size() should have matched mJointNames.size()
        // S24 : don't leave the caller's palette uninitialised
        for (S32 j = 0; j < count; ++j)
        {
            mat[j] = LLMatrix4a::identity();
        }
        return;
    }

    // S24 : the joint's world matrix is read in place (no intermediate copy of every joint matrix); a missing
    // joint gets identity, so the palette entry is the inverse bind matrix
    //NOTE: pointer striders used here as a micro-optimization over vector/array lookups
    const LLMatrix4a* invBind = &(skin->mInvBindMatrix[0]);
    const S32* joint_nums = skin->mJointNums.data();
    LLMatrix4a* m = mat;

    for (S32 j = 0; j < count; ++j)
    {
        LLJoint* joint = avatar->getJoint(joint_nums[j]);
        const LLMatrix4a& world = joint ? joint->getWorldMatrix4a() : LLMatrix4a::identity();
        matMulUnsafe(*(invBind++), world, *(m++));
    }
}

void LLSkinningUtil::checkSkinWeights(LLVector4a* weights, U32 num_vertices, const LLMeshSkinInfo* skin)
{
    // S24 : debug-only validation removed
}

void LLSkinningUtil::scrubSkinWeights(LLVector4a* weights, U32 num_vertices, const LLMeshSkinInfo* skin)
{
    const S32 max_joints = static_cast<S32>(skin->mJointNames.size());
    for (U32 j=0; j<num_vertices; j++)
    {
        F32 *w = weights[j].getF32ptr();

        for (U32 k=0; k<4; ++k)
        {
            S32 i = llfloor(w[k]);
            F32 f = w[k]-i;
            i = llclamp(i,0,max_joints-1);
            w[k] = i + f;
        }
    }
    checkSkinWeights(weights, num_vertices, skin);
}

void LLSkinningUtil::getPerVertexSkinMatrix(
    F32* weights,
    const LLMatrix4a* mat,
    bool handle_bad_scale,
    LLMatrix4a& final_mat,
    U32 max_joints)
{
    bool valid_weights = true;
    final_mat.clear();

    S32 idx[4];

    LLVector4 wght;

    F32 scale = 0.f;
    for (U32 k = 0; k < 4; k++)
    {
        F32 w = weights[k];

        // BENTO potential optimizations
        // - Do clamping in unpackVolumeFaces() (once instead of every time)
        // - int vs floor: if we know w is
        // >= 0.0, we can use int instead of floorf; the latter
        // allegedly has a lot of overhead due to ieeefp error
        // checking which we should not need.
        idx[k] = llclamp((S32) floorf(w), (S32)0, (S32)max_joints-1);

        wght[k] = w - floorf(w);
        scale += wght[k];
    }
    if (handle_bad_scale && scale <= 0.f)
    {
        wght = LLVector4(1.0f, 0.0f, 0.0f, 0.0f);
        valid_weights = false;
    }
    else
    {
        // This is enforced  in unpackVolumeFaces()
        llassert(scale>0.f);
        wght *= 1.f/scale;
    }

    for (U32 k = 0; k < 4; k++)
    {
        F32 w = wght[k];

        LLMatrix4a src;
        src.setMul(mat[idx[k]], w);

        final_mat.add(src);
    }
    // SL-366 - with weight validation/cleanup code, it should no longer be
    // possible to hit the bad scale case.
    llassert(valid_weights);
    // When building for Release, the above llassert() goes away. Ward off
    // variable-set-but-unused error.
    (void)valid_weights;
}

void LLSkinningUtil::initJointNums(LLMeshSkinInfo* skin, LLVOAvatar *avatar)
{
    if (!skin->mJointNumsInitialized)
    {
        for (U32 j = 0; j < skin->mJointNames.size(); ++j)
        {
            LLJoint *joint = (skin->mJointNums[j] == -1) ? avatar->getJoint(skin->mJointNames[j]) : avatar->getJoint(skin->mJointNums[j]);
            skin->mJointNums[j] = joint ? joint->getJointNum() : 0;
            // insure we have *a* valid joint to reference
            llassert(skin->mJointNums[j] >= 0);
        }
        skin->mJointNumsInitialized = true;
    }
}

void LLSkinningUtil::updateRiggingInfo(const LLMeshSkinInfo* skin, LLVOAvatar *avatar, LLVolumeFace& vol_face)
{
    if (vol_face.mJointRiggingInfoTab.needsUpdate())
    {
        S32 num_verts = vol_face.mNumVertices;
        S32 num_joints = static_cast<S32>(skin->mJointNames.size());
        if (num_verts > 0 && vol_face.mWeights && num_joints > 0)
        {
            initJointNums(const_cast<LLMeshSkinInfo*>(skin), avatar);
            if (vol_face.mJointRiggingInfoTab.size()==0)
            {
                vol_face.mJointRiggingInfoTab.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS);
                LLJointRiggingInfoTab &rig_info_tab = vol_face.mJointRiggingInfoTab;
                for (S32 i=0; i<vol_face.mNumVertices; i++)
                {
                    LLVector4a& pos = vol_face.mPositions[i];
                    F32 *weights = vol_face.mWeights[i].getF32ptr();
                    LLVector4 wght;
                    S32 idx[4];
                    F32 scale = 0.0f;
                    // FIXME unpacking of weights should be pulled into a common function and optimized if possible.
                    for (U32 k = 0; k < 4; k++)
                    {
                        F32 w = weights[k];
                        idx[k] = llclamp((S32) floorf(w), (S32)0, (S32)LL_CHARACTER_MAX_ANIMATED_JOINTS-1);
                        wght[k] = w - idx[k];
                    }

                    for (U32 k=0; k<4; ++k)
                    {
                        S32 joint_index = idx[k];
                        if (wght[k] > 0.2f && num_joints > joint_index)
                        {
                            S32 joint_num = skin->mJointNums[joint_index];
                            if (joint_num >= 0 && joint_num < LL_CHARACTER_MAX_ANIMATED_JOINTS)
                            {
                                rig_info_tab[joint_num].setIsRiggedTo(true);

                                size_t bind_poses_size = skin->mBindPoseMatrix.size();
                                const LLMatrix4a& mat = bind_poses_size > joint_index ? skin->mBindPoseMatrix[joint_index] : LLMatrix4a::identity();
                                LLVector4a pos_joint_space;

                                mat.affineTransform(pos, pos_joint_space);

                                LLVector4a *extents = rig_info_tab[joint_num].getRiggedExtents();
                                update_min_max(extents[0], extents[1], pos_joint_space);
                            }
                        }
                    }
                }
                vol_face.mJointRiggingInfoTab.setNeedsUpdate(false);
            }
        }
    }
}

// This is used for extracting rotation from a bind shape matrix that
// already has scales baked in
LLQuaternion LLSkinningUtil::getUnscaledQuaternion(const LLMatrix4& mat4)
{
    LLMatrix3 bind_mat = mat4.getMat3();
    for (auto i = 0; i < 3; i++)
    {
        F32 len = 0.0f;
        for (auto j = 0; j < 3; j++)
        {
            len += bind_mat.mMatrix[i][j] * bind_mat.mMatrix[i][j];
        }
        if (len > 0.0f)
        {
            len = sqrt(len);
            for (auto j = 0; j < 3; j++)
            {
                bind_mat.mMatrix[i][j] /= len;
            }
        }
    }
    bind_mat.invert();
    LLQuaternion bind_rot = bind_mat.quaternion();
    bind_rot.normalize();
    return bind_rot;
}

