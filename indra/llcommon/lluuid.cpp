/**
 * @file lluuid.cpp
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

#if LL_WINDOWS
#include "llwin32headers.h"
#include <iphlpapi.h>
#include <nb30.h>
#endif

#include "llapp.h"
#include "lldefs.h"
#include "llerror.h"

#include "lluuid.h"
#include "llerror.h"
#include "llrand.h"
#include "llstring.h"
#include "lltimer.h"
#include "llthread.h"
#include "llmutex.h"
#include "llmd5.h"
#include "hbxxh.h"

#include <atomic>
#include <cstring>

const LLUUID LLUUID::null;
const LLTransactionID LLTransactionID::tnull;

// static 
LLMutex* LLUUID::mMutex = NULL;

namespace
{
    // S24 : fixed-format lowercase hex, "8-4-4-4-12", written straight into the output with no printf machinery
    inline void uuid_to_hex(const U8* d, char* out)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
            {
                *out++ = '-';
            }
            *out++ = kHex[d[i] >> 4];
            *out++ = kHex[d[i] & 0x0f];
        }
    }
}

void LLUUID::to_chars(char* out) const
{
    uuid_to_hex(mData, out);
    out[UUID_STR_LENGTH - 1] = '\0';
}

void LLUUID::to_wchars(wchar_t* out) const
{
    swprintf(out, UUID_STR_LENGTH, L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   (U8)(mData[0]),
                   (U8)(mData[1]),
                   (U8)(mData[2]),
                   (U8)(mData[3]),
                   (U8)(mData[4]),
                   (U8)(mData[5]),
                   (U8)(mData[6]),
                   (U8)(mData[7]),
                   (U8)(mData[8]),
                   (U8)(mData[9]),
                   (U8)(mData[10]),
                   (U8)(mData[11]),
                   (U8)(mData[12]),
                   (U8)(mData[13]),
                   (U8)(mData[14]),
                   (U8)(mData[15]));
}

// Common to all UUID implementations
void LLUUID::toString(std::string& out) const
{
    char buf[UUID_STR_LENGTH];
    uuid_to_hex(mData, buf);
    out.assign(buf, UUID_STR_LENGTH - 1);
}

std::string LLUUID::getString() const
{
    return asString();
}

std::string LLUUID::asString() const
{
    std::string str;
    toString(str);
    return str;
}

bool LLUUID::set(const char* in_string, bool emit)
{
    return set(ll_safe_string(in_string), emit);
}

bool LLUUID::set(const std::string& in_string, bool emit)
{
    bool broken_format = false;

    // empty strings should make NULL uuid
    if (in_string.empty())
    {
        setNull();
        return true;
    }

    if (in_string.length() != (UUID_STR_LENGTH - 1))		/* Flawfinder: ignore */
    {
        // I'm a moron.  First implementation didn't have the right UUID format.
        // Shouldn't see any of these any more
        if (in_string.length() == (UUID_STR_LENGTH - 2))	/* Flawfinder: ignore */
        {
            if (emit)
            {
                // S24: LL_DEBUGS not WARNS - not actionable by the user, log poison.
                LL_DEBUGS() << "Warning! Using broken UUID string format" << LL_ENDL;
            }
            broken_format = true;
        }
        else
        {
            // Bad UUID string.  Spam as INFO, as most cases we don't care.
            if (emit)
            {
                //don't spam the logs because a resident can't spell.
                LL_WARNS() << "Bad UUID string: " << in_string << LL_ENDL;
            }
            setNull();
            return false;
        }
    }

    U8 cur_pos = 0;
    S32 i;
    for (i = 0; i < UUID_BYTES; i++)
    {
        if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
        {
            cur_pos++;
            if (broken_format && (i == 10))
            {
                // Missing - in the broken format
                cur_pos--;
            }
        }

        mData[i] = 0;

        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
            mData[i] += (U8)(in_string[cur_pos] - '0');
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <= 'f'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'a');
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <= 'F'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'A');
        }
        else
        {
            if (emit)
            {
                // S24: same as the broken-format case above.
                LL_DEBUGS() << "Invalid UUID string character" << LL_ENDL;
            }
            setNull();
            return false;
        }

        mData[i] = mData[i] << 4;
        cur_pos++;

        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
            mData[i] += (U8)(in_string[cur_pos] - '0');
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <= 'f'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'a');
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <= 'F'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'A');
        }
        else
        {
            if (emit)
            {
                // S24: same as the broken-format case above.
                LL_DEBUGS() << "Invalid UUID string character" << LL_ENDL;
            }
            setNull();
            return false;
        }
        cur_pos++;
    }

    return true;
}

