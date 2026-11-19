/**
 * @file   lleventfilter.h
 * @author Nat Goodspeed
 * @date   2009-03-05
 * @brief  Define LLEventFilter: LLEventStream subclass with conditions
 * 
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#if ! defined(LL_LLEVENTFILTER_H)
#define LL_LLEVENTFILTER_H

#include "llevents.h"
#include "stdtypes.h"
#include "lltimer.h"
#include "llsdutil.h"

#include <functional>

class LLEventTimer;
class LLDate;

/**
 * Generic base class
 */
class LL_COMMON_API LLEventFilter: public LLEventStream
{
public:
    /// construct a standalone LLEventFilter
    LLEventFilter(const std::string& name="filter", bool tweak=true):
        LLEventStream(name, tweak)
    {}
    /// construct LLEventFilter and connect it to the specified LLEventPump
    LLEventFilter(LLEventPump& source, const std::string& name="filter", bool tweak=true);

    /// Post an event to all listeners
    virtual bool post(const LLSD& event) = 0;

private:
    LLTempBoundListener mSource;
};

/**
 * LLStoreListener self-registers on the LLEventPump of interest, and
 * unregisters on destruction. As long as it exists, a particular element is
 * extracted from every event that comes through the upstream LLEventPump and
 * stored into the target variable.
 *
 * This is implemented as a subclass of LLEventFilter, though strictly
 * speaking it isn't really a "filter" at all: it never passes incoming events
 * to its own listeners, if any.
 *
 * TBD: A variant based on output iterators that stores and then increments
 * the iterator. Useful with boost::coroutine2!
 */
template <typename T>
class LLStoreListener: public LLEventFilter
{
public:
    // pass target and optional path to element
    LLStoreListener(T& target, const LLSD& path=LLSD(), bool consume=false):
        LLEventFilter("store"),
        mTarget(target),
        mPath(path),
        mConsume(consume)
    {}
    // construct and connect
    LLStoreListener(LLEventPump& source, T& target, const LLSD& path=LLSD(), bool consume=false):
        LLEventFilter(source, "store"),
        mTarget(target),
        mPath(path),
        mConsume(consume)
    {}

    // Calling post() with an LLSD event extracts the element indicated by
    // path, then stores it to mTarget.
    virtual bool post(const LLSD& event)
    {

        // Extract the element specified by 'mPath' from 'event'. To perform a
        // generic type-appropriate store through mTarget, construct an
        // LLSDParam<T> and store that, thus engaging LLSDParam's custom
        // conversions.
        storeTarget(LLSDParam<T>(llsd::drill(event, mPath)));
        return mConsume;
    }

private:
    // This method disambiguates LLStoreListener<LLSD>. Directly assigning
    // some_LLSD_var = LLSDParam<LLSD>(some_LLSD_value);
    // is problematic because the compiler has too many choices: LLSD has
    // multiple assignment operator overloads, and LLSDParam<LLSD> has a
    // templated conversion operator. But LLSDParam<LLSD> can convert to a
    // (const LLSD&) parameter, and LLSD::operator=(const LLSD&) works.
    void storeTarget(const T& value)
    {
        mTarget = value;
    }

    T& mTarget;
    const LLSD mPath;
    const bool mConsume;
};

#endif /* ! defined(LL_LLEVENTFILTER_H) */
