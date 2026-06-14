// AresWin32.h
// Ares subordinate function — seeks stray processes and plugins created from the viewer
// Audits, classifies, and ultimately kills!
// Makes viewer shutdown clean and logged.
// Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
#include "llwin32headers.h"
#include <string>             // std::string for tagging/logging
#include <vector>             // Dynamic HWND/thread collections
#include <map>                // Optional: ownership tracking




namespace AresWin32
{
    void RunFinalSweep(bool killmainexe, bool factoryReset = false);  // Entry point for post-shutdown sentinel pass
    BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam);  // HWND sweep callback

    // S24 Factory Reset: Spawn visible CMD process to delete folders after viewer exits
    void SpawnFactoryResetCleanup();  // Spawn CMD process with full logging

    // S24 Ares Shutdown Dialog: Show Ares kill list in real-time during shutdown
    void ShowAresShutdownDialog();     // Create and show Ares activity dialog
    void UpdateAresDialog(const std::string& message);  // Add message to dialog
    void CloseAresDialog();            // Close dialog when sweep complete
}

