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


LLDXTexture::LLDXTexture(bool usemipmaps)
{
    init();
    mUseMipMaps = usemipmaps;
}

LLDXTexture::LLDXTexture(const U32 width, const U32 height, const U8 components, bool usemipmaps)
{
    init();
    mFullWidth = width ;
    mFullHeight = height ;
    mUseMipMaps = usemipmaps;
    mComponents = components ;
    setTexelsPerImage();
}

LLDXTexture::LLDXTexture(const LLImageRaw* raw, bool usemipmaps)
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

LLDXTexture::~LLDXTexture()
{
    cleanup();
}

void LLDXTexture::init()
{
    mBoostLevel = LLDXTexture::BOOST_NONE;

    mFullWidth = 0;
    mFullHeight = 0;
    mTexelsPerImage = 0 ;
    mUseMipMaps = false ;
    mComponents = 0 ;

    mTextureState = NO_DELETE ;
    mDontDiscard = false;
    mNeedsGLTexture = false ;
}

void LLDXTexture::cleanup()
{
    if(mGLTexturep) mGLTexturep->cleanup(); // S24 
    
}

// virtual
void LLDXTexture::dump()
{
	if(mGLTexturep) mGLTexturep->dump(); // S24
    
}

void LLDXTexture::setBoostLevel(S32 level)
{
    if(mBoostLevel != level)
    {
        mBoostLevel = level ;
        if(mBoostLevel != LLDXTexture::BOOST_NONE
           && mBoostLevel != LLDXTexture::BOOST_ICON
           && mBoostLevel != LLDXTexture::BOOST_THUMBNAIL
           && mBoostLevel != LLDXTexture::BOOST_TERRAIN)
        {
            setNoDelete() ;
        }
    }
}

void LLDXTexture::forceActive()
{
    mTextureState = ACTIVE ;
}

void LLDXTexture::setActive() 
{ 
	if(mTextureState != NO_DELETE)
	{
		mTextureState = ACTIVE ; 
	}
}

//set the texture to stay in memory
void LLDXTexture::setNoDelete() 
{ 
	mTextureState = NO_DELETE ;
}

void LLDXTexture::generateGLTexture() 
{	
	if(mGLTexturep.isNull())
	{
		mGLTexturep = new LLImageDX(mFullWidth, mFullHeight, mComponents, mUseMipMaps) ;
	}
}

LLImageDX* LLDXTexture::getGLTexture() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep ;
}

bool LLDXTexture::createGLTexture()
{
    if(mGLTexturep.isNull())
    {
        generateGLTexture() ;
    }

    return mGLTexturep->createGLTexture() ;
}

bool LLDXTexture::createGLTexture(S32 discard_level, const LLImageRaw* imageraw, S32 usename, bool to_create, S32 category, bool defer_copy, LLGLuint* tex_name)
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

void LLDXTexture::setExplicitFormat(LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format, bool swap_bytes)
{
    llassert(mGLTexturep.notNull()) ;

    mGLTexturep->setExplicitFormat(internal_format, primary_format, type_format, swap_bytes) ;
}
void LLDXTexture::setAddressMode(LLTexUnit::eTextureAddressMode mode)
{
    llassert(mGLTexturep.notNull()) ;
    mGLTexturep->setAddressMode(mode) ;
}
void LLDXTexture::setFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
    llassert(mGLTexturep.notNull()) ;
	mGLTexturep->setFilteringOption(option) ;
}

//virtual
S32	LLDXTexture::getWidth(S32 discard_level) const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getWidth(discard_level) ;
}

//virtual
S32	LLDXTexture::getHeight(S32 discard_level) const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getHeight(discard_level) ;
}

S32 LLDXTexture::getMaxDiscardLevel() const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getMaxDiscardLevel() ;
}
S32 LLDXTexture::getDiscardLevel() const
{
	llassert(mGLTexturep.notNull()) ;
	return mGLTexturep->getDiscardLevel() ;
}
S8  LLDXTexture::getComponents() const 
{ 
	llassert(mGLTexturep.notNull()) ;
	
	return mGLTexturep->getComponents() ;
}

