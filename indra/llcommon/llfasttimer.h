/**
 * @file llfasttimer.h
 * @brief Declaration of a fast timer.
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

#ifndef LL_FASTTIMER_H
#define LL_FASTTIMER_H

#include "llinstancetracker.h"
#include "lltrace.h"
#include "lltreeiterators.h"
#include "llprocessor.h"


#include <intrin.h> // Always include for Windows-only build


#define LL_FAST_TIMER_ON 1
#define LL_FASTTIMER_USE_RDTSC 1

// NOTE: Also see llprofiler.h
#if !defined(LL_PROFILER_CONFIGURATION)
#define LL_RECORD_BLOCK_TIME(timer_stat) const LLTrace::BlockTimer& LL_GLUE_TOKENS(block_time_recorder, __LINE__)(LLTrace::timeThisBlock(timer_stat)); (void)LL_GLUE_TOKENS(block_time_recorder, __LINE__);
#endif // LL_PROFILER_CONFIGURATION

namespace LLTrace
{
// use to create blocktimer rvalue to be captured in a reference so that the BlockTimer lives to the end of the block.
class BlockTimer timeThisBlock(class BlockTimerStatHandle& timer);

class BlockTimer
{
public:
	typedef BlockTimer self_t;
	typedef class BlockTimerStatHandle DeclareTimer;

	~BlockTimer();

	F64Seconds getElapsedTime();

		//////////////////////////////////////////////////////////////////////////////
	//
	// Important note: These implementations must be FAST!
	//


#if LL_WINDOWS
	//
	// Windows implementation of CPU clock
	//

	//
	// NOTE: put back in when we aren't using platform sdk anymore
	//
	// because MS has different signatures for these functions in winnt.h
	// need to rename them to avoid conflicts
	//#define _interlockedbittestandset _renamed_interlockedbittestandset
	//#define _interlockedbittestandreset _renamed_interlockedbittestandreset
	//#include <intrin.h>
	//#undef _interlockedbittestandset
	//#undef _interlockedbittestandreset

	//inline U32 getCPUClockCount32()
	//{
	//	U64 time_stamp = __rdtsc();
	//	return (U32)(time_stamp >> 8);
	//}
	//
	//// return full timer value, *not* shifted by 8 bits
	//inline U64 getCPUClockCount64()
	//{
	//	return __rdtsc();
	//}

	

	// shift off lower 8 bits for lower resolution but longer term timing
	// on 1Ghz machine, a 32-bit word will hold ~1000 seconds of timing
#if LL_FASTTIMER_USE_RDTSC
	static U32 getCPUClockCount32()
	{
		unsigned __int64 val = __rdtsc();
		val = val >> 8;
		return static_cast<U32>(val);
	}

	// return full timer value, *not* shifted by 8 bits
	static U64 getCPUClockCount64()
	{
		return static_cast<U64>( __rdtsc() );
	}

#else
	//U64 get_clock_count(); // in lltimer.cpp
	// These use QueryPerformanceCounter, which is arguably fine and also works on AMD architectures.
	static U32 getCPUClockCount32()
	{
		return (U32)(get_clock_count()>>8);
	}

	static U64 getCPUClockCount64()
	{
		return get_clock_count();
	}

#endif

#endif


	static BlockTimerStatHandle& getRootTimeBlock();
	static void pushLog(LLSD sd);
	static void setLogLock(class LLMutex* mutex);
	static void writeLog(std::ostream& os);
	static void updateTimes();
	
	static U64 countsPerSecond();

	// updates cumulative times and hierarchy,
	// can be called multiple times in a frame, at any point
	static void processTimes();

	static void bootstrapTimerTree();
	static void incrementalUpdateTimerTree();

	// call this once a frame to periodically log timers
	static void logStats();

	// dumps current cumulative frame stats to log
	// call nextFrame() to reset timers
	static void dumpCurTimes();

private:
	friend class BlockTimerStatHandle;
	// FIXME: this friendship exists so that each thread can instantiate a root timer, 
	// which could be a derived class with a public constructor instead, possibly
	friend class ThreadRecorder; 
	friend BlockTimer timeThisBlock(BlockTimerStatHandle&); 

	BlockTimer(BlockTimerStatHandle& timer);

	// no-copy
	BlockTimer(const BlockTimer& other);
	BlockTimer& operator=(const BlockTimer& other);

private:
	U64						mStartTime;
    BlockTimerStackRecord   mParentTimerData{};

public:
	// statics
	static std::string		sLogName;
	static bool				sMetricLog,
							sLog;	
	static U64				sClockResolution;

};

// this dummy function assists in allocating a block timer with stack-based lifetime.
// this is done by capturing the return value in a stack-allocated const reference variable.  
// (This is most easily done using the macro LL_RECORD_BLOCK_TIME)
// Otherwise, it would be possible to store a BlockTimer on the heap, resulting in non-nested lifetimes, 
// which would break the invariants of the timing hierarchy logic

// Always-inline for lowest overhead (Unreal/Crytek style)
__forceinline class BlockTimer timeThisBlock(class BlockTimerStatHandle& timer)
{
	return BlockTimer(timer);
}

// stores a "named" timer instance to be reused via multiple BlockTimer stack instances
class BlockTimerStatHandle 
:	public StatType<TimeBlockAccumulator>
{
public:
	BlockTimerStatHandle(const char* name, const char* description = "");

	TimeBlockTreeNode& getTreeNode() const;
	BlockTimerStatHandle* getParent() const { return getTreeNode().getParent(); }
	void setParent(BlockTimerStatHandle* parent) { getTreeNode().setParent(parent); }

	typedef std::vector<BlockTimerStatHandle*>::iterator child_iter;
	typedef std::vector<BlockTimerStatHandle*>::const_iterator child_const_iter;
	child_iter beginChildren();
	child_iter endChildren();
	bool hasChildren();
	std::vector<BlockTimerStatHandle*>& getChildren();

	StatType<TimeBlockAccumulator::CallCountFacet>& callCount() 
	{
		return static_cast<StatType<TimeBlockAccumulator::CallCountFacet>&>(*(StatType<TimeBlockAccumulator>*)this);
	}

	StatType<TimeBlockAccumulator::SelfTimeFacet>& selfTime() 
	{
		return static_cast<StatType<TimeBlockAccumulator::SelfTimeFacet>&>(*(StatType<TimeBlockAccumulator>*)this);
	}

	bool						mCollapsed;				// don't show children
};

// iterators and helper functions for walking the call hierarchy of block timers in different ways
typedef LLTreeDFSIter<BlockTimerStatHandle, BlockTimerStatHandle::child_const_iter> block_timer_tree_df_iterator_t;
typedef LLTreeDFSPostIter<BlockTimerStatHandle, BlockTimerStatHandle::child_const_iter> block_timer_tree_df_post_iterator_t;
typedef LLTreeBFSIter<BlockTimerStatHandle, BlockTimerStatHandle::child_const_iter> block_timer_tree_bf_iterator_t;

block_timer_tree_df_iterator_t begin_block_timer_tree_df(BlockTimerStatHandle& id);
block_timer_tree_df_iterator_t end_block_timer_tree_df();
block_timer_tree_df_post_iterator_t begin_block_timer_tree_df_post(BlockTimerStatHandle& id);
block_timer_tree_df_post_iterator_t end_block_timer_tree_df_post();
block_timer_tree_bf_iterator_t begin_block_timer_tree_bf(BlockTimerStatHandle& id);
block_timer_tree_bf_iterator_t end_block_timer_tree_bf();


// Always-inline for lowest overhead (Unreal/Crytek style)
__forceinline BlockTimer::BlockTimer(BlockTimerStatHandle& timer)
{
#if LL_FAST_TIMER_ON
	BlockTimerStackRecord* cur_timer_data = LLThreadLocalSingletonPointer<BlockTimerStackRecord>::getInstance();
	if (!cur_timer_data)
	{
		mStartTime = 0;
		return;
	}
	TimeBlockAccumulator& accumulator = timer.getCurrentAccumulator();
	accumulator.mActiveCount++;
	accumulator.mMoveUpTree |= (accumulator.mParent->getCurrentAccumulator().mActiveCount == 0);
	mParentTimerData = *cur_timer_data;
	cur_timer_data->mActiveTimer = this;
	cur_timer_data->mTimeBlock = &timer;
	cur_timer_data->mChildTime = 0;
	mStartTime = getCPUClockCount64();
#endif
}


// Always-inline for lowest overhead (Unreal/Crytek style)
__forceinline BlockTimer::~BlockTimer()
{
#if LL_FAST_TIMER_ON
	U64 total_time = getCPUClockCount64() - mStartTime;
	BlockTimerStackRecord* cur_timer_data = LLThreadLocalSingletonPointer<BlockTimerStackRecord>::getInstance();
	if (!cur_timer_data) return;
	TimeBlockAccumulator& accumulator = cur_timer_data->mTimeBlock->getCurrentAccumulator();
	accumulator.mCalls++;
	accumulator.mTotalTimeCounter += total_time;
	accumulator.mSelfTimeCounter += total_time - cur_timer_data->mChildTime;
	accumulator.mActiveCount--;
	accumulator.mLastCaller = mParentTimerData.mTimeBlock;
	mParentTimerData.mChildTime += total_time;
	*cur_timer_data = mParentTimerData;
#endif
}

}

typedef LLTrace::BlockTimer LLFastTimer; 

#endif // LL_LLFASTTIMER_H
