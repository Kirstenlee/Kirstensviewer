/** 
 * @file lldxtexture.cpp
 * @brief DX texture implementation
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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
#include "linden_common.h"
#include "lldxtexture.h"


DXTextureBase::DXTextureBase(bool usemipmaps)
{
    init();
    mUseMipMaps = usemipmaps;
}

DXTextureBase::DXTextureBase(const U32 width, const U32 height, const U8 components, bool usemipmaps)
{
    init();
    mFullWidth = width ;
    mFullHeight = height ;
    mUseMipMaps = usemipmaps;
    mComponents = components ;
    setTexelsPerImage();
}

DXTextureBase::DXTextureBase(const LLImageRaw* raw, bool usemipmaps)
{
    init();
    mUseMipMaps = usemipmaps ;
    // Create an empty image of the specified size and width
    mGLTexturep = new LLImageDX(raw, usemipmaps) ;
    mFullWidth = mGLTexturep->getWidth();
    mFullHeight = mGLTexturep->getHeight();
    mComponents = mGLTexturep->getComponents();
    setTexelsPerImage();
}

DXTextureBase::~DXTextureBase()
{
    cleanup();
}

// S24: merged up from the retired LLTexture interface (lltexture.h) - same
// stub bodies as before the merge. Real behavior lives in LLViewerTexture
// (newview), which overrides all of these.
S8   DXTextureBase::getType() const { llassert(false); return 0; }
void DXTextureBase::setKnownDrawSize(S32 width, S32 height) { llassert(false); }
bool DXTextureBase::bindDefaultImage(const S32 stage) { llassert(false); return false; }
bool DXTextureBase::bindDebugImage(const S32 stage) { llassert(false); return false; }
void DXTextureBase::forceImmediateUpdate() { llassert(false); }
bool DXTextureBase::isActiveFetching() { llassert(false); return false; }
void DXTextureBase::updateBindStatsForTester() { }

void DXTextureBase::init()
{
    mBoostLevel = DXTextureBase::BOOST_NONE;

    mFullWidth = 0;
    mFullHeight = 0;
    mTexelsPerImage = 0 ;
    mUseMipMaps = false ;
    mComponents = 0 ;

    mTextureState = NO_DELETE ;
    mDontDiscard = false;
    mNeedsGLTexture = false ;
}

void DXTextureBase::cleanup()
{
    if(mGLTexturep) mGLTexturep->cleanup(); // S24 
    
}

// virtual
void DXTextureBase::dump()
{
	if(mGLTexturep) mGLTexturep->dump(); // S24
    
}

void DXTextureBase::setBoostLevel(S32 level)
{
    if(mBoostLevel != level)
    {
        mBoostLevel = level ;
        if(mBoostLevel != DXTextureBase::BOOST_NONE
           && mBoostLevel != DXTextureBase::BOOST_ICON
           && mBoostLevel != DXTextureBase::BOOST_THUMBNAIL
           && mBoostLevel != DXTextureBase::BOOST_TERRAIN)
        {
            setNoDelete() ;
        }
    }
}

void DXTextureBase::forceActive()
{
    mTextureState = ACTIVE ;
}

void DXTextureBase::setActive() 
{ 
	if(mTextureState != NO_DELETE)
	{
		mTextureState = ACTIVE ; 
	}
}

//set the texture to stay in memory
void DXTextureBase::setNoDelete() 
{ 
	mTextureState = NO_DELETE ;
}

void DXTextureBase::generateGLTexture() 
{	
	if(mGLTexturep.isNull())
	{
		mGLTexturep = new LLImageDX(mFullWidth, mFullHeight, mComponents, mUseMipMaps) ;
	}
}

LLImageDX* DXTextureBase::getGLTexture() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep ;
}

bool DXTextureBase::createGLTexture()
{
    if(mGLTexturep.isNull())
    {
        generateGLTexture() ;
    }

    return mGLTexturep->createGLTexture() ;
}

bool DXTextureBase::createGLTexture(S32 discard_level, const LLImageRaw* imageraw, S32 usename, bool to_create, S32 category, bool defer_copy, LLGLuint* tex_name)
{
    llassert(mGLTexturep.notNull());

    bool ret = mGLTexturep->createGLTexture(discard_level, imageraw, usename, to_create, category, defer_copy, tex_name) ;

    if(ret)
    {
        mFullWidth = mGLTexturep->getCurrentWidth() ;
        mFullHeight = mGLTexturep->getCurrentHeight() ;
        mComponents = mGLTexturep->getComponents() ;
        setTexelsPerImage();
    }

    return ret ;
}

bool DXTextureBase::createDynamicGLTexture(const LLImageRaw* imageraw)
{
    llassert(mGLTexturep.notNull());

    bool ret = mGLTexturep->createDynamicGLTexture(imageraw);

    if (ret)
    {
        mFullWidth = mGLTexturep->getCurrentWidth();
        mFullHeight = mGLTexturep->getCurrentHeight();
        mComponents = mGLTexturep->getComponents();
        setTexelsPerImage();
    }

    return ret;
}

bool DXTextureBase::updateDynamic(const U8* datap, S32 data_width, S32 data_height)
{
    llassert(mGLTexturep.notNull());

    return mGLTexturep->updateDynamic(datap, data_width, data_height);
}

void DXTextureBase::setExplicitFormat(LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format, bool swap_bytes)
{
    llassert(mGLTexturep.notNull()) ;

    mGLTexturep->setExplicitFormat(internal_format, primary_format, type_format, swap_bytes) ;
}
void DXTextureBase::setAddressMode(LLTexUnit::eTextureAddressMode mode)
{
    llassert(mGLTexturep.notNull()) ;
    mGLTexturep->setAddressMode(mode) ;
}
void DXTextureBase::setFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
    llassert(mGLTexturep.notNull()) ;
	mGLTexturep->setFilteringOption(option) ;
}

//virtual
S32	DXTextureBase::getWidth(S32 discard_level) const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getWidth(discard_level) ;
}

//virtual
S32	DXTextureBase::getHeight(S32 discard_level) const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getHeight(discard_level) ;
}

S32 DXTextureBase::getMaxDiscardLevel() const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getMaxDiscardLevel() ;
}
S32 DXTextureBase::getDiscardLevel() const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getDiscardLevel() ;
}
S8  DXTextureBase::getComponents() const 
{ 
	llassert(mGLTexturep.notNull()) ;
	
	return mGLTexturep->getComponents() ;
}

LLGLuint DXTextureBase::getTexName() const 
{ 
	llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getTexName() ;
}

bool DXTextureBase::hasGLTexture() const
{
    if(mGLTexturep.notNull())
    {
        return mGLTexturep->getHasGLTexture() ;
    }
    return false ;
}

bool DXTextureBase::getBoundRecently() const
{
    if(mGLTexturep.notNull())
    {
        return mGLTexturep->getBoundRecently() ;
    }
    return false ;
}

LLTexUnit::eTextureType DXTextureBase::getTarget(void) const
{
    llassert(mGLTexturep.notNull()) ;
    return mGLTexturep->getTarget() ;
}

bool DXTextureBase::setSubImage(const LLImageRaw* imageraw, S32 x_pos, S32 y_pos, S32 width, S32 height, LLGLuint use_name)
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->setSubImage(imageraw, x_pos, y_pos, width, height, 0, use_name) ;
}

bool DXTextureBase::setSubImage(const U8* datap, S32 data_width, S32 data_height, S32 x_pos, S32 y_pos, S32 width, S32 height, LLGLuint use_name)
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->setSubImage(datap, data_width, data_height, x_pos, y_pos, width, height, 0, use_name) ;
}

void DXTextureBase::setGLTextureCreated (bool initialized)
{
    llassert(mGLTexturep.notNull()) ;

	mGLTexturep->setGLTextureCreated (initialized) ;
}

void  DXTextureBase::setCategory(S32 category) 
{
	llassert(mGLTexturep.notNull()) ;

	mGLTexturep->setCategory(category) ;
}

void DXTextureBase::setTexName(LLGLuint texName)
{
    llassert(mGLTexturep.notNull());
    return mGLTexturep->setTexName(texName); 
}

void DXTextureBase::setTarget(const LLGLenum target, const LLTexUnit::eTextureType bind_target)
{
    llassert(mGLTexturep.notNull());
    return mGLTexturep->setTarget(target, bind_target); 
}

LLTexUnit::eTextureAddressMode DXTextureBase::getAddressMode(void) const
{
	llassert(mGLTexturep.notNull()) ;

	return mGLTexturep->getAddressMode() ;
}

S32Bytes DXTextureBase::getTextureMemory() const
{
	llassert(mGLTexturep.notNull()) ;

	return mGLTexturep->mTextureMemory ;
}

LLGLenum DXTextureBase::getPrimaryFormat() const
{
	llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getPrimaryFormat() ;
}

bool DXTextureBase::getIsAlphaMask() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getIsAlphaMask() ;
}

bool DXTextureBase::getMask(const LLVector2 &tc)
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getMask(tc) ;
}

F32 DXTextureBase::getTimePassedSinceLastBound()
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getTimePassedSinceLastBound() ;
}
bool DXTextureBase::getMissed() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getMissed() ;
}

void DXTextureBase::forceUpdateBindStats(void) const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->forceUpdateBindStats() ;
}

bool DXTextureBase::isGLTextureCreated() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->isGLTextureCreated() ;
}

void DXTextureBase::destroyGLTexture()
{
    if(mGLTexturep.notNull() && mGLTexturep->getHasGLTexture())
    {
        mGLTexturep->destroyGLTexture() ;
        mTextureState = DELETED ;
    }
}

void DXTextureBase::setTexelsPerImage()
{
    U32 fullwidth = llmin(mFullWidth,U32(MAX_IMAGE_SIZE_DEFAULT));
    U32 fullheight = llmin(mFullHeight,U32(MAX_IMAGE_SIZE_DEFAULT));
    mTexelsPerImage = (U32)fullwidth * fullheight;
}

static LLUUID sStubUUID;

const LLUUID& DXTextureBase::getID() const { return sStubUUID; }
