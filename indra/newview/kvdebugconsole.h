/**
 * @file kvdebugconsole.h
 * @brief S24 Console - Command-line interface to the viewer operating system
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef KV_KVDEBUGCONSOLE_H
#define KV_KVDEBUGCONSOLE_H

#include "llfloater.h"
#include "llerrorcontrol.h"
#include "lluuid.h"
#include "v3dmath.h"

class LLTextEditor;
class LLLineEditor;

class KVDebugConsole : public LLFloater
{
public:
    KVDebugConsole(LLSD const & key);
    virtual ~KVDebugConsole();

    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void onOpen(const LLSD& key) override;
    void draw() override;  // S24: Override to update persistent beacon each frame

    void onInput(LLUICtrl* ctrl, const LLSD& param);
    void onClickPause();
    void processHelpCommand(const std::string& topic);
    void processListCommand();
    void processUuidCommand(const std::string& uuid_str);
    void processScanCommand(const std::string& args);
    void performScanDisplay(F64 max_range);  // S24: Helper for delayed scan after name cache primed
    void processLookatCommand(const std::string& args);
    void processFindCommand(const std::string& args);
    void processTpCommand(const std::string& args);
    void processFlyCommand(const std::string& args);
    void processGetCommand(const std::string& args);
    void processSetCommand(const std::string& args);
    void processOpenCommand(const std::string& args);
    void processShutdownCommand(const std::string& args);
    void processWhoisCommand(const std::string& args);
    void requestAvatarProfile(const LLUUID& avatar_id);
    void processTopCommand(const std::string& args);

    // S24: Console utility commands (pure console functions, no LL-specific)
    void processCalcCommand(const std::string& expression);
    void processTimeCommand();
    void processUptimeCommand();
    void processVersionCommand();
    void processMotdCommand();
    void processWebCommand(const std::string& url);

    // S24: Calculator expression parser helpers
    F64 parseExpression(const std::string& expr, size_t& pos);
    F64 parseTerm(const std::string& expr, size_t& pos);
    F64 parseFactor(const std::string& expr, size_t& pos);

    static void addLogLine(const std::string& message);
    static void addLogLineWarning(const std::string& message);  // Inverse video: black on yellow
    static void addLogLineError(const std::string& message);    // Inverse video: white on red

    // S24: Console command feedback helpers using inverse video highlighting
    // The console uses a monospace read-only text editor which works best with background
    // highlighting (inverse video) rather than text color changes. This is classic terminal style.
    // To add new highlight styles, use LLStyle::Params with:
    //   - color + readonly_color: text color (must set BOTH for read-only editors)
    //   - highlight_bg_color: background color
    //   - draw_highlight_bg: must be true to enable
    void printSuccess(const std::string& message);   // Black on green - successful operations
    void printError(const std::string& message);     // White on red - failed operations
    void printWarning(const std::string& message);   // Black on yellow - warnings
    void printInfo(const std::string& message);      // Black on cyan - helpful guidance
    void printNormal(const std::string& message);    // White on black - standard output

private:
    void trimBuffer();

    LLTextEditor* mOutput;
    bool mPaused;
    S32 mLineCount;

    static KVDebugConsole* sInstance;
    static LLError::RecorderPtr sRecorder;

    // S24: Buffer limits to prevent memory bloat
    static const S32 MAX_LINES = 10000;      // Keep last 10k lines
    static const S32 TRIM_TO_LINES = 7500;   // Trim down to 7.5k when limit hit

    // S24: Delayed scan state (server name cache priming)
    struct PendingScan
    {
        bool active;
        F64 max_range;
        F32 delay_remaining;
    };
    PendingScan mPendingScan;

    // S24: Persistent beacon tracker (coordinate-based, works at unlimited range)
    struct BeaconTarget
    {
        LLUUID uuid;              // Target UUID (for avatar tracking)
        LLVector3d pos_global;    // Global coordinates (like TP uses)
        std::string name;         // Display name
        bool active;              // Is beacon active?
    };
    static BeaconTarget sBeaconTarget;
    static void updateBeacon();   // Called each frame to update beacon position
    static void clearBeacon();    // Remove active beacon

    // S24: Shutdown timer state
    struct ShutdownTimer
    {
        bool active;              // Is shutdown timer running?
        F32 time_remaining;       // Seconds until shutdown
        LLTimer timer;            // Timer for elapsed tracking
    };
    static ShutdownTimer sShutdownTimer;
    static void checkShutdownTimer();  // Called each frame to check timer
};

#endif // KV_KVDEBUGCONSOLE_H