bool LLUUID::validate(const std::string& in_string)
{
    bool broken_format = false;
    if (in_string.length() != (UUID_STR_LENGTH - 1))        /* Flawfinder: ignore */
    {
        // I'm a moron.  First implementation didn't have the right UUID format.
        if (in_string.length() == (UUID_STR_LENGTH - 2))        /* Flawfinder: ignore */
        {
            broken_format = true;
        }
        else
        {
            return false;
        }
    }

    U8 cur_pos = 0;
    for (U32 i = 0; i < 16; i++)
    {
        if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
        {
            cur_pos++;
            if (broken_format && (i == 10))
            {
                // Missing - in the broken format
                cur_pos--;
            }
        }

        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <= 'f'))
        {
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <= 'F'))
        {
        }
        else
        {
            return false;
        }

        cur_pos++;

        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <= 'f'))
        {
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <= 'F'))
        {
        }
        else
        {
            return false;
        }
        cur_pos++;
    }
    return true;
}

const LLUUID& LLUUID::operator^=(const LLUUID& rhs)
{
    // S24 : byte-wise, no unaligned/aliasing U32 cast
    for (S32 i = 0; i < UUID_BYTES; ++i)
    {
        mData[i] ^= rhs.mData[i];
    }
    return *this;
}

LLUUID LLUUID::operator^(const LLUUID& rhs) const
{
    LLUUID id(*this);
    id ^= rhs;
    return id;
}

// WARNING: this algorithm SHALL NOT be changed. It is also used by the server
// and plays a role in some assets validation (e.g. clothing items). Changing
// it would cause invalid assets.
void LLUUID::combine(const LLUUID& other, LLUUID& result) const
{
    LLMD5 md5_uuid;
    md5_uuid.update((unsigned char*)mData, 16);
    md5_uuid.update((unsigned char*)other.mData, 16);
    md5_uuid.finalize();
    md5_uuid.raw_digest(result.mData);
}

LLUUID LLUUID::combine(const LLUUID& other) const
{
    LLUUID combination;
    combine(other, combination);
    return combination;
}

std::ostream& operator<<(std::ostream& s, const LLUUID& uuid)
{
    char uuid_str[UUID_STR_LENGTH];
    uuid.to_chars(uuid_str);
    s << uuid_str;
    return s;
}

std::istream& operator>>(std::istream& s, LLUUID& uuid)
{
    U32 i;
    char uuid_str[UUID_STR_LENGTH] = {};		/* Flawfinder: ignore */
    for (i = 0; i < UUID_STR_LENGTH - 1; i++)
    {
        s >> uuid_str[i];
    }
    uuid_str[i] = '\0';
    // S24 : a failed read must not parse a half-filled buffer
    if (s.fail())
    {
        uuid.setNull();
        return s;
    }
    uuid.set(std::string(uuid_str));
    return s;
}

static void get_random_bytes(void* buf, int nbytes)
{
    int i;
    char* cp = (char*)buf;

    // *NOTE: ll_rand() generates at least 3 good bytes of data since it is
    // 0 to RAND_MAX. This could be made more efficient by copying all the
    // bytes.
    for (i = 0; i < nbytes; i++)
        * cp++ = ll_rand() & 0xFF;
    return;
}

typedef struct _ASTAT_
{
    ADAPTER_STATUS adapt;
    NAME_BUFFER    NameBuff[30];
}ASTAT, * PASTAT;

