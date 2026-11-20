/**
 * @file llsdl.cpp
 * @brief SDL initialization
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "llsdl.h"

#include "SDL3/SDL.h"

// S24 : the viewer keeps its own WinMain, SDL must not hijack it
#define SDL_MAIN_HANDLED 1
#include "SDL3/SDL_main.h"

static void sdl_logger(void*, int, SDL_LogPriority priority, const char* message)
{
    switch (priority)
    {
        case SDL_LOG_PRIORITY_WARN:
        case SDL_LOG_PRIORITY_ERROR:
        case SDL_LOG_PRIORITY_CRITICAL:
            LL_WARNS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        default:
            break;
    }
}

void init_sdl()
{
    SDL_SetMainReady();
    SDL_SetLogOutputFunction(&sdl_logger, nullptr);
}

void quit_sdl()
{
    SDL_Quit();
}
