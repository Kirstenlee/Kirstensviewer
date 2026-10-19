/**
 * @file dxdrawpoolbump.cpp
 * @brief LLDrawPoolBump/LLStandardBumpmap/LLBumpImageList class
 * implementations plus LLDrawPoolBump's DX11-native deferred bump and
 * fullbright-shiny/emboss-bump render paths.
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "llviewerprecompiledheaders.h"

#include "dxdrawpoolbump.h"

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
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "pipeline.h"
#include "dxpipeline.h"
#include "llspatialpartition.h"
#include "llviewershadermgr.h"
#include "llmodel.h"
#include "DXDevice.h"
#include "DXStateCache.h"

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
    LL_WARNS() << "Adding standard bumpmaps." << LL_ENDL;
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
        gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage->setBoostLevel(DXTextureBase::LOCAL) ;
        // This asset feeds generateNormalMapFromAlpha()'s finite-difference/
        // emboss technique, which subtracts two nearby texel samples to
        // derive a gradient - block-compression quantization error gets
        // amplified by that subtraction into visible artifacts. Opted out of
        // compression via the same allow_compression mechanism
        // LLFontBitmapCache uses for glyph atlases (gradient-sensitive data).
        if (LLImageDX* bump_gl_tex = gStandardBumpmapList[LLStandardBumpmap::sStandardBumpmapCount].mImage->getGLTexture())
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
    LL_WARNS() << "Clearing standard bumpmaps." << LL_ENDL;
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

void LLBumpImageList::init()
{
    llassert( mBrightnessEntries.size() == 0 );
    llassert( mDarknessEntries.size() == 0 );

    LLStandardBumpmap::restoreGL();
}

void LLBumpImageList::clear()
{
    LL_WARNS() << "Clearing dynamic bumpmaps." << LL_ENDL;
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
    if (success && DXPipeline::sRenderDeferred)
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

    // DXRenderTarget can't render into an arbitrary externally-owned DXTexture (see
    // llrendertarget.cpp), so bump-mapped surfaces just get whatever normal map `bump` already
    // held (blank).

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

    LLCullResult::drawinfo_iterator begin = DXPipeline::beginRenderMap(gPipeline, type);
    LLCullResult::drawinfo_iterator end = DXPipeline::endRenderMap(gPipeline, type);

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

namespace
{
    LLHLSLShader* shader = nullptr;
    S32 cube_channel = -1;
    S32 diffuse_channel = -1;

    void beginFullbrightShiny(LLDrawPoolBump& pool, bool rigged)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        shader = &gDeferredFullbrightShinyProgram;
        if (DXPipeline::sRenderingHUDs)
        {
            shader = &gHUDFullbrightShinyProgram;
        }

        if (rigged)
        {
            llassert(shader->mRiggedVariant);
            shader = shader->mRiggedVariant;
        }

        // bind exposure map so fullbright shader can cancel out exposure
        S32 channel = shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
        if (channel > -1)
        {
            gDX.getTexUnit(channel)->bind(&gPipeline.mExposureMap);
        }

        DXCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : nullptr;

        // use_legacy_env_map mirrors LLPipeline::bindDeferredShader()'s own
        // legacy-vs-modern env-map override (pipeline.cpp) - this pool has its
        // own separate copy of that branching since it doesn't route through
        // bindDeferredShader() at all, so keep the two in sync. S24: was reading
        // sReflectionProbesEnabled (the capture-pipeline master switch) alone -
        // narrower than DXPipeline::shouldUseLegacyEnvMap(), which also checks
        // RenderReflectionProbesEnabled (the actual user-facing "Reflections"
        // toggle). When only the latter was off, this pool took the "modern"
        // branch (binds probes, sets probes_enabled=0 in-shader) without ever
        // binding the legacy environmentMap texture - the shader's own fallback
        // then read whatever cubemap was last left in that slot by an unrelated
        // draw call. bindDeferredShader() already uses the correct helper; this
        // pool didn't.
        bool use_legacy_env_map = DXPipeline::shouldUseLegacyEnvMap();

        if (cube_map && use_legacy_env_map)
        {
            gDX.getTexUnit(1)->disable();
            cube_channel = shader->enableTexture(LLViewerShaderMgr::ENVIRONMENT_MAP, LLTexUnit::TT_CUBE_MAP);
            cube_map->enableTexture(cube_channel);
            diffuse_channel = shader->enableTexture(LLViewerShaderMgr::DIFFUSE_MAP);

            gDX.getTexUnit(cube_channel)->bind(cube_map);
            gDX.getTexUnit(0)->activate();
        }

        {
            LLMatrix4 mat;
            mat.initRows(LLVector4(gDXModelView + 0),
                         LLVector4(gDXModelView + 4),
                         LLVector4(gDXModelView + 8),
                         LLVector4(gDXModelView + 12));
            shader->bind();

            LLVector3 vec = LLVector3(gShinyOrigin) * mat;
            LLVector4 vec4(vec, gShinyOrigin.mV[3]);
            shader->uniform4fv(LLViewerShaderMgr::SHINY_ORIGIN, 1, vec4.mV);

            if (use_legacy_env_map)
            {
                DXPipeline::setEnvMat(gPipeline, *shader);
            }
            else
            {
                DXPipeline::bindReflectionProbes(gPipeline, *shader);
            }
        }

        if (pool.mShaderLevel > 1)
        { //indexed texture rendering, channel 0 is always diffuse
            diffuse_channel = 0;
        }
    }

    void renderFullbrightShiny(LLDrawPoolBump& pool, bool rigged)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        LLGLEnable blend_enable(GL_BLEND);

        // S24: gDeferredFullbrightShinyProgram sets mFeatures.mIndexedTextureChannels
        // unconditionally (llviewershadermgr.cpp), not gated on mShaderLevel - the shader
        // always compiles with HAS_DIFFUSE_LOOKUP and always expects a texture batch's
        // mTextureList bound across tex0..tex3. Gating batch_textures on mShaderLevel>1
        // here disagreed with that: at mShaderLevel<=1, real multi-texture LLDrawInfo
        // batches (built by the geometry side regardless of shader level) got pushed with
        // batch_textures=false, so pushBatch() only ever bound ONE of the batch's several
        // real textures (params.mTexture) to tex0/t5, leaving tex1-tex3 unbound - any face
        // whose vary_texture_index picked one of those read the D3D11 null-SRV default
        // (white/grey), while whichever face happened to land on index 0 rendered fine.
        // Matches LLDrawPoolSimple::renderDeferred()'s own unconditional
        // pushBatches(PASS_SIMPLE, true, true) - same shader capability, same fix.
        if (rigged)
        {
            pool.pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY_RIGGED, true, true);
        }
        else
        {
            pool.pushBatches(LLRenderPass::PASS_FULLBRIGHT_SHINY, true, true);
        }
    }

    void endFullbrightShiny()
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_SHINY);

        // Mirrors beginFullbrightShiny()'s legacy/modern branch: unbindReflectionProbes()
        // is intentionally skipped on the legacy path, since begin() called
        // setEnvMat() there instead of bindReflectionProbes().
        DXCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : nullptr;
        bool use_legacy_env_map = DXPipeline::shouldUseLegacyEnvMap(); // S24: see beginFullbrightShiny()'s comment
        if (cube_map && use_legacy_env_map)
        {
            cube_map->disable();
            shader->unbind();
        }
        else if (cube_map && shader->mFeatures.hasReflectionProbes)
        {
            DXPipeline::unbindReflectionProbes(gPipeline, *shader);
        }

        diffuse_channel = -1;
        cube_channel = 0;
    }

    void beginBump(LLDrawPoolBump& pool, bool rigged)
    {
        (void)pool; // unused - kept for signature symmetry with the other begin*/render* helpers
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);

        shader = &gObjectBumpProgram;

        if (rigged)
        {
            llassert(shader->mRiggedVariant);
            shader = shader->mRiggedVariant;
        }

        shader->bind();

        gDX.setSceneBlendType(LLRender::BT_MULT_X2);
    }

    void renderBump(LLDrawPoolBump& pool)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
        DXDepthTest gls_depth(true, false, GL_LEQUAL);
        LLGLEnable blend(GL_BLEND);
        gDX.diffuseColor4f(1, 1, 1, 1);

        // Biases depth (-1,-1), matching GL's glPolygonOffset(-1,-1): this
        // emboss-bump pass draws a second, MULTIPLY-blended layer at the same
        // depth as the base surface it decorates, and without the bias the two
        // passes z-fight on curved/grazing-angle geometry (thin foliage
        // especially). Reads current cull/scissor/depth-clamp so it doesn't
        // clobber other active state, matching other applyDXState() rasterizer cases.
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11RasterizerState* biased_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), -1.0f, -1.0f);
        ctx->RSSetState(biased_rs);

        pool.pushBumpBatches(LLRenderPass::PASS_POST_BUMP);

        // Restore the non-biased state so nothing after this pass inherits
        // the bias unexpectedly.
        ID3D11RasterizerState* normal_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), 0.f, 0.f);
        ctx->RSSetState(normal_rs);
    }

    // Rigged counterpart of renderBump() above. LLDrawPoolBump::mRigged (which
    // pushBumpBatches() checks internally) is private, so this DX pool can't
    // just call pushBumpBatches(PASS_POST_BUMP) a second time for rigged
    // behavior - the loop is reimplemented here using only public members
    // (bindBumpMap()/uploadMatrixPalette()/pushBumpBatch()).
    void renderBumpRigged(LLDrawPoolBump& pool)
    {
        LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);
        DXDepthTest gls_depth(true, false, GL_LEQUAL);
        LLGLEnable blend(GL_BLEND);
        gDX.diffuseColor4f(1, 1, 1, 1);

        // Same depth-bias fix as renderBump() above - rigged foliage/attachments
        // need it just as much as static mesh does.
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11RasterizerState* biased_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), -1.0f, -1.0f);
        ctx->RSSetState(biased_rs);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;

        LLCullResult::drawinfo_iterator begin = DXPipeline::beginRenderMap(gPipeline, LLRenderPass::PASS_POST_BUMP_RIGGED);
        LLCullResult::drawinfo_iterator end = DXPipeline::endRenderMap(gPipeline, LLRenderPass::PASS_POST_BUMP_RIGGED);

        for (LLCullResult::drawinfo_iterator i = begin; i != end; ++i)
        {
            LLDrawInfo& params = **i;

            if (LLDrawPoolBump::bindBumpMap(params))
            {
                if (LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                    pool.pushBumpBatch(params, false);
                }
            }
        }

        ID3D11RasterizerState* normal_rs = DXStateCache::getRasterizerState(
            DXState::isEnabled(GL_CULL_FACE), DXState::isEnabled(GL_SCISSOR_TEST), DXState::isEnabled(GL_DEPTH_CLAMP), 0.f, 0.f);
        ctx->RSSetState(normal_rs);
    }

    void endBump()
    {
        LLHLSLShader::unbind();
        gDX.setSceneBlendType(LLRender::BT_ALPHA);
    }
}

