/**
 * @file llimagedx.cpp
 * @brief Generic GL image handler
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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


// TODO: create 2 classes for images w/ and w/o discard levels?

#include "linden_common.h"

#include "llimagedx.h"

#include "llerror.h"
#include "llfasttimer.h"
#include "llimage.h"
#include <unordered_map>

#include "llmath.h"
#include "llgl.h"
#include "llhlslshader.h"
#include "llrender.h"
#include "llwindow.h"
#include "llframetimer.h"
#include <format>  // S24: for std::format (C++20)
#include <unordered_set>

#include "DXReadback.h"

extern LL_COMMON_API bool on_main_thread();

#if !LL_IMAGEDX_THREAD_CHECK
#define checkActiveThread()
#endif

//----------------------------------------------------------------------------
const F32 MIN_TEXTURE_LIFETIME = 10.f;

//which power of 2 is i?
//assumes i is a power of 2 > 0
U32 wpo2(U32 i);


U32 LLImageDX::sFrameCount = 0;


// texture memory accounting (for macOS)
static LLMutex sTexMemMutex;
static std::unordered_map<U32, U64> sTextureAllocs;
static std::atomic<U64> sTextureBytes{0};  // Made atomic for thread-safety

// track a texture alloc on the currently bound texture.
// asserts that no currently tracked alloc exists
void LLImageDXMemory::alloc_tex_image(U32 width, U32 height, U32 intformat, U32 count)
{
    U32 texUnit = gDX.getCurrentTexUnitIndex();
    llassert(texUnit == 0); // allocations should always be done on tex unit 0
    U32 texName = gDX.getTexUnit(texUnit)->getCurrTexture();
    U64 size = LLImageDX::dataFormatBytes(intformat, width, height);
    size *= count;

    llassert(size >= 0);

    sTexMemMutex.lock();

    // it is a precondition that no existing allocation exists for this texture
    llassert(sTextureAllocs.find(texName) == sTextureAllocs.end());

    sTextureAllocs[texName] = size;
    sTextureBytes.fetch_add(size, std::memory_order_relaxed);

    sTexMemMutex.unlock();
}

// track texture free on given texName
void LLImageDXMemory::free_tex_image(U32 texName)
{
    sTexMemMutex.lock();
    auto iter = sTextureAllocs.find(texName);
    if (iter != sTextureAllocs.end()) // sometimes a texName will be "freed" before allocated (e.g. first call to setManualImage for a given texName)
    {
        llassert(iter->second <= sTextureBytes); // sTextureBytes MUST NOT go below zero

        sTextureBytes.fetch_sub(iter->second, std::memory_order_relaxed);

        sTextureAllocs.erase(iter);
    }

    sTexMemMutex.unlock();
}

// track texture free on given texNames
void LLImageDXMemory::free_tex_images(U32 count, const U32* texNames)
{
    for (U32 i = 0; i < count; ++i)
    {
        free_tex_image(texNames[i]);
    }
}

// track texture free on currently bound texture
void LLImageDXMemory::free_cur_tex_image()
{
    U32 texUnit = gDX.getCurrentTexUnitIndex();
    llassert(texUnit == 0); // frees should always be done on tex unit 0
    U32 texName = gDX.getTexUnit(texUnit)->getCurrTexture();
    free_tex_image(texName);
}

// S24: DX_RENDER-specific texture memory accounting, keyed by the owning
// LLImageDX instance (`this`) rather than texName - under DX_RENDER every
// LLImageDX's mTexName is a shared fake sentinel, not a real per-texture
// identifier, so the texName-keyed map above would collide across textures.
//
// This only feeds updateClass()'s self-estimate FALLBACK path (used when the
// live DXGI VRAM query is unavailable - see LLViewerTexture::updateClass()'s
// has_live_vram_info check) - the primary, always-preferred path doesn't
// read this at all. But before this fix it was a hard-zero blind spot for
// every DX_RENDER texture: if the live DXGI query ever failed to initialize,
// the pressure system would see near-zero texture memory used and likely
// never ramp eviction bias even under real, severe VRAM exhaustion.
static std::unordered_map<const void*, U64> sDXTextureAllocs;

void LLImageDXMemory::allocDXTextureBytes(const void* key, U64 size)
{
    sTexMemMutex.lock();
    auto iter = sDXTextureAllocs.find(key);
    if (iter != sDXTextureAllocs.end())
    {
        // Unlike alloc_tex_image() above (which asserts no existing
        // allocation - GL always frees/regenerates a name first), a
        // DX_RENDER texture's underlying resource can legitimately be
        // re-created on the SAME LLImageDX instance without an intervening
        // destroyGLTexture()/freeDXTextureBytes() call - normal discard-
        // level streaming re-uploads routinely do exactly this. Adjust the
        // tracked size in place rather than asserting.
        sTextureBytes.fetch_sub(iter->second, std::memory_order_relaxed);
        iter->second = size;
    }
    else
    {
        sDXTextureAllocs[key] = size;
    }
    sTextureBytes.fetch_add(size, std::memory_order_relaxed);
    sTexMemMutex.unlock();
}

void LLImageDXMemory::freeDXTextureBytes(const void* key)
{
    sTexMemMutex.lock();
    auto iter = sDXTextureAllocs.find(key);
    if (iter != sDXTextureAllocs.end())
    {
        llassert(iter->second <= sTextureBytes); // sTextureBytes MUST NOT go below zero
        sTextureBytes.fetch_sub(iter->second, std::memory_order_relaxed);
        sDXTextureAllocs.erase(iter);
    }
    sTexMemMutex.unlock();
}

using namespace LLImageDXMemory;

// static
U64 LLImageDX::getTextureBytesAllocated()
{
    return sTextureBytes;
}

//statics

U32 LLImageDX::sUniqueCount             = 0;
U32 LLImageDX::sBindCount               = 0;
S32 LLImageDX::sCount                   = 0;

F32 LLImageDX::sLastFrameTime           = 0.f;
LLImageDX* LLImageDX::sDefaultGLTexture = NULL ;
std::unordered_set<LLImageDX*> LLImageDX::sImageList;


bool LLImageDXThread::sEnabledTextures = false;
bool LLImageDXThread::sEnabledMedia = false;

//****************************************************************************************************
//The below for texture auditing use only
//****************************************************************************************************
//-----------------------
//debug use
S32 LLImageDX::sCurTexSizeBar = -1 ;
S32 LLImageDX::sCurTexPickSize = -1 ;
S32 LLImageDX::sMaxCategories = 1 ;

//optimization for when we don't need to calculate mIsMask
bool LLImageDX::sSkipAnalyzeAlpha;


//------------------------
//****************************************************************************************************
//End for texture auditing use only
//****************************************************************************************************

//----------------------------------------------------------------------------
bool is_little_endian()
{
    S32 a = 0x12345678;
    U8 *c = (U8*)(&a);

    return (*c == 0x78) ;
}

//static
void LLImageDX::initClass(LLWindow* window, S32 num_catagories, bool skip_analyze_alpha /* = false */, bool thread_texture_loads /* = false */, bool thread_media_updates /* = false */)
{
    sSkipAnalyzeAlpha = skip_analyze_alpha;

    // S24: background-thread D3D11 texture/media creation was removed - a
    // driver-level NVIDIA bug (610.88) that no application-side mitigation
    // could route around. sEnabledTextures/sEnabledMedia stay permanently
    // false; texture/media creation is always synchronous under DX_RENDER.
    (void)window;
    (void)thread_texture_loads;
    (void)thread_media_updates;
}

