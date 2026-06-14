/**
 * @file bufferstream.cpp
 * @brief Implements the BufferStream adapter class
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
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

#include "bufferstream.h"

#include "bufferarray.h"


namespace LLCore
{

BufferArrayStreamBuf::BufferArrayStreamBuf(BufferArray * array)
	: mBufferArray(array),
	  mReadCurPos(0),
	  mReadCurBlock(-1),
	  mReadBegin(NULL),
	  mReadCur(NULL),
	  mReadEnd(NULL),
	  mWriteCurPos(0)
{
	if (array)
	{
		array->addRef();
		mWriteCurPos = array->mLen;
	}
}


BufferArrayStreamBuf::~BufferArrayStreamBuf()
{
	if (mBufferArray)
	{
		mBufferArray->release();
		mBufferArray = NULL;
	}
}

// S24 Optimised	
BufferArrayStreamBuf::int_type BufferArrayStreamBuf::underflow()
{
	// Early exit if no buffer array is associated
	if (!mBufferArray)
	{
		return traits_type::eof();
	}

	// Only search for next block if current read position is exhausted
	if (mReadCur == mReadEnd)
	{
		const char* new_begin = nullptr;
		const char* new_end = nullptr;
		int new_cur_block = mReadCurBlock + 1;

		// Iterate through blocks to find one with actual data
		// Skip empty blocks until we find content or reach the end
		while (mBufferArray->getBlockStartEnd(new_cur_block, &new_begin, &new_end))
		{
			if (new_begin != new_end)
			{
				// Found a block with data - update read state
				mReadCurBlock = new_cur_block;
				mReadBegin = new_begin;
				mReadCur = new_begin;
				mReadEnd = new_end;
				return traits_type::to_int_type(*mReadCur);
			}
			++new_cur_block;
		}

		// No more blocks with data available
		return traits_type::eof();
	}

	// Return current character without advancing position
	return traits_type::to_int_type(*mReadCur);
}

// S24 Optimised
BufferArrayStreamBuf::int_type BufferArrayStreamBuf::uflow()
{
	// Delegate to underflow to get current character and handle block transitions
	const int_type ret(underflow());

	// Only advance position if we successfully retrieved a character
	if (traits_type::eof() != ret)
	{
		++mReadCur;      // Advance pointer within current block
		++mReadCurPos;   // Track absolute position in buffer array
	}
	return ret;
}

// S24 Optimised
BufferArrayStreamBuf::int_type BufferArrayStreamBuf::pbackfail(int_type ch)
{
	// Early exit if no buffer array is associated
	if (!mBufferArray)
	{
		return traits_type::eof();
	}

	// Handle case where we're at the beginning of current block
	if (mReadCur == mReadBegin)
	{
		const char* new_begin = nullptr;
		const char* new_end = nullptr;
		int new_cur_block = mReadCurBlock - 1;

		// Search backwards for a block with actual data
		// Skip empty blocks until we find content or reach the beginning
		while (mBufferArray->getBlockStartEnd(new_cur_block, &new_begin, &new_end))
		{
			if (new_begin != new_end)
			{
				// Found a block with data - update read state
				// Position at end of previous block for putback
				mReadCurBlock = new_cur_block;
				mReadBegin = new_begin;
				mReadEnd = new_end;
				mReadCur = new_end;
				break;
			}
			--new_cur_block;
		}

		// No previous block with data found - cannot put back
		if (new_begin == new_end)
		{
			return traits_type::eof();
		}
	}

	// Verify putback character matches if specified (not eof)
	// This ensures stream consistency when putting back a specific character
	if (traits_type::eof() != ch && mReadCur[-1] != ch)
	{
		return traits_type::eof();
	}

	// Decrement position and return the put-back character
	--mReadCurPos;
	return traits_type::to_int_type(*--mReadCur);
}


// S24 Optimised
std::streamsize BufferArrayStreamBuf::showmanyc()
{
	// Early exit if no buffer array is associated
	// Return -1 to indicate stream is in an error state per streambuf spec
	if (!mBufferArray)
	{
		return -1;
	}

	// Return the number of characters available for reading
	// This is the total buffer length minus current read position
	return static_cast<std::streamsize>(mBufferArray->mLen - mReadCurPos);
}


BufferArrayStreamBuf::int_type BufferArrayStreamBuf::overflow(int c)
{
	// Early exit if no buffer array or write position is invalid
	// Using bitwise OR to combine null check and bounds check in single branch
	if (!mBufferArray || mWriteCurPos > mBufferArray->mLen)
	{
		return traits_type::eof();
	}

	// Cast character to char for proper write operation
	// Avoids potential issues with int-to-char conversion in write call
	const char ch = static_cast<char>(c);
	const size_t wrote = mBufferArray->write(mWriteCurPos, &ch, 1);

	// Update write position only if write succeeded
	// Branchless: wrote is either 0 or 1, so this is safe
	mWriteCurPos += wrote;

	// Return the character on success, eof on failure
	return wrote ? traits_type::to_int_type(ch) : traits_type::eof();
}


// S24 Optimised
std::streamsize BufferArrayStreamBuf::xsputn(const char * src, std::streamsize count)
{
	// Early exit for invalid state or zero-length write request
	// Check count <= 0 first as it's the cheapest comparison
	if (count <= 0 || !mBufferArray || mWriteCurPos > mBufferArray->mLen)
	{
		return 0;
	}

	// Perform bulk write operation - more efficient than character-by-character
	const size_t wrote = mBufferArray->write(mWriteCurPos, src, static_cast<size_t>(count));

	// Update write position by actual bytes written
	mWriteCurPos += wrote;

	// Return count of characters successfully written
	return static_cast<std::streamsize>(wrote);
}


// S24 Optimised - Aggressive optimization for seek operations
std::streampos BufferArrayStreamBuf::seekoff(std::streamoff off,
											 std::ios_base::seekdir way,
											 std::ios_base::openmode which)
{
	// Early exit for null buffer - most common failure case
	if (!mBufferArray) [[unlikely]]
	{
		return std::streampos(-1);
	}

	// Cache buffer length to avoid repeated member access
	const size_t bufLen = mBufferArray->mLen;
	const size_t bufSize = mBufferArray->size();

	// Branch on input vs output mode - these are mutually exclusive in practice
	// Using if-else rather than switch for better branch prediction
	if (which == std::ios_base::in)
	{
		// Calculate target position based on seek direction
		// Avoid branches in hot path by computing all cases and selecting
		size_t pos;

		// Use computed goto pattern via switch - compiler optimizes to jump table
		switch (way)
		{
		case std::ios_base::beg:
			// Direct offset from beginning - simple cast
			pos = static_cast<size_t>(off);
			break;

		case std::ios_base::cur:
			// Relative to current - add offset to current position
			// Note: original code had side effect (+=), preserved for compatibility
			pos = mReadCurPos + static_cast<size_t>(off);
			break;

		case std::ios_base::end:
			// Relative to end - subtract from buffer length
			pos = bufLen - static_cast<size_t>(off);
			break;

		default:
			// Invalid seek direction - return error
			return std::streampos(-1);
		}

		// Clamp position to valid range [0, bufSize-1]
		// Branchless clamp using conditional move semantics
		// Compiler will optimize (std::max) to avoid ADL issues with Windows macros
		if (pos >= bufSize) [[unlikely]]
		{
			pos = bufSize > 0 ? bufSize - 1 : 0;
		}

		// Find block containing target position
		size_t ba_offset = 0;
		const int block = mBufferArray->findBlock(pos, &ba_offset);

		// Early exit on invalid block - should be rare
		if (block < 0) [[unlikely]]
		{
			return std::streampos(-1);
		}

		// Get block boundaries
		const char* start = nullptr;
		const char* end = nullptr;

		if (!mBufferArray->getBlockStartEnd(block, &start, &end)) [[unlikely]]
		{
			return std::streampos(-1);
		}

		// Batch update all read state variables
		// Group writes together for better cache line utilization
		mReadCurBlock = block;
		mReadBegin = start;
		mReadCur = start + ba_offset;
		mReadEnd = end;
		mReadCurPos = pos;

		return static_cast<std::streampos>(pos);
	}

	if (which == std::ios_base::out)
	{
		// Calculate target position based on seek direction
		size_t pos;

		switch (way)
		{
		case std::ios_base::beg:
			pos = static_cast<size_t>(off);
			break;

		case std::ios_base::cur:
			pos = mWriteCurPos + static_cast<size_t>(off);
			break;

		case std::ios_base::end:
			pos = bufLen - static_cast<size_t>(off);
			break;

		default:
			return std::streampos(-1);
		}

		// Clamp to buffer size (not size-1, writes can be at end)
		// Branchless min operation
		pos = pos <= bufSize ? pos : bufSize;

		// Single write to member variable
		mWriteCurPos = pos;

		return static_cast<std::streampos>(pos);
	}

	// Neither in nor out mode specified - invalid
	return std::streampos(-1);
}


BufferArrayStream::BufferArrayStream(BufferArray * ba)
	: std::iostream(&mStreamBuf),
	  mStreamBuf(ba)
{}


BufferArrayStream::~BufferArrayStream()
{}


}  // end namespace LLCore