// static
S32	LLUUID::getNodeID(unsigned char* node_id)
{
    ASTAT Adapter;
    NCB Ncb;
    UCHAR uRetCode;
    LANA_ENUM   lenum = {};
    int      i;
    int retval = 0;

    memset(&Ncb, 0, sizeof(Ncb));
    Ncb.ncb_command = NCBENUM;
    Ncb.ncb_buffer = (UCHAR*)&lenum;
    Ncb.ncb_length = sizeof(lenum);
    uRetCode = Netbios(&Ncb);

    for (i = 0; i < lenum.length; i++)
    {
        memset(&Ncb, 0, sizeof(Ncb));
        Ncb.ncb_command = NCBRESET;
        Ncb.ncb_lana_num = lenum.lana[i];

        uRetCode = Netbios(&Ncb);

        memset(&Ncb, 0, sizeof(Ncb));
        Ncb.ncb_command = NCBASTAT;
        Ncb.ncb_lana_num = lenum.lana[i];

        strcpy((char*)Ncb.ncb_callname, "*              ");		/* Flawfinder: ignore */
        Ncb.ncb_buffer = (unsigned char*)&Adapter;
        Ncb.ncb_length = sizeof(Adapter);

        uRetCode = Netbios(&Ncb);
        if (uRetCode == 0)
        {
            memcpy(node_id, Adapter.adapt.adapter_address, 6);		/* Flawfinder: ignore */
            retval = 1;
        }
    }
    return retval;
}

S32 LLUUID::cmpTime(uuid_time_t* t1, uuid_time_t* t2)
{
    // Compare two time values.

    if (t1->high < t2->high) return -1;
    if (t1->high > t2->high) return 1;
    if (t1->low < t2->low)  return -1;
    if (t1->low > t2->low)  return 1;
    return 0;
}

void LLUUID::getSystemTime(uuid_time_t* timestamp)
{
    // Get system time with 100ns precision. Time is since Oct 15, 1582.
#if LL_WINDOWS
    ULARGE_INTEGER time;
    GetSystemTimeAsFileTime((FILETIME*)&time);
    // NT keeps time in FILETIME format which is 100ns ticks since
    // Jan 1, 1601. UUIDs use time in 100ns ticks since Oct 15, 1582.
    // The difference is 17 Days in Oct + 30 (Nov) + 31 (Dec)
    // + 18 years and 5 leap days.
    time.QuadPart +=
        (unsigned __int64)(1000 * 1000 * 10)       // seconds
        * (unsigned __int64)(60 * 60 * 24)       // days
        * (unsigned __int64)(17 + 30 + 31 + 365 * 18 + 5); // # of days

    timestamp->high = time.HighPart;
    timestamp->low = time.LowPart;
#else
    struct timeval tp;
    gettimeofday(&tp, 0);

    // Offset between UUID formatted times and Unix formatted times.
    // UUID UTC base time is October 15, 1582.
    // Unix base time is January 1, 1970.
    U64 uuid_time = ((U64)tp.tv_sec * 10000000) + (tp.tv_usec * 10) +
        U64L(0x01B21DD213814000);
    timestamp->high = (U32)(uuid_time >> 32);
    timestamp->low = (U32)(uuid_time & 0xFFFFFFFF);
#endif
}

void LLUUID::getCurrentTime(uuid_time_t* timestamp)
{
    // Get current time as 60 bit 100ns ticks since whenever.
    // Compensate for the fact that real clock resolution is less
    // than 100ns.

    const U32 uuids_per_tick = 1024;

    static uuid_time_t time_last;
    static U32    uuids_this_tick;
    static bool     init = false;

    if (!init) {
        getSystemTime(&time_last);
        uuids_this_tick = uuids_per_tick;
        init = true;
        mMutex = new LLMutex();
    }

    uuid_time_t time_now = { 0,0 };

    while (1) {
        getSystemTime(&time_now);

        // if clock reading changed since last UUID generated
        if (cmpTime(&time_last, &time_now)) {
            // reset count of uuid's generated with this clock reading
            uuids_this_tick = 0;
            break;
        }
        if (uuids_this_tick < uuids_per_tick) {
            uuids_this_tick++;
            break;
        }
        // going too fast for our clock; spin
    }

    time_last = time_now;

    if (uuids_this_tick != 0) {
        if (time_now.low & 0x80000000) {
            time_now.low += uuids_this_tick;
            if (!(time_now.low & 0x80000000))
                time_now.high++;
        }
        else
            time_now.low += uuids_this_tick;
    }

    timestamp->high = time_now.high;
    timestamp->low = time_now.low;
}

