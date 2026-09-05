/**
 * @file DXCubeMap.cpp
 * @brief DXCubeMap class implementation
 *
 * S24 (2026-08-31): real DX_RENDER-native replacement for LLCubeMap - see
 * DXCubeMap.h's header comment for the full rationale. initRawData()'s
 * per-face pixel-orientation logic is carried over UNCHANGED from the
 * original LLCubeMap::initRawData() (pure CPU image processing, never GL
 * API calls - nothing to "port"), and init()'s GL-face-index -> D3D11-slice
 * remap is carried over unchanged too, so this is a mechanical swap with no
 * intended behavior change for the legacy sky/"shiny" cubemap.
 */
#include "linden_common.h"

#include "llworkerthread.h"

#include "DXCubeMap.h"

#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "m3math.h"
#include "m4math.h"

#include "llrender.h"
#include "llhlslshader.h"

#include "llglheaders.h"

namespace {
    const U16 RESOLUTION = 64;
}

bool DXCubeMap::sUseCubeMaps = true;

DXCubeMap::DXCubeMap()
    : mTextureStage(0),
    mMatrixStage(0)
{
    mTargets[0] = GL_TEXTURE_CUBE_MAP_NEGATIVE_X;
    mTargets[1] = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
    mTargets[2] = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y;
    mTargets[3] = GL_TEXTURE_CUBE_MAP_POSITIVE_Y;
    mTargets[4] = GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    mTargets[5] = GL_TEXTURE_CUBE_MAP_POSITIVE_Z;
}

DXCubeMap::~DXCubeMap()
{}

void DXCubeMap::initFaceTextures()
{
    if (!DXCubeMap::sUseCubeMaps)
    {
        return; // silently ignore if cube maps are disabled
    }

    // Already initialised?
    if (!mImages[0].isNull())
    {
        disable();
        return;
    }

    U32 texname = 0;
    LLImageGL::generateTextures(1, &texname);

    for (int face = 0; face < 6; ++face)
    {
        mImages[face] = new LLImageGL(RESOLUTION, RESOLUTION, 4, false);
        mImages[face]->setTarget(mTargets[face], LLTexUnit::TT_CUBE_MAP);
        mRawImages[face] = new LLImageRaw(RESOLUTION, RESOLUTION, 4);

        if (!mImages[face]->createGLTexture(0, mRawImages[face], texname))
        {
            continue;
        }

        gDX.getTexUnit(0)->bindManual(LLTexUnit::TT_CUBE_MAP, texname);
        mImages[face]->setAddressMode(LLTexUnit::TAM_CLAMP);
    }

    gDX.getTexUnit(0)->disable();
    disable();
}

// S24: per-face pixel-orientation correction, carried over unchanged from
// LLCubeMap - pure CPU pixel repack (flip/transpose), no GL/D3D dependency
// at all. Keyed to the -X,+X,-Y,+Y,-Z,+Z face-slot order (mTargets above).
void DXCubeMap::initRawData(const std::vector<LLPointer<LLImageRaw>>& rawimages)
{
    static constexpr bool FLIP_X[6] = { false, true,  false, false, true,  false };
    static constexpr bool FLIP_Y[6] = { true,  true,  true,  false, true,  true };
    static constexpr bool TRANSPOSE[6] = { false, false, false, false, true,  true };

    for (int face = 0; face < 6; face++)
    {
        LLImageDataSharedLock lockIn(rawimages[face]);
        LLImageDataLock lockOut(mRawImages[face]);

        const U8* src = rawimages[face]->getData();
        U8* dst = mRawImages[face]->getData();

        const S32 width = rawimages[face]->getWidth();
        const S32 height = rawimages[face]->getHeight();
        const S32 bpp = rawimages[face]->getComponents(); // should be 4

        llassert(bpp == 4);

        for (S32 y = 0; y < height; ++y)
        {
            for (S32 x = 0; x < width; ++x)
            {
                S32 sx = x;
                S32 sy = y;

                if (FLIP_Y[face]) sy = height - 1 - sy;
                if (FLIP_X[face]) sx = width - 1 - sx;

                if (TRANSPOSE[face])
                    std::swap(sx, sy);

                const S32 src_index = (sy * width + sx) * bpp;
                const S32 dst_index = (y * width + x) * bpp;

                memcpy(dst + dst_index, src + src_index, 4);
            }
        }
    }
}

void DXCubeMap::initFaceData()
{
    constexpr int FACE_COUNT = 6;

    for (int face = 0; face < FACE_COUNT; ++face)
    {
        mImages[face]->setSubImage(
            mRawImages[face],
            0, 0,
            RESOLUTION,
            RESOLUTION
        );
    }
}

void DXCubeMap::init(const std::vector<LLPointer<LLImageRaw> >& rawimages)
{
    if (gGLManager.mIsDisabled)
    {
        return;
    }

    initFaceTextures();
    initRawData(rawimages);
    initFaceData();

    // S24: assembles the 6 already-uploaded 2D per-face textures into one
    // real D3D11 cubemap resource that TextureCube.Sample() can actually
    // sample. Silently leaves mDXCubeTexture invalid (not sampled -
    // LLTexUnit::bind(DXCubeMap*) falls back to a neutral white texture) if
    // any face failed to upload above.
    if (mDXCubeTexture.create(RESOLUTION, RESOLUTION, true))
    {
        // S24: mTargets[6] (this class's constructor) is the historical GL
        // face order: -X,+X,-Y,+Y,-Z,+Z. D3D11 cubemap array-slice order is
        // fixed by the API itself: slice 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z,
        // 5=-Z. This remaps that legacy face-slot index -> the correct
        // D3D11 slice index (each axis pair swapped).
        static const int kFaceSlotToDXSlice[6] = { 1, 0, 3, 2, 5, 4 };

        bool all_faces_ok = true;
        for (int face = 0; face < 6; ++face)
        {
            ID3D11Texture2D* face_tex = mImages[face].notNull() ? mImages[face]->getDXTexturePtr() : nullptr;
            if (!mDXCubeTexture.copyFace(kFaceSlotToDXSlice[face], face_tex))
            {
                all_faces_ok = false;
            }
        }
        if (all_faces_ok)
        {
            mDXCubeTexture.generateMipMaps();
        }
    }
}

