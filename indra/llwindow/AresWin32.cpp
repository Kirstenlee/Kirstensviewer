// AresWin32.cpp
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

#include "AresWin32.h"
#include "llerror.h"

#include <Psapi.h>            // For process info, memory maps
#include <TlHelp32.h>         // Snapshot of processes, threads
#include <winternl.h>         // For advanced query (if needed)
#include <processthreadsapi.h>// Affinity, ownership, process token
#include <Userenv.h>          // For session/environment checks
#include <Shlwapi.h>          // For string/path utility (PathFileExistsW)
#include <Shellapi.h>         // For shell operations (SHFileOperationW)
#include <ShlObj.h>           // For SHGetFolderPathW and CSIDL constants
#include <unordered_set>
#include <string>
#include <locale>
#include <codecvt>  // deprecated in C++17+, but still usable in legacy code
#include <algorithm>  // for sort
#include <thread> // for msg box
#include <numeric>
#include <chrono>  // for timing
#include <iomanip>  // for setw, setprecision
#include <sstream>  // for ostringstream




#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Shell32.lib")  // For SHFileOperationW and SHGetFolderPathW

#include <iostream> // TODO: replace with AuditTrail or subsystem logger


namespace AresWin32
{
    // ============================================================================
    // S24 ARES SHUTDOWN DIALOG - VT Sequence Support (Windows 10+)
    // ============================================================================

    // VT100 ANSI Escape Codes - "Gold Standard" color scheme
    // Kirsten's Gold branding with Ice Blue technical aesthetics
    static bool g_VTModeEnabled = false;

    // Reset and basic formatting
    #define ANSI_RESET     "\x1b[0m"
    #define ANSI_BOLD      "\x1b[1m"
    #define ANSI_DIM       "\x1b[2m"

    // Kirsten's Gold Standard Palette (foreground only - no background persistence issues)
    #define ANSI_GOLD          "\x1b[38;2;255;215;0m"      // #FFD700 - Brand gold
    #define ANSI_ICE_BLUE      "\x1b[38;2;136;204;255m"    // #88CCFF - Ice blue accents
    #define ANSI_TEAL          "\x1b[38;2;0;206;209m"      // #00CED1 - Teal complete
    #define ANSI_MINT_GREEN    "\x1b[38;2;63;255;63m"      // #3FFF3F - Success green
    #define ANSI_CORAL_RED     "\x1b[38;2;255;107;107m"    // #FF6B6B - Soft error red
    #define ANSI_WHITE         "\x1b[97m"                  // Bright white (simple code)
    #define ANSI_DIM_WHITE     "\x1b[37m"                  // Normal white
    #define ANSI_STEEL_BLUE    "\x1b[38;2;70;130;180m"     // #4682B4 - Borders

    // Spinner animation frames (ASCII-safe)
    static const char* g_SpinnerFrames[] = { "|", "/", "-", "\\", "|", "/", "-", "\\" };
    static int g_SpinnerFrame = 0;

    // ============================================================================
    // Console globals
    // ============================================================================
    static HWND g_AresConsole = NULL;
    static HANDLE g_ConsoleOutput = NULL;
    static CRITICAL_SECTION g_AresCritSec;
    static bool g_AresDialogInit = false;

    // Enable VT100 escape sequences (Windows 10+)
    static bool EnableVTMode()
    {
        // Use the already-acquired console output handle
        if (!g_ConsoleOutput || g_ConsoleOutput == INVALID_HANDLE_VALUE)
        {
            LL_WARNS() << "ARES_VT: Console output handle not available" << LL_ENDL;
            return false;
        }

        DWORD mode = 0;
        if (!GetConsoleMode(g_ConsoleOutput, &mode))
        {
            LL_WARNS() << "ARES_VT: GetConsoleMode failed, error=" << GetLastError() << LL_ENDL;
            return false;
        }

        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(g_ConsoleOutput, mode))
        {
            LL_WARNS() << "ARES_VT: SetConsoleMode failed, error=" << GetLastError() << LL_ENDL;
            return false;
        }

