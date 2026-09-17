/**
 * @file llprofiler.h
 * @brief Fast-timer recording macro (Tracy and the LL_PROFILE_* zone system removed)
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2021, Linden Research, Inc.
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

#ifndef LL_PROFILER_H
#define LL_PROFILER_H

// LL_RECORD_BLOCK_TIME(FTM_X) is the real, active profiling mechanism -
// records into the LLTrace::BlockTimerStatHandle FTM_X, visible in the
// in-viewer Fast Timers floater. Everything else this project used to
// route through Tracy (LL_PROFILE_ZONE_*, LL_PROFILE_GPU_*, LL_PROFILE_MUTEX*,
// LL_PROFILER_*, LL_LABEL_OBJECT_GL) has been removed entirely, tree-wide -
// Tracy itself was never linked in this build.
#define LL_RECORD_BLOCK_TIME(name) \
    const LLTrace::BlockTimer& LL_GLUE_TOKENS(block_time_recorder, __LINE__)(LLTrace::timeThisBlock(name)); \
    (void)LL_GLUE_TOKENS(block_time_recorder, __LINE__);

#endif // LL_PROFILER_H