LLGLuint LLDXTexture::getTexName() const 
{ 
	llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getTexName() ;
}

bool LLDXTexture::hasGLTexture() const
{
    if(mGLTexturep.notNull())
    {
        return mGLTexturep->getHasGLTexture() ;
    }
    return false ;
}

bool LLDXTexture::getBoundRecently() const
{
    if(mGLTexturep.notNull())
    {
        return mGLTexturep->getBoundRecently() ;
    }
    return false ;
}

LLTexUnit::eTextureType LLDXTexture::getTarget(void) const
{
    llassert(mGLTexturep.notNull()) ;
    return mGLTexturep->getTarget() ;
}

bool LLDXTexture::setSubImage(const LLImageRaw* imageraw, S32 x_pos, S32 y_pos, S32 width, S32 height, LLGLuint use_name)
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->setSubImage(imageraw, x_pos, y_pos, width, height, 0, use_name) ;
}

bool LLDXTexture::setSubImage(const U8* datap, S32 data_width, S32 data_height, S32 x_pos, S32 y_pos, S32 width, S32 height, LLGLuint use_name)
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->setSubImage(datap, data_width, data_height, x_pos, y_pos, width, height, 0, use_name) ;
}

void LLDXTexture::setGLTextureCreated (bool initialized)
{
    llassert(mGLTexturep.notNull()) ;

	mGLTexturep->setGLTextureCreated (initialized) ;
}

void  LLDXTexture::setCategory(S32 category) 
{
	llassert(mGLTexturep.notNull()) ;

	mGLTexturep->setCategory(category) ;
}

void LLDXTexture::setTexName(LLGLuint texName)
{
    llassert(mGLTexturep.notNull());
    return mGLTexturep->setTexName(texName); 
}

void LLDXTexture::setTarget(const LLGLenum target, const LLTexUnit::eTextureType bind_target)
{
    llassert(mGLTexturep.notNull());
    return mGLTexturep->setTarget(target, bind_target); 
}

LLTexUnit::eTextureAddressMode LLDXTexture::getAddressMode(void) const
{
	llassert(mGLTexturep.notNull()) ;

	return mGLTexturep->getAddressMode() ;
}

S32Bytes LLDXTexture::getTextureMemory() const
{
	llassert(mGLTexturep.notNull()) ;

	return mGLTexturep->mTextureMemory ;
}

LLGLenum LLDXTexture::getPrimaryFormat() const
{
	llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getPrimaryFormat() ;
}

bool LLDXTexture::getIsAlphaMask() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getIsAlphaMask() ;
}

bool LLDXTexture::getMask(const LLVector2 &tc)
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getMask(tc) ;
}

F32 LLDXTexture::getTimePassedSinceLastBound()
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getTimePassedSinceLastBound() ;
}
bool LLDXTexture::getMissed() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->getMissed() ;
}

void LLDXTexture::forceUpdateBindStats(void) const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->forceUpdateBindStats() ;
}

bool LLDXTexture::isGLTextureCreated() const
{
    llassert(mGLTexturep.notNull()) ;

    return mGLTexturep->isGLTextureCreated() ;
}

void LLDXTexture::destroyGLTexture()
{
    if(mGLTexturep.notNull() && mGLTexturep->getHasGLTexture())
    {
        mGLTexturep->destroyGLTexture() ;
        mTextureState = DELETED ;
    }
}

void LLDXTexture::setTexelsPerImage()
{
    U32 fullwidth = llmin(mFullWidth,U32(MAX_IMAGE_SIZE_DEFAULT));
    U32 fullheight = llmin(mFullHeight,U32(MAX_IMAGE_SIZE_DEFAULT));
    mTexelsPerImage = (U32)fullwidth * fullheight;
}

static LLUUID sStubUUID;

const LLUUID& LLDXTexture::getID() const { return sStubUUID; }
