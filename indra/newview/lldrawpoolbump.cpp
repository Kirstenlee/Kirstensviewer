/**
 * @file lldrawpoolbump.cpp
 * @brief LLDrawPoolBump class implementation
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

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolbump.h"

#include "llstl.h"
#include "llviewercontrol.h"
#include "lldir.h"
#include "m3math.h"
#include "m4math.h"
#include "v4math.h"
#include "llglheaders.h"
#include "llrender.h"

#include "DXCubeMap.h"
#include "lldrawable.h"
#include "llface.h"
#include "llsky.h"
#include "llstartup.h"
#include "lltextureentry.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "pipeline.h"
#include "llspatialpartition.h"
#include "llviewershadermgr.h"
#include "llmodel.h"
#include "dxdrawpoolbump.h"

//#include "llimagebmp.h"
//#include "../tools/imdebug/imdebug.h"

// static
LLStandardBumpmap gStandardBumpmapList[TEM_BUMPMAP_COUNT];
LLRenderTarget LLBumpImageList::sRenderTarget;

// static
U32 LLStandardBumpmap::sStandardBumpmapCount = 0;

// static
LLBumpImageList gBumpImageList;

const S32 STD_BUMP_LATEST_FILE_VERSION = 1;

const U32 VERTEX_MASK_SHINY = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL | LLVertexBuffer::MAP_COLOR;
const U32 VERTEX_MASK_BUMP = LLVertexBuffer::MAP_VERTEX |LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_TEXCOORD1;

U32 LLDrawPoolBump::sVertexMask = VERTEX_MASK_SHINY;

// static
void LLStandardBumpmap::shutdown()
{
    LLStandardBumpmap::destroyGL();
}

// static
void LLStandardBumpmap::restoreGL()
{
    addstandard();
}

// static
void LLStandardBumpmap::addstandard()
{
    if(!gTextureList.isInitialized())
    {
        //Note: loading pre-configuration sometimes triggers this call.
        //But it is safe to return here because bump images will be reloaded during initialization later.
        return ;
    }

    if (LLStartUp::getStartupState() < STATE_SEED_CAP_GRANTED)
    {
        // Not ready, need caps for images
        return;
    }

    // can't assert; we destroyGL and restoreGL a lot during *first* startup, which populates this list already, THEN we explicitly init the list as part of *normal* startup.  Sigh.  So clear the list every time before we (re-)add the standard bumpmaps.
  
    clear();
    LL_INFOS() << "Adding standard bumpmaps." << LL_ENDL;
    gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount++] = LLStandardBumpmap("None");       // BE_NO_BUMP
    gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount++] = LLStandardBumpmap("Brightness"); // BE_BRIGHTNESS
    gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount++] = LLStandardBumpmap("Darkness");   // BE_DARKNESS

    std::string file_name = gDirUtilp->getExpandedFilename( LL_PATH_APP_SETTINGS, "std_bump.ini" );
    LLFILE* file = LLFile::fopen( file_name, "rt" );     /*Flawfinder: ignore*/
    if( !file )
    {
        LL_WARNS() << "Could not open std_bump <" << file_name << ">" << LL_ENDL;
        return;
    }

    S32 file_version = 0;

    S32 fields_read = fscanf( file, "LLStandardBumpmap version %d", &file_version );
    if( fields_read != 1 )
    {
        LL_WARNS() << "Bad LLStandardBumpmap header" << LL_ENDL;
        return;
    }

    if( file_version > STD_BUMP_LATEST_FILE_VERSION )
    {
        LL_WARNS() << "LLStandardBumpmap has newer version (" << file_version << ") than viewer (" << STD_BUMP_LATEST_FILE_VERSION << ")" << LL_ENDL;
        return;
    }

    while( !feof(file) && (LLStandardBumpmap::sStandardBumpmapCount < (U32)TEM_BUMPMAP_COUNT) )
    {
        // *NOTE: This buffer size is hard coded into scanf() below.
        char label[2048] = "";  /* Flawfinder: ignore */
        char bump_image_id[2048] = "";  /* Flawfinder: ignore */
        fields_read = fscanf(   /* Flawfinder: ignore */
            file, "\n%2047s %2047s", label, bump_image_id);
        if( EOF == fields_read )
        {
            break;
        }
        if( fields_read != 2 )
        {
            LL_WARNS() << "Bad LLStandardBumpmap entry" << LL_ENDL;
            return;
        }


        gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mLabel = label;
        gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage =
            LLViewerTextureManager::getFetchedTexture(LLUUID(bump_image_id));
        gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage->setBoostLevel(LLGLTexture::LOCAL) ;
        // This asset feeds generateNormalMapFromAlpha()'s finite-difference/
        // emboss technique, which subtracts two nearby texel samples to
        // derive a gradient - block-compression quantization error gets
        // amplified by that subtraction into visible artifacts. Opted out of
        // compression via the same allow_compression mechanism
        // LLFontBitmapCache uses for glyph atlases (gradient-sensitive data).
        if (LLImageGL* bump_gl_tex = gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage->getGLTexture())
        {
            bump_gl_tex->setAllowCompression(false);
        }
        gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage->setLoadedCallback(LLBumpImageList::onSourceStandardLoaded, 0, true, false, NULL, NULL );
        gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage->forceToSaveRawImage(0, 30.f) ;
        LLStandardBumpmap::sStandardBumpmapCount++;
    }

    fclose( file );
}