//static
void LLImageDX::cleanupClass()
{
    LLImageDXThread::deleteSingleton();
}


//static
S32 LLImageDX::dataFormatBits(S32 dataformat)
{
    switch (dataformat)
    {
    case GL_COMPRESSED_RED:                         return 8;
    case GL_COMPRESSED_RG:                          return 16;
    case GL_COMPRESSED_RGB:                         return 24;
    case GL_COMPRESSED_SRGB:                        return 32;
    case GL_COMPRESSED_RGBA:                        return 32;
    case GL_COMPRESSED_SRGB_ALPHA:                  return 32;
    case GL_COMPRESSED_LUMINANCE:                   return 8;
    case GL_COMPRESSED_LUMINANCE_ALPHA:             return 16;
    case GL_COMPRESSED_ALPHA:                       return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:          return 4;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:    return 4;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:          return 8;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:    return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:          return 8;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:    return 8;
    case GL_COMPRESSED_RGBA_BPTC_UNORM:             return 8;  // BC7
    case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:       return 8;  // BC7 sRGB
    case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:       return 8;  // BC6H
    case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:     return 8;  // BC6H
    case GL_LUMINANCE:                              return 8;
    case GL_LUMINANCE8:                             return 8;
    case GL_ALPHA:                                  return 8;
    case GL_ALPHA8:                                 return 8;
    case GL_RED:                                    return 8;
    case GL_R8:                                     return 8;
    case GL_COLOR_INDEX:                            return 8;
    case GL_LUMINANCE_ALPHA:                        return 16;
    case GL_LUMINANCE8_ALPHA8:                      return 16;
    case GL_RG:                                     return 16;
    case GL_RG8:                                    return 16;
    case GL_RGB:                                    return 24;
    case GL_SRGB:                                   return 24;
    case GL_RGB8:                                   return 24;
    case GL_R11F_G11F_B10F:                         return 32;
    case GL_RGBA:                                   return 32;
    case GL_RGBA8:                                  return 32;
    case GL_RGB10_A2:                               return 32;
    case GL_SRGB_ALPHA:                             return 32;
    case GL_BGRA:                                   return 32;      // Used for QuickTime media textures on the Mac
    case GL_DEPTH_COMPONENT:                        return 24;
    case GL_DEPTH_COMPONENT24:                      return 24;
    case GL_RGBA16:                                 return 64;
    case GL_R16F:                                   return 16;
    case GL_RG16F:                                  return 32;
    case GL_RGB16F:                                 return 48;
    case GL_RGBA16F:                                return 64;
    case GL_R32F:                                   return 32;
    case GL_RG32F:                                  return 64;
    case GL_RGB32F:                                 return 96;
    case GL_RGBA32F:                                return 128;
    default:
        LL_ERRS() << "LLImageDX::Unknown format: " << std::hex << dataformat << std::dec << LL_ENDL;
        return 0;
    }
}

//static
S64 LLImageDX::dataFormatBytes(S32 dataformat, S32 width, S32 height)
{
    switch (dataformat)
    {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
      case GL_COMPRESSED_RGBA_BPTC_UNORM:
      case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
      case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
      case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:
        if (width < 4) width = 4;
        if (height < 4) height = 4;
        break;
    default:
        break;
    }
    S64 bytes (((S64)width * (S64)height * (S64)dataFormatBits(dataformat)+7)>>3);
    S64 aligned = (bytes+3)&~3;
    return aligned;
}

//static
S32 LLImageDX::dataFormatComponents(S32 dataformat)
{
    switch (dataformat)
    {
      case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:    return 3;
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: return 3;
      case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:    return 4;
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: return 4;
      case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:    return 4;
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: return 4;
      case GL_COMPRESSED_RGBA_BPTC_UNORM:    return 4;  // BC7
      case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM: return 4;  // BC7 sRGB
      case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT: return 3;  // BC6H
      case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT: return 3;  // BC6H
      case GL_LUMINANCE:                        return 1;
      case GL_ALPHA:                            return 1;
      case GL_RED:                              return 1;
      case GL_COLOR_INDEX:                      return 1;
      case GL_LUMINANCE_ALPHA:                  return 2;
      case GL_RG:                               return 2;
      case GL_RGB:                              return 3;
      case GL_SRGB:                             return 3;
      case GL_RGBA:                             return 4;
      case GL_SRGB_ALPHA:                       return 4;
      case GL_BGRA:                             return 4;       // Used for QuickTime media textures on the Mac
      default:
        LL_ERRS() << "LLImageDX::Unknown format: " << std::hex << dataformat << std::dec << LL_ENDL;
        return 0;
    }
}

//----------------------------------------------------------------------------

// static
void LLImageDX::updateStats(F32 current_time)
{
    sLastFrameTime = current_time;
}

//----------------------------------------------------------------------------

//static
void LLImageDX::destroyGL()
{
    for (S32 stage = 0; stage < gGLManager.mNumTextureImageUnits; stage++)
    {
        gDX.getTexUnit(stage)->unbind(LLTexUnit::TT_TEXTURE);
    }
}

//for server side use only.
//static
bool LLImageDX::create(LLPointer<LLImageDX>& dest, bool usemipmaps)
{
    dest = new LLImageDX(usemipmaps);
    return true;
}

//for server side use only.
bool LLImageDX::create(LLPointer<LLImageDX>& dest, U32 width, U32 height, U8 components, bool usemipmaps)
{
    dest = new LLImageDX(width, height, components, usemipmaps);
    return true;
}

//for server side use only.
bool LLImageDX::create(LLPointer<LLImageDX>& dest, const LLImageRaw* imageraw, bool usemipmaps)
{
    dest = new LLImageDX(imageraw, usemipmaps);
    return true;
}

//----------------------------------------------------------------------------

LLImageDX::LLImageDX(bool usemipmaps/* = true*/, bool allow_compression/* = true*/)
:   mSaveData(0), mExternalTexture(false)
{
    init(usemipmaps, allow_compression);
    setSize(0, 0, 0);
    sImageList.insert(this);
    sCount++;
}

LLImageDX::LLImageDX(U32 width, U32 height, U8 components, bool usemipmaps/* = true*/, bool allow_compression/* = true*/)
:   mSaveData(0), mExternalTexture(false)
{
    llassert( components <= 4 );
    init(usemipmaps, allow_compression);
    setSize(width, height, components);
    sImageList.insert(this);
    sCount++;
}

LLImageDX::LLImageDX(const LLImageRaw* imageraw, bool usemipmaps/* = true*/, bool allow_compression/* = true*/)
:   mSaveData(0), mExternalTexture(false)
{
    init(usemipmaps, allow_compression);
    setSize(0, 0, 0);
    sImageList.insert(this);
    sCount++;

    createGLTexture(0, imageraw);
}

LLImageDX::LLImageDX(
    LLGLuint texName,
    U32 components,
    LLGLenum target,
    LLGLint  formatInternal,
    LLGLenum formatPrimary,
    LLGLenum formatType,
    LLTexUnit::eTextureAddressMode addressMode)
{
    init(false, true);
    mTexName = texName;
    mTarget = target;
    mComponents = components;
    mAddressMode = addressMode;
    mFormatType = formatType;
    mFormatInternal = formatInternal;
    mFormatPrimary = formatPrimary;
}


LLImageDX::~LLImageDX()
{
    if (!mExternalTexture && gGLManager.mInited)
    {
        LLImageDX::cleanup();
        sImageList.erase(this);
        freePickMask();
        sCount--;
    }
}

