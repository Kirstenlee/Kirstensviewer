/** 
 * @file llxmlparser.cpp
 * @brief LLXmlParser implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

// llxmlparser.cpp
//              
// copyright 2002, linden research inc


#include "linden_common.h"

#include "llxmlparser.h"
#include "llerror.h"


LLXmlParser::LLXmlParser()
	:
	mParser( NULL ),
	mDepth( 0 )
{
	mAuxErrorString = "no error";

	// Override the document's declared encoding.
	mParser = XML_ParserCreate(NULL);

	XML_SetUserData(mParser, this);
	XML_SetElementHandler(					mParser,	startElementHandler, endElementHandler);
	XML_SetCharacterDataHandler(			mParser,	characterDataHandler);
	XML_SetProcessingInstructionHandler(	mParser,	processingInstructionHandler);
	XML_SetCommentHandler(					mParser,	commentHandler);

	XML_SetCdataSectionHandler(				mParser,	startCdataSectionHandler, endCdataSectionHandler);

	// This sets the default handler but does not inhibit expansion of internal entities.
	// The entity reference will not be passed to the default handler.
	XML_SetDefaultHandlerExpand(			mParser,	defaultDataHandler);
	
	XML_SetUnparsedEntityDeclHandler(		mParser,	unparsedEntityDeclHandler);
}

LLXmlParser::~LLXmlParser()
{
	XML_ParserFree( mParser );
}


bool LLXmlParser::parseFile(const std::string &path)
{
	llassert( !mDepth );
	
    bool success = true;

	LLFILE* file = LLFile::fopen(path, "rb");		/* Flawfinder: ignore */
	if( !file )
	{
		mAuxErrorString = llformat( "Couldn't open file %s", path.c_str());
        success = false;
	}
	else
	{
		S32 bytes_read = 0;
		
		fseek(file, 0L, SEEK_END);
		S32 buffer_size = ftell(file);
		fseek(file, 0L, SEEK_SET);

		void* buffer = XML_GetBuffer(mParser, buffer_size);
		if( !buffer ) 
		{
			mAuxErrorString = llformat( "Unable to allocate XML buffer while reading file %s", path.c_str() );
            success = false;
			goto exit_label;
		}
		
		bytes_read = (S32)fread(buffer, 1, buffer_size, file);
		if( bytes_read <= 0 )
		{
			mAuxErrorString = llformat( "Error while reading file  %s", path.c_str() );
            success = false;
			goto exit_label;
		}
		
        if( !XML_ParseBuffer(mParser, bytes_read, true ) )
		{
			mAuxErrorString = llformat( "Error while parsing file  %s", path.c_str() );
            success = false;
		}

exit_label: 
		fclose( file );
	}


	if( success )
	{
		llassert( !mDepth );
	}
	mDepth = 0;

	if( !success )
	{
		LL_WARNS() << mAuxErrorString << LL_ENDL;
	}

	return success;
}


//	Parses some input. Returns 0 if a fatal error is detected.
//	The last call must have isFinal true;
//	len may be zero for this call (or any other).
S32 LLXmlParser::parse( const char* buf, int len, int isFinal )
{
	return XML_Parse(mParser, buf, len, isFinal);
}

const char* LLXmlParser::getErrorString()
{
	const char* error_string = XML_ErrorString(XML_GetErrorCode( mParser ));
	if( !error_string )
	{
		error_string = mAuxErrorString.c_str();
	}
	return error_string;
}

S32 LLXmlParser::getCurrentLineNumber()
{
	return XML_GetCurrentLineNumber( mParser );
}

S32 LLXmlParser::getCurrentColumnNumber()
{
	return XML_GetCurrentColumnNumber(mParser);
}

///////////////////////////////////////////////////////////////////////////////
// Pseudo-private methods.  These are only used by internal callbacks.

// static 
void LLXmlParser::startElementHandler(
	 void *userData,
	 const XML_Char *name,
	 const XML_Char **atts)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->startElement( name, atts );
	self->mDepth++;
}

// static 
void LLXmlParser::endElementHandler(
	void *userData,
	const XML_Char *name)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->mDepth--;
	self->endElement( name );
}

// s is not 0 terminated.
// static 
void LLXmlParser::characterDataHandler(
	void *userData,
	const XML_Char *s,
	int len)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->characterData( s, len );
}

// target and data are 0 terminated
// static 
void LLXmlParser::processingInstructionHandler(
	void *userData,
	const XML_Char *target,
	const XML_Char *data)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->processingInstruction( target, data );
}

// data is 0 terminated 
// static
void LLXmlParser::commentHandler(void *userData, const XML_Char *data)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->comment( data );
}

// static
void LLXmlParser::startCdataSectionHandler(void *userData)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->mDepth++;
	self->startCdataSection();
}

// static
void LLXmlParser::endCdataSectionHandler(void *userData)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->endCdataSection();
	self->mDepth++;
}

//	This is called for any characters in the XML document for
//	which there is no applicable handler.  This includes both
//	characters that are part of markup which is of a kind that is
//	not reported (comments, markup declarations), or characters
//	that are part of a construct which could be reported but
//	for which no handler has been supplied. The characters are passed
//	exactly as they were in the XML document except that
//	they will be encoded in UTF-8.  Line boundaries are not normalized.
//	Note that a byte order mark character is not passed to the default handler.
//	There are no guarantees about how characters are divided between calls
//	to the default handler: for example, a comment might be split between
//	multiple calls.

// static 
void LLXmlParser::defaultDataHandler(
	void *userData,
	const XML_Char *s,
	int len)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->defaultData( s, len );
}

//	This is called for a declaration of an unparsed (NDATA)
//	entity.  The base argument is whatever was set by XML_SetBase.
//	The entityName, systemId and notationName arguments will never be null.
//	The other arguments may be.
// static 
void LLXmlParser::unparsedEntityDeclHandler(
	void *userData,
	const XML_Char *entityName,
	const XML_Char *base,
	const XML_Char *systemId,
	const XML_Char *publicId,
	const XML_Char *notationName)
{
	LLXmlParser* self = (LLXmlParser*) userData;
	self->unparsedEntityDecl( entityName, base, systemId, publicId, notationName );
}