// static
void LLStandardBumpmap::clear()
{
    LL_INFOS() << "Clearing standard bumpmaps." << LL_ENDL;
    for( U32 i = 0; i < LLStandardBumpmap::sStandardBumpmapCount; i++ )
    {
        gStandardBumpmapList[i].mLabel.assign("");
        gStandardBumpmapList[i].mImage = NULL;
    }
    sStandardBumpmapCount = 0;
}

// static
void LLStandardBumpmap::destroyGL()
{
    clear();
}



////////////////////////////////////////////////////////////////

LLDrawPoolBump::LLDrawPoolBump()
:  LLRenderPass(LLDrawPool::POOL_BUMP)
{
}


void LLDrawPoolBump::prerender()
{
    mShaderLevel = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}


void LLDrawPoolBump::renderGroup(LLSpatialGroup* group, U32 type, bool texture = true)
{
    LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[type];

    for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
    {
        LLDrawInfo& params = **k;

        applyModelMatrix(params);

        params.mVertexBuffer->setBuffer();
        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
    }
}


// static
bool LLDrawPoolBump::bindBumpMap(LLDrawInfo& params, S32 channel)
{
    U8 bump_code = params.mBump;

    return bindBumpMap(bump_code, params.mTexture, channel);
}

//static
bool LLDrawPoolBump::bindBumpMap(LLFace* face, S32 channel)
{
    const LLTextureEntry* te = face->getTextureEntry();
    if (te)
    {
        U8 bump_code = te->getBumpmap();
        return bindBumpMap(bump_code, face->getTexture(), channel);
    }

    return false;
}

//static
bool LLDrawPoolBump::bindBumpMap(U8 bump_code, LLViewerTexture* texture, S32 channel)
{
    //Note: texture atlas does not support bump texture now.
    LLViewerFetchedTexture* tex = LLViewerTextureManager::staticCastToFetchedTexture(texture) ;
    if(!tex)
    {
        //if the texture is not a fetched texture
        return false;
    }

    LLViewerTexture* bump = NULL;

    switch( bump_code )
    {
    case BE_NO_BUMP:
        break;
    case BE_BRIGHTNESS:
    case BE_DARKNESS:
        bump = gBumpImageList.getBrightnessDarknessImage( tex, bump_code );
        break;

    default:
        if( bump_code < LLStandardBumpmap::sStandardBumpmapCount )
        {
            bump = gStandardBumpmapList[bump_code].mImage;
            gBumpImageList.addTextureStats(bump_code, tex->getID(), tex->getMaxVirtualSize());
        }
        break;
    }

    if (bump)
    {
        if (channel == -2)
        {
            gDX.getTexUnit(1)->bindFast(bump);
            gDX.getTexUnit(0)->bindFast(bump);
        }
        else
        {
            // NOTE: do not use bindFast here (see SL-16222)
            gDX.getTexUnit(channel)->bind(bump);
        }

        return true;
    }

    return false;
}

S32 LLDrawPoolBump::getNumDeferredPasses()
{
    return 1;
}

void LLDrawPoolBump::renderDeferred(S32 pass)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
    DXDrawPoolBump::renderDeferred(*this, pass);
}

void LLDrawPoolBump::renderPostDeferred(S32 pass)
{
    DXDrawPoolBump::renderPostDeferred(*this, pass);
}


////////////////////////////////////////////////////////////////
// List of bump-maps created from other textures.


//const LLUUID TEST_BUMP_ID("3d33eaf2-459c-6f97-fd76-5fce3fc29447");