// static
void DXDrawPoolBump::renderDeferred(LLDrawPoolBump& pool, S32 pass)
{
    (void)pool; // unused - this body needs no per-instance state
    (void)pass; // unused - only ever called with a single (non-rigged) pass here
    LL_RECORD_BLOCK_TIME(FTM_RENDER_BUMP);

    // Loops static (pass_i==0) then rigged (pass_i==1), matching
    // lldrawpoolbump.cpp's renderDeferred().
    for (int pass_i = 0; pass_i < 2; ++pass_i)
    {
        bool rigged = (pass_i == 1);
        gDeferredBumpProgram.bind(rigged);

        // bump_channel is resolved via LLHLSLShader::enableTexture()'s shader-reflection
        // lookup (bumpF.hlsl declares bumpMap at register t1); LLDrawPoolBump::bindBumpMap()
        // (lldrawpoolbump.cpp) is backend-agnostic and does the actual per-drawinfo texture
        // selection from bump_code. diffuse_channel stays hardcoded to unit 0 below -
        // bumpF.hlsl samples diffuse at t0.
        S32 bump_channel = LLHLSLShader::sCurBoundShaderPtr->enableTexture(LLViewerShaderMgr::BUMP_MAP);
        if (bump_channel > -1)
        {
            gDX.getTexUnit(bump_channel)->unbind(LLTexUnit::TT_TEXTURE);
        }

        const U32 type = rigged ? LLRenderPass::PASS_BUMP_RIGGED : LLRenderPass::PASS_BUMP;
        LLCullResult::drawinfo_iterator begin = DXPipeline::beginRenderMap(gPipeline, type);
        LLCullResult::drawinfo_iterator end = DXPipeline::endRenderMap(gPipeline, type);

        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;
        // setMinimumAlpha() (llhlslshader.cpp) does a gDX.flush() plus a uniform
        // upload, so it's gated behind a last-value comparison rather than called
        // per draw item. -1.f is outside mAlphaMaskCutoff's valid [0,1] range so
        // the first item always uploads once.
        F32 lastAlphaMaskCutoff = -1.f;

        for (LLCullResult::drawinfo_iterator i = begin; i != end; )
        {
            LLDrawInfo& params = **i;
            LLCullResult::increment_iterator(i, end);

            if (lastAlphaMaskCutoff != params.mAlphaMaskCutoff)
            {
                lastAlphaMaskCutoff = params.mAlphaMaskCutoff;
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(lastAlphaMaskCutoff);
            }
            if (bump_channel > -1)
            {
                LLDrawPoolBump::bindBumpMap(params, bump_channel);
            }

            if (rigged)
            {
                if (!LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                    continue;
                }
            }

            LLRenderPass::applyModelMatrix(params);

            if (params.mTexture.notNull())
            {
                gDX.getTexUnit(0)->bindFast(params.mTexture);
            }
            else
            {
                gDX.getTexUnit(0)->unbindFast(LLTexUnit::TT_TEXTURE);
            }

            params.mVertexBuffer->setBuffer();
            params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
        }

        if (bump_channel > -1)
        {
            LLHLSLShader::sCurBoundShaderPtr->disableTexture(LLViewerShaderMgr::BUMP_MAP);
        }
        LLHLSLShader::sCurBoundShaderPtr->unbind();
        gDX.getTexUnit(0)->activate();
    }
}

// static
void DXDrawPoolBump::renderPostDeferred(LLDrawPoolBump& pool, S32 pass)
{
    (void)pass; // unused - only ever called with a single (non-rigged) pass here

    // Runs 2 passes (static + rigged) unless rendering HUDs, matching
    // lldrawpoolbump.cpp's renderPostDeferred().
    S32 num_passes = DXPipeline::sRenderingHUDs ? 1 : 2;

    for (S32 i = 0; i < num_passes; ++i)
    {
        bool rigged = (i == 1);

        beginFullbrightShiny(pool, rigged);
        renderFullbrightShiny(pool, rigged);
        endFullbrightShiny();

        beginBump(pool, rigged);
        if (rigged)
        {
            renderBumpRigged(pool);
        }
        else
        {
            renderBump(pool);
        }
        endBump();
    }
}