void DXCubeMap::initReflectionMap(U32 resolution, U32 components)
{
    U32 texname = 0;

    LLImageGL::generateTextures(1, &texname);

    mImages[0] = new LLImageGL(resolution, resolution, components, true);
    mImages[0]->setTexName(texname);
    mImages[0]->setTarget(mTargets[0], LLTexUnit::TT_CUBE_MAP);
    gDX.getTexUnit(0)->bindManual(LLTexUnit::TT_CUBE_MAP, texname);
    mImages[0]->setAddressMode(LLTexUnit::TAM_CLAMP);
}

void DXCubeMap::initEnvironmentMap(const std::vector<LLPointer<LLImageRaw>>& rawimages)
{
    if (rawimages.size() != 6)
        return;

    const U32 resolution = rawimages[0]->getWidth();
    const U32 components = rawimages[0]->getComponents();

    for (int face = 1; face < 6; ++face)
    {
        if (rawimages[face]->getWidth() != resolution ||
            rawimages[face]->getHeight() != resolution ||
            rawimages[face]->getComponents() != components)
        {
            return; // silently abort on mismatch
        }
    }

    U32 texname = 0;
    LLImageGL::generateTextures(1, &texname);

    for (int face = 0; face < 6; ++face)
    {
        mImages[face] = new LLImageGL(resolution, resolution, components, true);
        mImages[face]->setTarget(mTargets[face], LLTexUnit::TT_CUBE_MAP);
        mRawImages[face] = rawimages[face];
        mImages[face]->createGLTexture(0, mRawImages[face], texname);
        gDX.getTexUnit(0)->bindManual(LLTexUnit::TT_CUBE_MAP, texname);
        mImages[face]->setAddressMode(LLTexUnit::TAM_CLAMP);
        mImages[face]->setSubImage(mRawImages[face], 0, 0, resolution, resolution);
    }

    enableTexture(0);
    bind();

    mImages[0]->setFilteringOption(LLTexUnit::TFO_ANISOTROPIC);

    gDX.getTexUnit(0)->disable();
    disable();
}

void DXCubeMap::generateMipMaps()
{
    mImages[0]->setUseMipMaps(true);
    mImages[0]->setHasMipMaps(true);
    enableTexture(0);
    bind();
    mImages[0]->setFilteringOption(LLTexUnit::TFO_BILINEAR);
    gDX.getTexUnit(0)->disable();
    disable();
}

void DXCubeMap::bind()
{
    gDX.getTexUnit(mTextureStage)->bind(this);
}

void DXCubeMap::enable(S32 stage)
{
    enableTexture(stage);
}

void DXCubeMap::enableTexture(S32 stage)
{
    mTextureStage = stage;
    if (stage >= 0 && DXCubeMap::sUseCubeMaps)
    {
        gDX.getTexUnit(stage)->enable(LLTexUnit::TT_CUBE_MAP);
    }
}

void DXCubeMap::disable(void)
{
    disableTexture();
}

void DXCubeMap::disableTexture(void)
{
    if (mTextureStage >= 0 && DXCubeMap::sUseCubeMaps)
    {
        gDX.getTexUnit(mTextureStage)->disable();
        if (mTextureStage == 0)
        {
            gDX.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);
        }
    }
}

void DXCubeMap::setMatrix(S32 stage)
{
    mMatrixStage = stage;
    if (stage < 0)
        return;

    gDX.getTexUnit(stage)->activate();

    const F32* mv = gGLModelView;

    LLVector3 x(mv[0], mv[1], mv[2]);
    LLVector3 y(mv[4], mv[5], mv[6]);
    LLVector3 z(mv[8], mv[9], mv[10]);

    LLMatrix3 rot;
    rot.setRows(x, y, z);

    LLMatrix4 texmat(rot);
    texmat.transpose();

    gDX.matrixMode(LLRender::MM_TEXTURE);
    gDX.pushMatrix();
    gDX.loadMatrix((F32*)texmat.mMatrix);

    gDX.matrixMode(LLRender::MM_MODELVIEW);
}

void DXCubeMap::restoreMatrix()
{
    if (mMatrixStage < 0)
        return;

    gDX.getTexUnit(mMatrixStage)->activate();

    gDX.matrixMode(LLRender::MM_TEXTURE);
    gDX.popMatrix();

    gDX.matrixMode(LLRender::MM_MODELVIEW);
}

void DXCubeMap::destroy()
{
    for (int i = 0; i < 6; ++i)
    {
        if (mImages[i])
        {
            mImages[i]->destroyGLTexture();
            mImages[i] = nullptr;
        }

        mRawImages[i] = nullptr;
    }

    mDXCubeTexture.destroy();

    mMatrixStage = -1;
}
