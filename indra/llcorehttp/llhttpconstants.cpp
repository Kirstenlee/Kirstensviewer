/** 
 * @file llhttpconstants.cpp
 * @brief Implementation of the HTTP request / response constant lookups
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013-2014, Linden Research, Inc.
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
#include "llhttpconstants.h"
#include "lltimer.h"

// for curl_getdate() (apparently parsing RFC 1123 dates is hard)
#include <curl/curl.h>

// =============================================================================
// All constants are now defined as inline constexpr in the header file.
// This eliminates:
//   - Dynamic heap allocations at startup (~70+ std::string constructions)
//   - Static initialization order dependencies
//   - Destructor calls at program exit
//   - Scattered heap memory (now contiguous in .rodata section)
// =============================================================================

// Only function implementations remain in the .cpp file
