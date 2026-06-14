/*
================================================================================
 MIT License — Kirstens Viewer 2025
================================================================================

Copyright (c) 2025 Kirstens Viewer Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

================================================================================
*/

/*
================================================================================
 SLPlugin.exe — Plugin Process Shell S24 Version
================================================================================

Purpose:
- Launches a plugin child process for inter-process communication via a specified port.
- Runs headless with no UI or console.
- Cooperates with host application via heartbeat and message pump.

Security Notes:
- No exception suppression: crashes propagate naturally.
- Host application is responsible for crash detection and suppression.
- No stealth behavior, no remote control, no persistence.

AV-Friendly Design:
- Digitally signed
- Logs startup and shutdown
- No use of SetUnhandledExceptionFilter
- rc.file to satisfy Microsoft

Recommended Host Launch:
- Use CreateProcess with CREATE_NO_WINDOW
- Monitor heartbeat for crash detection

Notes:
- This shell is designed for maximum transparency and AV compatibility.
- All crash behavior is delegated to host-side logic.
- For internal audits or vendor submissions, refer to slplugin_startup.log.

================================================================================
*/

// slplugin.cpp — ultra-safe version
// Linden Includes
#include "linden_common.h"
#include "llpluginprocesschild.h"
#include "llpluginmessage.h"
#include "llerrorcontrol.h"
#include "llapr.h"
#include "llstring.h"

#include <iostream>
#include <fstream>
using namespace std;

// Windows SDK
#include "llwin32headers.h"

////////////////////////////////////////////////////////////////////////////////
// [subsystem:entry_point]
// Windows GUI entry point. No window created—plugin runs headless.
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    ll_init_apr();

    // [subsystem:logging]
    LLError::initForApplication(".", ".");
    LLError::setDefaultLevel(LLError::LEVEL_INFO);

    // [subsystem:startup_validation]
    if (strlen(lpCmdLine) == 0)
    {
        LL_ERRS("slplugin") << "Missing launcher port argument." << LL_ENDL;
        MessageBoxA(nullptr, "SLPlugin requires a launcher port argument.", "SLPlugin Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    U32 port = 0;
    if (!LLStringUtil::convertToU32(lpCmdLine, port))
    {
        LL_ERRS("slplugin") << "Port number must be numeric." << LL_ENDL;
        MessageBoxA(nullptr, "SLPlugin port must be numeric.", "SLPlugin Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // [subsystem:startup_logging]
    std::ofstream log("slplugin_startup.log");
    log << "SLPlugin launched with port: " << port << std::endl;
    LL_INFOS("slplugin") << "SLPlugin starting on port " << port << LL_ENDL;

    // [subsystem:plugin_lifecycle]
    LLPluginProcessChild* plugin = new LLPluginProcessChild();
    plugin->init(port);

    LLTimer timer;
    timer.start();

    // [subsystem:plugin_loop]
    while (!plugin->isDone())
    {
        timer.reset();
        plugin->idle();

        F64 elapsed = timer.getElapsedTimeF64();
        F64 remaining = plugin->getSleepTime() - elapsed;

        if (remaining <= 0.0)
        {
            plugin->pump();
        }
        else
        {
            plugin->sleep(remaining);
        }
    }

    delete plugin;

    // [subsystem:shutdown_logging]
    LL_INFOS("slplugin") << "SLPlugin shutting down cleanly." << LL_ENDL;
    log << "SLPlugin shutdown complete." << std::endl;

    ll_cleanup_apr();
    return 0;
}