void LLImageDX::init(bool usemipmaps, bool allow_compression)
{
#if LL_IMAGEDX_THREAD_CHECK
    mActiveThread = LLThread::currentID();
#endif

    // keep these members in the same order as declared in llimagehl.h
    // so that it is obvious by visual inspection if we forgot to
    // init a field.

    mTextureMemory = S64Bytes(0);
    mLastBindTime = 0.f;

    mPickMask = NULL;
    mPickMaskWidth = 0;
    mPickMaskHeight = 0;
    mUseMipMaps = usemipmaps;
    mHasExplicitFormat = false;

    mIsMask = false;
    mNeedsAlphaAndPickMask = true ;
    mAlphaStride = 0 ;
    mAlphaOffset = 0 ;

    mGLTextureCreated = false ;
    mTexName = 0;
    mWidth = 0;
    mHeight = 0;
    mCurrentDiscardLevel = -1;

    mAllowCompression = allow_compression;

    mTarget = GL_TEXTURE_2D;
    mBindTarget = LLTexUnit::TT_TEXTURE;
    mHasMipMaps = false;

    mIsResident = 0;

    mComponents = 0;
    mMaxDiscardLevel = MAX_DISCARD_LEVEL;

    mAddressMode = LLTexUnit::TAM_WRAP;
    mFilterOption = LLTexUnit::TFO_ANISOTROPIC;

    mFormatInternal = -1;
    mFormatPrimary = (LLGLenum) 0;
    mFormatType = GL_UNSIGNED_BYTE;
    mFormatSwapBytes = false;

#ifdef DEBUG_MISS
    mMissed = false;
#endif

    mCategory = -1;

    // Sometimes we have to post work for the main thread.
    mMainQueue = LL::WorkQueue::getInstance("mainloop");
}

void LLImageDX::cleanup()
{
    if (!gGLManager.mIsDisabled)
    {
        destroyGLTexture();
    }
    freePickMask();

    mSaveData = NULL; // deletes data
}

//----------------------------------------------------------------------------

//this function is used to check the size of a texture image.
//so dim should be a positive number
static bool check_power_of_two(S32 dim)
{
    if(dim < 0)
    {
        return false ;
    }
    if(!dim)//0 is a power-of-two number
    {
        return true ;
    }
    return !(dim & (dim - 1)) ;
}

//static
bool LLImageDX::checkSize(S32 width, S32 height)
{
    return check_power_of_two(width) && check_power_of_two(height);
}

bool LLImageDX::setSize(S32 width, S32 height, S32 ncomponents, S32 discard_level)
{
    if (width != mWidth || height != mHeight || ncomponents != mComponents)
    {
        // Check if dimensions are a power of two!
        if (!checkSize(width, height))
        {
            LL_WARNS() << llformat("Texture has non power of two dimension: %dx%d",width,height) << LL_ENDL;
            return false;
        }

        mWidth = width;
        mHeight = height;
        mComponents = ncomponents;
        if (ncomponents > 0)
        {
            mMaxDiscardLevel = 0;
            while (width > 1 && height > 1 && mMaxDiscardLevel < MAX_DISCARD_LEVEL)
            {
                mMaxDiscardLevel++;
                width >>= 1;
                height >>= 1;
            }

            if(discard_level > 0)
            {
                mMaxDiscardLevel = llmax(mMaxDiscardLevel, (S8)discard_level);
            }
        }
        else
        {
            mMaxDiscardLevel = MAX_DISCARD_LEVEL;
        }
    }

    return true;
}

//----------------------------------------------------------------------------

// virtual
void LLImageDX::dump()
{
    LL_INFOS() << "mMaxDiscardLevel " << S32(mMaxDiscardLevel)
            << " mLastBindTime " << mLastBindTime
            << " mTarget " << S32(mTarget)
            << " mBindTarget " << S32(mBindTarget)
            << " mUseMipMaps " << S32(mUseMipMaps)
            << " mHasMipMaps " << S32(mHasMipMaps)
            << " mCurrentDiscardLevel " << S32(mCurrentDiscardLevel)
            << " mFormatInternal " << S32(mFormatInternal)
            << " mFormatPrimary " << S32(mFormatPrimary)
            << " mFormatType " << S32(mFormatType)
            << " mFormatSwapBytes " << S32(mFormatSwapBytes)
            << " mHasExplicitFormat " << S32(mHasExplicitFormat)
#if DEBUG_MISS
            << " mMissed " << mMissed
#endif
            << LL_ENDL;

    LL_INFOS() << " mTextureMemory " << mTextureMemory
            << " mTexNames " << mTexName
            << " mIsResident " << S32(mIsResident)
            << LL_ENDL;
}

//----------------------------------------------------------------------------
void LLImageDX::forceUpdateBindStats(void) const
{
    mLastBindTime = sLastFrameTime;
}

bool LLImageDX::updateBindStats() const
{
    if (mTexName != 0)
    {
#ifdef DEBUG_MISS
        mMissed = ! getIsResident(true);
#endif
        sBindCount++;
        if (mLastBindTime != sLastFrameTime)
        {
            // we haven't accounted for this texture yet this frame
            sUniqueCount++;
            mLastBindTime = sLastFrameTime;

            return true ;
        }
    }
    return false ;
}

F32 LLImageDX::getTimePassedSinceLastBound()
{
    return sLastFrameTime - mLastBindTime ;
}

void LLImageDX::setExplicitFormat( LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format, bool swap_bytes )
{
    // Note: must be called before createTexture()
    // Note: it's up to the caller to ensure that the format matches the number of components.
    mHasExplicitFormat = true;
    mFormatInternal = internal_format;
    mFormatPrimary = primary_format;
    if(type_format == 0)
        mFormatType = GL_UNSIGNED_BYTE;
    else
        mFormatType = type_format;
    mFormatSwapBytes = swap_bytes;

    calcAlphaChannelOffsetAndStride() ;
}

//----------------------------------------------------------------------------

void LLImageDX::setImage(const LLImageRaw* imageraw)
{
    llassert((imageraw->getWidth() == getWidth(mCurrentDiscardLevel)) &&
             (imageraw->getHeight() == getHeight(mCurrentDiscardLevel)) &&
             (imageraw->getComponents() == getComponents()));
    const U8* rawdata = imageraw->getData();
    setImage(rawdata, false);
}