void LLUUID::generate()
{
    // Create a UUID.
    uuid_time_t timestamp;

    static unsigned char node_id[6];	/* Flawfinder: ignore */
    static int has_init = 0;

    // Create a UUID.
    static uuid_time_t time_last = { 0,0 };
    static U16 clock_seq = 0;
    if (!has_init)
    {
        has_init = 1;
        if (getNodeID(node_id) <= 0)
        {
            get_random_bytes(node_id, 6);
            /*
             * Set multicast bit, to prevent conflicts
             * with IEEE 802 addresses obtained from
             * network cards
             */
            node_id[0] |= 0x80;
        }

        getCurrentTime(&time_last);

        clock_seq = (U16)ll_rand(65536);
    }

    // get current time
    getCurrentTime(&timestamp);
    U16 our_clock_seq = clock_seq;

    // if clock hasn't changed or went backward, change clockseq
    if (cmpTime(&timestamp, &time_last) != 1)
    {
        LLMutexLock	lock(mMutex);
        clock_seq = (clock_seq + 1) & 0x3FFF;
        if (clock_seq == 0)
            clock_seq++;
        our_clock_seq = clock_seq;	// Ensure we're using a different clock_seq value from previous time
    }

    time_last = timestamp;

    memcpy(mData + 10, node_id, 6);		/* Flawfinder: ignore */
    U32 tmp;
    tmp = timestamp.low;
    mData[3] = (unsigned char)tmp;
    tmp >>= 8;
    mData[2] = (unsigned char)tmp;
    tmp >>= 8;
    mData[1] = (unsigned char)tmp;
    tmp >>= 8;
    mData[0] = (unsigned char)tmp;

    tmp = (U16)timestamp.high;
    mData[5] = (unsigned char)tmp;
    tmp >>= 8;
    mData[4] = (unsigned char)tmp;

    tmp = (timestamp.high >> 16) | 0x1000;
    mData[7] = (unsigned char)tmp;
    tmp >>= 8;
    mData[6] = (unsigned char)tmp;

    tmp = our_clock_seq;

    mData[9] = (unsigned char)tmp;
    tmp >>= 8;
    mData[8] = (unsigned char)tmp;

    HBXXH128::digest(*this, (const void*)mData, 16);
}

void LLUUID::generate(const std::string& hash_string)
{
    HBXXH128::digest(*this, hash_string);
}

U32 LLUUID::getRandomSeed()
{
    // S24 : local buffer plus per-call counter, so concurrent callers (e.g.
    // per-thread RNG seeding) never share a buffer or get identical seeds
    static std::atomic<U32> sCounter{ 0 };
    unsigned char seed[20] = {};		/* Flawfinder: ignore */

    getNodeID(&seed[0]);

    // Incorporate the pid into the seed to prevent
    // processes that start on the same host at the same
    // time from generating the same seed.
    pid_t pid = LLApp::getPid();

    seed[6] = (unsigned char)(pid >> 8);
    seed[7] = (unsigned char)(pid);

    uuid_time_t now;
    getSystemTime(&now);
    memcpy(&seed[8], &now, sizeof(now));

    const U32 counter = sCounter.fetch_add(1, std::memory_order_relaxed);
    memcpy(&seed[16], &counter, sizeof(counter));

   U64 seed64 = HBXXH64::digest((const void*)seed, sizeof(seed));
   return U32(seed64) ^ U32(seed64 >> 32);
}