void LLBumpImageList::init()
{
    llassert( mBrightnessEntries.size() == 0 );
    llassert( mDarknessEntries.size() == 0 );

    LLStandardBumpmap::restoreGL();
}

void LLBumpImageList::clear()
{
    LL_INFOS() << "Clearing dynamic bumpmaps." << LL_ENDL;
    // these will be re-populated on-demand
    mBrightnessEntries.clear();
    mDarknessEntries.clear();

    sRenderTarget.release();

    LLStandardBumpmap::clear();
}

void LLBumpImageList::shutdown()
{
    clear();
    LLStandardBumpmap::shutdown();
}

void LLBumpImageList::destroyGL()
{
    clear();
    LLStandardBumpmap::destroyGL();
}

void LLBumpImageList::restoreGL()
{
    if(!gTextureList.isInitialized())
    {
        //safe to return here because bump images will be reloaded during initialization later.
        return ;
    }

    LLStandardBumpmap::restoreGL();
    // Images will be recreated as they are needed.
}


LLBumpImageList::~LLBumpImageList()
{
    // Shutdown should have already been called.
    llassert( mBrightnessEntries.size() == 0 );
    llassert( mDarknessEntries.size() == 0 );
}


// Note: Does nothing for entries in gStandardBumpmapList that are not actually standard bump images (e.g. none, brightness, and darkness)
void LLBumpImageList::addTextureStats(U8 bump, const LLUUID& base_image_id, F32 virtual_size)
{
    bump &= TEM_BUMP_MASK;
    LLViewerFetchedTexture* bump_image = gStandardBumpmapList[bump].mImage;
    if( bump_image )
    {
        bump_image->addTextureStats(virtual_size);
    }
}


void LLBumpImageList::updateImages()
{
    llassert(LLCoros::on_main_thread_main_coro()); // This code is not thread safe

    for (bump_image_map_t::iterator iter = mBrightnessEntries.begin(); iter != mBrightnessEntries.end(); )
    {
        LLViewerTexture* image = iter->second;
        if( image )
        {
            bool destroy = true;
            if( image->hasGLTexture())
            {
                if( image->getBoundRecently() )
                {
                    destroy = false;
                }
                else
                {
                    image->destroyGLTexture();
                }
            }

            if( destroy )
            {
                iter = mBrightnessEntries.erase(iter);   // deletes the image thanks to reference counting
                continue;
            }
        }
        ++iter;
    }

    for (bump_image_map_t::iterator iter = mDarknessEntries.begin(); iter != mDarknessEntries.end(); )
    {
        bump_image_map_t::iterator curiter = iter++;
        LLViewerTexture* image = curiter->second;
        if( image )
        {
            bool destroy = true;
            if( image->hasGLTexture())
            {
                if( image->getBoundRecently() )
                {
                    destroy = false;
                }
                else
                {
                    image->destroyGLTexture();
                }
            }

            if( destroy )
            {
                mDarknessEntries.erase(curiter);  // deletes the image thanks to reference counting
            }
        }
    }
}


// Note: the caller SHOULD NOT keep the pointer that this function returns.  It may be updated as more data arrives.
LLViewerTexture* LLBumpImageList::getBrightnessDarknessImage(LLViewerFetchedTexture* src_image, U8 bump_code )
{
    llassert( (bump_code == BE_BRIGHTNESS) || (bump_code == BE_DARKNESS) );

    LLViewerTexture* bump = nullptr;

    bump_image_map_t* entries_list = nullptr;

    switch( bump_code )
    {
    case BE_BRIGHTNESS:
        entries_list = &mBrightnessEntries;
        break;
    case BE_DARKNESS:
        entries_list = &mDarknessEntries;
        break;
    default:
        llassert(0);
        return nullptr;
    }

    bump_image_map_t::iterator iter = entries_list->find(src_image->getID());
    if (iter != entries_list->end() && iter->second.notNull())
    {
        bump = iter->second;
    }

    if (bump == nullptr ||
        src_image->getWidth() != bump->getWidth() ||
        src_image->getHeight() != bump->getHeight())
    {
        onSourceUpdated(src_image, (EBumpEffect) bump_code);
    }

    return (*entries_list)[src_image->getID()];
}