bool LLImageDX::setImage(const U8* data_in, bool data_hasmips /* = false */, S32 usename /* = 0 */)
{
    // data_in == nullptr is NOT a rare/unsupported case - every real
    // "unbound" hit at startup is this: LLViewerFetchedTexture's normal discard-level streaming
    // pattern creates the texture object before real pixel data has
    // arrived, matching GL's own glTexImage2D(..., nullptr) allocate-only
    // call. DXTexture::create() now tolerates null data the same way.
    // S24: setSubImage()'s full-image HACK branch used to call this on every
    // glyph addition, destroying/recreating the whole DXTexture each time -
    // that branch is now DX_RENDER-excluded, see there.
    //
    // S24: `data_in` as received already points at the top-level
    // (mCurrentDiscardLevel) image regardless of data_hasmips - see the GL
    // branch's loop below, which only decrements `data_in` for
    // d > mCurrentDiscardLevel. DXTexture::create() synthesizes the rest of
    // the mip chain via GenerateMips(), so pre-baked lower-res mips later in
    // the buffer simply go unread. `dx_success` carries the real
    // create()/createCompressed() outcome through to the return below,
    // rather than always reporting success regardless of it.
    bool dx_success = false;
    if (!isSourceFormatCompressed())
    {
        dx_success = mDXTexture.create(data_in, getWidth(), getHeight(), mComponents, mUseMipMaps, isAlphaOnlyFormat(), isBGRAFormat());
        if (!dx_success)
        {
            LL_WARNS("Texture") << "LLImageDX::setImage: DXTexture::create failed" << LL_ENDL;
        }
    }
    else
    {
        // S24: mip0-only - see DXTexture::createCompressed()'s header
        // comment for why a full mip chain isn't possible for BC formats.
        // SRGB GL variants map to the plain UNORM DXGI format, not _SRGB -
        // this pipeline does its own manual srgb_to_linear() in shader code,
        // and mixing the two here would double-decode.
        DXGI_FORMAT dx_format = DXGI_FORMAT_UNKNOWN;
        switch (mFormatPrimary)
        {
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
            dx_format = DXGI_FORMAT_BC1_UNORM;
            break;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
            dx_format = DXGI_FORMAT_BC2_UNORM;
            break;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            dx_format = DXGI_FORMAT_BC3_UNORM;
            break;
        default:
            break;
        }

        if (dx_format == DXGI_FORMAT_UNKNOWN)
        {
            LL_WARNS_ONCE("Texture") << "LLImageDX::setImage: unrecognized compressed format, texture will render unbound"
                << " (mFormatPrimary=0x" << std::hex << mFormatPrimary << std::dec << ")" << LL_ENDL;
        }
        else
        {
            dx_success = mDXTexture.createCompressed(data_in, getWidth(), getHeight(), dx_format);
            if (!dx_success)
            {
                LL_WARNS("Texture") << "LLImageDX::setImage: DXTexture::createCompressed failed" << LL_ENDL;
            }
        }
    }

    // S24: bumped unconditionally, success or failure - see
    // getDXUploadGeneration(). Even a failed upload means an in-flight
    // background BC7 job's source pixel data is no longer current.
    ++mDXUploadGeneration;

    return dx_success;
}

bool LLImageDX::setSubImage(const U8* datap, S32 data_width, S32 data_height, S32 x_pos, S32 y_pos, S32 width, S32 height, bool force_fast_update /* = false */, LLGLuint use_name)
{
    if (!width || !height)
    {
        return true;
    }
    LLGLuint tex_name = use_name != 0 ? use_name : mTexName;
    if (0 == tex_name)
    {
        // *TODO: Re-enable warning?  Ran into thread locking issues? DK 2011-02-18
        //LL_WARNS() << "Setting subimage on image without GL texture" << LL_ENDL;
        return false;
    }
    if (datap == NULL)
    {
        // *TODO: Re-enable warning?  Ran into thread locking issues? DK 2011-02-18
        //LL_WARNS() << "Setting subimage on image with NULL datap" << LL_ENDL;
        return false;
    }

    // S24: setImage()'s DX_RENDER implementation unconditionally destroys+
    // recreates the texture and SRV, unlike GL's lightweight in-place
    // re-specification - addGlyphFromFont()'s setSubImage() call always
    // covers the full atlas, so the old "full-extent write -> call
    // setImage() instead" fast path would rebuild the whole font atlas from
    // scratch on every glyph addition. The incremental path below
    // (UpdateSubresource() via DXTexture::updateSubImage()) is correct even
    // for a full-extent write, so that's always used instead.
    if (mUseMipMaps)
    {
        dump();
        LL_ERRS() << "setSubImage called with mipmapped image (not supported)" << LL_ENDL;
    }
    llassert_always(mCurrentDiscardLevel == 0);
    llassert_always(x_pos >= 0 && y_pos >= 0);

    if (((x_pos + width) > getWidth()) ||
        (y_pos + height) > getHeight())
    {
        dump();
        LL_ERRS() << "Subimage not wholly in target image!"
               << " x_pos " << x_pos
               << " y_pos " << y_pos
               << " width " << width
               << " height " << height
               << " getWidth() " << getWidth()
               << " getHeight() " << getHeight()
               << LL_ENDL;
    }

    if ((x_pos + width) > data_width ||
        (y_pos + height) > data_height)
    {
        dump();
        LL_ERRS() << "Subimage not wholly in source image!"
               << " x_pos " << x_pos
               << " y_pos " << y_pos
               << " width " << width
               << " height " << height
               << " source_width " << data_width
               << " source_height " << data_height
               << LL_ENDL;
    }

    // Mirrors GL's GL_UNPACK_ROW_LENGTH + glTexSubImage2D approach - see
    // DXTexture::updateSubImage()'s comment. This is the per-CEF-paint hot
    // path (LLViewerMediaImpl::doMediaTexUpdate() -> setSubImage() -> here,
    // every repaint) - isBGRAFormat() must be threaded through here too,
    // not just the one-time create() path.
    if (!mDXTexture.updateSubImage(datap, data_width, x_pos, y_pos, width, height, mComponents, isAlphaOnlyFormat(), isBGRAFormat()))
    {
        LL_WARNS("Texture") << "LLImageDX::setSubImage: DXTexture::updateSubImage failed" << LL_ENDL;
    }
    mGLTextureCreated = true;
    return true;
}

bool LLImageDX::setSubImage(const LLImageRaw* imageraw, S32 x_pos, S32 y_pos, S32 width, S32 height, bool force_fast_update /* = false */, LLGLuint use_name)
{
    return setSubImage(imageraw->getData(), imageraw->getWidth(), imageraw->getHeight(), x_pos, y_pos, width, height, force_fast_update, use_name);
}

// Copy sub image from frame buffer
bool LLImageDX::setSubImageFromFrameBuffer(S32 fb_x, S32 fb_y, S32 x_pos, S32 y_pos, S32 width, S32 height)
{
    // S24: real callers are LLViewerDynamicTexture::postRender() and
    // llterrainpaintmap.cpp's PBR terrain paint-map baking.
    // DXTexture::copySubImageFromFrameBuffer() does the real work via
    // CopySubresourceRegion against whatever render target is bound.
    if (!mDXTexture.copySubImageFromFrameBuffer(fb_x, fb_y, x_pos, y_pos, width, height))
    {
        return false;
    }
    mGLTextureCreated = true;
    return true;
}

// static
void LLImageDX::generateTextures(S32 numTextures, U32 *textures)
{
    // S24: real callers under DX_RENDER only need a unique, non-zero,
    // stable U32 "name" to use as an opaque key (DXTexture/LLDXTexture
    // lookups don't go through the GL name at all) - hand out an
    // incrementing counter rather than a repeated sentinel, which would
    // risk aliasing distinct textures onto the same key downstream.
    static U32 s_next_name = 1;
    for (S32 i = 0; i < numTextures; ++i)
    {
        textures[i] = s_next_name++;
    }
}

constexpr int DELETE_DELAY = 3; // number of frames to wait before deleting textures
static std::vector<U32> sFreeList[DELETE_DELAY+1];

// static
void LLImageDX::updateClass()
{
    sFrameCount++;

    // wait a few frames before actually deleting the textures to avoid
    // synchronization issues with the GPU
    U32 idx = (sFrameCount+DELETE_DELAY) % (DELETE_DELAY+1);

    if (!sFreeList[idx].empty())
    {
        free_tex_images((GLsizei) sFreeList[idx].size(), sFreeList[idx].data());
        glDeleteTextures((GLsizei)sFreeList[idx].size(), sFreeList[idx].data());
        sFreeList[idx].resize(0);
    }
}