bool LLUUID::parseUUID(const std::string& buf, LLUUID* value)
{
    if (buf.empty() || value == NULL)
    {
        return false;
    }

    std::string temp(buf);
    LLStringUtil::trim(temp);
    if (LLUUID::validate(temp))
    {
        value->set(temp);
        return true;
    }
    return false;
}

//static
LLUUID LLUUID::generateNewID(std::string hash_string)
{
    LLUUID new_id;
    if (hash_string.empty())
    {
        new_id.generate();
    }
    else
    {
        new_id.generate(hash_string);
    }
    return new_id;
}

LLAssetID LLTransactionID::makeAssetID(const LLUUID& session) const
{
    LLAssetID result;
    if (isNull())
    {
        result.setNull();
    }
    else
    {
        combine(session, result);
    }
    return result;
}

// Construct
LLUUID::LLUUID()
{
    setNull();
}


// Faster than copying from memory
void LLUUID::setNull()
{
    memset(mData, 0, sizeof(mData));
}


// S24 : the comparisons below load two 64-bit words through memcpy (no
// unaligned/aliasing cast); MSVC folds the memcpy into plain loads, so the
// hot-path codegen stays branchless like the old U32 version.

// Compare
bool LLUUID::operator==(const LLUUID& rhs) const
{
    U64 a[2], b[2];
    memcpy(a, mData, sizeof(a));
    memcpy(b, rhs.mData, sizeof(b));
    // Note: binary & to avoid branching
    return (a[0] == b[0]) & (a[1] == b[1]);
}


bool LLUUID::operator!=(const LLUUID& rhs) const
{
    U64 a[2], b[2];
    memcpy(a, mData, sizeof(a));
    memcpy(b, rhs.mData, sizeof(b));
    // Note: binary | to avoid branching
    return (a[0] != b[0]) | (a[1] != b[1]);
}

/*
// JC: This is dangerous.  It allows UUIDs to be cast automatically
// to integers, among other things.  Use isNull() or notNull().
 LLUUID::operator bool() const
{
    U32 *word = (U32 *)mData;
    return (word[0] | word[1] | word[2] | word[3]) > 0;
}
*/

bool LLUUID::notNull() const
{
    U64 w[2];
    memcpy(w, mData, sizeof(w));
    return (w[0] | w[1]) != 0;
}

// Faster than == LLUUID::null because doesn't require
// as much memory access.
bool LLUUID::isNull() const
{
    U64 w[2];
    memcpy(w, mData, sizeof(w));
    // If all bits are zero, return !0 == true
    return !(w[0] | w[1]);
}

LLUUID::LLUUID(const char* in_string)
{
    if (!in_string || in_string[0] == 0)
    {
        setNull();
        return;
    }

    set(in_string);
}

LLUUID::LLUUID(const std::string& in_string)
{
    if (in_string.empty())
    {
        setNull();
        return;
    }

    set(in_string);
}

// IW: DON'T "optimize" these w/ U32s or you'll scoogie the sort order
// IW: this will make me very sad
bool LLUUID::operator<(const LLUUID& rhs) const
{
    U32 i;
    for (i = 0; i < (UUID_BYTES - 1); i++)
    {
        if (mData[i] != rhs.mData[i])
        {
            return (mData[i] < rhs.mData[i]);
        }
    }
    return (mData[UUID_BYTES - 1] < rhs.mData[UUID_BYTES - 1]);
}

bool LLUUID::operator>(const LLUUID& rhs) const
{
    U32 i;
    for (i = 0; i < (UUID_BYTES - 1); i++)
    {
        if (mData[i] != rhs.mData[i])
        {
            return (mData[i] > rhs.mData[i]);
        }
    }
    return (mData[UUID_BYTES - 1] > rhs.mData[UUID_BYTES - 1]);
}

U32 LLUUID::getCRC32() const
{
    U32 tmp[4];
    memcpy(tmp, mData, sizeof(tmp)); // S24 : native-endian words, same value as before
    return tmp[0] + tmp[1] + tmp[2] + tmp[3];
}
