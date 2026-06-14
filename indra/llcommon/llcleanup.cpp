/**
 * @file   llcleanup.cpp
 * @author Nat Goodspeed
 * @date   2016-08-30
 * @brief  Implementation for llcleanup.
 * 
 * $LicenseInfo:firstyear=2016&license=viewerlgpl$
 * Copyright (c) 2016, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llcleanup.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "llerror.h"
#include "llerrorcontrol.h"

void log_subsystem_cleanup(LLError::ELevel level,
                           const char* file,
                           int line,
                           const char* function,
                           const char* classname)
{
    // S24: Reduced excessive cleanup spam - this is debug info, not production logging
    // LL used this to track shutdown order during development, but it's just noise in production
    // Changed to DEBUG level and simplified message - no need for full file/line/function spam
    LL_DEBUGS("Cleanup") << classname << "::cleanupClass()" << LL_ENDL;
}