// static
void LLImageDX::deleteTextures(S32 numTextures, const U32 *textures)
{
    if (gGLManager.mInited)
    {
        U32 idx = sFrameCount % (DELETE_DELAY+1);
        for (S32 i = 0; i < numTextures; ++i)
        {
            sFreeList[idx].push_back(textures[i]);
        }
    }
}

// static
//create an empty GL texture: just create a texture name
//the texture is assiciate with some image by calling glTexImage outside LLImageDX
bool LLImageDX::createGLTexture()
{
    checkActiveThread();

    if (gGLManager.mIsDisabled)
    {
        LL_WARNS() << "Trying to create a texture while GL is disabled!" << LL_ENDL;
        return false;
    }

    mGLTextureCreated = false ; //do not save this texture when gl is destroyed.

    // S24: mirrors GL's "name reserved, no storage yet" semantic - mDXTexture
    // stays unallocated until a real create()/updateSubImage() call gives it
    // actual dimensions and pixel data.
    mTexName = 1; // non-zero sentinel - never used as a real GL name under DX_RENDER, see getHasGLTexture()
    return true;
}

bool LLImageDX::createGLTexture(S32 discard_level, const LLImageRaw* imageraw, S32 usename/*=0*/, bool to_create, S32 category, bool defer_copy, LLGLuint* tex_name)
{
    checkActiveThread();

    if (!imageraw || imageraw->isBufferInvalid())
    {
        LL_WARNS() << "Trying to create a texture from invalid image data" << LL_ENDL;
        mGLTextureCreated = false;
        return false;
    }

    if (discard_level < 0)
    {
        llassert(mCurrentDiscardLevel >= 0);
        discard_level = mCurrentDiscardLevel;
    }
    discard_level = llmin(discard_level, MAX_DISCARD_LEVEL);

    // Actual image width/height = raw image width/height * 2^discard_level
    S32 raw_w = imageraw->getWidth() ;
    S32 raw_h = imageraw->getHeight() ;

    S32 w = raw_w << discard_level;
    S32 h = raw_h << discard_level;

    // setSize may call destroyGLTexture if the size does not match
    if (!setSize(w, h, imageraw->getComponents(), discard_level))
    {
        LL_WARNS() << "Trying to create a texture with incorrect dimensions!" << LL_ENDL;
        mGLTextureCreated = false;
        return false;
    }

    if (mHasExplicitFormat && isExplicitFormatMismatched())
    {
        LL_WARNS()  << "Incorrect format: " << std::hex << mFormatPrimary << " components: " << (U32)mComponents <<  LL_ENDL;
        mHasExplicitFormat = false;
    }

    if( !mHasExplicitFormat )
    {
        switch (mComponents)
        {
        case 1:
            // Use luminance alpha (for fonts)
            mFormatInternal = GL_LUMINANCE8;
            mFormatPrimary = GL_LUMINANCE;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        case 2:
            // Use luminance alpha (for fonts)
            mFormatInternal = GL_LUMINANCE8_ALPHA8;
            mFormatPrimary = GL_LUMINANCE_ALPHA;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        case 3:
            mFormatInternal = GL_RGB8;
            mFormatPrimary = GL_RGB;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        case 4:
            mFormatInternal = GL_RGBA8;
            mFormatPrimary = GL_RGBA;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        default:
            LL_ERRS() << "Bad number of components for texture: " << (U32)getComponents() << LL_ENDL;
        }

        calcAlphaChannelOffsetAndStride() ;
    }

    if(!to_create) //not create a gl texture
    {
        destroyGLTexture();
        mCurrentDiscardLevel = discard_level;
        mLastBindTime = sLastFrameTime;
        mGLTextureCreated = false;
        return true ;
    }

    setCategory(category);
    const U8* rawdata = imageraw->getData();
    return createGLTexture(discard_level, rawdata, false, usename, defer_copy, tex_name);
}

bool LLImageDX::createGLTexture(S32 discard_level, const U8* data_in, bool data_hasmips, S32 usename, bool defer_copy, LLGLuint* tex_name)
// Call with void data, vmem is allocated but unitialized
{
    checkActiveThread();

    // Deliberately scoped-down first pass (see setImage()'s comment) - skips
    // GL texture-name generation/bind/mip-parameter setup entirely (none of
    // it applies to a DXTexture) and creates it directly via setImage().
    // mTexName is set to a non-zero sentinel - never used as a real GL name
    // under DX_RENDER (every GL call that would consume it is already
    // bypassed elsewhere) - purely so backend-agnostic callers of
    // getHasGLTexture()/isGLTextureCreated() still see "yes, this texture is
    // ready" once uploaded.
    if (defer_copy)
    {
        data_in = nullptr;
    }

    if (discard_level < 0)
    {
        discard_level = llmax((S32)mCurrentDiscardLevel, 0);
    }
    discard_level = llclamp(discard_level, 0, (S32)mMaxDiscardLevel);
    discard_level = llmin(discard_level, MAX_DISCARD_LEVEL);
    mCurrentDiscardLevel = discard_level;

    bool success = setImage(data_in, data_hasmips);
    mGLTextureCreated = success;
    mTexName = success ? 1 : 0;

    // S24: populate per-texture GPU-footprint accounting here too - this
    // path returns before the GL-only assignment below runs, so it would
    // otherwise never be set for DX_RENDER textures.
    if (success)
    {
        mTextureMemory = (S64Bytes)getMipBytes(mCurrentDiscardLevel);
        // S24: feed the sTextureBytes fallback total too - see
        // allocDXTextureBytes().
        LLImageDXMemory::allocDXTextureBytes(this, mTextureMemory.value());
    }

    if (tex_name != nullptr)
    {
        *tex_name = mTexName;
    }
    return success;
}

void LLImageDX::syncToMainThread(LLGLuint new_tex_name)
{
    llassert(!on_main_thread());

    // Reached from LLViewerMediaImpl::doMediaTexUpdate() on the media
    // update worker thread (e.g. every CEF frame for an embedded browser -
    // LLPanelLogin's own "login_html" LLMediaCtrl is exactly this path,
    // meaning this was very likely the actual reason the login form never
    // painted while the surrounding scene/splash rendered fine). GL's
    // cross-context fence/flush dance has no meaning here - there's no GL
    // context at all on this thread. DXTexture::create()/updateSubImage()
    // (called synchronously just before this, by createGLTexture()/
    // setSubImage() above this function's only caller) already issue their
    // D3D11 calls immediately, so just post the texture-name swap straight
    // to the main thread.
    ref();
    LL::WorkQueue::postMaybe(
        mMainQueue,
        [=, this]()
        {
            syncTexName(new_tex_name);
            unref();
        });
}


void LLImageDX::syncTexName(LLGLuint texname)
{
    if (texname != 0)
    {
        if (mTexName != 0 && mTexName != texname)
        {
            LLImageDX::deleteTextures(1, &mTexName);
        }
        mTexName = texname;
    }
}

