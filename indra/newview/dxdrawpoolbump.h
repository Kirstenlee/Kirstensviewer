/**
 * @file dxdrawpoolbump.h
 * @brief LLDrawPoolBump/LLStandardBumpmap/LLBumpImageList class definitions
 * plus LLDrawPoolBump's DX11-native deferred bump and fullbright-shiny/
 * emboss-bump render paths - lldrawpoolbump.h/.cpp retired entirely,
 * 2026-10-13.
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

#pragma once

#include "dxdrawpool.h"
#include "llstring.h"
#include "lltextureentry.h"
#include "lluuid.h"

#include <unordered_map>

class LLImageRaw;
class LLHLSLShader;

class LLDrawPoolBump : public LLRenderPass
{
protected :
    LLDrawPoolBump(const U32 type):LLRenderPass(type) { mShiny = false; }
public:
    static U32 sVertexMask;
    bool mShiny;

    virtual U32 getVertexDataMask() override { return sVertexMask; }

    LLDrawPoolBump();

    /*virtual*/ void prerender() override;

    void pushBumpBatches(U32 type);
    void renderGroup(LLSpatialGroup* group, U32 type, bool texture) override;

    virtual S32 getNumDeferredPasses() override;
    /*virtual*/ void renderDeferred(S32 pass) override;

    virtual S32 getNumPostDeferredPasses() override { return 1; }
    /*virtual*/ void renderPostDeferred(S32 pass) override;

    static bool bindBumpMap(LLDrawInfo& params, S32 channel = -2);
    static bool bindBumpMap(LLFace* face, S32 channel = -2);

private:
    static bool bindBumpMap(U8 bump_code, LLViewerTexture* tex, S32 channel);
    bool mRigged = false; // if true, doing a rigged pass

};

enum EBumpEffect
{
    BE_NO_BUMP = 0,
    BE_BRIGHTNESS = 1,
    BE_DARKNESS = 2,
    BE_STANDARD_0 = 3,  // Standard must always be the last one
    BE_COUNT = 4
};

////////////////////////////////////////////////////////////////
// List of standard bumpmaps that are specificed by LLTextureEntry::mBump's lower bits

class LLStandardBumpmap
{
public:
    LLStandardBumpmap() : mLabel() {}
    LLStandardBumpmap( const std::string& label ) : mLabel(label) {}

    std::string mLabel;
    LLPointer<LLViewerFetchedTexture> mImage;

    static  U32 sStandardBumpmapCount;  // Number of valid values in gStandardBumpmapList[]

    static void clear();
    static void addstandard();

    static void shutdown();
    static void restoreGL();
    static void destroyGL();
};

extern LLStandardBumpmap gStandardBumpmapList[TEM_BUMPMAP_COUNT];

////////////////////////////////////////////////////////////////
// List of one-component bump-maps created from other texures.

struct LLBumpImageEntry;

class LLBumpImageList
{
public:
    LLBumpImageList() {}
    ~LLBumpImageList();

    void        init();
    void        shutdown();
    void            clear();
    void        destroyGL();
    void        restoreGL();
    void        updateImages();


    LLViewerTexture*    getBrightnessDarknessImage(LLViewerFetchedTexture* src_image, U8 bump_code);
    void        addTextureStats(U8 bump, const LLUUID& base_image_id, F32 virtual_size);

    static void onSourceStandardLoaded( bool success, LLViewerFetchedTexture *src_vi, LLImageRaw* src, LLImageRaw* aux_src, S32 discard_level, bool final, void* userdata );
    static void generateNormalMapFromAlpha(LLImageRaw* src, LLImageRaw* nrm_image);


private:
    // should be called whenever resolution of src_vi changes compared to the current entry
    static void onSourceUpdated( LLViewerTexture *src_vi, EBumpEffect bump );

private:
    typedef std::unordered_map<LLUUID, LLPointer<LLViewerTexture> > bump_image_map_t;
    bump_image_map_t mBrightnessEntries;
    bump_image_map_t mDarknessEntries;
    static LLRenderTarget sRenderTarget;
};

extern LLBumpImageList gBumpImageList;

// Staged full duplicate, same pattern as dxdrawpoolalpha.{h,cpp} - chosen
// deliberately over a thin redirect: API separation between pools pays off
// once GL is fully retired, even where the GL body needs little change.
//
// Real differences from the GL source:
// - Only the primary diffuse texture is bound (same hardcoded unit every
//   converted pool uses); the bump/normal-map texture is skipped entirely
//   since no per-material texture-channel registration exists yet under
//   DX_RENDER (deferred bump/normal geometry renders flat until that lands).
// - The emboss-bump pass applies real D3D11 rasterizer-state DepthBias/
//   SlopeScaledDepthBias (DXStateCache::getRasterizerState()) around its
//   draw calls, since GL's raw glPolygonOffset() has no DX_RENDER
//   equivalent - without it the bump pass z-fights against the base
//   surface. LLDrawPoolGlow has its own separate, still-unconverted
//   polygon-offset call site, unaffected by this.
// - renderDeferred()/renderPostDeferred() run both a static and a rigged
//   pass. Since LLDrawPoolBump::mRigged is private, renderBumpRigged()
//   (the .cpp) reimplements pushBumpBatches()'s rigged branch directly via
//   public members instead of calling the real member function.
//
// LLDrawPoolBump::bindBumpMap()/pushBumpBatches()/LLRenderPass::
// pushBumpBatch() are NOT duplicated here - reached directly (the first via
// the always-safe channel==-2 sentinel path) since they're real shared
// code, not the dead GL-only body redirected around. The old GL-only
// numBumpPasses()/bindCubeMap()/unbindCubeMap()/begin-render-endBump()/
// begin-render-endFullbrightShiny() had zero callers once that redirect
// went unconditional and were deleted outright, not kept dead - see task
// #294's consolidation pass.
class DXDrawPoolBump
{
public:
    static void renderDeferred(LLDrawPoolBump& pool, S32 pass);
    static void renderPostDeferred(LLDrawPoolBump& pool, S32 pass);
};