void LLBumpImageList::onSourceStandardLoaded( bool success, LLViewerFetchedTexture* src_vi, LLImageRaw* src, LLImageRaw* aux_src, S32 discard_level, bool final, void* userdata)
{
    if (success && LLPipeline::sRenderDeferred)
    {
        LLPointer<LLImageRaw> nrm_image = new LLImageRaw(src->getWidth(), src->getHeight(), 4);
        {
            generateNormalMapFromAlpha(src, nrm_image);
        }
        src_vi->setExplicitFormat(GL_RGBA, GL_RGBA);
        {
            if (!src_vi->createGLTexture(src_vi->getDiscardLevel(), nrm_image))
            {
                LL_WARNS() << "Failed to create bump image texture for image " << src_vi->getID() << LL_ENDL;
            }
        }
    }
}

void LLBumpImageList::generateNormalMapFromAlpha(LLImageRaw* src, LLImageRaw* nrm_image)
{
    LLImageDataSharedLock lockIn(src);
    LLImageDataLock lockOut(nrm_image);

    U8* nrm_data = nrm_image->getData();
    S32 resX = src->getWidth();
    S32 resY = src->getHeight();

    const U8* src_data = src->getData();

    S32 src_cmp = src->getComponents();

    F32 norm_scale = gSavedSettings.getF32("RenderNormalMapScale");

    U32 idx = 0;
    //generate normal map from pseudo-heightfield
    for (S32 j = 0; j < resY; ++j)
    {
        for (S32 i = 0; i < resX; ++i)
        {
            S32 rX = (i+1)%resX;
            S32 rY = (j+1)%resY;
            S32 lX = (i-1)%resX;
            S32 lY = (j-1)%resY;

            if (lX < 0)
            {
                lX += resX;
            }
            if (lY < 0)
            {
                lY += resY;
            }

            F32 cH = (F32) src_data[(j*resX+i)*src_cmp+src_cmp-1];

            LLVector3 right = LLVector3(norm_scale, 0, (F32) src_data[(j*resX+rX)*src_cmp+src_cmp-1]-cH);
            LLVector3 left = LLVector3(-norm_scale, 0, (F32) src_data[(j*resX+lX)*src_cmp+src_cmp-1]-cH);
            LLVector3 up = LLVector3(0, -norm_scale, (F32) src_data[(lY*resX+i)*src_cmp+src_cmp-1]-cH);
            LLVector3 down = LLVector3(0, norm_scale, (F32) src_data[(rY*resX+i)*src_cmp+src_cmp-1]-cH);

            LLVector3 norm = right%down + down%left + left%up + up%right;

            norm.normVec();

            norm *= 0.5f;
            norm += LLVector3(0.5f,0.5f,0.5f);

            idx = (j*resX+i)*4;
            nrm_data[idx+0]= (U8) (norm.mV[0]*255);
            nrm_data[idx+1]= (U8) (norm.mV[1]*255);
            nrm_data[idx+2]= (U8) (norm.mV[2]*255);
            nrm_data[idx+3]= src_data[(j*resX+i)*src_cmp+src_cmp-1];
        }
    }
}