bool LLImageDX::readBackRaw(S32 discard_level, LLImageRaw* imageraw, bool compressed_ok) const
{

    if (discard_level < 0)
    {
        discard_level = mCurrentDiscardLevel;
    }

    // S24: uses DXReadback to read the texture's real GPU content back.
    // Deliberately scoped down like DXTexture::create() itself: only the
    // current top-level mip is supported - DXTexture doesn't store discrete
    // per-discard-level resources. Compressed-format readback isn't
    // supported either, matching DXTexture's own lack of compressed support.
    if (discard_level != mCurrentDiscardLevel)
    {
        return false;
    }

    ID3D11Texture2D* dx_tex = mDXTexture.getTexture();
    if (!dx_tex)
    {
        return false;
    }

    // S24: LLImageDX::upgradeToCompressedMips() can replace this texture's
    // GPU resource with a BC7 one after the fact; DXReadback::readPixels()
    // below assumes RGBA8 stride/size, so reading a BC7 resource with those
    // assumptions is an out-of-bounds read. Bail out cleanly instead.
    if (mDXTexture.isCompressedFormat())
    {
        return false;
    }

    S32 dx_width = getWidth(discard_level);
    S32 dx_height = getHeight(discard_level);
    S32 dx_ncomponents = getComponents();
    if (dx_ncomponents == 0 || dx_width <= 0 || dx_height <= 0)
    {
        return false;
    }

    LLImageDataLock dx_lock(imageraw);
    if (!imageraw->allocateDataSize(dx_width, dx_height, dx_ncomponents))
    {
        return false;
    }

    // DXTexture always stores RGBA8 on the GPU regardless of the source's
    // real component count (see DXTexture::create()'s repackPixel()) - read
    // back the full RGBA8 content, then repack down to dx_ncomponents the
    // same way GL's own luminance/luminance-alpha/RGB formats are derived
    // from an RGBA source, mirroring repackPixel() in reverse.
    std::vector<uint8_t> rgba((size_t)dx_width * dx_height * 4);
    if (!DXReadback::readPixels(dx_tex, 0, 0, dx_width, dx_height, 4, rgba.data()))
    {
        return false;
    }

    U8* dx_dst = imageraw->getData();
    for (S32 i = 0; i < dx_width * dx_height; ++i)
    {
        const uint8_t* src = &rgba[(size_t)i * 4];
        U8* d = dx_dst + (size_t)i * dx_ncomponents;
        switch (dx_ncomponents)
        {
        case 1: d[0] = src[0]; break;
        case 2: d[0] = src[0]; d[1] = src[3]; break;
        case 3: d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; break;
        case 4: d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = src[3]; break;
        default: return false;
        }
    }

    return true;
}

void LLImageDX::destroyGLTexture()
{
    checkActiveThread();

    if (mTexName != 0)
    {
        if(mTextureMemory != S64Bytes(0))
        {
            mTextureMemory = (S64Bytes)0;
        }

        // mTexName is a non-zero sentinel under DX_RENDER (see
        // createGLTexture()'s comment), not a real GL name.
        // S24: free this instance's tracked accounting - see
        // allocDXTextureBytes()/freeDXTextureBytes().
        LLImageDXMemory::freeDXTextureBytes(this);
        mDXTexture.destroy();
        mCurrentDiscardLevel = -1 ; //invalidate mCurrentDiscardLevel.
        mTexName = 0;
        mGLTextureCreated = false ;
    }
}

//force to invalidate the gl texture, most likely a sculpty texture
void LLImageDX::forceToInvalidateGLTexture()
{
    checkActiveThread();
    if (mTexName != 0)
    {
        destroyGLTexture();
    }
    else
    {
        mCurrentDiscardLevel = -1 ; //invalidate mCurrentDiscardLevel.
    }
}

//----------------------------------------------------------------------------

void LLImageDX::setAddressMode(LLTexUnit::eTextureAddressMode mode)
{
    if (mAddressMode != mode)
    {
        mAddressMode = mode;
    }

    if (gDX.getTexUnit(gDX.getCurrentTexUnitIndex())->getCurrTexture() == mTexName)
    {
        gDX.getTexUnit(gDX.getCurrentTexUnitIndex())->setTextureAddressMode(mode);
    }
}

void LLImageDX::setFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
    if (mFilterOption != option)
    {
        mFilterOption = option;
    }

    // S24: the old GL immediate-apply-if-currently-bound branch here was dead
    // under DX_RENDER, not just unreachable - doubly so. getCurrTexture()
    // reads LLTexUnit::mCurrTexture, which (per its own comment in
    // llrender.cpp) stays 0 forever under DX_RENDER since bindFast() never
    // assigns it, so the guard condition was always false; and even if it
    // fired, LLTexUnit::setTextureFilteringOption() is itself already a
    // documented DX_RENDER no-op (see llrender.cpp). The real filter
    // application happens at bind time - bindFast() reads mFilterOption
    // (set above) fresh via getFilteringOption() to build the DXSampler, so
    // call order relative to bindTexture() never matters here.
}

bool LLImageDX::getIsResident(bool test_now)
{
    if (test_now)
    {
        // S24: raw glAreTexturesResident() has no D3D11 equivalent -
        // residency is driver/OS-managed, not queried this way.
        mIsResident = mTexName != 0;
    }

    return mIsResident;
}

S32 LLImageDX::getHeight(S32 discard_level) const
{
    if (discard_level < 0)
    {
        discard_level = mCurrentDiscardLevel;
    }
    S32 height = mHeight >> discard_level;
    if (height < 1) height = 1;
    return height;
}

S32 LLImageDX::getWidth(S32 discard_level) const
{
    if (discard_level < 0)
    {
        discard_level = mCurrentDiscardLevel;
    }
    S32 width = mWidth >> discard_level;
    if (width < 1) width = 1;
    return width;
}

S64 LLImageDX::getBytes(S32 discard_level) const
{
    if (discard_level < 0)
    {
        discard_level = mCurrentDiscardLevel;
    }
    S32 w = mWidth>>discard_level;
    S32 h = mHeight>>discard_level;
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    return dataFormatBytes(mFormatPrimary, w, h);
}

S64 LLImageDX::getMipBytes(S32 discard_level) const
{
    if (discard_level < 0)
    {
        discard_level = mCurrentDiscardLevel;
    }
    S32 w = mWidth>>discard_level;
    S32 h = mHeight>>discard_level;
    // S24: DXTexture::create() unconditionally repacks every uncompressed
    // source format to DXGI_FORMAT_R8G8B8A8_UNORM (4 bytes/pixel) regardless
    // of mFormatPrimary, so dataFormatBytes(mFormatPrimary,...) would
    // undercount by up to 4x for 1-3 component sources. A pre-compressed
    // SOURCE instead goes through DXTexture::createCompressed() (BC1/BC2/
    // BC3, 0.5-1 byte/pixel) and must be counted separately, below, or the
    // RGBA8 assumption would over-count it by up to 8x. Mirrors setImage()'s
    // GL->DXGI format mapping - keep the two in sync.
    if (isSourceFormatCompressed())
    {
        S32 block_bytes;
        switch (mFormatPrimary)
        {
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
            block_bytes = 8;
            break;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            block_bytes = 16;
            break;
        default:
            // Unrecognized - match DXTexture::createCompressed()'s own
            // "anything else is treated as 16" convention.
            block_bytes = 16;
            break;
        }
        const S32 blocks_wide = (w + 3) / 4;
        const S32 blocks_high = (h + 3) / 4;
        // createCompressed() is mip0-only (BC formats can't GenerateMips())
        // - return here, skipping the mip-chain summation loop below
        // entirely, since summing a synthetic chain that was never actually
        // uploaded would reintroduce a smaller version of the same
        // over-count.
        return (S64)blocks_wide * blocks_high * block_bytes;
    }

    // S24: a texture that started life as an ordinary RGBA8 upload
    // (isSourceFormatCompressed() false) can still end up block-compressed
    // later via DXBC7UploadManager's background BC7 upgrade
    // (upgradeToCompressedMips()) - that path deliberately never touches
    // mFormatPrimary, so it's invisible to the check above. Ask the real
    // GPU resource instead. Always DXGI_FORMAT_BC7_UNORM (16-byte blocks) -
    // requestUpgrade() never requests anything else - and mip0-only for the
    // same reason as the isSourceFormatCompressed() case above.
    if (isGpuResourceCompressed())
    {
        const S32 blocks_wide = (w + 3) / 4;
        const S32 blocks_high = (h + 3) / 4;
        return (S64)blocks_wide * blocks_high * 16;
    }

    S64 res = (S64)w * h * 4;
    if (mUseMipMaps)
    {
        while (w > 1 && h > 1)
        {
            w >>= 1; if (w == 0) w = 1;
            h >>= 1; if (h == 0) h = 1;
            res += (S64)w * h * 4;
        }
    }
    return res;
}

