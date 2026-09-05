#pragma once

#include "llgl.h"
#include "llimagegl.h"
#include "DXCubeTexture.h"

#include <vector>

// S24 (2026-08-31, task #300-adjacent "stop this insanity" rewrite): real
// DX_RENDER-native replacement for LLCubeMap ("Environment map hack!" - LL's
// own header comment, copyright 2002/2010). No GL body, no #ifdef DX_RENDER -
// this project is DX_RENDER-only now. Composes the existing, already-correct
// DXCubeTexture resource wrapper (unchanged, task #113) rather than
// reinventing the raw D3D11 resource layer - only the ORCHESTRATION logic
// (per-face upload, face ordering, bind/texture-matrix state) that used to
// live inside the GL-era class is what's actually new here.
//
// S24 (2026-08-31, layering fix): lives in llrender/, not dxrender/resources/
// - this class needs LLImageGL/LLGLenum/LLTexUnit friendship, all llrender
// types, and dxrender is a lower-level module that must never depend back on
// llrender (llrender already depends on dxrender via DXRENDER_INCLUDE_DIRS/
// DXRENDER_LIBRARIES - see llrender/CMakeLists.txt). Composes the still-
// dxrender-resident DXCubeTexture unchanged via its already-exported include
// path, matching where the original LLCubeMap lived.
//
// Scope: the legacy sky/"shiny" environment-map cubemap (LLVOSky::mCubeMap),
// consumed by lldrawpoolbump.cpp/dxdrawpoolbump.cpp/pipeline.cpp for the
// bump/fullbright-shiny materials fallback path. See
// llreflectionmapmanager.h's DXCubeMapArray for the separate, much larger
// multi-probe reflection system.
class DXCubeMap : public LLRefCount
{
public:
    DXCubeMap();

    // Uploads 6 raw face images (order: -X,+X,-Y,+Y,-Z,+Z, matching this
    // class's historical LLCubeMap-era face-slot convention - see
    // initRawData()'s FLIP_X/FLIP_Y/TRANSPOSE tables, which are keyed to
    // that exact order and must not be reordered independently of it) into
    // both the per-face 2D textures and the assembled D3D11 cube resource.
    void init(const std::vector<LLPointer<LLImageRaw>>& rawimages);

    // Allocates an undefined cubemap at the given resolution for
    // render-to-cubemap use (no LLImageRaw upload).
    void initReflectionMap(U32 resolution, U32 components = 3);

    // Full init from a pre-supplied 6-face environment map (takes ownership
    // of rawimages, unlike init() which copies into its own storage).
    void initEnvironmentMap(const std::vector<LLPointer<LLImageRaw>>& rawimages);

    void generateMipMaps();

    void bind();
    void enable(S32 stage);
    void enableTexture(S32 stage);
    S32  getStage(void) { return mTextureStage; }

    void disable(void);
    void disableTexture(void);
    void setMatrix(S32 stage);
    void restoreMatrix();

    U32 getResolution() { return mImages[0].notNull() ? mImages[0]->getWidth(0) : 0; }

    void destroy();

    // Real cubemap SRV, assembled from the 6 individually-uploaded mImages[]
    // faces. Used by LLTexUnit::bind(DXCubeMap*). Returns nullptr if this
    // cubemap hasn't been assembled yet (e.g. init() never called, or a face
    // failed to upload).
    ID3D11ShaderResourceView* getDXSRV() const { return mDXCubeTexture.getSRV(); }

public:
    static bool sUseCubeMaps;

protected:
    friend class LLTexUnit;
    ~DXCubeMap();

    // S24: retained purely as an internal face-slot label for the per-face
    // LLImageGL upload path (LLImageGL::setTarget() still requires a GL
    // enum tag - that's an LLImageGL API detail, out of scope to change
    // here) - NOT a D3D11 face/slice convention. The GL->D3D11 slice remap
    // happens once, explicitly, in init()'s call to mDXCubeTexture.copyFace().
    LLGLenum mTargets[6];
    LLPointer<LLImageGL> mImages[6];
    LLPointer<LLImageRaw> mRawImages[6];
    S32 mTextureStage;
    S32 mMatrixStage;
    DXCubeTexture mDXCubeTexture;

private:
    void initFaceTextures();
    void initRawData(const std::vector<LLPointer<LLImageRaw>>& rawimages);
    void initFaceData();
};
