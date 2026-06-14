/**
 * @file llthrottle.cpp
 * @brief LLThrottle class used for network bandwidth control.
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

#include "linden_common.h"

#include "llthrottle.h"
#include "llmath.h"
#include "lldatapacker.h"
#include "message.h"


LLThrottle::LLThrottle(const F32 rate)
{
    mRate = rate;
    mAvailable = 0.f;
    mLookaheadSecs = 0.25f;
    mLastSendTime = LLMessageSystem::getMessageTimeSeconds(true);
}


void LLThrottle::setRate(const F32 rate)
{
    // Need to accumulate available bits when adjusting the rate.
    mAvailable = getAvailable();
    mLastSendTime = LLMessageSystem::getMessageTimeSeconds();
    mRate = rate;
}

F32 LLThrottle::getAvailable()
{
    // use a temporary bits_available
    // since we don't want to change mBitsAvailable every time
    F32Seconds elapsed_time = LLMessageSystem::getMessageTimeSeconds() - mLastSendTime;
    return mAvailable + (mRate * elapsed_time.value());
}

bool LLThrottle::checkOverflow(const F32 amount)
{
    bool retval = true;

    F32 lookahead_amount = mRate * mLookaheadSecs;

    // use a temporary bits_available
    // since we don't want to change mBitsAvailable every time
    F32Seconds elapsed_time =  LLMessageSystem::getMessageTimeSeconds() - mLastSendTime;
    F32 amount_available = mAvailable + (mRate * elapsed_time.value());

    if ((amount_available >= lookahead_amount) || (amount_available > amount))
    {
        // ...enough space to send this message
        // Also do if > lookahead so we can use if amount > capped amount.
        retval = false;
    }

    return retval;
}

bool LLThrottle::throttleOverflow(const F32 amount)
{
    F32Seconds elapsed_time;
    F32 lookahead_amount;
    bool retval = true;

    lookahead_amount = mRate * mLookaheadSecs;

    F64Seconds mt_sec = LLMessageSystem::getMessageTimeSeconds();
    elapsed_time = mt_sec - mLastSendTime;
    mLastSendTime = mt_sec;

    mAvailable += mRate * elapsed_time.value();

    if (mAvailable >= lookahead_amount)
    {
        // ...channel completely open, so allow send regardless
        // of size.  This allows sends on very low BPS channels.
        mAvailable = lookahead_amount;
        retval = false;
    }
    else if (mAvailable > amount)
    {
        // ...enough space to send this message
        retval = false;
    }

    // We actually already sent the bits.
    mAvailable -= amount;

    // What if bitsavailable goes negative?
    // That's OK, because it means someone is banging on the channel,
    // so we need some time to recover.

    return retval;
}



const F32 THROTTLE_LOOKAHEAD_TIME = 1.f;    // seconds

// Make sure that we don't set above these
// values, even if the client asks to be set
// higher
// Note that these values are replicated on the
// client side to set max bandwidth throttling there,
// in llviewerthrottle.cpp. These values are the sum
// of the top two tiers of bandwidth there.
// [S24] Modernized for 2025 fiber connections (was 3.5 Mbps, now 40 Mbps for texture/task)

F32 gThrottleMaximumBPS[TC_EOF] =
{
    500000.f,  // TC_RESEND   = 500 KB/s = 4 Mbps (was 150 KB/s)
    500000.f,  // TC_LAND     = 500 KB/s = 4 Mbps (was 170 KB/s)
    100000.f,  // TC_WIND     = 100 KB/s = 0.8 Mbps (was 34 KB/s)
    100000.f,  // TC_CLOUD    = 100 KB/s = 0.8 Mbps (was 34 KB/s)
    5000000.f, // TC_TASK     = 5 MB/s = 40 Mbps (was 446 KB/s = 3.5 Mbps)
    5000000.f, // TC_TEXTURE  = 5 MB/s = 40 Mbps (was 446 KB/s = 3.5 Mbps)
    2000000.f, // TC_ASSET    = 2 MB/s = 16 Mbps (was 220 KB/s = 1.7 Mbps)
};

// Start low until viewer informs us of capability
// Asset and resend get high values, since they
// aren't used JUST by the viewer necessarily.
// This is a HACK and should be dealt with more properly on
// circuit creation.

F32 gThrottleDefaultBPS[TC_EOF] =
{
    100000.f, // TC_RESEND
    4000.f, // TC_LAND
    4000.f, // TC_WIND
    4000.f, // TC_CLOUD
    4000.f, // TC_TASK
    4000.f, // TC_TEXTURE
    100000.f, // TC_ASSET
};

// Don't throttle down lower than this
// This potentially wastes 50 kbps, but usually
// wont.
// [S24] Raised minimums for modern baseline connections
F32 gThrottleMinimumBPS[TC_EOF] =
{
    50000.f,   // TC_RESEND   = 50 KB/s (was 10 KB/s)
    50000.f,   // TC_LAND     = 50 KB/s (was 10 KB/s)
    10000.f,   // TC_WIND     = 10 KB/s (unchanged, low priority)
    10000.f,   // TC_CLOUD    = 10 KB/s (unchanged, low priority)
    100000.f,  // TC_TASK     = 100 KB/s (was 20 KB/s)
    100000.f,  // TC_TEXTURE  = 100 KB/s (was 10 KB/s)
    50000.f,   // TC_ASSET    = 50 KB/s (was 10 KB/s)
};

const char* THROTTLE_NAMES[TC_EOF] =
{
    "Resend ",
    "Land   ",
    "Wind   ",
    "Cloud  ",
    "Task   ",
    "Texture",
    "Asset  "
};

LLThrottleGroup::LLThrottleGroup()
{
    S32 i;
    for (i = 0; i < TC_EOF; i++)
    {
        mThrottleTotal[i]   = gThrottleDefaultBPS[i];
        mNominalBPS[i]      = gThrottleDefaultBPS[i];
    }

    resetDynamicAdjust();
}

void LLThrottleGroup::packThrottle(LLDataPacker &dp) const
{
    S32 i;
    for (i = 0; i < TC_EOF; i++)
    {
        dp.packF32(mThrottleTotal[i], "Throttle");
    }
}

void LLThrottleGroup::unpackThrottle(LLDataPacker &dp)
{
    S32 i;
    for (i = 0; i < TC_EOF; i++)
    {
        F32 temp_throttle;
        dp.unpackF32(temp_throttle, "Throttle");
        temp_throttle = llclamp(temp_throttle, 0.f, 2250000.f);
        mThrottleTotal[i] = temp_throttle;
        if(mThrottleTotal[i] > gThrottleMaximumBPS[i])
        {
            mThrottleTotal[i] = gThrottleMaximumBPS[i];
        }
    }
}

// Call this whenever mNominalBPS changes.  Need to reset
// the measurement systems.  In the future, we should look
// into NOT resetting the system.
void LLThrottleGroup::resetDynamicAdjust()
{
    F64Seconds mt_sec = LLMessageSystem::getMessageTimeSeconds();
    S32 i;
    for (i = 0; i < TC_EOF; i++)
    {
        mCurrentBPS[i]      = mNominalBPS[i];
        mBitsAvailable[i]   = mNominalBPS[i] * THROTTLE_LOOKAHEAD_TIME;
        mLastSendTime[i] = mt_sec;
        mBitsSentThisPeriod[i] = 0;
        mBitsSentHistory[i] = 0;
    }
    mDynamicAdjustTime = mt_sec;
}


bool LLThrottleGroup::setNominalBPS(F32* throttle_vec)
{
    bool changed = false;
    S32 i;
    for (i = 0; i < TC_EOF; i++)
    {
        if (mNominalBPS[i] != throttle_vec[i])
        {
            changed = true;
            mNominalBPS[i] = throttle_vec[i];
        }
    }

    // If we changed the nominal settings, reset the dynamic
    // adjustment subsystem.
    if (changed)
    {
        resetDynamicAdjust();
    }

    return changed;
}

// Return bits available in the channel
S32     LLThrottleGroup::getAvailable(S32 throttle_cat)
{
    S32 retval = 0;

    F32 category_bps = mCurrentBPS[throttle_cat];
    F32 lookahead_bits = category_bps * THROTTLE_LOOKAHEAD_TIME;

    // use a temporary bits_available
    // since we don't want to change mBitsAvailable every time
    F32Seconds elapsed_time = LLMessageSystem::getMessageTimeSeconds() - mLastSendTime[throttle_cat];
    F32 bits_available = mBitsAvailable[throttle_cat] + (category_bps * elapsed_time.value());

    if (bits_available >= lookahead_bits)
    {
        retval = (S32) gThrottleMaximumBPS[throttle_cat];
    }
    else
    {
        retval = (S32) bits_available;
    }

    return retval;
}


bool LLThrottleGroup::checkOverflow(S32 throttle_cat, F32 bits)
{
    bool retval = true;

    F32 category_bps = mCurrentBPS[throttle_cat];
    F32 lookahead_bits = category_bps * THROTTLE_LOOKAHEAD_TIME;

    // use a temporary bits_available
    // since we don't want to change mBitsAvailable every time
    F32Seconds elapsed_time = LLMessageSystem::getMessageTimeSeconds() - mLastSendTime[throttle_cat];
    F32 bits_available = mBitsAvailable[throttle_cat] + (category_bps * elapsed_time.value());

    if (bits_available >= lookahead_bits)
    {
        // ...channel completely open, so allow send regardless
        // of size.  This allows sends on very low BPS channels.
        mBitsAvailable[throttle_cat] = lookahead_bits;
        retval = false;
    }
    else if ( bits_available > bits )
    {
        // ...enough space to send this message
        retval = false;
    }

    return retval;
}

bool LLThrottleGroup::throttleOverflow(S32 throttle_cat, F32 bits)
{
    F32Seconds elapsed_time;
    F32 category_bps;
    F32 lookahead_bits;
    bool retval = true;

    category_bps = mCurrentBPS[throttle_cat];
    lookahead_bits = category_bps * THROTTLE_LOOKAHEAD_TIME;

    F64Seconds mt_sec = LLMessageSystem::getMessageTimeSeconds();
    elapsed_time = mt_sec - mLastSendTime[throttle_cat];
    mLastSendTime[throttle_cat] = mt_sec;
    mBitsAvailable[throttle_cat] += category_bps * elapsed_time.value();

    if (mBitsAvailable[throttle_cat] >= lookahead_bits)
    {
        // ...channel completely open, so allow send regardless
        // of size.  This allows sends on very low BPS channels.
        mBitsAvailable[throttle_cat] = lookahead_bits;
        retval = false;
    }
    else if ( mBitsAvailable[throttle_cat] > bits )
    {
        // ...enough space to send this message
        retval = false;
    }

    // We actually already sent the bits.
    mBitsAvailable[throttle_cat] -= bits;

    mBitsSentThisPeriod[throttle_cat] += bits;

    // What if bitsavailable goes negative?
    // That's OK, because it means someone is banging on the channel,
    // so we need some time to recover.

    return retval;
}

// S24
bool LLThrottleGroup::dynamicAdjust()
{
    const F32Seconds DYNAMIC_ADJUST_TIME(1.0f);
    const F32 CURRENT_PERIOD_WEIGHT = 0.25f;
    const F32 BUSY_PERCENT = 0.75f;
    const F32 IDLE_PERCENT = 0.70f;
    const F32 TRANSFER_PERCENT = 0.90f;
    const F32 RECOVER_PERCENT = 0.25f;

    F64Seconds mt_sec = LLMessageSystem::getMessageTimeSeconds();
    if ((mt_sec - mDynamicAdjustTime) < DYNAMIC_ADJUST_TIME) return false;
    mDynamicAdjustTime = mt_sec;

    bool channels_busy = false;
    F32 busy_nominal_sum = 0.f, starved_nominal_sum = 0.f;
    F32 pool_bps = 0.f, unused_bps = 0.f;

    bool channel_busy[TC_EOF] = {};
    bool channel_idle[TC_EOF] = {};
    bool channel_over_nominal[TC_EOF] = {};

    for (S32 i = 0; i < TC_EOF; ++i)
    {
        mBitsSentHistory[i] = (mBitsSentHistory[i] == 0)
            ? mBitsSentThisPeriod[i]
            : (1.f - CURRENT_PERIOD_WEIGHT) * mBitsSentHistory[i] + CURRENT_PERIOD_WEIGHT * mBitsSentThisPeriod[i];

            mBitsSentThisPeriod[i] = 0;

            F32 bps_used = mBitsSentHistory[i] / DYNAMIC_ADJUST_TIME.value();
            F32 bps_current = mCurrentBPS[i];
            F32 bps_nominal = mNominalBPS[i];

            channel_busy[i] = (mBitsSentHistory[i] >= BUSY_PERCENT * DYNAMIC_ADJUST_TIME.value() * bps_current);
            channel_idle[i] = (mBitsSentHistory[i] < IDLE_PERCENT * DYNAMIC_ADJUST_TIME.value() * bps_current) && (mBitsAvailable[i] > 0);
            channel_over_nominal[i] = (bps_current > bps_nominal);

            if (channel_busy[i])
            {
                channels_busy = true;
                busy_nominal_sum += bps_nominal;
            }
    }

    if (channels_busy)
    {
        for (S32 i = 0; i < TC_EOF; ++i)
        {
            if (channel_idle[i] || channel_over_nominal[i])
            {
                F32 used_bps = mBitsSentHistory[i] / DYNAMIC_ADJUST_TIME.value();
                used_bps = llmax(used_bps, gThrottleMinimumBPS[i]);

                F32 avail_bps = channel_over_nominal[i]
                    ? llmax(mCurrentBPS[i] - mNominalBPS[i], mCurrentBPS[i] - used_bps)
                        : mCurrentBPS[i] - used_bps;

                    if (avail_bps < 0.f) continue;

                    F32 transfer_bps = avail_bps * TRANSFER_PERCENT;
                    mCurrentBPS[i] -= transfer_bps;
                    pool_bps += transfer_bps;
            }
        }

        for (S32 i = 0; i < TC_EOF; ++i)
        {
            if (channel_busy[i])
            {
                F32 add_bps = pool_bps * (mNominalBPS[i] / busy_nominal_sum);
                mCurrentBPS[i] += add_bps;

                const F32 MAX_BPS = 4 * mNominalBPS[i];
                if (mCurrentBPS[i] > MAX_BPS)
                {
                    F32 overage = mCurrentBPS[i] - MAX_BPS;
                    mCurrentBPS[i] = MAX_BPS;
                    unused_bps += overage;
                }

                if (mCurrentBPS[i] < gThrottleMinimumBPS[i])
                {
                    mCurrentBPS[i] = gThrottleMinimumBPS[i];
                }
            }
        }

        if (unused_bps > 0.f)
        {
            mCurrentBPS[TC_TASK] += unused_bps;
        }
    }
    else
    {
        for (S32 i = 0; i < TC_EOF; ++i)
        {
            if (mCurrentBPS[i] > mNominalBPS[i])
            {
                F32 avail_bps = mCurrentBPS[i] - mNominalBPS[i];
                F32 transfer_bps = avail_bps * RECOVER_PERCENT;
                mCurrentBPS[i] -= transfer_bps;
                pool_bps += transfer_bps;
            }
        }

        for (S32 i = 0; i < TC_EOF; ++i)
        {
            if (mCurrentBPS[i] < mNominalBPS[i])
            {
                starved_nominal_sum += mNominalBPS[i];
            }
        }

        for (S32 i = 0; i < TC_EOF; ++i)
        {
            if (mCurrentBPS[i] < mNominalBPS[i])
            {
                mCurrentBPS[i] += pool_bps * (mNominalBPS[i] / starved_nominal_sum);
            }
        }
    }

    return true;
}