bool LLImageDX::getBoundRecently() const
{
    return (bool)(sLastFrameTime - mLastBindTime < MIN_TEXTURE_LIFETIME);
}

bool LLImageDX::getIsAlphaMask() const
{
    llassert_always(!sSkipAnalyzeAlpha);
    return mIsMask;
}

void LLImageDX::setTarget(const LLGLenum target, const LLTexUnit::eTextureType bind_target)
{
    mTarget = target;
    mBindTarget = bind_target;
}

const S8 INVALID_OFFSET = -99 ;
void LLImageDX::setNeedsAlphaAndPickMask(bool need_mask)
{
    if(mNeedsAlphaAndPickMask != need_mask)
    {
        mNeedsAlphaAndPickMask = need_mask;

        if(mNeedsAlphaAndPickMask)
        {
            mAlphaOffset = 0 ;
        }
        else //do not need alpha mask
        {
            mAlphaOffset = INVALID_OFFSET ;
            mIsMask = false;
        }
    }
}

void LLImageDX::calcAlphaChannelOffsetAndStride()
{
    if(mAlphaOffset == INVALID_OFFSET)//do not need alpha mask
    {
        return ;
    }

    mAlphaStride = -1 ;
    switch (mFormatPrimary)
    {
    case GL_LUMINANCE:
    case GL_ALPHA:
        mAlphaStride = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        mAlphaStride = 2;
        break;
    case GL_RED:
    case GL_RGB:
    case GL_SRGB:
        mNeedsAlphaAndPickMask = false;
        mIsMask = false;
        return; //no alpha channel.
    case GL_RGBA:
    case GL_SRGB_ALPHA:
        mAlphaStride = 4;
        break;
    case GL_BGRA_EXT:
        mAlphaStride = 4;
        break;
    default:
        break;
    }

    mAlphaOffset = -1 ;
    if (mFormatType == GL_UNSIGNED_BYTE)
    {
        mAlphaOffset = mAlphaStride - 1 ;
    }
    else if(is_little_endian())
    {
        if (mFormatType == GL_UNSIGNED_INT_8_8_8_8)
        {
            mAlphaOffset = 0 ;
        }
        else if (mFormatType == GL_UNSIGNED_INT_8_8_8_8_REV)
        {
            mAlphaOffset = 3 ;
        }
    }
    else //big endian
    {
        if (mFormatType == GL_UNSIGNED_INT_8_8_8_8)
        {
            mAlphaOffset = 3 ;
        }
        else if (mFormatType == GL_UNSIGNED_INT_8_8_8_8_REV)
        {
            mAlphaOffset = 0 ;
        }
    }

    if( mAlphaStride < 1 || //unsupported format
        mAlphaOffset < 0 || //unsupported type
        (mFormatPrimary == GL_BGRA_EXT && mFormatType != GL_UNSIGNED_BYTE)) //unknown situation
    {
        LL_WARNS() << "Cannot analyze alpha for image with format type " << std::hex << mFormatType << std::dec << LL_ENDL;

        mNeedsAlphaAndPickMask = false ;
        mIsMask = false;
    }
}

void LLImageDX::analyzeAlpha(const void* data_in, U32 w, U32 h)
{
    if(!data_in || sSkipAnalyzeAlpha || !mNeedsAlphaAndPickMask)
    {
        return ;
    }


    U32 length = w * h;
    U32 alphatotal = 0;

    U32 sample[16];
    memset(sample, 0, sizeof(U32)*16);

    // generate histogram of quantized alpha.
    // also add-in the histogram of a 2x2 box-sampled version.  The idea is
    // this will mid-skew the data (and thus increase the chances of not
    // being used as a mask) from high-frequency alpha maps which
    // suffer the worst from aliasing when used as alpha masks.
    if (w >= 2 && h >= 2)
    {
        llassert(w%2 == 0);
        llassert(h%2 == 0);
        const GLubyte* rowstart = ((const GLubyte*) data_in) + mAlphaOffset;
        for (U32 y = 0; y < h; y+=2)
        {
            const GLubyte* current = rowstart;
            for (U32 x = 0; x < w; x+=2)
            {
                const U32 s1 = current[0];
                alphatotal += s1;
                const U32 s2 = current[w * mAlphaStride];
                alphatotal += s2;
                current += mAlphaStride;
                const U32 s3 = current[0];
                alphatotal += s3;
                const U32 s4 = current[w * mAlphaStride];
                alphatotal += s4;
                current += mAlphaStride;

                ++sample[s1/16];
                ++sample[s2/16];
                ++sample[s3/16];
                ++sample[s4/16];

                const U32 asum = (s1+s2+s3+s4);
                alphatotal += asum;
                sample[asum/(16*4)] += 4;
            }


            rowstart += 2 * w * mAlphaStride;
        }
        length *= 2; // we sampled everything twice, essentially
    }
    else
    {
        const GLubyte* current = ((const GLubyte*) data_in) + mAlphaOffset;
        for (U32 i = 0; i < length; i++)
        {
            const U32 s1 = *current;
            alphatotal += s1;
            ++sample[s1/16];
            current += mAlphaStride;
        }
    }

    // if more than 1/16th of alpha samples are mid-range, this
    // shouldn't be treated as a 1-bit mask

    // also, if all of the alpha samples are clumped on one half
    // of the range (but not at an absolute extreme), then consider
    // this to be an intentional effect and don't treat as a mask.

    U32 midrangetotal = 0;
    for (U32 i = 2; i < 13; i++)
    {
        midrangetotal += sample[i];
    }
    U32 lowerhalftotal = 0;
    for (U32 i = 0; i < 8; i++)
    {
        lowerhalftotal += sample[i];
    }
    U32 upperhalftotal = 0;
    for (U32 i = 8; i < 16; i++)
    {
        upperhalftotal += sample[i];
    }

    if (midrangetotal > length/48 || // lots of midrange, or
        (lowerhalftotal == length && alphatotal != 0) || // all close to transparent but not all totally transparent, or
        (upperhalftotal == length && alphatotal != 255*length)) // all close to opaque but not all totally opaque
    {
        mIsMask = false; // not suitable for masking
    }
    else
    {
        mIsMask = true;
    }
}

//----------------------------------------------------------------------------
U32 LLImageDX::createPickMask(S32 pWidth, S32 pHeight)
{
    freePickMask();
    U32 pick_width = pWidth/2 + 1;
    U32 pick_height = pHeight/2 + 1;

    U32 size = pick_width * pick_height;
    size = (size + 7) / 8; // pixelcount-to-bits
    mPickMask = new U8[size];
    mPickMaskWidth = pick_width - 1;
    mPickMaskHeight = pick_height - 1;

    memset(mPickMask, 0, sizeof(U8) * size);

    return size;
}