// static
void LLBumpImageList::onSourceUpdated(LLViewerTexture* src, EBumpEffect bump_code)
{

    const LLUUID& src_id = src->getID();

    bump_image_map_t& entries_list(bump_code == BE_BRIGHTNESS ? gBumpImageList.mBrightnessEntries : gBumpImageList.mDarknessEntries);
    bump_image_map_t::iterator iter = entries_list.find(src_id);

    if (iter == entries_list.end())
    { //make sure an entry exists for this image
        entries_list[src_id] = LLViewerTextureManager::getLocalTexture(true);
        iter = entries_list.find(src_id);
    }

    //---------------------------------------------------
    // immediately assign bump to a smart pointer in case some local smart pointer
    // accidentally releases it.
    LLPointer<LLViewerTexture> bump = iter->second;

#ifndef DX_RENDER
    // GL-only: this renders into an existing, externally-owned LLImageGL via
    // LLRenderTarget::setColorAttachment() - DXRenderTarget can't render into
    // an arbitrary externally-owned DXTexture yet (see llrendertarget.cpp).
    // Excluded entirely rather than partially no-op; bump-mapped surfaces
    // get whatever normal map `bump` already held (blank) under DX_RENDER.
    if (bump->getWidth() != src->getWidth() ||
        bump->getHeight() != src->getHeight()) // bump not cached yet or has changed resolution
    {
        //convert to normal map

        bump->setExplicitFormat(GL_RGBA, GL_RGBA);

        LLImageGL* src_img = src->getGLTexture();
        LLImageGL* dst_img = bump->getGLTexture();
        if (!dst_img->setSize(src->getWidth(), src->getHeight(), 4, 0))
        {
            LL_WARNS() << "Failed to setSize for image " << bump->getID() << LL_ENDL;
        }
        dst_img->setUseMipMaps(true);
        dst_img->setDiscardLevel(0);
        dst_img->createGLTexture();

        gDX.getTexUnit(0)->bind(bump);

        LLImageGL::setManualImage(GL_TEXTURE_2D, 0, dst_img->getPrimaryFormat(), dst_img->getWidth(), dst_img->getHeight(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr, false);

        LLGLuint tex_name = dst_img->getTexName();
        // point render target at empty buffer
        sRenderTarget.setColorAttachment(bump->getGLTexture(), tex_name);

        // generate normal map in empty texture
        {
            sRenderTarget.bindTarget();

            LLGLDepthTest depth(GL_FALSE);
            LLGLDisable cull(GL_CULL_FACE);
            LLGLDisable blend(GL_BLEND);
            gDX.setColorWriteMask(true, true);

            LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
            gNormalMapGenProgram.bind();

            static LLStaticHashedString sNormScale("norm_scale");
            static LLStaticHashedString sStepX("stepX");
            static LLStaticHashedString sStepY("stepY");
            static LLStaticHashedString sBumpCode("bump_code");

            gNormalMapGenProgram.uniform1f(sNormScale, gSavedSettings.getF32("RenderNormalMapScale"));
            gNormalMapGenProgram.uniform1f(sStepX, 1.f / bump->getWidth());
            gNormalMapGenProgram.uniform1f(sStepY, 1.f / bump->getHeight());
            gNormalMapGenProgram.uniform1i(sBumpCode, bump_code);

            gDX.getTexUnit(0)->bind(src);

            gDX.begin(LLRender::TRIANGLE_STRIP);
            gDX.texCoord2f(0, 0);
            gDX.vertex2f(0, 0);

            gDX.texCoord2f(0, 1);
            gDX.vertex2f(0, 1);

            gDX.texCoord2f(1, 0);
            gDX.vertex2f(1, 0);

            gDX.texCoord2f(1, 1);
            gDX.vertex2f(1, 1);

            gDX.end();

            gDX.flush();

            sRenderTarget.flush();
            sRenderTarget.releaseColorAttachment();

            if (shader)
            {
                shader->bind();
            }
        }

        // generate mipmap
        gDX.getTexUnit(0)->bind(bump);
        glGenerateMipmap(GL_TEXTURE_2D);
        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    }
#endif // !DX_RENDER

    iter->second = bump; // derefs (and deletes) old image
    //---------------------------------------------------

}

void LLDrawPoolBump::pushBumpBatches(U32 type)
{
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    if (mRigged)
    { // nudge type enum and include skinweights for rigged pass
        type += 1;
    }

    LLCullResult::drawinfo_iterator begin = gPipeline.beginRenderMap(type);
    LLCullResult::drawinfo_iterator end = gPipeline.endRenderMap(type);

    for (LLCullResult::drawinfo_iterator i = begin; i != end; ++i)
    {
        LLDrawInfo& params = **i;

        if (LLDrawPoolBump::bindBumpMap(params))
        {
            if (mRigged)
            {
                if (!uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                        continue;
                }
            }
            pushBumpBatch(params, false);
        }
    }
}

void LLRenderPass::pushBumpBatch(LLDrawInfo& params, bool texture, bool batch_textures)
{
    applyModelMatrix(params);

    bool tex_setup = false;

    if (batch_textures && params.mTextureList.size() > 1)
    {
        for (U32 i = 0; i < params.mTextureList.size(); ++i)
        {
            if (params.mTextureList[i].notNull())
            {
                gDX.getTexUnit(i)->bindFast(params.mTextureList[i]);
            }
        }
    }
    else
    { //not batching textures or batch has only 1 texture -- might need a texture matrix
        if (params.mTextureMatrix)
        {
            gDX.getTexUnit(0)->activate();
            gDX.matrixMode(LLRender::MM_TEXTURE);
            gDX.loadMatrix((F32*) params.mTextureMatrix->mMatrix);
            gPipeline.mTextureMatrixOps++;

            tex_setup = true;
        }
    }

    params.mVertexBuffer->setBuffer();
    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);

    if (tex_setup)
    {
        gDX.getTexUnit(0)->activate();
        gDX.matrixMode(LLRender::MM_TEXTURE);
        gDX.loadIdentity();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
    }
}