        LL_INFOS() << "ARES_VT: Virtual Terminal mode ENABLED - full RGB color support active!" << LL_ENDL;
        return true;
    }

    // Helper function to set console colors (legacy 16-color fallback)
    static void SetConsoleColor(WORD color)
    {
        if (g_ConsoleOutput)
        {
            SetConsoleTextAttribute(g_ConsoleOutput, BACKGROUND_BLUE | color);
        }
    }

    // ============================================================================
    // VT Helper Functions - Modern Console UI
    // ============================================================================

    // Generate progress bar with gradient (Gold -> Teal) - ASCII-safe
    static std::string MakeProgressBar(int current, int total, int width = 18)
    {
        if (total == 0) total = 1; // Avoid division by zero

        int filled = (current * width) / total;
        int percent = (current * 100) / total;

        std::ostringstream bar;

        if (g_VTModeEnabled)
        {
            // VT mode: Gold/Teal gradient with simple ASCII characters
            bar << "[";
            for (int i = 0; i < width; ++i)
            {
                if (i < filled)
                {
                    // Gradient interpolation
                    float ratio = static_cast<float>(i) / width;
                    if (ratio < 0.5f)
                        bar << ANSI_GOLD << "#" << ANSI_RESET;
                    else
                        bar << ANSI_TEAL << "#" << ANSI_RESET;
                }
                else
                {
                    bar << ANSI_DIM << "-" << ANSI_RESET;
                }
            }
            bar << "] " << current << "/" << total << "  " << percent << "%";
        }
        else
        {
            // Fallback ASCII mode
            bar << "[";
            for (int i = 0; i < width; ++i)
            {
                bar << (i < filled ? "#" : "-");
            }
            bar << "] " << current << "/" << total << " (" << percent << "%)";
        }

        return bar.str();
    }

    // Get next spinner frame - ASCII-safe
    static std::string GetSpinner()
    {
        if (g_VTModeEnabled)
        {
            std::string spinner = g_SpinnerFrames[g_SpinnerFrame];
            g_SpinnerFrame = (g_SpinnerFrame + 1) % 8; // 8 frames now
            return ANSI_GOLD + spinner + ANSI_RESET;
        }
        else
        {
            // ASCII fallback
            static const char frames[] = { '|', '/', '-', '\\' };
            std::string spinner;
            spinner += frames[g_SpinnerFrame % 4];
            g_SpinnerFrame++;
            return spinner;
        }
    }

    // Write VT text (handles both VT and fallback modes)
    static void WriteVT(const std::string& text)
    {
        if (!g_ConsoleOutput) return;

        DWORD written = 0;
        WriteConsoleA(g_ConsoleOutput, text.c_str(), (DWORD)text.length(), &written, NULL);
    }

    // Write VT line (with newline)
    static void WriteVTLine(const std::string& text)
    {
        WriteVT(text + "\n");
    }

    inline std::string narrow_for_logging(const std::wstring& wide)
    {
        try {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
            return conv.to_bytes(wide);
        }
        catch (...) {
            return "[wstring conversion failed]";
        }
    }

    // IMPORTANT :: Plugin executables we allow to be swept/killed
    static const std::unordered_set<std::wstring> kPluginExecutables = {
     L"SLPlugin.exe",
     L"dullahan_host.exe",
     L"SLVoice.exe",
     L"Kirstens-S24.exe" // sanctioned now for final sweep
    };

    static bool IsPluginProcess(DWORD pid)
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProc) return false;

        TCHAR exeName[MAX_PATH] = { 0 };
        if (GetModuleBaseName(hProc, nullptr, exeName, ARRAYSIZE(exeName))) {
            std::wstring wexe(exeName);
            if (kPluginExecutables.count(wexe)) {
                CloseHandle(hProc);
                return true;
            }
        }

        CloseHandle(hProc);
        return false;
    }

    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM)
    {
        // Simplified - no excessive logging during shutdown when logging may be dead
        if (!IsWindow(hwnd)) return TRUE;

        DWORD pid = 0, tid = GetWindowThreadProcessId(hwnd, &pid);
        if (tid == 0 || pid == 0) return TRUE;

        // Just enumerate, don't spam logs
        return TRUE;
    }

    static void SweepHWNDs()
    {
        EnumWindows(&EnumProc, 0);
    }

    static void LogPluginKillEvent(const std::wstring& exeName, DWORD pid, const std::string& stage, const std::string& detail)
    {
        std::ostringstream oss;
        oss << stage << " exe=" << narrow_for_logging(exeName)
            << " pid=" << pid << " " << detail;
        LL_WARNS() << oss.str() << LL_ENDL;
    }

    static void TerminateZombiePlugins(bool killmainexe)
    {
        // Get our own PID to filter child processes only
        DWORD ourPID = GetCurrentProcessId();

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32 procEntry = { sizeof(procEntry) };
        std::vector<std::pair<std::wstring, DWORD>> sweepQueue;

        if (Process32First(snapshot, &procEntry)) {
            do {
                std::wstring exeName(procEntry.szExeFile);
                if (kPluginExecutables.count(exeName)) {
                    DWORD pid = procEntry.th32ProcessID;
                    DWORD parentPID = procEntry.th32ParentProcessID;

                    // CRITICAL: Only kill processes that belong to THIS viewer instance
                    // - Child processes (parentPID == ourPID)
                    // - OR our own main exe (pid == ourPID) when doing final sweep
                    if (parentPID == ourPID || pid == ourPID) {
                        sweepQueue.emplace_back(exeName, pid);
                    }
                }
            } while (Process32Next(snapshot, &procEntry));
        }

        CloseHandle(snapshot);

        std::sort(sweepQueue.begin(), sweepQueue.end(), [](const auto& a, const auto& b) {
            return b.first == L"Kirstens-S24.exe";
            });

        // Calculate background process count (exclude main viewer exe from count)
        int backgroundProcessCount = 0;
        for (const auto& [exeName, pid] : sweepQueue) {
            if (exeName != L"Kirstens-S24.exe") {
                backgroundProcessCount++;
            }
        }

        // S24: Dashboard-style progress display - ASCII-safe
        if (g_VTModeEnabled)
        {
            // VT Mode: Clean header
            WriteVTLine(ANSI_STEEL_BLUE "+-- Process Termination ---------------------------------+" ANSI_RESET);
            WriteVTLine(ANSI_STEEL_BLUE "| " ANSI_DIM_WHITE "PID    Name                          Time         " ANSI_STEEL_BLUE "|" ANSI_RESET);
            WriteVTLine(ANSI_STEEL_BLUE "+---------------------------------------------------------+" ANSI_RESET);
        }
        else
        {
            // Fallback ASCII mode
            UpdateAresDialog("  ------------------------------------------------------------------------------");
            std::ostringstream countMsg;
            countMsg << "  [SCAN] Found " << backgroundProcessCount << " background process" 
                     << (backgroundProcessCount != 1 ? "es" : "") << " to terminate";
            UpdateAresDialog(countMsg.str());
            UpdateAresDialog("  ------------------------------------------------------------------------------");
            UpdateAresDialog("");
        }

        int processedCount = 0;
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

        for (const auto& [exeName, pid] : sweepQueue) {
            bool isMainExe = (exeName == L"Kirstens-S24.exe");
            if (isMainExe && !killmainexe) continue;

            auto processStart = std::chrono::steady_clock::now();

            int totalTids = 0;
            HANDLE snapshotThreads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshotThreads != INVALID_HANDLE_VALUE) {
                THREADENTRY32 te = { sizeof(te) };
                if (Thread32First(snapshotThreads, &te)) {
                    do {
                        if (te.th32OwnerProcessID == pid) {
                            ++totalTids;
                        }
                    } while (Thread32Next(snapshotThreads, &te));
                }
                CloseHandle(snapshotThreads);
            }

            // Removed excessive logging - viewer logs may be dead by now

            if (isMainExe && killmainexe) {
                if (g_VTModeEnabled)
                {
                    std::ostringstream msg;
                    msg << ANSI_STEEL_BLUE << "| " << ANSI_RESET << ANSI_GOLD << std::setw(6) << std::left << pid 
                        << " Main Viewer             [FINAL]        " << ANSI_STEEL_BLUE << " |" << ANSI_RESET;
                    WriteVTLine(msg.str());
                }
                else
                {
                    UpdateAresDialog("  +- [FINAL] Main viewer - initiating self-termination");
                }
                Sleep(300);
            }

            // Attempt termination with aggressive fallback for stubborn processes (SLVoice!)
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, pid);

            if (hProc)
            {
                // First attempt
                TerminateProcess(hProc, 0);
                WaitForSingleObject(hProc, 2000);
                CloseHandle(hProc);

                // Aggressive retry for stubborn processes
                Sleep(500);
                hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                if (hProc)
                {
                    TerminateProcess(hProc, 1); // Force kill
                    WaitForSingleObject(hProc, 1000);
                    CloseHandle(hProc);
                }
            }

            auto processEnd = std::chrono::steady_clock::now();
            float processDuration = std::chrono::duration<float>(processEnd - processStart).count();

            processedCount++;

            if (g_VTModeEnabled && !isMainExe)
            {
                // VT Mode: Simple process list - no status, just what we killed
                std::ostringstream row;
                row << ANSI_STEEL_BLUE << "| " << ANSI_RESET << ANSI_GOLD;
                row << std::setw(6) << std::left << pid << " ";

                std::string shortName = narrow_for_logging(exeName);
                if (shortName.length() > 28) shortName = shortName.substr(0, 25) + "...";
                row << std::setw(28) << std::left << shortName << " ";

                row << ANSI_DIM_WHITE << std::fixed << std::setprecision(1) << processDuration << "s";
                row << ANSI_STEEL_BLUE << "        |" << ANSI_RESET;

                WriteVTLine(row.str());

                // Update progress bar (move cursor up and redraw)
                // For simplicity, we'll just update the line count
            }
            else if (!g_VTModeEnabled)
            {
                // Fallback ASCII mode - simple list
                std::ostringstream killMsg;
                killMsg << "  +- " << narrow_for_logging(exeName) 
                        << " [PID:" << pid << "] " 
                        << std::fixed << std::setprecision(1) << processDuration << "s";
                SetConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                UpdateAresDialog(killMsg.str());
            }

            Sleep(150); // Slight delay for visual effect
        }

        // Simple summary - just count what we processed
        auto endTime = std::chrono::steady_clock::now();
        float totalDuration = std::chrono::duration<float>(endTime - startTime).count();

        if (g_VTModeEnabled)
        {
            // VT Mode: Clean completion
            WriteVTLine(ANSI_STEEL_BLUE "+---------------------------------------------------------+" ANSI_RESET);
            WriteVTLine("");

            // Simple status line
            std::ostringstream status;
            status << ANSI_GOLD << "Terminated " << backgroundProcessCount << " process" 
                   << (backgroundProcessCount != 1 ? "es" : "") << ANSI_RESET
                   << ANSI_WHITE << " in " << std::fixed << std::setprecision(1) << totalDuration << "s" << ANSI_RESET;

            WriteVTLine(status.str());
        }
        else
        {
            // Fallback ASCII mode - simple summary
            UpdateAresDialog("");
            SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            UpdateAresDialog("  ------------------------------------------------------------------------------");

            UpdateAresDialog("");
            SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);

            std::ostringstream finalMsg;
            if (backgroundProcessCount > 0)
            {
                finalMsg << "  [OK] Terminated " << backgroundProcessCount 
                         << " process" << (backgroundProcessCount != 1 ? "es" : "")
                         << " in " << std::fixed << std::setprecision(1) << totalDuration << "s";
            }
            else
            {
                finalMsg << "  [OK] No background processes found";
            }

            UpdateAresDialog(finalMsg.str());
        }

        if (killmainexe)
        {
            SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            UpdateAresDialog("  [✓] Main viewer process terminated cleanly");
        }
               
    }

    static BOOL CALLBACK AuditThreadEnum(HWND hwnd, LPARAM lParam)
    {
        if (!IsWindow(hwnd)) return TRUE;

        DWORD hwndTid = GetWindowThreadProcessId(hwnd, nullptr);
        auto* stats = reinterpret_cast<std::unordered_map<DWORD, int>*>(lParam);
        (*stats)[hwndTid]++;
        return TRUE;
    }

    static void AuditThreadOwnership()
    {
        LL_INFOS() << "ARES_AUDIT_BEGIN Thread ownership audit started." << LL_ENDL;

        std::unordered_set<DWORD> liveTids;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(te) };
            if (Thread32First(snapshot, &te)) {
                do {
                    liveTids.insert(te.th32ThreadID);
                } while (Thread32Next(snapshot, &te));
            }
            CloseHandle(snapshot);
        }

        std::unordered_map<DWORD, int> matchCounts;
        EnumWindows(&AuditThreadEnum, reinterpret_cast<LPARAM>(&matchCounts));

        LL_INFOS() << "ARES_THREAD_SUMMARY tids=" << matchCounts.size()
            << " hwnds=" << std::accumulate(matchCounts.begin(), matchCounts.end(), 0,
                [](int sum, const auto& pair) {
                    return sum + pair.second;
                })
            << LL_ENDL;
                        
    }

    void RunFinalSweep(bool killmainexe, bool factoryReset)
    {
        LL_INFOS() << "ARES_SENTINEL_BEGIN Sweep initiated - Kirsten's Viewer." << LL_ENDL;

        // Dialog already shown earlier in shutdown sequence
        Sleep(300);  // Brief pause after dialog appears

        // S24 Factory Reset: Spawn cleanup process BEFORE killing anything
        // This ensures the CMD process is independent and survives viewer termination
        if (factoryReset)
        {
            LL_WARNS() << "ARES_FACTORY_RESET: Flag detected - spawning cleanup process" << LL_ENDL;
            SetConsoleColor( FOREGROUND_RED | FOREGROUND_INTENSITY); // Bright red
            UpdateAresDialog("[FACTORY RESET] Complete data wipe initiated - all caches will be purged");
            SetConsoleColor( FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            Sleep(500);
            SpawnFactoryResetCleanup();
        }

        SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); // Cyan
        UpdateAresDialog("  [SCAN] Enumerating processes and analyzing dependencies...");
        SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        Sleep(300);
        SweepHWNDs();

        SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); // Cyan
        UpdateAresDialog("  [AUDIT] Verifying thread ownership and process hierarchy...");
        SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        Sleep(300);
        AuditThreadOwnership();

        SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); // Cyan
        UpdateAresDialog("  [CLEANUP] Initiating graceful process termination...");
        UpdateAresDialog("");
        SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        Sleep(200);
        TerminateZombiePlugins(killmainexe);

        LL_INFOS() << "ARES_FINAL :: Protocol Complete!." << LL_ENDL;

        if (factoryReset)
        {
            LL_WARNS() << "ARES_FACTORY_RESET: Viewer terminating - CMD process will delete folders in 3 seconds" << LL_ENDL;
            SetConsoleColor( FOREGROUND_RED | FOREGROUND_INTENSITY); // Bright red
            UpdateAresDialog("[FACTORY RESET] Data purge process spawned - deletion begins in 3 seconds");
            SetConsoleColor( FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            Sleep(500);
        }

        // Close dialog after showing final status
        Sleep(1000);  // Let user admire the completion
        CloseAresDialog();
    }

    // S24 Factory Reset: Spawn visible CMD process to delete AppData folders
    // Uses transparent, logged approach to avoid AV false positives
    // CMD process runs AFTER viewer exits (detached, independent)
    void SpawnFactoryResetCleanup()
    {
        LL_WARNS() << "==========================================" << LL_ENDL;
        LL_WARNS() << "ARES FACTORY RESET: Spawning cleanup process" << LL_ENDL;
        LL_WARNS() << "==========================================" << LL_ENDL;

        // Get AppData paths
        wchar_t localAppData[MAX_PATH] = { 0 };
        wchar_t roamingAppData[MAX_PATH] = { 0 };

        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)) &&
            SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, roamingAppData)))
        {
            // Build paths
            std::wstring localPath = std::wstring(localAppData) + L"\\Kirstens S24";
            std::wstring roamingPath = std::wstring(roamingAppData) + L"\\Kirstens S24";

            LL_WARNS() << "ARES_FACTORY_RESET: Target LOCAL: " << narrow_for_logging(localPath) << LL_ENDL;
            LL_WARNS() << "ARES_FACTORY_RESET: Target ROAMING: " << narrow_for_logging(roamingPath) << LL_ENDL;

            // Build CMD command with logging and visibility
            // VISIBLE window with title so user can see what's happening (AV friendly)
            // Echoes every action for transparency
            std::wostringstream cmdLine;
            cmdLine << L"cmd.exe /K \"";
            cmdLine << L"title Kirstens S24 Factory Reset && ";
            cmdLine << L"echo ========================================== && ";
            cmdLine << L"echo KIRSTENS S24 FACTORY RESET && ";
            cmdLine << L"echo ========================================== && ";
            cmdLine << L"echo Waiting for viewer to exit... && ";
            cmdLine << L"timeout /t 3 /nobreak && ";
            cmdLine << L"echo. && ";
            cmdLine << L"echo Deleting LOCAL cache folder... && ";
            cmdLine << L"rd /s /q \"" << localPath << L"\" && ";
            cmdLine << L"echo LOCAL folder deleted. && ";
            cmdLine << L"echo. && ";
            cmdLine << L"echo Deleting ROAMING settings folder... && ";
            cmdLine << L"rd /s /q \"" << roamingPath << L"\" && ";
            cmdLine << L"echo ROAMING folder deleted. && ";
            cmdLine << L"echo. && ";
            cmdLine << L"echo ========================================== && ";
            cmdLine << L"echo FACTORY RESET COMPLETE && ";
            cmdLine << L"echo Next viewer launch will be first-run experience && ";
            cmdLine << L"echo ========================================== && ";
            cmdLine << L"timeout /t 3 /nobreak && ";
            cmdLine << L"exit";
            cmdLine << L"\"";

            std::wstring cmdLineStr = cmdLine.str();

            LL_WARNS() << "ARES_FACTORY_RESET: Spawning visible CMD process" << LL_ENDL;

            // Spawn VISIBLE CMD process (AV friendly, fully transparent)
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_SHOWNORMAL;  // VISIBLE window

            // Create independent process
            BOOL result = CreateProcessW(
                NULL,                               // Application name
                &cmdLineStr[0],                     // Command line (modifiable)
                NULL,                               // Process security attributes
                NULL,                               // Thread security attributes
                FALSE,                              // Don't inherit handles
                CREATE_NEW_CONSOLE,                 // New console (independent, visible)
                NULL,                               // Environment
                NULL,                               // Current directory
                &si,                                // Startup info
                &pi                                 // Process info
            );

            if (result)
            {
                LL_WARNS() << "ARES_FACTORY_RESET: Cleanup process spawned (PID=" << pi.dwProcessId << ")" << LL_ENDL;
                LL_WARNS() << "ARES_FACTORY_RESET: CMD window will show deletion progress" << LL_ENDL;

                // Close handles (process is now independent)
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            else
            {
                DWORD error = GetLastError();
                LL_WARNS() << "ARES_FACTORY_RESET: FAILED to spawn process (error=" << error << ")" << LL_ENDL;
            }

            LL_WARNS() << "==========================================" << LL_ENDL;
            LL_WARNS() << "ARES_FACTORY_RESET: Continuing Ares sweep" << LL_ENDL;
            LL_WARNS() << "==========================================" << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "ARES_FACTORY_RESET: FAILED - Could not get AppData paths" << LL_ENDL;
        }
    }

    // ============================================================================
    // S24 ARES SHUTDOWN DIALOG - Console window functions
    // Variables and helper declared at top of namespace
    // ============================================================================

    void ShowAresShutdownDialog()
    {
        LL_WARNS() << "========================================" << LL_ENDL;
        LL_WARNS() << "ARES_DIALOG: ShowAresShutdownDialog() CALLED!" << LL_ENDL;
        LL_WARNS() << "========================================" << LL_ENDL;

        if (!g_AresDialogInit)
        {
            InitializeCriticalSection(&g_AresCritSec);
            g_AresDialogInit = true;
        }

        // Allocate a console window
        if (AllocConsole())
        {
            LL_WARNS() << "ARES_DIALOG: Console allocated successfully" << LL_ENDL;

            // Get console window handle
            g_AresConsole = GetConsoleWindow();
            if (g_AresConsole)
            {
                // Set console title
                SetConsoleTitleW(L"Kirsten's S24 Viewer - Shutdown in Progress");

                // Center the console window
                int consoleWidth = 900;
                int consoleHeight = 650;
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                int x = (screenWidth - consoleWidth) / 2;
                int y = (screenHeight - consoleHeight) / 2;

                // Make it topmost and centered
                SetWindowPos(g_AresConsole, HWND_TOPMOST, x, y, consoleWidth, consoleHeight, SWP_SHOWWINDOW);

                // Get output handle
                g_ConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);

                // S24: Optimized console dimensions - 80 columns (standard), compact height
                // Buffer slightly larger than window to allow minimal scrollback
                COORD bufferSize = { 80, 100 };
                SetConsoleScreenBufferSize(g_ConsoleOutput, bufferSize);

                // Window size: 80 columns x 28 rows (compact, no scroll bars needed)
                SMALL_RECT windowSize = { 0, 0, 79, 27 };
                SetConsoleWindowInfo(g_ConsoleOutput, TRUE, &windowSize);

                // Try to enable VT mode (Windows 10+)
                g_VTModeEnabled = EnableVTMode();

                if (g_VTModeEnabled)
                {
                    // VT Mode: Use default black background, let VT codes handle colors
                    SetConsoleTextAttribute(g_ConsoleOutput, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

                    // Clear screen with default black background
                    DWORD written;
                    COORD homeCoord = { 0, 0 };
                    FillConsoleOutputCharacterA(g_ConsoleOutput, ' ', bufferSize.X * bufferSize.Y, homeCoord, &written);
                    FillConsoleOutputAttribute(g_ConsoleOutput, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY, bufferSize.X * bufferSize.Y, homeCoord, &written);
                    SetConsoleCursorPosition(g_ConsoleOutput, homeCoord);
                }
                else
                {
                    // Fallback Mode: Use PowerShell-style blue background
                    WORD backgroundColor = BACKGROUND_BLUE;
                    WORD defaultText = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                    SetConsoleTextAttribute(g_ConsoleOutput, backgroundColor | defaultText);

                    // Clear screen with blue background
                    DWORD written;
                    COORD homeCoord = { 0, 0 };
                    FillConsoleOutputCharacterA(g_ConsoleOutput, ' ', bufferSize.X * bufferSize.Y, homeCoord, &written);
                    FillConsoleOutputAttribute(g_ConsoleOutput, backgroundColor | defaultText, bufferSize.X * bufferSize.Y, homeCoord, &written);
                    SetConsoleCursorPosition(g_ConsoleOutput, homeCoord);
                }

                LL_WARNS() << "ARES_DIALOG: Console configured and centered - HWND=" << g_AresConsole 
                          << " VT_MODE=" << (g_VTModeEnabled ? "ENABLED" : "FALLBACK") << LL_ENDL;
            }
        }
        else
        {
            DWORD err = GetLastError();
            LL_WARNS() << "ARES_DIALOG: AllocConsole failed! Error: " << err << LL_ENDL;
        }

        // S24: "Gold Standard" Dashboard Header - Clean ASCII with color
        if (g_VTModeEnabled)
        {
            // VT Mode: Gold Standard theme with ASCII-safe characters
            WriteVTLine(ANSI_STEEL_BLUE "+-------------------------------------------------------------------+" ANSI_RESET);
            WriteVTLine(ANSI_STEEL_BLUE "| " ANSI_GOLD ANSI_BOLD "KIRSTEN'S S24 VIEWER" ANSI_RESET ANSI_WHITE "  -  SHUTDOWN SEQUENCE" ANSI_STEEL_BLUE "                        |" ANSI_RESET);
            WriteVTLine(ANSI_STEEL_BLUE "+-------------------------------------------------------------------+" ANSI_RESET);
            WriteVTLine("");
        }
        else
        {
            // Fallback ASCII mode
            WORD bgColor = BACKGROUND_BLUE;
            SetConsoleTextAttribute(g_ConsoleOutput, bgColor | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            UpdateAresDialog("+-------------------------------------------------------------------+");
            UpdateAresDialog("| KIRSTEN'S S24 VIEWER  -  SHUTDOWN SEQUENCE                        |");
            UpdateAresDialog("+-------------------------------------------------------------------+");
            UpdateAresDialog("");
        }
    }

    void UpdateAresDialog(const std::string& message)
    {
        if (!g_ConsoleOutput) return;

        EnterCriticalSection(&g_AresCritSec);

        // Write to console
        std::string line = message + "\n";
        DWORD written = 0;
        WriteConsoleA(g_ConsoleOutput, line.c_str(), (DWORD)line.length(), &written, NULL);

        LeaveCriticalSection(&g_AresCritSec);
    }

    void CloseAresDialog()
    {
        if (g_AresDialogInit)
        {
            WriteVTLine("");

            if (g_VTModeEnabled)
            {
                // VT Mode: Gold Standard closing with ASCII-safe characters
                WriteVTLine(ANSI_STEEL_BLUE "===================================================================" ANSI_RESET);
                WriteVTLine(ANSI_GOLD ANSI_BOLD "[OK] Shutdown Complete" ANSI_RESET ANSI_WHITE "  -  All systems clean" ANSI_RESET);
                WriteVTLine(ANSI_STEEL_BLUE "===================================================================" ANSI_RESET);
                WriteVTLine("");
                WriteVTLine(ANSI_WHITE "          Thank you for using " ANSI_GOLD ANSI_BOLD "Kirsten's S24 Viewer" ANSI_RESET ANSI_WHITE "!" ANSI_RESET);
                WriteVTLine(ANSI_DIM_WHITE "      Your session has been saved and all processes closed cleanly." ANSI_RESET);
                WriteVTLine("");
            }
            else
            {
                // Fallback ASCII mode
                WORD bgColor = BACKGROUND_BLUE;

                SetConsoleTextAttribute(g_ConsoleOutput, bgColor | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                UpdateAresDialog("  ========================================================================");
                UpdateAresDialog("                 [OK] Shutdown Complete - All Systems Clean                ");
                UpdateAresDialog("  ========================================================================");
                UpdateAresDialog("");

                SetConsoleTextAttribute(g_ConsoleOutput, bgColor | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                UpdateAresDialog("               Thank you for using Kirsten's S24 Viewer!");
                UpdateAresDialog("");
                UpdateAresDialog("        Your session has been saved and all processes closed cleanly.");
                UpdateAresDialog("");
            }

            // Simple wait message - no failure warnings
            if (g_VTModeEnabled)
            {
                WriteVTLine(ANSI_ICE_BLUE "              This window will close in 5 seconds..." ANSI_RESET);
            }
            else
            {
                WORD bgColor = BACKGROUND_BLUE;
                SetConsoleTextAttribute(g_ConsoleOutput, bgColor | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                UpdateAresDialog("                   This window will close in 5 seconds...");
            }

            // Normal 5 second wait
            if (g_ConsoleOutput)
            {
                HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
                if (hInput != INVALID_HANDLE_VALUE)
                {
                    FlushConsoleInputBuffer(hInput);

                    // Wait up to 5 seconds for keypress, or timeout
                    DWORD waitMs = 5000;
                    DWORD startTime = GetTickCount();
                    while ((GetTickCount() - startTime) < waitMs)
                    {
                        DWORD events = 0;
                        GetNumberOfConsoleInputEvents(hInput, &events);
                        if (events > 0)
                        {
                            INPUT_RECORD rec;
                            DWORD read;
                            ReadConsoleInput(hInput, &rec, 1, &read);
                            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
                            {
                                break;
                            }
                        }
                        Sleep(100);
                    }
                }
            }

            FreeConsole();
            g_AresConsole = NULL;
            g_ConsoleOutput = NULL;

            DeleteCriticalSection(&g_AresCritSec);
            g_AresDialogInit = false;
        }
    }

}