//----------------------------------------------------------------------------
void LLImageDX::freePickMask()
{
    if (mPickMask != NULL)
    {
        delete [] mPickMask;
    }
    mPickMask = NULL;
    mPickMaskWidth = mPickMaskHeight = 0;
}

bool LLImageDX::isSourceFormatCompressed() const
{
    llassert(mFormatPrimary != 0);
    // *NOTE: Not all compressed formats are included here.
    bool is_compressed = false;
    switch (mFormatPrimary)
    {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        is_compressed = true;
        break;
    default:
        break;
    }
    return is_compressed;
}

//----------------------------------------------------------------------------
void LLImageDX::updatePickMask(S32 width, S32 height, const U8* data_in)
{
    if(!mNeedsAlphaAndPickMask)
    {
        return ;
    }

    if (mFormatType != GL_UNSIGNED_BYTE ||
        ((mFormatPrimary != GL_RGBA)
      && (mFormatPrimary != GL_SRGB_ALPHA)))
    {
        //cannot generate a pick mask for this texture
        freePickMask();
        return;
    }


#ifdef SHOW_ASSERT
    const U32 pickSize = createPickMask(width, height);
#else // SHOW_ASSERT
    createPickMask(width, height);
#endif // SHOW_ASSERT

    U32 pick_bit = 0;

    for (S32 y = 0; y < height; y += 2)
    {
        for (S32 x = 0; x < width; x += 2)
        {
            U8 alpha = data_in[(y*width+x)*4+3];

            if (alpha > 32)
            {
                U32 pick_idx = pick_bit/8;
                U32 pick_offset = pick_bit%8;
                llassert(pick_idx < pickSize);

                mPickMask[pick_idx] |= 1 << pick_offset;
            }

            ++pick_bit;
        }
    }
}

bool LLImageDX::getMask(const LLVector2 &tc)
{
    bool res = true;

    if (mPickMask)
    {
        F32 u,v;
        if (LL_LIKELY(tc.isFinite()))
        {
            u = tc.mV[0] - floorf(tc.mV[0]);
            v = tc.mV[1] - floorf(tc.mV[1]);
        }
        else
        {
            LL_WARNS_ONCE("render") << "Ugh, non-finite u/v in mask pick" << LL_ENDL;
            u = v = 0.f;
            // removing assert per EXT-4388
            // llassert(false);
        }

        if (LL_UNLIKELY(u < 0.f || u > 1.f ||
                v < 0.f || v > 1.f))
        {
            LL_WARNS_ONCE("render") << "Ugh, u/v out of range in image mask pick" << LL_ENDL;
            u = v = 0.f;
            // removing assert per EXT-4388
            // llassert(false);
        }

        S32 x = llfloor(u * mPickMaskWidth);
        S32 y = llfloor(v * mPickMaskHeight);

        if (LL_UNLIKELY(x > mPickMaskWidth))
        {
            LL_WARNS_ONCE("render") << "Ooh, width overrun on pick mask read, that coulda been bad." << LL_ENDL;
            x = llmax((U16)0, mPickMaskWidth);
        }
        if (LL_UNLIKELY(y > mPickMaskHeight))
        {
            LL_WARNS_ONCE("render") << "Ooh, height overrun on pick mask read, that woulda been bad." << LL_ENDL;
            y = llmax((U16)0, mPickMaskHeight);
        }

        S32 idx = y*mPickMaskWidth+x;
        S32 offset = idx%8;

        res = (mPickMask[idx/8] & (1 << offset)) != 0;
    }

    return res;
}

void LLImageDX::setCurTexSizebar(S32 index, bool set_pick_size)
{
    sCurTexSizeBar = index ;

    if(set_pick_size)
    {
        sCurTexPickSize = (1 << index) ;
    }
    else
    {
        sCurTexPickSize = -1 ;
    }
}
void LLImageDX::resetCurTexSizebar()
{
    sCurTexSizeBar = -1 ;
    sCurTexPickSize = -1 ;
}

bool LLImageDX::scaleDown(S32 desired_discard)
{
    // S24: real D3D11 downscale, driving the VRAM-pressure discard-bias
    // system for already-resident textures. Every real caller
    // (LLViewerLODTexture) is guaranteed a full mip chain already, so
    // DXTexture::scaleDown() can do this as a pure GPU-to-GPU resource copy.
    if (mFormatInternal == -1) // not initialized
    {
        return false;
    }

    desired_discard = llmin(desired_discard, mMaxDiscardLevel);

    if (desired_discard <= mCurrentDiscardLevel)
    {
        return false;
    }

    S32 mip = desired_discard - mCurrentDiscardLevel;
    S32 desired_width = getWidth(desired_discard);
    S32 desired_height = getHeight(desired_discard);

    if (!mDXTexture.scaleDown(mip, desired_width, desired_height))
    {
        return false;
    }

    mCurrentDiscardLevel = desired_discard;
    mTextureMemory = (S64Bytes)getMipBytes(mCurrentDiscardLevel);
    // S24: feed the sTextureBytes fallback total too - see allocDXTextureBytes().
    LLImageDXMemory::allocDXTextureBytes(this, mTextureMemory.value());

    return true;
}


//----------------------------------------------------------------------------
#if LL_IMAGEDX_THREAD_CHECK
void LLImageDX::checkActiveThread()
{
    llassert(mActiveThread == LLThread::currentID());
}
#endif

//----------------------------------------------------------------------------


// Manual Mip Generation
/*
        S32 width = getWidth(discard_level);
        S32 height = getHeight(discard_level);
        S32 w = width, h = height;
        S32 nummips = 1;
        while (w > 4 && h > 4)
        {
            w >>= 1; h >>= 1;
            nummips++;
        }
        w = width, h = height;
        const U8* prev_mip_data = 0;
        const U8* cur_mip_data = 0;
        for (int m=0; m<nummips; m++)
        {
            if (m==0)
            {
                cur_mip_data = rawdata;
            }
            else
            {
                S32 bytes = w * h * mComponents;
                U8* new_data = new U8[bytes];
                LLImageBase::generateMip(prev_mip_data, new_data, w, h, mComponents);
                cur_mip_data = new_data;
            }
            llassert(w > 0 && h > 0 && cur_mip_data);
            U8 test = cur_mip_data[w*h*mComponents-1];
            {
                LLImageDX::setManualImage(mTarget, m, mFormatInternal, w, h, mFormatPrimary, mFormatType, cur_mip_data);
            }
            if (prev_mip_data && prev_mip_data != rawdata)
            {
                delete prev_mip_data;
            }
            prev_mip_data = cur_mip_data;
            w >>= 1;
            h >>= 1;
        }
        if (prev_mip_data && prev_mip_data != rawdata)
        {
            delete prev_mip_data;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  nummips);
*/

LLImageDXThread::LLImageDXThread(LLWindow* window)
    // We want exactly one thread.
    : LL::ThreadPool("LLImageDX", 1)
    , mWindow(window)
{
    mFinished = false;

    mContext = mWindow->createSharedContext();
    LL::ThreadPool::start();
}

// S24: DX_RENDER never constructs this class - background texture/media
// creation is permanently disabled under DX_RENDER. GL-only.
void LLImageDXThread::run()
{
    // We must perform setup on this thread before actually servicing our
    // WorkQueue, likewise cleanup afterwards.
    mWindow->makeContextCurrent(mContext);
    gDX.init(false);
    LL::ThreadPool::run();
    gDX.shutdown();
    mWindow->destroySharedContext(mContext);
}

