/**
 * @file llprofiler.h
 * @brief Lightweight profiler wrapper (Tracy removed)
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

// For selective category-based profiling, see `llprofilercategories.h`.

#define LL_PROFILER_CONFIG_NONE             0  // No profiling
#define LL_PROFILER_CONFIG_FAST_TIMER       1  // Profiling on: Only Fast Timers
#define LL_PROFILER_CONFIG_TRACY            2  // (tracy removed) Mapped to fast-timer / no-op
#define LL_PROFILER_CONFIG_TRACY_FAST_TIMER 3  // (tracy removed) Mapped to fast-timer

#ifndef LL_PROFILER_CONFIGURATION
#define LL_PROFILER_CONFIGURATION           LL_PROFILER_CONFIG_FAST_TIMER
#endif

extern thread_local bool gProfilerEnabled;

#if LL_PROFILER_CONFIGURATION == LL_PROFILER_CONFIG_NONE

    #define LL_PROFILER_FRAME_END
    #define LL_PROFILER_SET_THREAD_NAME( name ) (void)(name)
    #define LL_RECORD_BLOCK_TIME(name)          (void)(name)
    #define LL_PROFILE_ZONE_NAMED(name)         (void)(name)
    #define LL_PROFILE_ZONE_NAMED_COLOR(name,color) (void)(name); (void)(color)
    #define LL_PROFILE_ZONE_SCOPED              /* no-op */
    #define LL_PROFILE_ZONE_NUM( val )          (void)(val)
    #define LL_PROFILE_ZONE_TEXT(text,size)     (void)(text); (void)(size)
    #define LL_PROFILE_ZONE_ERR(name)           (void)(name)
    #define LL_PROFILE_ZONE_INFO(name)          (void)(name)
    #define LL_PROFILE_ZONE_WARN(name)          (void)(name)

    #define LL_PROFILE_MUTEX(type, varname)                     type varname
    #define LL_PROFILE_MUTEX_NAMED(type, varname, desc)         type varname
    #define LL_PROFILE_MUTEX_SHARED(type, varname)              type varname
    #define LL_PROFILE_MUTEX_SHARED_NAMED(type, varname, desc)  type varname
    #define LL_PROFILE_MUTEX_LOCK(varname)                      /* no-op */

    #define LL_PROFILE_GPU_ZONE(name)        (void)(name)
    #define LL_PROFILE_GPU_ZONEC(name,color) (void)(name); (void)(color)
    #define LL_PROFILER_GPU_COLLECT
    #define LL_PROFILER_GPU_CONTEXT

    #define LL_LABEL_OBJECT_GL(type, name, length, label)

    #define LL_PROFILE_ALLOC(ptr, size)      (void)(ptr); (void)(size)
    #define LL_PROFILE_FREE(ptr)             (void)(ptr)

#elif LL_PROFILER_CONFIGURATION == LL_PROFILER_CONFIG_FAST_TIMER \
   || LL_PROFILER_CONFIGURATION == LL_PROFILER_CONFIG_TRACY \
   || LL_PROFILER_CONFIGURATION == LL_PROFILER_CONFIG_TRACY_FAST_TIMER

    /* S24: Profiler macros configured as no-ops for clean LL merges.
     * LL_RECORD_BLOCK_TIME remains active for S24 fast-timer integration.
     * All LL_PROFILE_* macros compile to nothing, allowing selective conversion
     * to LL_RECORD_BLOCK_TIME on a case-by-case basis when profiling is needed. */

    #define LL_PROFILER_FRAME_END
    #define LL_PROFILER_SET_THREAD_NAME( name ) (void)(name); gProfilerEnabled = false;

    /* Keep existing LLTrace fast-timer integration for S24 block timers */
    #define LL_RECORD_BLOCK_TIME(name) \
        const LLTrace::BlockTimer& LL_GLUE_TOKENS(block_time_recorder, __LINE__)(LLTrace::timeThisBlock(name)); \
        (void)LL_GLUE_TOKENS(block_time_recorder, __LINE__);

    /* All LL profiler macros are no-ops: allows merging LL code without modification */
    #define LL_PROFILE_ZONE_NAMED(name)             /* no-op: convert to LL_RECORD_BLOCK_TIME if profiling needed */
    #define LL_PROFILE_ZONE_NAMED_COLOR(name,color) /* no-op: convert to LL_RECORD_BLOCK_TIME if profiling needed */
    #define LL_PROFILE_ZONE_SCOPED                  /* no-op: convert to LL_RECORD_BLOCK_TIME if profiling needed */

    #define LL_PROFILE_ZONE_NUM( val )              /* no-op */
    #define LL_PROFILE_ZONE_TEXT( text, size )      /* no-op */

    #define LL_PROFILE_ZONE_ERR(name)               /* no-op */
    #define LL_PROFILE_ZONE_INFO(name)              /* no-op */
    #define LL_PROFILE_ZONE_WARN(name)              /* no-op */

    #define LL_PROFILE_MUTEX(type, varname)                     type varname
    #define LL_PROFILE_MUTEX_NAMED(type, varname, desc)         type varname
    #define LL_PROFILE_MUTEX_SHARED(type, varname)              type varname
    #define LL_PROFILE_MUTEX_SHARED_NAMED(type, varname, desc)  type varname
    #define LL_PROFILE_MUTEX_LOCK(varname)                      /* no-op */

    #define LL_PROFILE_GPU_ZONE(name)        /* no-op */
    #define LL_PROFILE_GPU_ZONEC(name,color) /* no-op */
    #define LL_PROFILER_GPU_COLLECT
    #define LL_PROFILER_GPU_CONTEXT

    #define LL_LABEL_OBJECT_GL(type, name, length, label)

    /* Memory tracking disabled */
    #define LL_PROFILE_ALLOC(ptr, size)      /* no-op */
    #define LL_PROFILE_FREE(ptr)             /* no-op */

#else
    #error "Unsupported LL_PROFILER_CONFIGURATION"
#endif

#include "llprofilercategories.h"

#endif // LL_PROFILER_H
