/**
 * @file kvdebugconsole.cpp
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

#include "llviewerprecompiledheaders.h"

#include "kvdebugconsole.h"

#include "lllineeditor.h"
#include "lltexteditor.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "lltimer.h"
#include "lleventtimer.h"

// S24: Standard library includes
#include <algorithm>  // std::sort for avatar distance sorting
#include <sstream>    // std::istringstream for argument parsing

// S24: Includes for UUID lookup command
#include "lluuid.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llviewerobjectlist.h"
#include "llviewerobject.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "llavatarnamecache.h"
#include "llviewertexturelist.h"
#include "llviewertexture.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llcachename.h"
#include "llworld.h"
#include "llviewerregion.h"
#include "llsurface.h"
#include "llviewerparcelmgr.h"
#include "llfloaterimnearbychat.h"
#include "llselectmgr.h"
#include "llteleporthistory.h"
#include "pipeline.h"
#include "llfloaterreg.h"
#include "llappviewer.h"
#include "llversioninfo.h"
#include "llavatarpropertiesprocessor.h"
#include "llavatarnamecache.h"
#include "llweb.h"
#include "llviewerobjectlist.h"
#include "llcallingcard.h"
#include "llmutelist.h"
#include "llstringtable.h"

// S24: Custom recorder that feeds the debug console floater
class RecordToDebugConsole : public LLError::Recorder
{
public:
    RecordToDebugConsole()
    {
        showMultiline(true);
        showTags(true);
        showLevel(true);
        showLocation(false);
        showFunctionName(false);
    }

    virtual void recordMessage(LLError::ELevel level, const std::string& message) override
    {
        // S24: Time-based throttling to prevent flood crashes
        // WARN/ERROR/FATAL always pass through (important messages)
        // DEBUG/INFO are throttled to prevent spam overload
        static LLTimer sDebugThrottle;
        static LLTimer sInfoThrottle;
        static const F32 DEBUG_THROTTLE_INTERVAL = 0.1f;  // 100ms = max 10 DEBUG/sec
        static const F32 INFO_THROTTLE_INTERVAL = 0.05f;  // 50ms = max 20 INFO/sec

        if (level == LLError::LEVEL_DEBUG)
        {
            if (sDebugThrottle.getElapsedTimeF32() < DEBUG_THROTTLE_INTERVAL)
            {
                return; // Skip this DEBUG message
            }
            sDebugThrottle.reset();
        }
        else if (level == LLError::LEVEL_INFO)
        {
            if (sInfoThrottle.getElapsedTimeF32() < INFO_THROTTLE_INTERVAL)
            {
                return; // Skip this INFO message
            }
            sInfoThrottle.reset();
        }
        // WARN/ERROR pass through immediately (no throttling)

        // S24: Use subtle text separators to highlight warnings/errors
        // Plain text separators preserve copy/paste functionality while making severity stand out
        if (level == LLError::LEVEL_WARN)
        {
            KVDebugConsole::addLogLineWarning(message);
        }
        else if (level == LLError::LEVEL_ERROR)
        {
            KVDebugConsole::addLogLineError(message);
        }
        else
        {
            KVDebugConsole::addLogLine(message);
        }
    }
};

KVDebugConsole* KVDebugConsole::sInstance = NULL;
LLError::RecorderPtr KVDebugConsole::sRecorder;
KVDebugConsole::BeaconTarget KVDebugConsole::sBeaconTarget;
KVDebugConsole::ShutdownTimer KVDebugConsole::sShutdownTimer;

const S32 KVDebugConsole::MAX_LINES;
const S32 KVDebugConsole::TRIM_TO_LINES;

KVDebugConsole::KVDebugConsole(LLSD const & key)
: LLFloater(key), mOutput(NULL), mPaused(false), mLineCount(0)
{
    sInstance = this;
    mPendingScan.active = false;
    mPendingScan.max_range = 0.0;
    mPendingScan.delay_remaining = 0.0f;
}

KVDebugConsole::~KVDebugConsole()
{
    sInstance = NULL;
}

bool KVDebugConsole::postBuild()
{
    mOutput = getChild<LLTextEditor>("debug_console_output");

    // Enable text selection and context menu for copy/paste
    if (mOutput)
    {
        mOutput->setShowContextMenu(true);
    }

    LLLineEditor* input = findChild<LLLineEditor>("debug_console_input");
    if (input)
    {
        input->setEnableLineHistory(true);
        input->setCommitCallback(boost::bind(&KVDebugConsole::onInput, this, _1, _2));
        input->setCommitOnFocusLost(false);
    }

    childSetAction("pause_btn", boost::bind(&KVDebugConsole::onClickPause, this));

    return true;
}

void KVDebugConsole::onOpen(const LLSD& key)
{
    // S24: Display boot message on console open
    if (mOutput)
    {
        mOutput->appendText(
            "S24 VOS [Version 24]\n"
            "(c) 1970-2026 Kirstenlee Cinquetti. All rights reserved.\n"
            "\n"
            "System initialized.\n"
            "> ",
            false
        );
        mLineCount += 5;
    }

    // S24: Register our log recorder when console opens
    if (!sRecorder)
    {
        sRecorder.reset(new RecordToDebugConsole());
        LLError::addRecorder(sRecorder);
    }
}

void KVDebugConsole::onClose(bool app_quitting)
{
    // S24: Remove recorder when console closes to save performance
    if (sRecorder)
    {
        LLError::removeRecorder(sRecorder);
        sRecorder.reset();
    }

    // S24: Clear beacon when console closes
    clearBeacon();

    LLFloater::onClose(app_quitting);
}

// S24: Override draw to update persistent beacon each frame
void KVDebugConsole::draw()
{
    // Update beacon position (tracks moving avatars)
    updateBeacon();

    // Check shutdown timer
    checkShutdownTimer();

    // S24: Check for pending scan completion (name cache priming delay)
    if (mPendingScan.active)
    {
        mPendingScan.delay_remaining -= gFrameIntervalSeconds;
        if (mPendingScan.delay_remaining <= 0.0f)
        {
            mPendingScan.active = false;
            performScanDisplay(mPendingScan.max_range);
        }
    }

    // Call parent draw
    LLFloater::draw();
}

void KVDebugConsole::addLogLine(const std::string& message)
{
    if (sInstance && sInstance->mOutput && !sInstance->mPaused)
    {
        // S24: Bounds check - prevent unlimited buffer growth
        if (sInstance->mLineCount >= MAX_LINES)
        {
            sInstance->trimBuffer();
        }

        // S24: Normal white-on-black text for standard log messages (DEBUG/INFO)
        // No style params = uses default text_color from XML (White)
        if (!message.empty() && message.back() != '\n')
        {
            sInstance->mOutput->appendText(message + "\n", false);
        }
        else
        {
            sInstance->mOutput->appendText(message, false);
        }

        sInstance->mLineCount++;
    }
}

void KVDebugConsole::addLogLineWarning(const std::string& message)
{
    if (sInstance && sInstance->mOutput && !sInstance->mPaused)
    {
        // S24: Bounds check - prevent unlimited buffer growth
        if (sInstance->mLineCount >= MAX_LINES)
        {
            sInstance->trimBuffer();
        }

        // S24: Subtle dashed separator for warnings - easy to spot but doesn't break copy/paste
        sInstance->mOutput->appendText("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\n", false);
        sInstance->mLineCount++;

        if (!message.empty() && message.back() != '\n')
        {
            sInstance->mOutput->appendText("WARNING: " + message + "\n", false);
        }
        else
        {
            sInstance->mOutput->appendText("WARNING: " + message, false);
        }
        sInstance->mLineCount++;

        sInstance->mOutput->appendText("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\n", false);
        sInstance->mLineCount++;
    }
}

void KVDebugConsole::addLogLineError(const std::string& message)
{
    if (sInstance && sInstance->mOutput && !sInstance->mPaused)
    {
        // S24: Bounds check - prevent unlimited buffer growth
        if (sInstance->mLineCount >= MAX_LINES)
        {
            sInstance->trimBuffer();
        }

        // S24: Subtle dashed separator for errors - slightly different pattern for distinction
        sInstance->mOutput->appendText("= = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =\n", false);
        sInstance->mLineCount++;

        if (!message.empty() && message.back() != '\n')
        {
            sInstance->mOutput->appendText("ERROR: " + message + "\n", false);
        }
        else
        {
            sInstance->mOutput->appendText("ERROR: " + message, false);
        }
        sInstance->mLineCount++;

        sInstance->mOutput->appendText("= = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =\n", false);
        sInstance->mLineCount++;
    }
}

// S24: Helper methods for console command feedback using inverse video highlighting
// These methods use the same background highlighting technique as the log message handlers
// to ensure consistent visual styling throughout the console
void KVDebugConsole::printSuccess(const std::string& message)
{
    if (mOutput)
    {
        // S24: Plain text output
        mOutput->appendText(message, false);
        mLineCount++;
    }
}

void KVDebugConsole::printError(const std::string& message)
{
    if (mOutput)
    {
        // S24: Plain text output
        mOutput->appendText(message, false);
        mLineCount++;
    }
}

void KVDebugConsole::printWarning(const std::string& message)
{
    if (mOutput)
    {
        // S24: Plain text output
        mOutput->appendText(message, false);
        mLineCount++;
    }
}

void KVDebugConsole::printInfo(const std::string& message)
{
    if (mOutput)
    {
        // S24: Plain text output
        mOutput->appendText(message, false);
        mLineCount++;
    }
}

void KVDebugConsole::printNormal(const std::string& message)
{
    if (mOutput)
    {
        // S24: Plain text output
        mOutput->appendText(message, false);
        mLineCount++;
    }
}

// S24: Update persistent beacon - called each frame to track moving targets
void KVDebugConsole::updateBeacon()
{
    if (!sBeaconTarget.active)
    {
        return; // No active beacon
    }

    // If we have a UUID, try to update position from LLWorld (for moving avatars)
    if (sBeaconTarget.uuid.notNull())
    {
        uuid_vec_t avatar_ids;
        std::vector<LLVector3d> positions;
        LLVector3d my_pos = gAgent.getPositionGlobal();

        // Get all avatars (large radius to catch distant ones)
        LLWorld::getInstance()->getAvatars(&avatar_ids, &positions, my_pos, 1000000.0f);

        // Find our target and update position
        for (size_t i = 0; i < avatar_ids.size(); ++i)
        {
            if (avatar_ids[i] == sBeaconTarget.uuid)
            {
                sBeaconTarget.pos_global = positions[i];
                break;
            }
        }
    }

    // Ensure beacons are enabled
    if (!LLPipeline::getRenderBeacons())
    {
        LLPipeline::setRenderBeacons(true);
    }

    // Convert global position to agent-relative position
    LLVector3 pos_agent = gAgent.getPosAgentFromGlobal(sBeaconTarget.pos_global);

    // Re-add the beacon every frame (like sound/light beacons do)
    // Bright magenta GL line from ground to target
    gObjectList.addDebugBeacon(
        pos_agent,
        sBeaconTarget.name,  // Label text
        LLColor4(1.f, 0.f, 1.f, 0.9f),  // Bright magenta line
        LLColor4(1.f, 1.f, 1.f, 1.f),   // White text label
        LLPipeline::DebugBeaconLineWidth
    );
}

// S24: Clear the active beacon
void KVDebugConsole::clearBeacon()
{
    sBeaconTarget.active = false;
    sBeaconTarget.uuid.setNull();
    sBeaconTarget.name.clear();
    // Note: No need to clean up effect, we're using addDebugBeacon now which auto-clears
}

void KVDebugConsole::trimBuffer()
{
    if (!mOutput)
    {
        return;
    }

    // S24: Trim buffer by removing oldest lines from the top
    // Calculate how many lines to remove
    S32 lines_to_remove = mLineCount - TRIM_TO_LINES;

    if (lines_to_remove > 0)
    {
        std::string text = mOutput->getText();

        // Find the Nth newline to know where to cut
        size_t pos = 0;
        for (S32 i = 0; i < lines_to_remove && pos != std::string::npos; ++i)
        {
            pos = text.find('\n', pos);
            if (pos != std::string::npos)
            {
                pos++; // Move past the newline
            }
        }

        if (pos != std::string::npos && pos < text.length())
        {
            // Keep only the text after the cut point
            std::string new_text = text.substr(pos);
            mOutput->setText(new_text);
            mLineCount = TRIM_TO_LINES;

            // Scroll to bottom
            mOutput->setCursorPos(mOutput->getLength());
        }
    }
}

void KVDebugConsole::onClickPause()
{
    mPaused = !mPaused;

    LLButton* pause_btn = findChild<LLButton>("pause_btn");
    if (pause_btn)
    {
        pause_btn->setLabel(mPaused ? "Resume" : "Pause");
    }
}

void KVDebugConsole::onInput(LLUICtrl* ctrl, const LLSD& param)
{
    LLLineEditor* input = static_cast<LLLineEditor*>(ctrl);
    std::string command = input->getText();

    if (!command.empty())
    {
        // Echo the command to console
        mOutput->appendText("> " + command + "\n", false);
        mLineCount++;

        // S24: Parse command and arguments
        // Commands are space-delimited: "command arg1 arg2 arg3..."
        std::string cmd;
        std::string args;
        size_t space_pos = command.find(' ');

        if (space_pos != std::string::npos)
        {
            cmd = command.substr(0, space_pos);
            args = command.substr(space_pos + 1);

            // Trim leading spaces from arguments for cleaner parsing
            size_t first_non_space = args.find_first_not_of(' ');
            if (first_non_space != std::string::npos)
            {
                args = args.substr(first_non_space);
            }
        }
        else
        {
            // No arguments - command only
            cmd = command;
        }

        // S24: Convert command to lowercase for case-insensitive matching
        // This allows "GET", "get", "Get" to all work identically
        LLStringUtil::toLower(cmd);

        // S24: Command execution
        // CRITICAL: Do NOT use lambda captures with references to local variables!
        // The map is static but we need to pass data to handlers via direct calls

        if (cmd == "cls" || cmd == "clear")
        {
            // Clear console output buffer
            mOutput->clear();
            mLineCount = 0;
        }
        else if (cmd == "exit" || cmd == "quit")
        {
            // Close the S24 Console floater
            closeFloater();
        }
        else if (cmd == "help" || cmd == "?")
        {
            // Display help - general or command-specific
            processHelpCommand(args);
        }
        else if (cmd == "list")
        {
            // Display all available commands
            processListCommand();
        }
        else if (cmd == "uuid")
        {
            // UUID lookup and identification
            if (args.empty())
            {
                mOutput->appendText("Usage: uuid <uuid>\n", false);
                mLineCount++;
            }
            else
            {
                processUuidCommand(args);
            }
        }
        else if (cmd == "scan")
        {
            // Scan for nearby avatars (radar functionality)
            processScanCommand(args);
        }
        else if (cmd == "say")
        {
            // Send message in normal chat
            if (args.empty())
            {
                mOutput->appendText("Usage: say <text>\n", false);
                mLineCount++;
            }
            else
            {
                LLFloaterIMNearbyChat::sendChatFromViewer(args, CHAT_TYPE_NORMAL, true);
                mOutput->appendText(llformat("✓ Said: %s\n", args.c_str()), false);
                mLineCount++;
            }
        }
        else if (cmd == "shout")
        {
            // Send message in shout chat
            if (args.empty())
            {
                mOutput->appendText("Usage: shout <text>\n", false);
                mLineCount++;
            }
            else
            {
                LLFloaterIMNearbyChat::sendChatFromViewer(args, CHAT_TYPE_SHOUT, true);
                mOutput->appendText(llformat("✓ Shouted: %s\n", args.c_str()), false);
                mLineCount++;
            }
        }
        else if (cmd == "lookat")
        {
            // Focus camera on avatar or object
            if (args.empty())
            {
                mOutput->appendText("Usage: lookat <name|uuid>\n", false);
                mLineCount++;
            }
            else
            {
                processLookatCommand(args);
            }
        }
        else if (cmd == "find")
        {
            // Select and beacon an object or avatar
            if (args.empty())
            {
                mOutput->appendText("Usage: find <name|uuid>\n", false);
                mLineCount++;
            }
            else
            {
                processFindCommand(args);
            }
        }
        else if (cmd == "tp")
        {
            // Teleport command
            if (args.empty())
            {
                mOutput->appendText("Usage: tp <home|back|uuid|name>\n", false);
                mLineCount++;
            }
            else
            {
                processTpCommand(args);
            }
        }
        else if (cmd == "fly")
        {
            // Flight control command
            processFlyCommand(args);
        }
        else if (cmd == "hippos")
        {
            // S24: Easter egg - unofficial mascot tribute :)
            mOutput->appendText(
                "\n"
                "    ██╗  ██╗██╗██████╗ ██████╗  ██████╗ ███████╗██╗\n"
                "    ██║  ██║██║██╔══██╗██╔══██╗██╔═══██╗██╔════╝██║\n"
                "    ███████║██║██████╔╝██████╔╝██║   ██║███████╗██║\n"
                "    ██╔══██║██║██╔═══╝ ██╔═══╝ ██║   ██║╚════██║╚═╝\n"
                "    ██║  ██║██║██║     ██║     ╚██████╔╝███████║██╗\n"
                "    ╚═╝  ╚═╝╚═╝╚═╝     ╚═╝      ╚═════╝ ╚══════╝╚═╝\n"
                "\n",
                false
            );
            mLineCount += 8;
        }
        else if (cmd == "version" || cmd == "ver")
        {
            // Display viewer version information
            processVersionCommand();
        }
        else if (cmd == "whois")
        {
            // Look up avatar profile information
            if (args.empty())
            {
                mOutput->appendText("Usage: whois <name|uuid>\n", false);
                mOutput->appendText("  Displays avatar profile information\n", false);
                mLineCount += 2;
            }
            else
            {
                processWhoisCommand(args);
            }
        }
        else if (cmd == "get")
        {
            // Retrieve debug setting value(s) with wildcard support
            if (args.empty())
            {
                mOutput->appendText("Usage: get <setting_name> or get *wildcard*\n", false);
                mLineCount++;
            }
            else
            {
                processGetCommand(args);
            }
        }
        else if (cmd == "set")
        {
            // Modify debug setting value with type auto-detection
            if (args.empty())
            {
                mOutput->appendText("Usage: set <setting_name> <value>\n", false);
                mLineCount++;
            }
            else
            {
                processSetCommand(args);
            }
        }
        else if (cmd == "open")
        {
            // Open/close/toggle floaters directly
            if (args.empty())
            {
                mOutput->appendText("Usage: open <list|floater_name> or open close <name> or open toggle <name>\n", false);
                mLineCount++;
            }
            else
            {
                processOpenCommand(args);
            }
        }
        else if (cmd == "shutdown")
        {
            // Shutdown viewer cleanly
            if (args.empty())
            {
                mOutput->appendText("Usage: shutdown <now|seconds>\n", false);
                mLineCount++;
            }
            else
            {
                processShutdownCommand(args);
            }
        }
        else if (cmd == "echo")
        {
            // Echo text back to console (useful for testing/scripting)
            if (args.empty())
            {
                mOutput->appendText("\n", false);
            }
            else
            {
                mOutput->appendText(args + "\n", false);
            }
            mLineCount++;
        }
        else if (cmd == "calc")
        {
            // Simple calculator for math expressions
            if (args.empty())
            {
                mOutput->appendText("Usage: calc <expression>\n", false);
                mOutput->appendText("  Supports: + - * / % ( )\n", false);
                mLineCount += 2;
            }
            else
            {
                processCalcCommand(args);
            }
        }
        else if (cmd == "time")
        {
            // Display current SL time and local time
            processTimeCommand();
        }
        else if (cmd == "uptime")
        {
            // Show viewer uptime
            processUptimeCommand();
        }
        else if (cmd == "motd")
        {
            // Display message of the day
            processMotdCommand();
        }
        else if (cmd == "web")
        {
            // Open URL in browser
            if (args.empty())
            {
                mOutput->appendText("Usage: web <url>\n", false);
                mLineCount++;
            }
            else
            {
                processWebCommand(args);
            }
        }
        else if (cmd == "top")
        {
            // Display top objects by various metrics
            processTopCommand(args);
        }
        else
        {
            // Unknown command - provide helpful error message
            mOutput->appendText("'" + cmd + "' is not recognized as an internal or external command.\n", false);
            mOutput->appendText("Type 'list' to see available commands or 'help <command>' for details.\n", false);
            mLineCount += 2;
        }

        // Clear input line for next command
        input->clear();
    }
}

// S24: Help command - display command reference (DOS-style)
void KVDebugConsole::processHelpCommand(const std::string& topic)
{
    if (topic.empty())
    {
        // General help - brief introduction
        mOutput->appendText(
            "╔═══════════════════════════════════════════════════════════════════════╗\n"
            "║                      S24 CONSOLE v1.0                                 ║\n"
            "╚═══════════════════════════════════════════════════════════════════════╝\n"
            "\n"
            "Welcome to the S24 Console - your command-line interface to the\n"
            "viewer operating system.\n"
            "\n"
            "For a complete list of available commands, type:\n"
            "  list\n"
            "\n"
            "For detailed help on a specific command, type:\n"
            "  help <command>\n"
            "\n"
            "Examples:\n"
            "  list              Show all available commands\n"
            "  help get          Detailed help for GET command\n"
            "  help set          Detailed help for SET command\n"
            "  help top          Overview of TOP command\n"
            "\n"
            "Some commands also support extended help via --help flag:\n"
            "  top --help        Full reference with all flags and examples\n"
            "\n"
            "Refer to the S24 Command Reference documentation for comprehensive\n"
            "information about all console features and advanced usage.\n",
            false
        );
        mLineCount += 23;
    }
    else
    {
        // Command-specific help
        std::string cmd = topic;
        LLStringUtil::toLower(cmd);

        if (cmd == "cls" || cmd == "clear")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  CLS / CLEAR                                                          ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Clears the console output buffer.\n"
                "\n"
                "SYNTAX:\n"
                "  cls\n"
                "  clear\n"
                "\n"
                "DESCRIPTION:\n"
                "  Removes all text from the console window. Does not affect settings\n"
                "  or viewer state, only the display buffer.\n"
                "\n"
                "EXAMPLES:\n"
                "  cls                   Clear the console\n"
                "  CLS                   Same (case-insensitive)\n",
                false
            );
            mLineCount += 17;
        }
        else if (cmd == "exit" || cmd == "quit")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  EXIT / QUIT                                                          ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Closes the S24 Console floater.\n"
                "\n"
                "SYNTAX:\n"
                "  exit\n"
                "  quit\n"
                "\n"
                "DESCRIPTION:\n"
                "  Closes the S24 Console window. Does not exit the viewer,\n"
                "  only closes this floater. Console can be reopened from the\n"
                "  Advanced menu.\n"
                "\n"
                "EXAMPLES:\n"
                "  exit                  Close console\n"
                "  QUIT                  Same (case-insensitive)\n",
                false
            );
            mLineCount += 18;
        }
        else if (cmd == "uuid")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  UUID                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Lookup and identify what a UUID represents in the viewer.\n"
                "\n"
                "SYNTAX:\n"
                "  uuid <uuid_string>\n"
                "\n"
                "DESCRIPTION:\n"
                "  Searches local caches to identify what a UUID represents:\n"
                "  - Avatars (in scene or in cache)\n"
                "  - Objects and prims\n"
                "  - Textures and assets\n"
                "  - Inventory items\n"
                "  - Groups\n"
                "  - Special UUIDs (NULL_UUID, your agent ID, session ID)\n"
                "\n"
                "  Displays detailed information including names, distances, sizes,\n"
                "  and other relevant data depending on the UUID type.\n"
                "\n"
                "EXAMPLES:\n"
                "  uuid 00000000-0000-0000-0000-000000000000   Check NULL_UUID\n"
                "  uuid 12345678-1234-1234-1234-123456789abc   Lookup any UUID\n",
                false
            );
            mLineCount += 24;
        }
        else if (cmd == "scan")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  SCAN                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Radar functionality - scan for nearby avatars.\n"
                "\n"
                "SYNTAX:\n"
                "  scan              List all avatars in scene (unlimited range)\n"
                "  scan <range>      List avatars within specified distance (meters)\n"
                "\n"
                "DESCRIPTION:\n"
                "  Scans the current scene for all rendered avatars and displays:\n"
                "  - Avatar display name\n"
                "  - Distance from your position (in meters)\n"
                "  - Avatar UUID (for use with UUID command)\n"
                "\n"
                "  Results are sorted by distance (nearest first). Your own avatar\n"
                "  is excluded from results.\n"
                "\n"
                "  Optional range parameter filters results to only show avatars\n"
                "  within the specified distance in meters.\n"
                "\n"
                "EXAMPLES:\n"
                "  scan                  List all avatars currently in scene\n"
                "  scan 50               List avatars within 50 meters\n"
                "  scan 10               Show only avatars within 10m (close range)\n"
                "  scan 96               Default draw distance scan\n"
                "\n"
                "OUTPUT FORMAT:\n"
                "  Distance (m)  Avatar Name                    UUID\n"
                "  ------------  -----------------------------  ---------------------\n"
                "    12.3m      Username Resident              12345678-1234-...\n"
                "\n"
                "NOTES:\n"
                "  - Only shows avatars currently rendered in your viewer\n"
                "  - Avatars beyond draw distance will not appear\n"
                "  - Use UUID from output with 'uuid <id>' for detailed info\n"
                "\n"
                "SEE ALSO:\n"
                "  uuid - Get detailed information about an avatar UUID\n",
                false
            );
            mLineCount += 44;
        }
        else if (cmd == "get")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  GET                                                                  ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Retrieve debug setting values from gSavedSettings.\n"
                "\n"
                "SYNTAX:\n"
                "  get <setting_name>    Get exact setting by name\n"
                "  get *wildcard*        Search settings using wildcards\n"
                "\n"
                "DESCRIPTION:\n"
                "  Displays the current value of one or more debug settings.\n"
                "  \n"
                "  EXACT MODE:\n"
                "    Retrieves a single setting and shows:\n"
                "    - Full setting name\n"
                "    - Data type (BOOLEAN, S32, F32, STRING, etc.)\n"
                "    - Current value\n"
                "    - Description/comment (if available)\n"
                "  \n"
                "  WILDCARD MODE:\n"
                "    Searches all settings for names containing the pattern.\n"
                "    Returns list of matching settings with types and values.\n"
                "    Asterisks (*) are treated as wildcards (any characters).\n"
                "    Search is case-insensitive.\n"
                "    Limited to 50 results to prevent spam.\n"
                "\n"
                "EXAMPLES:\n"
                "  get DebugShowFPS              Show FPS counter setting\n"
                "  get RenderFarClip             Show draw distance\n"
                "  get *texture*                 List all texture settings\n"
                "  get *render*quality*          Find quality-related render settings\n"
                "  get *thread*                  List threading settings\n"
                "\n"
                "SEE ALSO:\n"
                "  set - Modify setting values\n",
                false
            );
            mLineCount += 39;
        }
        else if (cmd == "set")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  SET                                                                  ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Modify debug setting values in gSavedSettings.\n"
                "\n"
                "SYNTAX:\n"
                "  set <setting_name> <value>\n"
                "\n"
                "DESCRIPTION:\n"
                "  Changes the value of a debug setting. The setting type is\n"
                "  automatically detected and the value is converted accordingly.\n"
                "  Shows the old value → new value upon success.\n"
                "\n"
                "SUPPORTED TYPES:\n"
                "  BOOLEAN    true/false, 1/0, yes/no, on/off (case-insensitive)\n"
                "  S32        Signed 32-bit integer (-2147483648 to 2147483647)\n"
                "  U32        Unsigned 32-bit integer (0 to 4294967295)\n"
                "  F32        Floating-point number (decimal)\n"
                "  STRING     Text string (quotes optional)\n"
                "\n"
                "EXAMPLES:\n"
                "  set DebugShowFPS true         Enable FPS counter\n"
                "  set DebugShowFPS false        Disable FPS counter\n"
                "  set RenderFarClip 256         Set draw distance to 256m\n"
                "  set RenderVolumeLODFactor 4.5 Increase LOD quality\n"
                "  set FirstName \"TestUser\"      Set string with quotes\n"
                "  set FirstName TestUser        Set string without quotes\n"
                "\n"
                "NOTES:\n"
                "  - Some settings take effect immediately (e.g., DebugShowFPS)\n"
                "  - Others require restart or specific actions (e.g., MaxHeapSize64)\n"
                "  - Changes are saved to settings.xml on clean viewer exit\n"
                "  - Invalid values will be rejected with an error message\n"
                "\n"
                "SEE ALSO:\n"
                "  get - Retrieve setting values\n",
                false
            );
            mLineCount += 41;
        }
        else if (cmd == "help")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  HELP                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Display command reference and usage information.\n"
                "\n"
                "SYNTAX:\n"
                "  help              Show general console help\n"
                "  help <command>    Show detailed help for specific command\n"
                "\n"
                "EXAMPLES:\n"
                "  help              General introduction\n"
                "  help get          Detailed help for GET command\n"
                "  help uuid         Detailed help for UUID command\n"
                "\n"
                "SEE ALSO:\n"
                "  list - Show all available commands\n",
                false
            );
            mLineCount += 18;
        }
        else if (cmd == "list")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  LIST                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Display list of all available console commands.\n"
                "\n"
                "SYNTAX:\n"
                "  list\n"
                "\n"
                "DESCRIPTION:\n"
                "  Shows a comprehensive list of all commands currently available\n"
                "  in the S24 Debug Console with brief descriptions.\n"
                "\n"
                "EXAMPLES:\n"
                "  list              Show all commands\n"
                "\n"
                "SEE ALSO:\n"
                "  help - Show detailed help for specific commands\n",
                false
            );
            mLineCount += 19;
        }
        else if (cmd == "open")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  OPEN                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Open, close, or toggle viewer floater windows directly.\n"
                "\n"
                "SYNTAX:\n"
                "  open list              List all registered floaters\n"
                "  open <floater_name>    Open a specific floater window\n"
                "  open close <name>      Close a specific floater window\n"
                "  open toggle <name>     Toggle a floater's visibility\n"
                "\n"
                "DESCRIPTION:\n"
                "  Direct control over viewer floater windows using their registered\n"
                "  names. This bypasses menu navigation and allows instant window\n"
                "  control from the console.\n"
                "\n"
                "  The 'open list' command shows all commonly-used floaters with\n"
                "  their registration status and descriptive names.\n"
                "\n"
                "  Floater names are case-sensitive and must match exactly.\n"
                "\n"
                "EXAMPLES:\n"
                "  open list                     List all available floaters\n"
                "  open inventory                Open inventory window\n"
                "  open preferences              Open preferences\n"
                "  open world_map                Open world map\n"
                "  open close world_map          Close world map\n"
                "  open toggle beacons           Toggle beacons floater\n"
                "  open debug_console            Open S24 Console\n"
                "  open build                    Open build tools\n"
                "\n"
                "COMMON FLOATERS:\n"
                "  inventory, preferences, world_map, camera, gestures, people,\n"
                "  appearance, search, nearby_chat, beacons, build, snapshot,\n"
                "  destinations, help_browser, map, scene_load_stats, etc.\n"
                "\n"
                "SAFETY:\n"
                "  Some floaters are protected from direct opening to prevent crashes\n"
                "  or server errors when opened without required context/data:\n"
                "\n"
                "  SAFE (Passive):\n"
                "    inventory, world_map, preferences, beacons, build, camera,\n"
                "    gestures, people, appearance, search, nearby_chat, etc.\n"
                "    These are standalone windows with no special requirements.\n"
                "\n"
                "  AUTO-FILLED (Your Data):\n"
                "    profile → Opens YOUR profile automatically\n"
                "    avatar_textures → Shows YOUR avatar textures\n"
                "\n"
                "  PROTECTED (Server/Context Required):\n"
                "    - Session-based: impanel, incoming_call, outgoing_call\n"
                "    - ID-required: event, classified, experience_profile\n"
                "    - Picker dialogs: group_picker, avatar_picker\n"
                "    - Properties: item_properties, task_properties\n"
                "    - Preview: preview_* floaters (need inventory assets)\n"
                "    - Upload: upload_* floaters (need file context)\n"
                "    - Object-based: inspect, openobject\n"
                "\n"
                "  The console will provide helpful guidance if you attempt to\n"
                "  open protected floaters and suggest the proper access path.\n"
                "\n"
                "NOTES:\n"
                "  - System floaters (like god_tools) may require admin access\n"
                "  - Floater names are case-sensitive and must match exactly\n"
                "  - Use 'open list' to see all available floaters\n"
                "\n"
                "SEE ALSO:\n"
                "  list - Show all console commands\n",
                false
            );
            mLineCount += 48;
        }
        else if (cmd == "shutdown")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  SHUTDOWN                                                             ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Immediately shut down the viewer without confirmation prompts.\n"
                "\n"
                "⚠ WARNING: This is a HARD STOP for experienced users only!\n"
                "   Bypasses ALL cleanup dialogs and exits immediately.\n"
                "\n"
                "SYNTAX:\n"
                "  shutdown now         Immediately quit viewer (BRUTAL exit)\n"
                "  shutdown <seconds>   Schedule shutdown after timer countdown\n"
                "  shutdown cancel      Abort a scheduled shutdown\n"
                "\n"
                "DESCRIPTION:\n"
                "  Provides direct control over viewer shutdown. This is NOT a graceful\n"
                "  exit - it's an immediate STOP that bypasses all user prompts and\n"
                "  cleanup dialogs (including Ares process cleanup).\n"
                "\n"
                "  IMMEDIATE MODE ('shutdown now'):\n"
                "    Triggers an immediate viewer exit. The client will be GONE.\n"
                "    Settings are saved and logout is attempted, but no dialogs shown.\n"
                "\n"
                "  TIMER MODE ('shutdown <seconds>'):\n"
                "    Schedules a shutdown after the specified number of seconds.\n"
                "    Countdown warnings are displayed at 10s, 5s, and 3s intervals.\n"
                "    Timer can be cancelled with 'shutdown cancel' before expiry.\n"
                "    When timer expires, shutdown is immediate (same as 'now').\n"
                "\n"
                "  CANCEL MODE ('shutdown cancel'):\n"
                "    Aborts an active shutdown timer. Has no effect if no timer\n"
                "    is currently running.\n"
                "\n"
                "EXAMPLES:\n"
                "  shutdown now                  Quit immediately (BRUTAL)\n"
                "  shutdown 60                   Quit in 60 seconds\n"
                "  shutdown 300                  Quit in 5 minutes\n"
                "  shutdown cancel               Cancel scheduled shutdown\n"
                "\n"
                "NOTES:\n"
                "  - This is a POWER USER command for testing and automation\n"
                "  - No confirmation dialogs are shown\n"
                "  - Ares cleanup dialogs are bypassed\n"
                "  - Timer warnings appear in the console output\n"
                "  - For normal exit, use File > Quit instead\n"
                "\n"
                "SEE ALSO:\n"
                "  exit - Close the S24 Console only (does not quit viewer)\n",
                false
            );
            mLineCount += 50;
        }
        else if (cmd == "echo")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  ECHO                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Print text back to the console.\n"
                "\n"
                "SYNTAX:\n"
                "  echo <text>\n"
                "\n"
                "DESCRIPTION:\n"
                "  Displays the provided text in the console output. Useful for\n"
                "  testing, scripting, debugging, or creating visual separators.\n"
                "\n"
                "EXAMPLES:\n"
                "  echo Hello World!                Print simple text\n"
                "  echo ═══════════════════════     Create separator line\n"
                "  echo Testing console output...   Debugging message\n"
                "\n"
                "SEE ALSO:\n"
                "  cls - Clear console output\n",
                false
            );
            mLineCount += 23;
        }
        else if (cmd == "calc")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  CALC                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Calculate math expressions.\n"
                "\n"
                "SYNTAX:\n"
                "  calc <expression>\n"
                "\n"
                "DESCRIPTION:\n"
                "  Evaluates mathematical expressions using standard operators.\n"
                "  Supports floating-point arithmetic with proper order of operations.\n"
                "\n"
                "OPERATORS:\n"
                "  +    Addition\n"
                "  -    Subtraction (also unary minus)\n"
                "  *    Multiplication\n"
                "  /    Division\n"
                "  %    Modulo (remainder)\n"
                "  ( )  Parentheses for grouping\n"
                "\n"
                "EXAMPLES:\n"
                "  calc 2 + 3 * 4              = 14 (multiplication first)\n"
                "  calc (2 + 3) * 4            = 20 (parentheses override)\n"
                "  calc 100 / 3                = 33.333333 (floating-point)\n"
                "  calc 512 / 2                = 256 (draw distance half)\n"
                "  calc -5 + 10                = 5 (unary minus)\n"
                "  calc 17 % 5                 = 2 (modulo/remainder)\n"
                "\n"
                "NOTES:\n"
                "  - Results are displayed with 6 decimal places\n"
                "  - Division by zero produces an error\n"
                "  - Order of operations: ( ) then * / % then + -\n"
                "\n"
                "SEE ALSO:\n"
                "  set - Use calc results to set debug settings\n",
                false
            );
            mLineCount += 44;
        }
        else if (cmd == "time")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  TIME                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Display current Second Life time and local time.\n"
                "\n"
                "SYNTAX:\n"
                "  time\n"
                "\n"
                "DESCRIPTION:\n"
                "  Shows both SL Time (Pacific Time) and your local system time\n"
                "  in YYYY-MM-DD HH:MM:SS format.\n"
                "\n"
                "EXAMPLES:\n"
                "  time                        Show current time\n"
                "\n"
                "SEE ALSO:\n"
                "  uptime - Show viewer runtime duration\n",
                false
            );
            mLineCount += 21;
        }
        else if (cmd == "uptime")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  UPTIME                                                               ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Show how long the viewer has been running.\n"
                "\n"
                "SYNTAX:\n"
                "  uptime\n"
                "\n"
                "DESCRIPTION:\n"
                "  Displays the total runtime of the viewer since launch,\n"
                "  formatted as days, hours, minutes, and seconds.\n"
                "\n"
                "  Useful for:\n"
                "  - Monitoring stability (long uptimes = stable viewer)\n"
                "  - Checking if viewer needs restart\n"
                "  - Debugging memory leak issues over time\n"
                "\n"
                "EXAMPLES:\n"
                "  uptime                      Show viewer runtime\n"
                "\n"
                "SEE ALSO:\n"
                "  time - Show current SL and local time\n",
                false
            );
            mLineCount += 27;
        }
        else if (cmd == "version" || cmd == "ver")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  VERSION / VER                                                        ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Display viewer version and build information.\n"
                "\n"
                "SYNTAX:\n"
                "  version\n"
                "  ver\n"
                "\n"
                "DESCRIPTION:\n"
                "  Shows comprehensive version information including:\n"
                "  - Channel name\n"
                "  - Full version string with build number\n"
                "  - Version components (major, minor, patch)\n"
                "  - Build configuration\n"
                "  - Platform and architecture\n"
                "\n"
                "  Useful for bug reports, support requests, and verifying\n"
                "  you're running the correct viewer build.\n"
                "\n"
                "EXAMPLES:\n"
                "  version                     Show version info\n"
                "  ver                         Same (short alias)\n",
                false
            );
            mLineCount += 30;
        }
        else if (cmd == "whois")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  WHOIS                                                                ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Look up avatar identity by name or UUID.\n"
                "\n"
                "SYNTAX:\n"
                "  whois <name>\n"
                "  whois <uuid>\n"
                "\n"
                "DESCRIPTION:\n"
                "  Displays avatar identity information including:\n"
                "  - Display Name: User's chosen display name (can be changed)\n"
                "  - Username:     Permanent SL account name (resident.name format)\n"
                "  - UUID:         Unique avatar identifier\n"
                "\n"
                "  Searches nearby avatars by name or looks up specific UUID directly.\n"
                "  Data is retrieved from the viewer's name cache (no server request).\n"
                "\n"
                "EXAMPLES:\n"
                "  whois Foo                     Partial name search (nearby)\n"
                "  whois FooResident             Full username search\n"
                "  whois 12345678-1234-...       Look up by UUID\n"
                "\n"
                "NOTES:\n"
                "  - Name search only finds nearby avatars in current region\n"
                "  - UUID lookup works for any cached avatar\n"
                "  - Data comes from viewer cache (must have seen avatar recently)\n",
                false
            );
            mLineCount += 33;
        }
        else if (cmd == "motd")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  MOTD                                                                 ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Display Message of the Day captured at login.\n"
                "\n"
                "SYNTAX:\n"
                "  motd\n"
                "\n"
                "DESCRIPTION:\n"
                "  Displays the Message of the Day (MOTD) that was received from\n"
                "  the grid at login. This message typically contains:\n"
                "  - Grid announcements and notices\n"
                "  - Scheduled maintenance information\n"
                "  - Important updates or policy changes\n"
                "  - Links to relevant websites or resources\n"
                "\n"
                "  The MOTD is captured once at login and remains available for\n"
                "  the duration of the session. If any URLs are present in the\n"
                "  message, they will be highlighted separately for easy access.\n"
                "\n"
                "EXAMPLES:\n"
                "  motd                          Show message of the day\n"
                "\n"
                "NOTES:\n"
                "  - MOTD is only available after a successful login\n"
                "  - Message persists until viewer restart\n"
                "  - Some grids may not provide a MOTD\n",
                false
            );
            mLineCount += 36;
        }
        else if (cmd == "web")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  WEB                                                                  ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Open a URL in your preferred web browser.\n"
                "\n"
                "SYNTAX:\n"
                "  web <url>\n"
                "\n"
                "DESCRIPTION:\n"
                "  Opens the specified URL using your configured browser preference.\n"
                "  Respects the 'Web browser' setting in Preferences > Advanced:\n"
                "  - Internal browser: Opens in S24's built-in web content floater\n"
                "  - External browser: Opens in your system's default web browser\n"
                "\n"
                "  If no protocol is specified (http://, https://), defaults to https://\n"
                "  Supported protocols: http://, https://, ftp://\n"
                "\n"
                "  Useful for:\n"
                "  - Quickly opening documentation or help pages\n"
                "  - Testing URLs from console without typing in browser\n"
                "  - Opening links from MOTD or other console output\n"
                "  - Accessing grid-related web resources\n"
                "\n"
                "EXAMPLES:\n"
                "  web https://secondlife.com        Open SL homepage\n"
                "  web secondlife.com                Assumes https:// protocol\n"
                "  web http://example.com/docs       Open documentation\n"
                "  web https://wiki.secondlife.com   Open SL wiki\n"
                "\n"
                "NOTES:\n"
                "  - Browser choice is controlled by viewer Preferences\n"
                "  - Internal browser supports most web content\n"
                "  - External browser uses your OS default (Chrome, Firefox, etc.)\n"
                "  - URLs are validated before opening\n",
                false
            );
            mLineCount += 42;
        }
        else if (cmd == "top")
        {
            mOutput->appendText(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  TOP                                                                  ║\n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n"
                "\n"
                "Display ranked list of objects by performance metrics.\n"
                "\n"
                "BASIC SYNTAX:\n"
                "  top [mode]                     Show top 10 objects by metric\n"
                "  top --help                     Show FULL command reference\n"
                "\n"
                "COMMON MODES:\n"
                "  cost, render        Object render cost (default)\n"
                "  physics, phys       Physics simulation cost\n"
                "  streaming, stream   Asset streaming/download cost\n"
                "  triangles, tris     Triangle/polygon count\n"
                "  script, scripts     Scripted objects (sorted by render cost)\n"
                "\n"
                "BASIC EXAMPLES:\n"
                "  top                 Show top 10 by render cost (all regions)\n"
                "  top physics         Show physics-heavy objects\n"
                "  top scripts         Show scripted objects only\n"
                "\n"
                "ADVANCED FEATURES:\n"
                "  The top command supports UNIX-style flags for advanced filtering:\n"
                "\n"
                "  SCOPE FILTERS:\n"
                "    -p, --parcel      Limit to current parcel only\n"
                "    -r, --region      Limit to current region only\n"
                "\n"
                "  OWNERSHIP FILTERS:\n"
                "    -s, --self        Show only YOUR objects\n"
                "    -g, --group       Show only group-owned objects\n"
                "    -o UUID           Filter by specific owner UUID\n"
                "\n"
                "  DISPLAY OPTIONS:\n"
                "    -n N, --limit N   Show top N results (default: 10, max: 50)\n"
                "\n"
                "ADVANCED EXAMPLES:\n"
                "  top scripts -p -s            YOUR scripts in YOUR parcel\n"
                "  top cost -p --limit 20       Top 20 in parcel (any owner)\n"
                "  top physics -r -g            Group objects in region\n"
                "  top scripts -o <UUID>        All scripts by specific owner\n"
                "\n"
                "FULL DOCUMENTATION:\n"
                "  For complete flag reference, detailed examples, and workflows:\n"
                "    top --help\n"
                "    top -h\n"
                "\n"
                "USAGE WORKFLOW:\n"
                "  1. Run top command to identify problem objects\n"
                "  2. Copy UUID from results\n"
                "  3. Use 'find <uuid>' to beacon and locate object in-world\n"
                "  4. Delete or optimize the object\n",
                false
            );
            mLineCount += 62;
        }
        else
        {
            mOutput->appendText(llformat(
                "✗ No help available for '%s'\n"
                "\n"
                "Type 'list' to see all available commands.\n"
                "Type 'help <command>' for detailed help on a specific command.\n",
                topic.c_str()
            ), false);
            mLineCount += 4;
        }
    }
}

// S24: List command - show all available commands
void KVDebugConsole::processListCommand()
{
    mOutput->appendText(
        "╔═══════════════════════════════════════════════════════════════════════╗\n"
        "║                 S24 CONSOLE - AVAILABLE COMMANDS                      ║\n"
        "╚═══════════════════════════════════════════════════════════════════════╝\n"
        "\n"
        "SYSTEM COMMANDS:\n"
        "  cls, clear          Clear the console output buffer\n"
        "  exit, quit          Close the S24 Console floater\n"
        "  help [command]      Display general help or command-specific help\n"
        "  list                Display this command list\n"
        "  ?                   Alias for help (DOS compatibility)\n"
        "\n"
        "DIAGNOSTIC COMMANDS:\n"
        "  uuid <uuid>         Lookup and identify what a UUID represents\n"
        "                      (avatars, objects, textures, inventory, groups)\n"
        "  scan [range]        Radar - list nearby avatars with distance & UUID\n"
        "                      (optional range in meters, default = unlimited)\n"
        "  lookat <name|uuid>  Focus camera on avatar or object by name/UUID\n"
        "                      (partial name matching, case-insensitive)\n"
        "  find <name|uuid>    Select and beacon an object/avatar (shows beacon)\n"
        "                      (great for finding transparent or hard to see items)\n"
        "  tp <dest>           Teleport to location (home/back/cam/uuid/avatar name)\n"
        "                      (use positions from scan/lookat for coordinates)\n"
        "  fly [height]        Toggle flight or fly to height (fly 1000, fly down)\n"
        "                      (enables flight mode before vertical positioning)\n"
        "\n"
        "CHAT COMMANDS:\n"
        "  say <text>          Send message in normal chat (20m range)\n"
        "  shout <text>        Send message in shout chat (100m range)\n"
        "\n"
        "SETTINGS COMMANDS:\n"
        "  get <name>          Retrieve debug setting value (exact match)\n"
        "  get *wildcard*      Search settings using wildcards (* = any chars)\n"
        "  set <name> <value>  Modify debug setting value\n"
        "                      (supports BOOL, S32, U32, F32, STRING types)\n"
        "\n"
        "FLOATER COMMANDS:\n"
        "  open list           List all registered floaters in viewer\n"
        "  open <name>         Open a specific floater window\n"
        "  open close <name>   Close a specific floater window\n"
        "  open toggle <name>  Toggle a floater's visibility\n"
        "\n"
        "SYSTEM COMMANDS:\n"
        "  shutdown now        Immediately quit viewer (clean shutdown, no prompts)\n"
        "  shutdown <seconds>  Quit viewer after timer countdown\n"
        "  top [mode]          Display top 10 objects by cost/physics/streaming/tris/script\n"
        "\n"
        "CONSOLE UTILITIES:\n"
        "  echo <text>         Print text back to console (useful for testing)\n"
        "  calc <expression>   Calculate math expression (+ - * / % parentheses)\n"
        "  time                Display current SL time and local time\n"
        "  uptime              Show how long the viewer has been running\n"
        "  version, ver        Display viewer version and build information\n"
        "  whois <name|uuid>   Look up avatar profile (display name, username, UUID)\n"
        "  motd                Display message of the day (captured at login)\n"
        "  web <url>           Open URL in browser (respects preferences)\n"
        "\n"
        "USAGE EXAMPLES:\n"
        "  help get                      Show detailed GET command help\n"
        "  cls                           Clear screen\n"
        "  scan                          List all avatars in scene\n"
        "  scan 50                       List avatars within 50m\n"
        "  uuid 00000000-0000-0000...    Identify a UUID\n"
        "  lookat Foo Resident           Focus camera on avatar (partial name OK)\n"
        "  lookat 00000000-0000-...      Focus camera by UUID\n"
        "  find transparent_object       Select & beacon object (shows beacon)\n"
        "  find 00000000-0000-...        Beacon object by UUID\n"
        "  tp home                       Teleport to home location\n"
        "  tp back                       Teleport to previous location\n"
        "  tp cam                        Teleport to camera position\n"
        "  tp Foo Resident               Teleport to avatar\n"
        "  tp 00000000-0000-...          Teleport to avatar/object by UUID\n"
        "  fly                           Toggle flight mode on/off\n"
        "  fly 1000                      Fly 1000m above current position\n"
        "  fly -50                       Descend 50m from current position\n"
        "  fly down                      Land at ground level\n"
        "  say Hello everyone!           Say in normal chat\n"
        "  shout EVERYONE LISTEN!        Shout in chat\n"
        "  get *texture*                 List all texture-related settings\n"
        "  get DebugShowFPS              Show FPS counter setting\n"
        "  set DebugShowFPS true         Enable FPS counter\n"
        "  set RenderFarClip 512         Set draw distance\n"
        "  open list                     List all available floaters\n"
        "  open inventory                Open inventory floater\n"
        "  open close inventory          Close inventory floater\n"
        "  open toggle preferences       Toggle preferences window\n"
        "  shutdown now                  Quit viewer immediately\n"
        "  shutdown 30                   Quit viewer in 30 seconds\n"
        "  calc 2 + 2 * 3                Calculate expression (result: 8)\n"
        "  version                       Show viewer version info\n"
        "  whois FooResident             Look up avatar profile\n"
        "  exit                          Close console\n"
        "\n"
        "NOTE: Commands are case-insensitive (GET, get, Get all work).\n"
        "\n"
        "For detailed information about a specific command, type:\n"
        "  help <command>\n"
        "\n"
        "Refer to S24 Command Reference documentation for comprehensive guide.\n",
        false
    );
    mLineCount += 52;
}

// S24: UUID lookup command - identify what a UUID represents
void KVDebugConsole::processUuidCommand(const std::string& uuid_str)
{
    LLUUID id;
    if (!id.set(uuid_str, false)) // false = don't generate warning on parse failure
    {
        mOutput->appendText("✗ Invalid UUID format\n", false);
        mLineCount++;
        return;
    }

    std::string result;
    bool found = false;

    // Check for special UUIDs first
    if (id.isNull())
    {
        result = "✓ NULL_UUID (00000000-0000-0000-0000-000000000000)\n";
        found = true;
    }
    else if (id == gAgentID)
    {
        result = "✓ YOUR AGENT ID\n";
        found = true;
    }
    else if (id == gAgent.getSessionID())
    {
        result = "✓ YOUR SESSION ID\n";
        found = true;
    }

    // Check for avatar (rendered in scene)
    if (!found)
    {
        LLViewerObject* obj = gObjectList.findObject(id);
        if (obj && obj->isAvatar())
        {
            LLVOAvatar* avatar = (LLVOAvatar*)obj;
            std::string av_name = avatar->getFullname();
            LLVector3d av_pos = avatar->getPositionGlobal();
            LLVector3d my_pos = gAgent.getPositionGlobal();
            F64 distance = dist_vec(av_pos, my_pos);

            result = llformat("✓ AVATAR (in scene): \"%s\"\n  Distance: %.1fm\n", 
                av_name.c_str(), distance);
            found = true;
        }
    }

    // Check avatar name cache (profiles, IMs, group members, etc.)
    if (!found)
    {
        LLAvatarName av_name;
        if (LLAvatarNameCache::get(id, &av_name))
        {
            result = llformat("✓ AVATAR: \"%s\"\n  Username: %s\n  (not currently in scene)\n", 
                av_name.getDisplayName().c_str(),
                av_name.getUserName().c_str());
            found = true;
        }
    }

    // Check for object
    if (!found)
    {
        LLViewerObject* obj = gObjectList.findObject(id);
        if (obj && !obj->isAvatar())
        {
            LLVector3d obj_pos = obj->getPositionGlobal();
            LLVector3d my_pos = gAgent.getPositionGlobal();
            F64 distance = dist_vec(obj_pos, my_pos);

            result = llformat("✓ OBJECT (ID match in scene)\n  Distance: %.1fm, Prims: %d\n", 
                distance, obj->numChildren() + 1);
            found = true;
        }
    }

    // Check for texture
    if (!found)
    {
        // Try to find already-loaded texture first
        LLViewerFetchedTexture* texture = LLViewerTextureManager::findFetchedTexture(id, TEX_LIST_STANDARD);

        // If not found, try to get/fetch it (this will trigger a fetch if valid UUID)
        if (!texture)
        {
            texture = LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, TRUE, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
        }

        if (texture)
        {
            S32 width = texture->getFullWidth();
            S32 height = texture->getFullHeight();
            S32 components = texture->getComponents();
            S32 discard = texture->getDiscardLevel();

            // Check if it's actually loaded with real data
            if (width > 0 && height > 0)
            {
                std::string format = (components == 4) ? "RGBA" : (components == 3) ? "RGB" : "Other";
                result = llformat("✓ TEXTURE: %dx%d %s, Discard: %d\n", 
                    width, height, format.c_str(), discard);
                found = true;
            }
            else if (texture->isMissingAsset())
            {
                result = "✗ TEXTURE UUID (asset not found on server)\n";
                found = true;
            }
            else if (texture->getDiscardLevel() < 0)
            {
                result = "✓ TEXTURE UUID (valid)\n";
                found = true;
            }
        }
    }

    // Check for inventory item
    if (!found)
    {
        LLViewerInventoryItem* item = gInventory.getItem(id);
        if (item)
        {
            std::string item_name = item->getName();
            std::string type_name = LLInventoryType::lookupHumanReadable(item->getInventoryType());

            result = llformat("✓ INVENTORY ITEM: \"%s\"\n  Type: %s\n", 
                item_name.c_str(), type_name.c_str());
            found = true;
        }
    }

    // Check for group
    if (!found)
    {
        std::string group_name;
        if (gCacheName->getGroupName(id, group_name))
        {
            result = llformat("✓ GROUP: \"%s\"\n", group_name.c_str());
            found = true;
        }
    }

    // Nothing found
    if (!found)
    {
        result = "✗ UUID not found in local cache\n  (may be inactive object, remote texture, or unknown type)\n";
    }

    mOutput->appendText(result, false);
    mLineCount++;
}

// S24: Scan command - radar functionality for nearby avatars
// Lists all avatars currently rendered in the scene with distance and UUID
void KVDebugConsole::processScanCommand(const std::string& args)
{
    // S24: Parse optional range argument (defaults to unlimited)
    // Usage: scan [max_distance]
    F64 max_range = 0.0; // 0 = unlimited range
    if (!args.empty())
    {
        max_range = atof(args.c_str());
        if (max_range < 0.0)
        {
            mOutput->appendText("✗ Invalid range. Use positive number or omit for unlimited.\n", false);
            mLineCount++;
            return;
        }
    }

    // S24: Server name cache foible - first scan primes UUID->name cache, second scan shows names
    // Solution: Do silent pre-scan, wait 1 sec for server name resolution, then display results
    mOutput->appendText("Scanning avatars...\n", false);
    mLineCount++;

    // S24: Silent pre-scan to prime the name cache
    uuid_vec_t priming_ids;
    std::vector<LLVector3d> priming_positions;
    LLVector3d my_pos_prime = gAgent.getPositionGlobal();
    F32 radius_prime = (max_range > 0.0) ? (F32)max_range : 1000000.0f;
    LLWorld::getInstance()->getAvatars(&priming_ids, &priming_positions, my_pos_prime, radius_prime);

    // Request name lookups for all discovered UUIDs
    for (const LLUUID& av_id : priming_ids)
    {
        if (av_id != gAgent.getID())  // Skip self
        {
            LLAvatarName av_name;
            LLAvatarNameCache::get(av_id, &av_name);  // Prime the cache
        }
    }

    // S24: Schedule the actual scan display after 1 second to allow name resolution
    mPendingScan.active = true;
    mPendingScan.max_range = max_range;
    mPendingScan.delay_remaining = 1.0f;  // 1 second delay
}

// S24: Helper - performs the actual scan and displays results (after name cache primed)
void KVDebugConsole::performScanDisplay(F64 max_range)
{
    // S24: Collect all avatars currently in scene using the proper LLWorld API
    // This is the same method used by minimap and nearby people floater
    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> positions;
    LLVector3d my_pos = gAgent.getPositionGlobal();

    // Get all avatars (uses LLCharacter::sInstances + region coarse location updates)
    // Pass max_range as radius filter, or a very large number for unlimited
    // Note: F32_MAX can cause issues, use 1000000.0f (1000km) for "unlimited" instead
    F32 radius = (max_range > 0.0) ? (F32)max_range : 1000000.0f;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &positions, my_pos, radius);

    // S24: Build list of avatars with distances, excluding self
    struct AvatarInfo {
        LLUUID id;
        LLVector3d pos;
        F64 distance;
    };
    std::vector<AvatarInfo> avatars;

    for (size_t i = 0; i < avatar_ids.size(); ++i)
    {
        // Skip self
        if (avatar_ids[i] == gAgent.getID())
            continue;

        F64 distance = dist_vec(positions[i], my_pos);
        avatars.push_back({avatar_ids[i], positions[i], distance});
    }

    // S24: Sort avatars by distance (nearest first)
    std::sort(avatars.begin(), avatars.end(), [](const AvatarInfo& a, const AvatarInfo& b) {
        return a.distance < b.distance;
    });

    // S24: Display results
    if (avatars.empty())
    {
        if (max_range > 0.0)
        {
            mOutput->appendText(llformat("No avatars found within %.1fm\n", max_range), false);
        }
        else
        {
            mOutput->appendText("No avatars currently in scene (you may be alone)\n", false);
        }
        mLineCount++;
    }
    else
    {
        // Header
        std::string header;
        if (max_range > 0.0)
        {
            header = llformat(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  AVATAR SCAN - %d avatar(s) within %.0fm                              \n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n",
                (int)avatars.size(), max_range
            );
        }
        else
        {
            header = llformat(
                "╔═══════════════════════════════════════════════════════════════════════╗\n"
                "║  AVATAR SCAN - %d avatar(s) in scene                                  \n"
                "╚═══════════════════════════════════════════════════════════════════════╝\n",
                (int)avatars.size()
            );
        }
        mOutput->appendText(header, false);
        mLineCount += 3;

        // Column headers
        mOutput->appendText(
            "\n"
            "DISTANCE  NAME                              UUID\n"
            "--------  --------------------------------  ------------------------------------\n",
            false
        );
        mLineCount += 3;

        // S24: Display each avatar with formatting
        // Format: Distance (right-aligned), Name (left-aligned, truncated), UUID
        int displayed_count = 0;
        int uncached_count = 0;
        for (const AvatarInfo& av_info : avatars)
        {
            // Get avatar name from cache (same method as minimap/nearby floater)
            LLAvatarName av_name;
            std::string display_name;

            if (LLAvatarNameCache::get(av_info.id, &av_name))
            {
                display_name = av_name.getDisplayName();
            }
            else
            {
                // Name not cached - show UUID prefix instead
                display_name = "[" + av_info.id.asString().substr(0, 8) + "...]";
                uncached_count++;
            }

            // Truncate long names to fit formatting
            if (display_name.length() > 32)
            {
                display_name = display_name.substr(0, 29) + "...";
            }

            // Format line with proper alignment
            std::string line = llformat("%6.1fm  %-32s  %s\n",
                av_info.distance,
                display_name.c_str(),
                av_info.id.asString().c_str()
            );

            mOutput->appendText(line, false);
            mLineCount++;
            displayed_count++;
        }

        // Summary footer
        mOutput->appendText(
            llformat("\nTotal: %d avatar(s)\n", (int)avatars.size()),
            false
        );
        mLineCount += 2;
    }
}

// S24: Lookat command - focus camera on avatar or object
void KVDebugConsole::processLookatCommand(const std::string& args)
{
    std::string search = args;
    LLStringUtil::trim(search);

    if (search.empty())
    {
        mOutput->appendText("Usage: lookat <name|uuid>\n", false);
        mLineCount++;
        return;
    }

    // Try to parse as UUID first
    LLUUID target_id;
    bool is_uuid = target_id.set(search, false);

    LLViewerObject* target_obj = nullptr;
    std::string target_name;

    if (is_uuid && !target_id.isNull())
    {
        // UUID provided - find the object/avatar
        target_obj = gObjectList.findObject(target_id);
        if (target_obj)
        {
            if (target_obj->isAvatar())
            {
                LLVOAvatar* avatar = (LLVOAvatar*)target_obj;
                target_name = avatar->getFullname();
            }
            else
            {
                target_name = "Object " + target_id.asString().substr(0, 8) + "...";
            }
        }
    }
    else
    {
        // Name provided - search for matching avatar
        // Convert search to lowercase for case-insensitive matching
        std::string search_lower = search;
        LLStringUtil::toLower(search_lower);

        // Search through rendered avatars
        S32 num_objects = gObjectList.getNumObjects();
        for (S32 i = 0; i < num_objects; ++i)
        {
            LLViewerObject* obj = gObjectList.getObject(i);
            if (obj && obj->isAvatar() && !obj->isAttachment())
            {
                LLVOAvatar* avatar = (LLVOAvatar*)obj;
                if (avatar->isSelf()) continue;

                std::string av_name = avatar->getFullname();
                std::string av_name_lower = av_name;
                LLStringUtil::toLower(av_name_lower);

                // Check if name matches (partial match OK)
                if (av_name_lower.find(search_lower) != std::string::npos)
                {
                    target_obj = obj;
                    target_name = av_name;
                    target_id = avatar->getID();
                    break; // Use first match
                }
            }
        }
    }

    if (!target_obj)
    {
        mOutput->appendText(llformat("✗ Could not find '%s' in scene\n", search.c_str()), false);
        mOutput->appendText("  Tip: Target must be within draw distance or use UUID\n", false);
        mLineCount += 2;
        return;
    }

    // Focus camera on the target and zoom in
    LLVector3d target_pos = target_obj->getPositionGlobal();
    LLVector3d my_pos = gAgent.getPositionGlobal();
    F64 original_distance = dist_vec(target_pos, my_pos);

    // Check draw distance
    F32 draw_distance = gSavedSettings.getF32("RenderFarClip");
    bool beyond_draw_distance = (original_distance > draw_distance);

    // Smart zoom distance based on how far away the target is
    // For nearby objects: zoom to 3m
    // For distant objects: zoom to a reasonable viewing distance based on distance
    F32 zoom_distance = 3.0f;
    const F32 MAX_SAFE_CAMERA_DIST = 400.0f; // Stay under the 496m hard limit
    bool use_directional_point = false;

    if (beyond_draw_distance)
    {
        // Target is beyond draw distance - can't actually see it!
        mOutput->appendText(llformat("✗ Target is beyond draw distance!\n"), false);
        mOutput->appendText(llformat("  Target: %.1fm away\n", original_distance), false);
        mOutput->appendText(llformat("  Draw distance: %.1fm (RenderFarClip)\n", draw_distance), false);
        mOutput->appendText("  Solutions:\n", false);
        mOutput->appendText(llformat("    • set RenderFarClip %.0f (increase draw distance)\n", original_distance + 50.0), false);
        mOutput->appendText(llformat("    • tp %s (teleport closer first)\n", target_name.c_str()), false);
        mLineCount += 6;
        return;
    }

    if (original_distance > 50.0)
    {
        // For distant targets, use a percentage of the distance
        // This keeps the camera between us and the target
        zoom_distance = llmin((F32)(original_distance * 0.7), MAX_SAFE_CAMERA_DIST);
        use_directional_point = (original_distance > 100.0); // For very far, just point
    }
    else if (original_distance > 10.0)
    {
        // Mid-range: scale between 3m and 70% of distance
        zoom_distance = llmin((F32)(original_distance * 0.5), 30.0f);
    }

    // Calculate camera position
    LLVector3d offset_dir = my_pos - target_pos;
    offset_dir.mdV[VZ] = 0.5; // Slight elevation for better angle
    offset_dir.normalize();
    LLVector3d camera_offset = offset_dir * zoom_distance;
    LLVector3d camera_pos = target_pos + camera_offset;

    // Unlock camera from avatar first, then set position and focus
    gAgentCamera.setFocusOnAvatar(false, true); // animate = true for smooth transition

    if (use_directional_point)
    {
        // For very distant targets, just focus without moving camera too far
        // This at least points the camera in the right direction
        gAgentCamera.setFocusGlobal(target_pos, target_id);
    }
    else
    {
        // For closer targets, actually move the camera
        gAgentCamera.setCameraPosAndFocusGlobal(camera_pos, target_pos, target_id);
    }

    if (original_distance > 100.0)
    {
        mOutput->appendText(llformat("✓ Camera pointed at: %s (distant)\n", target_name.c_str()), false);
        mOutput->appendText(llformat("  Distance: %.1fm (camera cannot reach)\n", original_distance), false);
        mOutput->appendText("  Solution: Use 'tp' to get closer:\n", false);
        mOutput->appendText(llformat("    tp %s\n", target_id.asString().c_str()), false);
        mLineCount += 4;
    }
    else
    {
        mOutput->appendText(llformat("✓ Camera focused on: %s\n", target_name.c_str()), false);
        mOutput->appendText(llformat("  Distance: %.1fm → %.1fm (zoomed)\n", original_distance, zoom_distance), false);
        mLineCount += 2;
    }
}

// S24: Find command - select and beacon object or avatar
void KVDebugConsole::processFindCommand(const std::string& args)
{
    std::string search = args;
    LLStringUtil::trim(search);

    if (search.empty())
    {
        mOutput->appendText("Usage: find <name|uuid> or 'find clear' to remove beacon\n", false);
        mLineCount++;
        return;
    }

    // Check for "clear" command
    std::string search_lower = search;
    LLStringUtil::toLower(search_lower);
    if (search_lower == "clear" || search_lower == "off")
    {
        if (sBeaconTarget.active)
        {
            clearBeacon();
            mOutput->appendText("✓ Beacon cleared\n", false);
            mLineCount++;
        }
        else
        {
            mOutput->appendText("No active beacon to clear\n", false);
            mLineCount++;
        }
        return;
    }

    // Try to parse as UUID first
    LLUUID target_id;
    bool is_uuid = target_id.set(search, false);

    LLViewerObject* target_obj = nullptr;
    std::string target_name;

    if (is_uuid && !target_id.isNull())
    {
        // UUID provided - find the object/avatar
        target_obj = gObjectList.findObject(target_id);
        if (target_obj)
        {
            if (target_obj->isAvatar())
            {
                LLVOAvatar* avatar = (LLVOAvatar*)target_obj;
                target_name = avatar->getFullname();
            }
            else
            {
                target_name = "Object " + target_id.asString().substr(0, 8) + "...";
            }
        }
    }
    else
    {
        // Name provided - search for matching avatar only
        std::string search_lower = search;
        LLStringUtil::toLower(search_lower);

        S32 num_objects = gObjectList.getNumObjects();
        for (S32 i = 0; i < num_objects; ++i)
        {
            LLViewerObject* obj = gObjectList.getObject(i);
            if (obj && obj->isAvatar() && !obj->isAttachment())
            {
                LLVOAvatar* avatar = (LLVOAvatar*)obj;
                if (avatar->isSelf()) continue;

                std::string av_name = avatar->getFullname();
                std::string av_name_lower = av_name;
                LLStringUtil::toLower(av_name_lower);

                if (av_name_lower.find(search_lower) != std::string::npos)
                {
                    target_obj = obj;
                    target_name = av_name;
                    target_id = avatar->getID();
                    break;
                }
            }
        }
    }

    if (!target_obj)
    {
        // Target not rendered - try to find via LLWorld coarse location (like scan/TP)
        uuid_vec_t avatar_ids;
        std::vector<LLVector3d> positions;
        LLVector3d my_pos = gAgent.getPositionGlobal();

        // Get all avatars from coarse location data (works at unlimited range)
        LLWorld::getInstance()->getAvatars(&avatar_ids, &positions, my_pos, 1000000.0f);

        // Search for our target UUID
        bool found_via_coarse = false;
        LLVector3d target_pos_global;

        if (is_uuid && !target_id.isNull())
        {
            for (size_t i = 0; i < avatar_ids.size(); ++i)
            {
                if (avatar_ids[i] == target_id)
                {
                    target_pos_global = positions[i];
                    found_via_coarse = true;

                    // Try to get name from cache
                    LLAvatarName av_name;
                    if (LLAvatarNameCache::get(target_id, &av_name))
                    {
                        target_name = av_name.getDisplayName();
                    }
                    else
                    {
                        target_name = "[" + target_id.asString().substr(0, 8) + "...]";
                    }
                    break;
                }
            }
        }

        if (!found_via_coarse)
        {
            mOutput->appendText(llformat("✗ Could not find '%s' in scene or world\n", search.c_str()), false);
            mOutput->appendText("  Tip: Use UUID for avatars, or they may be offline/out of range\n", false);
            mLineCount += 2;
            return;
        }

        // Found via coarse location - use coordinate-based beacon (works at unlimited range)
        F64 distance = dist_vec(target_pos_global, my_pos);

        // Clear any previous beacon
        clearBeacon();

        // Set up new persistent beacon
        sBeaconTarget.uuid = target_id;
        sBeaconTarget.pos_global = target_pos_global;
        sBeaconTarget.name = target_name;
        sBeaconTarget.active = true;

        // Beacon will be created in next updateBeacon() call (every frame)

        mOutput->appendText(llformat("✓ Beacon active: %s (coordinate-based)\n", target_name.c_str()), false);
        mOutput->appendText(llformat("  UUID: %s\n", target_id.asString().c_str()), false);
        mOutput->appendText(llformat("  Distance: %.1fm\n", distance), false);
        mOutput->appendText("  Note: Magenta GL beacon line visible (like sound/light beacons)\n", false);
        mOutput->appendText("  Type 'find clear' to remove beacon\n", false);
        mLineCount += 5;
        return;
    }

    // Target IS rendered - use both traditional selection + persistent beacon
    LLVector3d target_pos = target_obj->getPositionGlobal();
    LLVector3d my_pos = gAgent.getPositionGlobal();
    F64 distance = dist_vec(target_pos, my_pos);

    // Clear any previous beacon
    clearBeacon();

    // Set up persistent beacon
    sBeaconTarget.uuid = target_id;
    sBeaconTarget.pos_global = target_pos;
    sBeaconTarget.name = target_name;
    sBeaconTarget.active = true;

    // Beacon will be created in next updateBeacon() call (every frame)

    // Also try to select for highlight (blue box), but silently ignore failures
    // Some objects (temp, phantom, not fully loaded) can't be selected
    LLSelectMgr::getInstance()->deselectAll();
    if (target_obj->flagObjectPermanent() || target_obj->permYouOwner() || !target_obj->flagTemporaryOnRez())
    {
        // Only try to select if object appears stable/selectable
        LLSelectMgr::getInstance()->selectObjectOnly(target_obj);
    }

    mOutput->appendText(llformat("✓ Beacon active: %s\n", target_name.c_str()), false);
    mOutput->appendText(llformat("  UUID: %s\n", target_id.asString().c_str()), false);
    mOutput->appendText(llformat("  Distance: %.1fm\n", distance), false);
    mOutput->appendText("  Note: Magenta GL beacon", false);

    // Check if we actually got a selection
    if (LLSelectMgr::getInstance()->getSelection()->getObjectCount() > 0)
    {
        mOutput->appendText(" + blue selection highlight\n", false);
    }
    else
    {
        mOutput->appendText(" (object not selectable)\n", false);
    }

    mOutput->appendText("  Type 'find clear' to remove beacon\n", false);
    mLineCount += 5;
}

// S24: TP command - teleport to home/back/uuid/avatar
void KVDebugConsole::processTpCommand(const std::string& args)
{
    std::string dest = args;
    LLStringUtil::trim(dest);
    LLStringUtil::toLower(dest);

    if (dest.empty())
    {
        mOutput->appendText("Usage: tp <home|back|cam|uuid|name>\n", false);
        mLineCount++;
        return;
    }

    // Handle special destinations
    if (dest == "home")
    {
        gAgent.teleportHome();
        mOutput->appendText("✓ Teleporting home...\n", false);
        mLineCount++;
        return;
    }
    else if (dest == "back" || dest == "last")
    {
        LLTeleportHistory* hist = LLTeleportHistory::getInstance();
        if (hist->isEmpty())
        {
            mOutput->appendText("✗ No teleport history available\n", false);
            mLineCount++;
            return;
        }

        hist->goBack();
        mOutput->appendText("✓ Teleporting to previous location...\n", false);
        mLineCount++;
        return;
    }
    else if (dest == "cam" || dest == "camera")
    {
        // Teleport to current camera position
        LLVector3d camera_pos = gAgentCamera.getCameraPositionGlobal();
        LLVector3d my_pos = gAgent.getPositionGlobal();
        F64 distance = dist_vec(camera_pos, my_pos);

        gAgent.teleportViaLocation(camera_pos);

        mOutput->appendText("✓ Teleporting to camera position...\n", false);
        mOutput->appendText(llformat("  Coordinates: %.1f, %.1f, %.1f\n", 
                                     camera_pos.mdV[VX], camera_pos.mdV[VY], camera_pos.mdV[VZ]), false);
        mOutput->appendText(llformat("  Distance: %.1fm\n", distance), false);
        mLineCount += 3;
        return;
    }

    // Try to parse as UUID
    LLUUID target_id;
    bool is_uuid = target_id.set(dest, false);

    LLViewerObject* target_obj = nullptr;
    std::string target_name;
    LLVector3d target_pos;

    if (is_uuid && !target_id.isNull())
    {
        // UUID provided - find the object/avatar
        target_obj = gObjectList.findObject(target_id);
        if (target_obj)
        {
            target_pos = target_obj->getPositionGlobal();
            if (target_obj->isAvatar())
            {
                LLVOAvatar* avatar = (LLVOAvatar*)target_obj;
                target_name = avatar->getFullname();
            }
            else
            {
                target_name = "Object " + target_id.asString().substr(0, 8) + "...";
            }
        }
    }
    else
    {
        // Name provided - search for matching avatar
        std::string search_lower = dest;

        S32 num_objects = gObjectList.getNumObjects();
        for (S32 i = 0; i < num_objects; ++i)
        {
            LLViewerObject* obj = gObjectList.getObject(i);
            if (obj && obj->isAvatar() && !obj->isAttachment())
            {
                LLVOAvatar* avatar = (LLVOAvatar*)obj;
                if (avatar->isSelf()) continue;

                std::string av_name = avatar->getFullname();
                std::string av_name_lower = av_name;
                LLStringUtil::toLower(av_name_lower);

                if (av_name_lower.find(search_lower) != std::string::npos)
                {
                    target_obj = obj;
                    target_name = av_name;
                    target_id = avatar->getID();
                    target_pos = obj->getPositionGlobal();
                    break;
                }
            }
        }
    }

    if (!target_obj)
    {
        mOutput->appendText(llformat("✗ Could not find '%s' in scene\n", dest.c_str()), false);
        mOutput->appendText("  Tip: Use 'home', 'back', avatar name, or UUID\n", false);
        mLineCount += 2;
        return;
    }

    // Calculate distance before TP
    LLVector3d my_pos = gAgent.getPositionGlobal();
    F64 distance = dist_vec(target_pos, my_pos);

    // Teleport to the target location
    gAgent.teleportViaLocation(target_pos);

    mOutput->appendText(llformat("✓ Teleporting to: %s\n", target_name.c_str()), false);
    mOutput->appendText(llformat("  Coordinates: %.1f, %.1f, %.1f\n", 
                                 target_pos.mdV[VX], target_pos.mdV[VY], target_pos.mdV[VZ]), false);
    mOutput->appendText(llformat("  Distance: %.1fm\n", distance), false);
    mLineCount += 3;
}

// S24: Fly command - flight mode control and vertical positioning
void KVDebugConsole::processFlyCommand(const std::string& args)
{
    std::string param = args;
    LLStringUtil::trim(param);
    LLStringUtil::toLower(param);

    // No argument - toggle flight mode
    if (param.empty())
    {
        bool is_flying = gAgent.getFlying();

        // Clear any movement control flags before toggling
        gAgent.clearControlFlags(AGENT_CONTROL_UP_POS | AGENT_CONTROL_UP_NEG | AGENT_CONTROL_STOP);
        gAgent.setFlying(!is_flying);

        if (!is_flying)
        {
            printSuccess("✓ Flight mode enabled\n");
        }
        else
        {
            printSuccess("✓ Flight mode disabled\n");
        }
        return;
    }

    // "down" argument - descend to ground level with optional --fast
    if (param.find("down") == 0 || param.find("ground") == 0)
    {
        bool use_fast_mode = (param.find("--fast") != std::string::npos);

        LLVector3d current_pos = gAgent.getPositionGlobal();

        // Get ground height at current X/Y position
        LLVector3 current_pos_region = gAgent.getPositionAgent();
        F32 ground_height = 0.0f;

        LLViewerRegion* region = gAgent.getRegion();
        if (region)
        {
            ground_height = region->getLand().resolveHeightRegion(current_pos_region);
        }

        // Target position: same X/Y, ground Z + small offset for safety
        LLVector3d target_pos = current_pos;
        target_pos.mdV[VZ] = ground_height + 0.5;  // 0.5m above ground for safety
        F64 descent = current_pos.mdV[VZ] - target_pos.mdV[VZ];

        // Enable flight mode to prevent fall damage
        if (!gAgent.getFlying())
        {
            gAgent.setFlying(true);
        }

        // Turbo mode: larger stop distance for faster movement
        F32 stop_distance = use_fast_mode ? 5.0f : 0.5f;

        // Use autopilot for smooth descent (same as numeric fly command)
        gAgent.startAutoPilotGlobal(
            target_pos,           // target position at ground level
            "",                   // no behavior name
            nullptr,              // no target rotation
            nullptr,              // no callback
            nullptr,              // no callback data
            stop_distance,        // larger distance = faster in turbo mode
            0.03f,                // rotation threshold
            true                  // allow flying
        );

        printSuccess(llformat("✓ Descending to ground level...%s\n", 
                              use_fast_mode ? " [TURBO]" : ""));
        mOutput->appendText(llformat("  Target altitude: %.1fm\n", target_pos.mdV[VZ]), false);
        mOutput->appendText(llformat("  Descent: %.1fm\n", descent), false);
        mLineCount += 3;
        // ============================================================================
        // END S24 FIX: Use actual flight descent instead of teleport
        // ============================================================================
        return;
    }

    // Numeric argument - fly to altitude with optional --fast flag
    bool use_fast_mode = false;

    // Check for --fast flag
    size_t fast_pos = param.find("--fast");
    if (fast_pos != std::string::npos)
    {
        use_fast_mode = true;
        param.erase(fast_pos, 6);  // Remove "--fast" from string
        LLStringUtil::trim(param);
    }

    char* endptr = nullptr;
    F64 height_offset = strtod(param.c_str(), &endptr);

    if (endptr == param.c_str() || *endptr != '\0')
    {
        // Not a valid number
        printError("✗ Invalid argument\n");
        mOutput->appendText("Usage: fly                Toggle flight mode\n", false);
        mOutput->appendText("       fly <altitude>     Fly to absolute altitude\n", false);
        mOutput->appendText("       fly <altitude> --fast  Fly with turbo boost\n", false);
        mOutput->appendText("       fly down           Descend to ground level\n", false);
        mOutput->appendText("\nExamples:\n", false);
        mOutput->appendText("  fly                    Toggle flight on/off\n", false);
        mOutput->appendText("  fly 1000               Fly to altitude z=1000m\n", false);
        mOutput->appendText("  fly 1000 --fast        Fly to z=1000m with turbo\n", false);
        mOutput->appendText("  fly 50                 Fly to altitude z=50m\n", false);
        mOutput->appendText("  fly down               Land at ground level\n", false);
        mLineCount += 11;
        return;
    }

    // Calculate target position
    LLVector3d current_pos = gAgent.getPositionGlobal();
    LLVector3d target_pos = current_pos;

    // S24: Use absolute altitude instead of relative offset
    // User commands "fly 1000" = go to z=1000, not z=current+1000
    target_pos.mdV[VZ] = height_offset;

    // Safety check - don't go below ground
    LLVector3 current_pos_region = gAgent.getPositionAgent();
    F32 ground_height = 0.0f;

    LLViewerRegion* region = gAgent.getRegion();
    if (region)
    {
        ground_height = region->getLand().resolveHeightRegion(current_pos_region);
    }

    if (target_pos.mdV[VZ] < ground_height)
    {
        printWarning(llformat("⚠ Target height (%.1fm) is below ground (%.1fm)\n", 
                              target_pos.mdV[VZ], ground_height));
        mOutput->appendText("  Adjusting to ground level + 0.5m for safety\n", false);
        target_pos.mdV[VZ] = ground_height + 0.5;
        mLineCount += 2;
    }

    // ============================================================================
    // S24 FIX: Use built-in autopilot system for smooth flight movement
    // ============================================================================
    // ISSUE:      Original teleport approach hit parcel landing zones.
    //             Timer-based control flag approach caused stuttering/jerkiness.
    // ROOT CAUSE: Manual control flag manipulation fights with physics every tick.
    // FIX:        Use LLAgent::startAutoPilotGlobal() - the viewer's built-in
    //             smooth movement system that handles acceleration/deceleration.
    // WHY:        Autopilot system is designed for smooth, natural movement with
    //             proper physics integration. It handles all the complexities of
    //             velocity ramping, obstacle avoidance, and clean stops.
    // BENEFIT:    - Smooth, non-jerky flight
    //             - No landing zone interference
    //             - Natural hover on arrival
    //             - Uses proven, battle-tested code path
    // MERGE:      Essential fix - replaces broken teleport + broken timer approaches.
    // ============================================================================

    // Enable flight mode
    if (!gAgent.getFlying())
    {
        gAgent.setFlying(true);
        printInfo("  Enabling flight mode...\n");
        mLineCount++;
    }

    // ============================================================================
    // S24 FEATURE: Turbo mode via larger stop distance
    // ============================================================================
    // DISCOVERY:   AGENT_CONTROL_FAST_* flags don't affect autopilot speed.
    //              Autopilot uses internal velocity calculations.
    // APPROACH:    Autopilot moves faster when farther from target (velocity
    //              scales with distance). By increasing stop_distance, we can
    //              keep the avatar in "far zone" longer = faster movement.
    // LIMITATION:  This is indirect speed boost via stop-distance manipulation.
    //              True speed multiplier would require autopilot internals modification.
    // ============================================================================
    F32 stop_distance = use_fast_mode ? 5.0f : 0.5f;  // 5m vs 0.5m tolerance

    // Use autopilot to smoothly fly to target position
    gAgent.startAutoPilotGlobal(
        target_pos,           // target position
        "",                   // no behavior name
        nullptr,              // no target rotation
        nullptr,              // no callback
        nullptr,              // no callback data
        stop_distance,        // larger distance = faster approach in turbo mode
        0.03f,                // rotation threshold
        true                  // allow flying
    );

    printSuccess(llformat("✓ Flying to altitude: z=%.1fm%s\n", 
                          target_pos.mdV[VZ], 
                          use_fast_mode ? " [TURBO]" : ""));
    mOutput->appendText(llformat("  Current altitude: %.1fm\n", current_pos.mdV[VZ]), false);
    mOutput->appendText(llformat("  Target altitude:  %.1fm\n", target_pos.mdV[VZ]), false);
    F64 vertical_change = target_pos.mdV[VZ] - current_pos.mdV[VZ];
    mOutput->appendText(llformat("  Vertical change:  %+.1fm\n", vertical_change), false);
    mLineCount += 4;
    // ============================================================================
    // END S24 FIX: Use actual flight movement instead of teleport
    // ============================================================================
}

// S24: Get command - retrieve debug settings with wildcard support
void KVDebugConsole::processGetCommand(const std::string& args)
{
    std::string search = args;
    bool is_wildcard = (search.find('*') != std::string::npos);

    if (is_wildcard)
    {
        // Wildcard search - find all matching settings
        // Convert wildcard to lowercase for case-insensitive search
        std::string pattern = search;
        LLStringUtil::toLower(pattern);

        // Remove asterisks for substring matching
        LLStringUtil::replaceString(pattern, "*", "");

        // S24: Use ApplyFunctor to iterate all settings
        struct MatchCollector : public LLControlGroup::ApplyFunctor
        {
            std::string pattern;
            std::vector<std::string> matches;

            virtual void apply(const std::string& name, LLControlVariable* control) override
            {
                std::string name_lower = name;
                LLStringUtil::toLower(name_lower);

                if (name_lower.find(pattern) != std::string::npos)
                {
                    matches.push_back(name);
                }
            }
        };

        MatchCollector collector;
        collector.pattern = pattern;
        gSavedSettings.applyToAll(&collector);

        if (collector.matches.empty())
        {
            mOutput->appendText(llformat("✗ No settings found matching '%s'\n", search.c_str()), false);
            mLineCount++;
        }
        else
        {
            mOutput->appendText(llformat("Found %d setting(s) matching '%s':\n", collector.matches.size(), search.c_str()), false);
            mLineCount++;

            // Limit output to first 50 matches to prevent spam
            S32 display_count = llmin((S32)collector.matches.size(), 50);
            for (S32 i = 0; i < display_count; i++)
            {
                LLControlVariable* control = gSavedSettings.getControl(collector.matches[i]).get();
                if (control)
                {
                    std::string type_str;
                    std::string value_str;

                    switch (control->type())
                    {
                        case TYPE_BOOLEAN:
                            type_str = "BOOL";
                            value_str = control->get().asBoolean() ? "TRUE" : "FALSE";
                            break;
                        case TYPE_S32:
                            type_str = "S32";
                            value_str = llformat("%d", control->get().asInteger());
                            break;
                        case TYPE_U32:
                            type_str = "U32";
                            value_str = llformat("%u", control->get().asInteger());
                            break;
                        case TYPE_F32:
                            type_str = "F32";
                            value_str = llformat("%.3f", control->get().asReal());
                            break;
                        case TYPE_STRING:
                            type_str = "STRING";
                            value_str = "\"" + control->get().asString() + "\"";
                            break;
                        case TYPE_VEC3:
                            type_str = "VEC3";
                            value_str = control->get().asString();
                            break;
                        case TYPE_COL4:
                            type_str = "COLOR";
                            value_str = control->get().asString();
                            break;
                        default:
                            type_str = "OTHER";
                            value_str = control->get().asString();
                            break;
                    }

                    mOutput->appendText(llformat("  [%s] %s = %s\n", 
                        type_str.c_str(), collector.matches[i].c_str(), value_str.c_str()), false);
                    mLineCount++;
                }
            }

            if (collector.matches.size() > 50)
            {
                mOutput->appendText(llformat("  ... and %d more (output limited)\n", collector.matches.size() - 50), false);
                mLineCount++;
            }
        }
    }
    else
    {
        // Exact setting name lookup
        LLControlVariablePtr control = gSavedSettings.getControl(search);

        if (!control)
        {
            mOutput->appendText(llformat("✗ Setting '%s' not found\n", search.c_str()), false);
            mLineCount++;
            return;
        }

        // Display the setting with type and value
        std::string type_str;
        std::string value_str;
        std::string comment = control->getComment();

        switch (control->type())
        {
            case TYPE_BOOLEAN:
                type_str = "BOOLEAN";
                value_str = control->get().asBoolean() ? "TRUE" : "FALSE";
                break;
            case TYPE_S32:
                type_str = "SIGNED INT (S32)";
                value_str = llformat("%d", control->get().asInteger());
                break;
            case TYPE_U32:
                type_str = "UNSIGNED INT (U32)";
                value_str = llformat("%u", control->get().asInteger());
                break;
            case TYPE_F32:
                type_str = "FLOAT (F32)";
                value_str = llformat("%.6f", control->get().asReal());
                break;
            case TYPE_STRING:
                type_str = "STRING";
                value_str = "\"" + control->get().asString() + "\"";
                break;
            case TYPE_VEC3:
                type_str = "VECTOR3";
                value_str = control->get().asString();
                break;
            case TYPE_COL4:
                type_str = "COLOR (RGBA)";
                value_str = control->get().asString();
                break;
            default:
                type_str = llformat("TYPE_%d", control->type());
                value_str = control->get().asString();
                break;
        }

        std::string output = llformat(
            "✓ Setting: %s\n"
            "  Type:  %s\n"
            "  Value: %s\n",
            search.c_str(), type_str.c_str(), value_str.c_str()
        );

        if (!comment.empty())
        {
            output += "  Info:  " + comment + "\n";
        }

        mOutput->appendText(output, false);
        mLineCount += (comment.empty() ? 3 : 4);
    }
}

// S24: Set command - modify debug settings with type auto-detection
void KVDebugConsole::processSetCommand(const std::string& args)
{
    // Parse: set <setting_name> <value>
    size_t space_pos = args.find(' ');

    if (space_pos == std::string::npos)
    {
        mOutput->appendText("✗ Usage: set <setting_name> <value>\n", false);
        mLineCount++;
        return;
    }

    std::string setting_name = args.substr(0, space_pos);
    std::string value_str = args.substr(space_pos + 1);

    // Trim leading/trailing spaces
    LLStringUtil::trim(setting_name);
    LLStringUtil::trim(value_str);

    if (value_str.empty())
    {
        mOutput->appendText("✗ No value specified\n", false);
        mLineCount++;
        return;
    }

    LLControlVariablePtr control = gSavedSettings.getControl(setting_name);

    if (!control)
    {
        mOutput->appendText(llformat("✗ Setting '%s' not found\n", setting_name.c_str()), false);
        mLineCount++;
        return;
    }

    // Get old value for comparison
    std::string old_value;
    switch (control->type())
    {
        case TYPE_BOOLEAN:
            old_value = control->get().asBoolean() ? "TRUE" : "FALSE";
            break;
        case TYPE_S32:
        case TYPE_U32:
            old_value = llformat("%d", control->get().asInteger());
            break;
        case TYPE_F32:
            old_value = llformat("%.6f", control->get().asReal());
            break;
        default:
            old_value = control->get().asString();
            break;
    }

    // Set value based on type
    bool success = false;
    std::string error_msg;

    try
    {
        switch (control->type())
        {
            case TYPE_BOOLEAN:
            {
                std::string value_lower = value_str;
                LLStringUtil::toLower(value_lower);

                if (value_lower == "true" || value_lower == "1" || value_lower == "yes" || value_lower == "on")
                {
                    gSavedSettings.setBOOL(setting_name, TRUE);
                    success = true;
                }
                else if (value_lower == "false" || value_lower == "0" || value_lower == "no" || value_lower == "off")
                {
                    gSavedSettings.setBOOL(setting_name, FALSE);
                    success = true;
                }
                else
                {
                    error_msg = "Invalid boolean value (use: true/false, 1/0, yes/no, on/off)";
                }
                break;
            }

            case TYPE_S32:
            {
                S32 value = atoi(value_str.c_str());
                gSavedSettings.setS32(setting_name, value);
                success = true;
                break;
            }

            case TYPE_U32:
            {
                U32 value = strtoul(value_str.c_str(), NULL, 10);
                gSavedSettings.setU32(setting_name, value);
                success = true;
                break;
            }

            case TYPE_F32:
            {
                F32 value = (F32)atof(value_str.c_str());
                gSavedSettings.setF32(setting_name, value);
                success = true;
                break;
            }

            case TYPE_STRING:
            {
                // Remove quotes if present
                std::string clean_value = value_str;
                if (clean_value.length() >= 2 && clean_value[0] == '"' && clean_value[clean_value.length()-1] == '"')
                {
                    clean_value = clean_value.substr(1, clean_value.length() - 2);
                }
                gSavedSettings.setString(setting_name, clean_value);
                success = true;
                break;
            }

            default:
            {
                error_msg = llformat("Setting type %d not supported for modification", control->type());
                break;
            }
        }
    }
    catch (...)
    {
        error_msg = "Exception occurred while setting value";
        success = false;
    }

    if (success)
    {
        std::string new_value;
        switch (control->type())
        {
            case TYPE_BOOLEAN:
                new_value = control->get().asBoolean() ? "TRUE" : "FALSE";
                break;
            case TYPE_S32:
            case TYPE_U32:
                new_value = llformat("%d", control->get().asInteger());
                break;
            case TYPE_F32:
                new_value = llformat("%.6f", control->get().asReal());
                break;
            default:
                new_value = control->get().asString();
                break;
        }

        mOutput->appendText(llformat("✓ '%s' changed: %s → %s\n", 
            setting_name.c_str(), old_value.c_str(), new_value.c_str()), false);
        mLineCount++;
    }
    else
    {
        mOutput->appendText(llformat("✗ Failed to set '%s': %s\n", 
            setting_name.c_str(), error_msg.c_str()), false);
        mLineCount++;
    }
}

// S24: Open/close/toggle floaters command
void KVDebugConsole::processOpenCommand(const std::string& args)
{
    // S24: Parse subcommand: open [list|close|toggle] <floater_name>
    std::string subcommand;
    std::string floater_name;
    size_t space_pos = args.find(' ');

    if (space_pos != std::string::npos)
    {
        subcommand = args.substr(0, space_pos);
        floater_name = args.substr(space_pos + 1);
        LLStringUtil::trim(subcommand);
        LLStringUtil::trim(floater_name);
        LLStringUtil::toLower(subcommand);
    }
    else
    {
        subcommand = args;
        LLStringUtil::toLower(subcommand);
    }

    // S24: 'open list' - show all common registered floaters
    if (subcommand == "list")
    {
        // S24: Curated list of commonly-used floaters
        // This is a practical subset since the full registry map is private
        static const std::vector<std::pair<std::string, std::string>> common_floaters = {
            {"360capture", "360° Snapshot Capture"},
            {"about_land", "About Land"},
            {"appearance", "Appearance / Avatar"},
            {"avatar_picker", "Avatar Picker"},
            {"avatar_render_settings", "Avatar Render Settings"},
            {"beacons", "Beacons"},
            {"build", "Build Tools"},
            {"build_options", "Build Options"},
            {"bumps", "Bumps, Pushes & Hits"},
            {"buy_currency", "Buy L$"},
            {"buy_land", "Buy Land"},
            {"camera", "Camera Controls"},
            {"camera_presets", "Camera Presets"},
            {"compile_queue", "Recompile Scripts"},
            {"conversation", "Conversation Log"},
            {"destinations", "Destinations"},
            {"env_adjust_snapshot", "Environment Adjust"},
            {"experiences", "Experiences"},
            {"gestures", "Gestures"},
            {"god_tools", "God Tools"},
            {"grid_status", "Grid Status"},
            {"help_browser", "Help Browser"},
            {"hud", "HUD"},
            {"im_container", "Conversations"},
            {"inspect", "Inspect Object"},
            {"inventory", "Inventory"},
            {"joystick", "Joystick Configuration"},
            {"lag_meter", "Lag Meter"},
            {"land_holdings", "My Land"},
            {"map", "Mini Map"},
            {"marketplace_listings", "Marketplace Listings"},
            {"mem_leaking", "Memory Leaking Simulation"},
            {"move", "Move & View Controls"},
            {"my_environments", "My Environments"},
            {"my_scripts", "My Scripts"},
            {"nearby_chat", "Nearby Chat"},
            {"notifications_console", "Notifications Console"},
            {"object_weights", "Object Weights"},
            {"outbox", "Merchant Outbox"},
            {"pathfinding_characters", "Pathfinding Characters"},
            {"pathfinding_console", "Pathfinding Console"},
            {"pathfinding_linksets", "Pathfinding Linksets"},
            {"people", "People"},
            {"performance", "Performance"},
            {"places", "Places"},
            {"preferences", "Preferences"},
            {"prefs_graphics_advanced", "Graphics Preferences"},
            {"profile", "My Profile"},
            {"quickchat", "Quick Chat"},
            {"debug_console", "S24 Console"},
            {"region_debug_console", "Region Debug Console"},
            {"region_info", "Region/Estate"},
            {"scene_load_stats", "Scene Load Statistics"},
            {"script_debug", "Script Debug"},
            {"script_limits", "Script Info/Limits"},
            {"search", "Search"},
            {"settings_debug", "Advanced Settings"},
            {"snapshot", "Snapshot"},
            {"stats", "Statistics Bar"},
            {"top_objects", "Top Colliders/Scripts"},
            {"voice_controls", "Voice Controls"},
            {"voice_effect", "Voice Morph"},
            {"volume_pulldown", "Volume Controls"},
            {"web_content", "Web Content"},
            {"whitelist_entry", "Whitelist Entry"},
            {"world_map", "World Map"}
        };

        mOutput->appendText(
            "╔═══════════════════════════════════════════════════════════════════════╗\n"
            "║                     REGISTERED FLOATER WINDOWS                        ║\n"
            "╚═══════════════════════════════════════════════════════════════════════╝\n"
            "\n"
            "USAGE:\n"
            "  open <name>         Open a floater window\n"
            "  open close <name>   Close a floater window\n"
            "  open toggle <name>  Toggle a floater's visibility\n"
            "\n"
            "COMMON FLOATERS:\n",
            false
        );
        mLineCount += 10;

        for (const auto& floater : common_floaters)
        {
            // S24: Check if floater is registered
            bool registered = LLFloaterReg::isRegistered(floater.first);
            std::string status = registered ? "✓" : "✗";

            mOutput->appendText(llformat("  %s %-30s %s\n", 
                status.c_str(), 
                floater.first.c_str(), 
                floater.second.c_str()), false);
            mLineCount++;
        }

        mOutput->appendText(
            "\n"
            "EXAMPLES:\n"
            "  open inventory              Open inventory window\n"
            "  open preferences            Open preferences\n"
            "  open close world_map        Close world map\n"
            "  open toggle beacons         Toggle beacons floater\n"
            "\n"
            "NOTE: Some floaters may require specific permissions or states to open.\n"
            "      Floater names are case-sensitive.\n",
            false
        );
        mLineCount += 9;
        return;
    }

    // S24: 'open close <name>' - hide a floater
    if (subcommand == "close")
    {
        if (floater_name.empty())
        {
            mOutput->appendText("✗ Usage: open close <floater_name>\n", false);
            mLineCount++;
            return;
        }

        if (!LLFloaterReg::isRegistered(floater_name))
        {
            mOutput->appendText(llformat("✗ Floater '%s' is not registered\n", floater_name.c_str()), false);
            mOutput->appendText("  Use 'open list' to see available floaters\n", false);
            mLineCount += 2;
            return;
        }

        bool success = LLFloaterReg::hideInstance(floater_name);
        if (success)
        {
            mOutput->appendText(llformat("✓ Closed floater: %s\n", floater_name.c_str()), false);
        }
        else
        {
            mOutput->appendText(llformat("✗ Could not close floater: %s (may not be open)\n", floater_name.c_str()), false);
        }
        mLineCount++;
        return;
    }

    // S24: 'open toggle <name>' - toggle visibility
    if (subcommand == "toggle")
    {
        if (floater_name.empty())
        {
            mOutput->appendText("✗ Usage: open toggle <floater_name>\n", false);
            mLineCount++;
            return;
        }

        if (!LLFloaterReg::isRegistered(floater_name))
        {
            mOutput->appendText(llformat("✗ Floater '%s' is not registered\n", floater_name.c_str()), false);
            mOutput->appendText("  Use 'open list' to see available floaters\n", false);
            mLineCount += 2;
            return;
        }

        bool visible = LLFloaterReg::toggleInstance(floater_name);
        std::string state = visible ? "opened" : "closed";
        mOutput->appendText(llformat("✓ Toggled floater: %s (%s)\n", floater_name.c_str(), state.c_str()), false);
        mLineCount++;
        return;
    }

    // S24: Default: 'open <name>' - show a floater
    floater_name = subcommand;  // The whole args is the floater name

    if (floater_name.empty())
    {
        mOutput->appendText("✗ Usage: open <floater_name>\n", false);
        mOutput->appendText("  Type 'open list' to see available floaters\n", false);
        mLineCount += 2;
        return;
    }

    if (!LLFloaterReg::isRegistered(floater_name))
    {
        mOutput->appendText(llformat("✗ Floater '%s' is not registered\n", floater_name.c_str()), false);
        mOutput->appendText("  Use 'open list' to see available floaters\n", false);
        mLineCount += 2;
        return;
    }

    // S24: Floater Safety Model
    // ────────────────────────────────────────────────────────────────
    // This protection layer prevents crashes and server errors when opening
    // floaters without required context. Floaters fall into three categories:
    //
    // SAFE (Passive):
    //   - Standalone windows with no special requirements
    //   - Examples: inventory, world_map, preferences, beacons, camera, build
    //   - These can be opened directly with empty LLSD key
    //
    // AUTO-FILLED (Your Data):
    //   - Require IDs but can safely default to the user's own data
    //   - profile → uses gAgentID (your profile)
    //   - avatar_textures → uses gAgentID (your avatar textures)
    //
    // PROTECTED (Server/Context Required):
    //   - Cannot function without external data or active sessions
    //   - Opening these without context causes crashes or server errors
    //   - Categories:
    //     * Session-based: impanel, incoming_call, outgoing_call
    //     * ID-required: event, classified, experience_profile
    //     * Picker dialogs: group_picker, avatar_picker (need callbacks)
    //     * Properties: item_properties, task_properties (need selection)
    //     * Preview: preview_* (need inventory asset IDs)
    //     * Upload: upload_* (need file upload context)
    //     * Object-based: inspect, openobject (need world object selection)
    //
    // This approach matches world_map (passive/safe) vs group floaters
    // (server-centric/protected). The console guides users to the proper
    // access path for protected floaters rather than letting them crash.
    // ────────────────────────────────────────────────────────────────

    LLSD key;

    // Floaters that need specific IDs or context
    if (floater_name == "profile")
    {
        // Profile floater needs an avatar ID - use your own profile
        key["id"] = gAgentID;
    }
    else if (floater_name == "avatar_textures")
    {
        // Avatar textures needs an avatar ID - use yourself
        key["avatar_id"] = gAgentID;
    }
    else if (floater_name == "impanel")
    {
        // IM session needs a session ID - can't open without context
        mOutput->appendText("✗ 'impanel' requires an active IM session\n", false);
        mOutput->appendText("  Use the IM interface to start conversations\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name == "incoming_call" || floater_name == "outgoing_call")
    {
        // Voice call floaters need active call context
        mOutput->appendText(llformat("✗ '%s' requires an active voice call\n", floater_name.c_str()), false);
        mLineCount++;
        return;
    }
    else if (floater_name == "event" || floater_name == "classified")
    {
        // Event/classified needs specific event/classified ID
        mOutput->appendText(llformat("✗ '%s' requires a specific event/classified ID\n", floater_name.c_str()), false);
        mOutput->appendText("  Browse events/classifieds through Search instead\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name == "experience_profile")
    {
        // Experience profile needs an experience key
        mOutput->appendText("✗ 'experience_profile' requires a specific experience ID\n", false);
        mOutput->appendText("  Browse experiences through Search instead\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name == "group_picker" || floater_name == "avatar_picker")
    {
        // Picker dialogs need callback context
        mOutput->appendText(llformat("✗ '%s' is a selection dialog that requires callback context\n", floater_name.c_str()), false);
        mOutput->appendText("  These are used by other UI elements, not standalone\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name == "item_properties" || floater_name == "task_properties")
    {
        // Properties dialogs need an item/object ID
        mOutput->appendText(llformat("✗ '%s' requires a specific inventory item or object\n", floater_name.c_str()), false);
        mOutput->appendText("  Right-click items in inventory to view properties\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name.find("preview_") == 0)
    {
        // Preview floaters need asset IDs
        mOutput->appendText(llformat("✗ '%s' requires a specific asset to preview\n", floater_name.c_str()), false);
        mOutput->appendText("  Double-click items in inventory to preview them\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name.find("upload_") == 0)
    {
        // Upload floaters need file context
        mOutput->appendText(llformat("✗ '%s' requires a file to upload\n", floater_name.c_str()), false);
        mOutput->appendText("  Use Build > Upload menu instead\n", false);
        mLineCount += 2;
        return;
    }
    else if (floater_name == "inspect" || floater_name == "openobject")
    {
        // Inspect/open needs selected object
        mOutput->appendText(llformat("✗ '%s' requires an object selection\n", floater_name.c_str()), false);
        mOutput->appendText("  Select an object first, then use this floater\n", false);
        mLineCount += 2;
        return;
    }

    LLFloater* floater = LLFloaterReg::showInstance(floater_name, key);
    if (floater)
    {
        mOutput->appendText(llformat("✓ Opened floater: %s\n", floater_name.c_str()), false);
    }
    else
    {
        mOutput->appendText(llformat("✗ Failed to open floater: %s\n", floater_name.c_str()), false);
        mOutput->appendText("  Floater may require specific state or permissions\n", false);
        mLineCount++;
    }
    mLineCount++;
}

// S24: Shutdown command - clean viewer exit without prompts
void KVDebugConsole::processShutdownCommand(const std::string& args)
{
    std::string param = args;
    LLStringUtil::trim(param);
    LLStringUtil::toLower(param);

    // S24: Cancel active shutdown timer
    if (param == "cancel" || param == "abort" || param == "stop")
    {
        if (sShutdownTimer.active)
        {
            sShutdownTimer.active = false;
            mOutput->appendText("✓ Shutdown timer cancelled\n", false);
            mLineCount++;
        }
        else
        {
            mOutput->appendText("✗ No shutdown timer is active\n", false);
            mLineCount++;
        }
        return;
    }

    if (param == "now")
    {
        // S24: Immediate shutdown - no timer, no prompts, clean exit
        // Use requestQuit() instead of forceQuit() to properly logout and send metrics
        // This bypasses userQuit()'s confirmation dialog but still does clean shutdown
        mOutput->appendText("✓ Shutting down viewer now...\n", false);
        mLineCount++;

        LLAppViewer::instance()->requestQuit();
        return;
    }

    // S24: Parse timer value (seconds)
    F32 seconds = (F32)atof(param.c_str());

    if (seconds <= 0.0f)
    {
        mOutput->appendText("✗ Usage: shutdown now | shutdown <seconds> | shutdown cancel\n", false);
        mOutput->appendText("  Example: shutdown 30      (quit in 30 seconds)\n", false);
        mOutput->appendText("           shutdown now     (quit immediately)\n", false);
        mOutput->appendText("           shutdown cancel  (abort scheduled shutdown)\n", false);
        mLineCount += 4;
        return;
    }

    // S24: Set up shutdown timer
    sShutdownTimer.active = true;
    sShutdownTimer.time_remaining = seconds;
    sShutdownTimer.timer.reset();

    mOutput->appendText(llformat("✓ Shutdown scheduled in %.1f seconds\n", seconds), false);
    mOutput->appendText("  Type 'shutdown cancel' to abort\n", false);
    mLineCount += 2;
}

// S24: Check shutdown timer - called each frame
void KVDebugConsole::checkShutdownTimer()
{
    if (!sShutdownTimer.active)
    {
        return;
    }

    F32 elapsed = sShutdownTimer.timer.getElapsedTimeF32();
    F32 remaining = sShutdownTimer.time_remaining - elapsed;

    if (remaining <= 0.0f)
    {
        // S24: Time's up - shut down cleanly
        // Use requestQuit() to properly logout and send metrics (bypasses confirmation)
        if (sInstance && sInstance->mOutput)
        {
            sInstance->mOutput->appendText("✓ Shutdown timer expired - exiting viewer...\n", false);
            sInstance->mLineCount++;
        }

        sShutdownTimer.active = false;
        LLAppViewer::instance()->requestQuit();
        return;
    }

    // S24: Optional: Show countdown warnings at intervals (10s, 5s, 3s, 2s, 1s)
    static F32 last_warning_time = 0.0f;

    if (remaining <= 10.0f && last_warning_time > 10.0f)
    {
        if (sInstance && sInstance->mOutput)
        {
            sInstance->mOutput->appendText("⚠ Viewer will shut down in 10 seconds\n", false);
            sInstance->mLineCount++;
        }
    }
    else if (remaining <= 5.0f && last_warning_time > 5.0f)
    {
        if (sInstance && sInstance->mOutput)
        {
            sInstance->mOutput->appendText("⚠ Viewer will shut down in 5 seconds\n", false);
            sInstance->mLineCount++;
        }
    }
    else if (remaining <= 3.0f && last_warning_time > 3.0f)
    {
        if (sInstance && sInstance->mOutput)
        {
            sInstance->mOutput->appendText("⚠ Viewer will shut down in 3 seconds\n", false);
            sInstance->mLineCount++;
        }
    }

    last_warning_time = remaining;
}

// ════════════════════════════════════════════════════════════════════════════
// S24: CONSOLE UTILITY COMMANDS
// Pure console functionality - no LL-specific features
// These make the command line more powerful and user-friendly
// ════════════════════════════════════════════════════════════════════════════

// S24: Calculator - evaluate simple math expressions
void KVDebugConsole::processCalcCommand(const std::string& expression)
{
    // S24: Simple recursive descent calculator
    // Supports: + - * / % ( ) and floating-point numbers
    // Example: calc 2 + 3 * 4 = 14
    //          calc (2 + 3) * 4 = 20
    //          calc 100 / 3 = 33.333333

    std::string expr = expression;
    LLStringUtil::trim(expr);

    if (expr.empty())
    {
        mOutput->appendText("✗ Empty expression\n", false);
        mLineCount++;
        return;
    }

    try
    {
        size_t pos = 0;
        F64 result = parseExpression(expr, pos);

        // Check if we consumed the entire expression
        while (pos < expr.length() && std::isspace(expr[pos])) pos++;

        if (pos < expr.length())
        {
            mOutput->appendText(llformat("✗ Unexpected character at position %d: '%c'\n", 
                (int)pos, expr[pos]), false);
            mLineCount++;
            return;
        }

        mOutput->appendText(llformat("= %.6f\n", result), false);
        mLineCount++;
    }
    catch (const std::exception& e)
    {
        mOutput->appendText(llformat("✗ Calculation error: %s\n", e.what()), false);
        mLineCount++;
    }
}

// S24: Recursive descent expression parser helpers
F64 KVDebugConsole::parseExpression(const std::string& expr, size_t& pos)
{
    F64 result = parseTerm(expr, pos);

    while (pos < expr.length())
    {
        while (pos < expr.length() && std::isspace(expr[pos])) pos++;
        if (pos >= expr.length()) break;

        char op = expr[pos];
        if (op == '+' || op == '-')
        {
            pos++;
            F64 term = parseTerm(expr, pos);
            result = (op == '+') ? (result + term) : (result - term);
        }
        else
        {
            break;
        }
    }

    return result;
}

F64 KVDebugConsole::parseTerm(const std::string& expr, size_t& pos)
{
    F64 result = parseFactor(expr, pos);

    while (pos < expr.length())
    {
        while (pos < expr.length() && std::isspace(expr[pos])) pos++;
        if (pos >= expr.length()) break;

        char op = expr[pos];
        if (op == '*' || op == '/' || op == '%')
        {
            pos++;
            F64 factor = parseFactor(expr, pos);
            if (op == '*')
                result *= factor;
            else if (op == '/')
            {
                if (factor == 0.0)
                    throw std::runtime_error("Division by zero");
                result /= factor;
            }
            else // %
            {
                if (factor == 0.0)
                    throw std::runtime_error("Modulo by zero");
                result = fmod(result, factor);
            }
        }
        else
        {
            break;
        }
    }

    return result;
}

F64 KVDebugConsole::parseFactor(const std::string& expr, size_t& pos)
{
    while (pos < expr.length() && std::isspace(expr[pos])) pos++;

    if (pos >= expr.length())
        throw std::runtime_error("Unexpected end of expression");

    // Handle parentheses
    if (expr[pos] == '(')
    {
        pos++;
        F64 result = parseExpression(expr, pos);
        while (pos < expr.length() && std::isspace(expr[pos])) pos++;
        if (pos >= expr.length() || expr[pos] != ')')
            throw std::runtime_error("Missing closing parenthesis");
        pos++;
        return result;
    }

    // Handle unary minus
    if (expr[pos] == '-')
    {
        pos++;
        return -parseFactor(expr, pos);
    }

    // Handle unary plus
    if (expr[pos] == '+')
    {
        pos++;
        return parseFactor(expr, pos);
    }

    // Parse number
    size_t start = pos;
    bool has_dot = false;

    while (pos < expr.length() && (std::isdigit(expr[pos]) || expr[pos] == '.'))
    {
        if (expr[pos] == '.')
        {
            if (has_dot)
                throw std::runtime_error("Invalid number format (multiple decimal points)");
            has_dot = true;
        }
        pos++;
    }

    if (pos == start)
        throw std::runtime_error(llformat("Expected number but found '%c'", expr[pos]));

    std::string num_str = expr.substr(start, pos - start);
    return std::stod(num_str);
}

// S24: Time command - display SL time and local time
void KVDebugConsole::processTimeCommand()
{
    // Get SL (Pacific) time
    time_t now = time(nullptr);
    struct tm* sl_time = gmtime(&now);  // SL time is Pacific (UTC-8/UTC-7)
    sl_time->tm_hour -= 8;  // Rough approximation to Pacific
    mktime(sl_time);  // Normalize

    char sl_buffer[64];
    strftime(sl_buffer, sizeof(sl_buffer), "%Y-%m-%d %H:%M:%S SLT", sl_time);

    // Get local time
    struct tm* local_time = localtime(&now);
    char local_buffer[64];
    strftime(local_buffer, sizeof(local_buffer), "%Y-%m-%d %H:%M:%S", local_time);

    mOutput->appendText(llformat("SL Time:    %s\n", sl_buffer), false);
    mOutput->appendText(llformat("Local Time: %s\n", local_buffer), false);
    mLineCount += 2;
}

// S24: Uptime command - show how long the viewer has been running
void KVDebugConsole::processUptimeCommand()
{
    F64 uptime = LLFrameTimer::getElapsedSeconds();

    // Convert to days, hours, minutes, seconds
    S32 days = (S32)(uptime / 86400.0);
    uptime -= days * 86400.0;
    S32 hours = (S32)(uptime / 3600.0);
    uptime -= hours * 3600.0;
    S32 minutes = (S32)(uptime / 60.0);
    uptime -= minutes * 60.0;
    S32 seconds = (S32)uptime;

    std::string uptime_str;
    if (days > 0)
        uptime_str = llformat("%d day%s, %d hour%s, %d minute%s, %d second%s",
            days, (days == 1) ? "" : "s",
            hours, (hours == 1) ? "" : "s",
            minutes, (minutes == 1) ? "" : "s",
            seconds, (seconds == 1) ? "" : "s");
    else if (hours > 0)
        uptime_str = llformat("%d hour%s, %d minute%s, %d second%s",
            hours, (hours == 1) ? "" : "s",
            minutes, (minutes == 1) ? "" : "s",
            seconds, (seconds == 1) ? "" : "s");
    else if (minutes > 0)
        uptime_str = llformat("%d minute%s, %d second%s",
            minutes, (minutes == 1) ? "" : "s",
            seconds, (seconds == 1) ? "" : "s");
    else
        uptime_str = llformat("%d second%s", seconds, (seconds == 1) ? "" : "s");

    mOutput->appendText(llformat("✓ Viewer uptime: %s\n", uptime_str.c_str()), false);
    mLineCount++;
}

// S24: Version command - display viewer version and build information
void KVDebugConsole::processVersionCommand()
{
    LLVersionInfo& version = LLVersionInfo::instance();

    // Get version components
    std::string channel = version.getChannel();
    std::string full_version = version.getVersion();
    std::string short_version = version.getShortVersion();
    std::string build_config = version.getBuildConfig();
    S32 major = version.getMajor();
    S32 minor = version.getMinor();
    S32 patch = version.getPatch();
    U64 build = version.getBuild();

    // Display formatted version information
    mOutput->appendText(
        "╔═══════════════════════════════════════════════════════════════════════╗\n"
        "║                       VIEWER VERSION INFORMATION                      ║\n"
        "╚═══════════════════════════════════════════════════════════════════════╝\n"
        "\n",
        false
    );

    mOutput->appendText(llformat("Channel:        %s\n", channel.c_str()), false);
    mOutput->appendText(llformat("Version:        %s (Build %llu)\n", full_version.c_str(), build), false);
    mOutput->appendText(llformat("Short Version:  %s\n", short_version.c_str()), false);
    mOutput->appendText(llformat("Components:     Major: %d, Minor: %d, Patch: %d\n", major, minor, patch), false);
    mOutput->appendText(llformat("Build Config:   %s\n", build_config.c_str()), false);

    // S24: Platform detection - Windows x64 only, anything else is unknown/unsupported
    std::string platform_info;
#if LL_WINDOWS && LL_X86_64
    platform_info = "Windows x64 (64-bit)";
#else
    platform_info = "Unknown (Unsupported Platform)";
#endif

    mOutput->appendText(llformat("Platform:       %s\n", platform_info.c_str()), false);
    mOutput->appendText("\n", false);

    mLineCount += 12;
}

// S24: MOTD command - display message of the day from login
void KVDebugConsole::processMotdCommand()
{
    // Display formatted MOTD header
    mOutput->appendText(
        "╔═══════════════════════════════════════════════════════════════════════╗\n"
        "║                         MESSAGE OF THE DAY                            ║\n"
        "╚═══════════════════════════════════════════════════════════════════════╝\n"
        "\n",
        false
    );

    if (gAgent.mMOTD.empty())
    {
        printWarning("No message of the day available.\n");
        printInfo("MOTD is captured at login and may not be available if viewer was not restarted.\n\n");
        mLineCount += 7;
    }
    else
    {
        // Parse MOTD for URLs - simple detection of http:// or https://
        std::string motd_text = gAgent.mMOTD;
        std::string url_found;

        // Look for http:// or https://
        size_t http_pos = motd_text.find("http://");
        size_t https_pos = motd_text.find("https://");

        if (http_pos != std::string::npos || https_pos != std::string::npos)
        {
            // Find the start of the URL
            size_t url_start = (http_pos != std::string::npos) ? http_pos : https_pos;

            // Find the end of the URL (space, newline, or end of string)
            size_t url_end = motd_text.find_first_of(" \n\r\t", url_start);
            if (url_end == std::string::npos)
            {
                url_end = motd_text.length();
            }

            url_found = motd_text.substr(url_start, url_end - url_start);
        }

        // Display the MOTD text
        mOutput->appendText(motd_text + "\n\n", false);

        // If URL found, display it separately
        if (!url_found.empty())
        {
            LLStyle::Params style;
            style.color = LLColor4::cyan;
            mOutput->appendText("URL: ", false);
            mOutput->appendText(url_found + "\n\n", false, style);
            mLineCount += 7;
        }
        else
        {
            mLineCount += 6;
        }
    }
}

// S24: Web command - open URL in preferred browser
void KVDebugConsole::processWebCommand(const std::string& url)
{
    // Validate URL has a protocol
    std::string url_to_open = url;

    // If no protocol specified, assume https://
    if (url.find("://") == std::string::npos)
    {
        url_to_open = "https://" + url;
        printInfo(llformat("No protocol specified, assuming: %s\n", url_to_open.c_str()));
    }

    // Basic validation - check for common protocols
    bool valid_protocol = false;
    if (url_to_open.substr(0, 7) == "http://" ||
        url_to_open.substr(0, 8) == "https://" ||
        url_to_open.substr(0, 6) == "ftp://")
    {
        valid_protocol = true;
    }

    if (!valid_protocol)
    {
        printError(llformat("✗ Invalid URL protocol: %s\n", url_to_open.c_str()));
        printWarning("Supported protocols: http://, https://, ftp://\n\n");
        mLineCount += 3;
        return;
    }

    // Open URL using user's preferred browser setting
    LLWeb::loadURL(url_to_open);

    printSuccess(llformat("✓ Opening URL: %s\n", url_to_open.c_str()));
    printInfo("URL opened in browser (respects Preferences > Web browser setting)\n\n");
    mLineCount += 3;
}

// S24: Top command - display ranked objects by various metrics
void KVDebugConsole::processTopCommand(const std::string& args)
{
    // Show help if requested
    if (args == "-h" || args == "--help" || args == "help" || args == "?")
    {
        mOutput->appendText(
            "╔═══════════════════════════════════════════════════════════════════════╗\n"
            "║                         TOP COMMAND - HELP                            ║\n"
            "╚═══════════════════════════════════════════════════════════════════════╝\n\n",
            false
        );

        printInfo("SYNOPSIS:\n");
        mOutput->appendText("  top [mode] [options]\n\n", false);

        printInfo("DESCRIPTION:\n");
        mOutput->appendText("  Display top objects by various performance metrics.\n\n", false);

        printInfo("SORT MODES:\n");
        mOutput->appendText("  cost, render     Object render cost/weight (default)\n", false);
        mOutput->appendText("  physics, phys    Physics cost\n", false);
        mOutput->appendText("  streaming        Streaming cost\n", false);
        mOutput->appendText("  triangles, tris  Triangle/polygon count\n", false);
        mOutput->appendText("  script, scripts  Scripted objects (sorted by cost)\n\n", false);

        printInfo("SCOPE OPTIONS:\n");
        mOutput->appendText("  -p, --parcel     Limit to current parcel only\n", false);
        mOutput->appendText("  -r, --region     Limit to current region only\n\n", false);

        printInfo("OWNERSHIP FILTERS:\n");
        mOutput->appendText("  -s, --self       Show only objects you own\n", false);
        mOutput->appendText("  -g, --group      Show only group-owned objects\n", false);
        mOutput->appendText("  -o, --owner UUID Filter by specific owner UUID\n\n", false);

        printInfo("DISPLAY OPTIONS:\n");
        mOutput->appendText("  -n, --limit N    Show top N results (default: 10, max: 50)\n\n", false);

        printInfo("EXAMPLES:\n");
        LLStyle::Params example_style;
        example_style.color = LLColor4::cyan;
        mOutput->appendText("  top scripts -p -s\n", false, example_style);
        mOutput->appendText("    → Your scripted objects in your parcel\n\n", false);
        mOutput->appendText("  top cost -r --limit 20\n", false, example_style);
        mOutput->appendText("    → Top 20 highest-cost objects in region\n\n", false);
        mOutput->appendText("  top physics -p -g\n", false, example_style);
        mOutput->appendText("    → Group-owned objects by physics cost in parcel\n\n", false);
        mOutput->appendText("  top scripts -o aaaabbbb-1234-5678-90ab-cdef12345678\n", false, example_style);
        mOutput->appendText("    → All scripted objects owned by specific UUID\n\n", false);

        printInfo("NOTES:\n");
        mOutput->appendText("  • Objects must have cost data calculated by server\n", false);
        mOutput->appendText("  • Use 'find <uuid>' to locate/beacon any listed object\n", false);
        mOutput->appendText("  • Attachments are always excluded\n\n", false);

        mLineCount += 38;
        return;
    }

    // Parse UNIX-style arguments
    std::string mode = "cost";
    bool region_only = false;
    bool parcel_only = false;
    bool filter_self = false;
    bool filter_group = false;
    LLUUID filter_owner;
    S32 limit = 10;

    // Split arguments by whitespace
    std::vector<std::string> tokens;
    std::istringstream iss(args);
    std::string token;
    while (iss >> token)
    {
        tokens.push_back(token);
    }

    // Parse tokens
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        std::string arg = tokens[i];
        LLStringUtil::toLower(arg);

        // Check for flags
        if (arg[0] == '-')
        {
            // Scope flags
            if (arg == "-p" || arg == "--parcel")
            {
                parcel_only = true;
            }
            else if (arg == "-r" || arg == "--region")
            {
                region_only = true;
            }
            // Ownership flags
            else if (arg == "-s" || arg == "--self")
            {
                filter_self = true;
            }
            else if (arg == "-g" || arg == "--group")
            {
                filter_group = true;
            }
            else if (arg == "-o" || arg == "--owner")
            {
                // Next token should be UUID
                if (i + 1 >= tokens.size())
                {
                    printError("✗ -o/--owner requires a UUID argument\n\n");
                    mLineCount += 2;
                    return;
                }
                ++i;
                filter_owner.set(tokens[i], FALSE);
                if (filter_owner.isNull())
                {
                    printError(llformat("✗ Invalid UUID: %s\n\n", tokens[i].c_str()));
                    mLineCount += 2;
                    return;
                }
            }
            else if (arg == "-n" || arg == "--limit")
            {
                // Next token should be number
                if (i + 1 >= tokens.size())
                {
                    printError("✗ -n/--limit requires a number argument\n\n");
                    mLineCount += 2;
                    return;
                }
                ++i;
                limit = atoi(tokens[i].c_str());
                if (limit < 1 || limit > 50)
                {
                    printError("✗ Limit must be between 1 and 50\n\n");
                    mLineCount += 2;
                    return;
                }
            }
            else
            {
                printError(llformat("✗ Unknown flag: %s\n", arg.c_str()));
                printInfo("Use 'top --help' for usage information\n\n");
                mLineCount += 3;
                return;
            }
        }
        else
        {
            // Not a flag, must be the sort mode
            mode = arg;
        }
    }

    // Determine sort criterion
    enum SortMode {
        SORT_COST,
        SORT_PHYSICS,
        SORT_STREAMING,
        SORT_TRIANGLES,
        SORT_SCRIPT
    };

    SortMode sort_mode = SORT_COST;
    std::string mode_name = "Object Cost";

    if (mode == "cost" || mode == "render" || mode == "")
    {
        sort_mode = SORT_COST;
        mode_name = "Object Cost (Render Weight)";
    }
    else if (mode == "physics" || mode == "phys")
    {
        sort_mode = SORT_PHYSICS;
        mode_name = "Physics Cost";
    }
    else if (mode == "streaming" || mode == "stream")
    {
        sort_mode = SORT_STREAMING;
        mode_name = "Streaming Cost";
    }
    else if (mode == "triangles" || mode == "tris" || mode == "poly")
    {
        sort_mode = SORT_TRIANGLES;
        mode_name = "Triangle Count";
    }
    else if (mode == "script" || mode == "scripts")
    {
        sort_mode = SORT_SCRIPT;
        mode_name = "Scripted Objects";
    }
    else
    {
        printError(llformat("✗ Unknown sort mode: %s\n", mode.c_str()));
        printInfo("Use 'top --help' for usage information\n\n");
        mLineCount += 3;
        return;
    }

    // Get current region and parcel if filtering
    LLViewerRegion* current_region = nullptr;
    LLParcel* current_parcel = nullptr;

    if (region_only)
    {
        current_region = gAgent.getRegion();
        if (!current_region)
        {
            printError("✗ Could not determine current region\n\n");
            mLineCount += 2;
            return;
        }
    }

    if (parcel_only)
    {
        current_region = gAgent.getRegion();
        if (!current_region)
        {
            printError("✗ Could not determine current region\n\n");
            mLineCount += 2;
            return;
        }

        current_parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
        if (!current_parcel)
        {
            printError("✗ Could not determine current parcel\n\n");
            mLineCount += 2;
            return;
        }
    }

    // Collect objects and their metrics
    struct ObjectMetric
    {
        LLViewerObject* object;
        F32 cost;
        F32 physics;
        F32 streaming;
        U32 triangles;
        bool is_scripted;
        LLUUID id;
        LLVector3d global_pos;
    };

    std::vector<ObjectMetric> metrics;
    S32 num_objects = gObjectList.getNumObjects();

    for (S32 i = 0; i < num_objects; i++)
    {
        LLViewerObject* obj = gObjectList.getObject(i);
        if (!obj || obj->isDead() || obj->isAvatar())
        {
            continue;
        }

        // Skip attachments
        if (obj->isAttachment())
        {
            continue;
        }

        // Get object position for filtering
        LLVector3d obj_pos = obj->getPositionGlobal();

        // Region filter
        if (region_only && obj->getRegion() != current_region)
        {
            continue;
        }

        // Parcel filter (implies same region too)
        if (parcel_only)
        {
            if (obj->getRegion() != current_region)
            {
                continue;
            }
            if (!LLViewerParcelMgr::getInstance()->inAgentParcel(obj_pos))
            {
                continue;
            }
        }

        // Ownership filters
        if (filter_self && !obj->permYouOwner())
        {
            continue;
        }

        if (filter_group && !obj->permGroupOwner())
        {
            continue;
        }

        if (!filter_owner.isNull() && obj->mOwnerID != filter_owner)
        {
            continue;
        }

        ObjectMetric metric;
        metric.object = obj;
        metric.cost = obj->getObjectCost();
        metric.physics = obj->getPhysicsCost();
        metric.streaming = obj->getStreamingCost();
        metric.triangles = obj->getTriangleCount();
        metric.is_scripted = obj->flagScripted();
        metric.id = obj->getID();
        metric.global_pos = obj_pos;

        // Filter for script mode
        if (sort_mode == SORT_SCRIPT && !metric.is_scripted)
        {
            continue;
        }

        // Skip objects with zero/invalid cost (not yet calculated)
        // For script mode, we still want cost > 0 since we sort by it
        if ((sort_mode == SORT_COST || sort_mode == SORT_SCRIPT) && metric.cost <= 0.0f)
        {
            continue;
        }
        if (sort_mode == SORT_PHYSICS && metric.physics <= 0.0f)
        {
            continue;
        }
        if (sort_mode == SORT_STREAMING && metric.streaming <= 0.0f)
        {
            continue;
        }

        metrics.push_back(metric);
    }

    // Sort by selected criterion
    std::sort(metrics.begin(), metrics.end(), [sort_mode](const ObjectMetric& a, const ObjectMetric& b) {
        switch (sort_mode)
        {
            case SORT_COST:
                return a.cost > b.cost;
            case SORT_PHYSICS:
                return a.physics > b.physics;
            case SORT_STREAMING:
                return a.streaming > b.streaming;
            case SORT_TRIANGLES:
                return a.triangles > b.triangles;
            case SORT_SCRIPT:
                return a.cost > b.cost;  // For scripts, sort by render cost
            default:
                return a.cost > b.cost;
        }
    });

    // Display header
    mOutput->appendText(
        "╔═══════════════════════════════════════════════════════════════════════╗\n",
        false
    );
    mOutput->appendText(
        llformat("║  TOP OBJECTS - %-55s ║\n", mode_name.c_str()),
        false
    );
    if (parcel_only && current_parcel)
    {
        std::string parcel_name = LLViewerParcelMgr::getInstance()->getAgentParcelName();
        if (parcel_name.length() > 55)
        {
            parcel_name = parcel_name.substr(0, 52) + "...";
        }
        mOutput->appendText(
            llformat("║  Parcel: %-61s ║\n", parcel_name.c_str()),
            false
        );
    }
    else if (region_only && current_region)
    {
        std::string region_name = current_region->getName();
        if (region_name.length() > 55)
        {
            region_name = region_name.substr(0, 52) + "...";
        }
        mOutput->appendText(
            llformat("║  Region: %-62s ║\n", region_name.c_str()),
            false
        );
    }
    mOutput->appendText(
        "╚═══════════════════════════════════════════════════════════════════════╝\n\n",
        false
    );

    if (metrics.empty())
    {
        printWarning("No objects found matching criteria.\n");
        printInfo("Note: Objects must have cost data calculated by server.\n\n");
        mLineCount += (parcel_only || region_only) ? 7 : 6;
        return;
    }

    // Display results (use custom limit)
    S32 count = llmin((S32)metrics.size(), limit);

    // Build scope string
    std::string scope;
    if (parcel_only)
    {
        scope = " in parcel";
    }
    else if (region_only)
    {
        scope = " in region";
    }
    else
    {
        scope = " (all loaded regions)";
    }

    // Add ownership filter info
    std::string filter_info;
    if (filter_self)
    {
        filter_info = ", owned by you";
    }
    else if (filter_group)
    {
        filter_info = ", group-owned";
    }
    else if (!filter_owner.isNull())
    {
        filter_info = llformat(", owner: %s", filter_owner.asString().substr(0, 8).c_str()) + "...";
    }

    mOutput->appendText(llformat("Showing top %d of %d objects%s%s:\n\n", 
                                  count, (S32)metrics.size(), scope.c_str(), filter_info.c_str()), false);

    LLVector3d my_pos = gAgent.getPositionGlobal();

    for (S32 i = 0; i < count; i++)
    {
        const ObjectMetric& m = metrics[i];

        // Calculate distance
        F32 distance = (F32)dist_vec(my_pos, m.global_pos);

        // Rank number
        LLStyle::Params rank_style;
        rank_style.color = LLColor4::cyan;
        mOutput->appendText(llformat("%2d. ", i + 1), false, rank_style);

        // Metric value
        LLStyle::Params value_style;
        value_style.color = (i < 3) ? LLColor4::yellow : LLColor4::white;  // Highlight top 3

        std::string value_str;
        switch (sort_mode)
        {
            case SORT_COST:
                value_str = llformat("Cost: %-6.1f", m.cost);
                break;
            case SORT_PHYSICS:
                value_str = llformat("Phys: %-6.1f", m.physics);
                break;
            case SORT_STREAMING:
                value_str = llformat("Stream: %-6.1f", m.streaming);
                break;
            case SORT_TRIANGLES:
                value_str = llformat("Tris: %-7u", m.triangles);
                break;
            case SORT_SCRIPT:
                value_str = llformat("Cost: %-6.1f", m.cost);
                break;
        }

        mOutput->appendText(llformat("%-22s", value_str.c_str()), false, value_style);

        // Distance
        mOutput->appendText(llformat(" @ %6.1fm", distance), false);

        // Script tag
        if (m.is_scripted)
        {
            LLStyle::Params script_style;
            script_style.color = LLColor4::green;
            mOutput->appendText(" [SCRIPT]", false, script_style);
        }

        mOutput->appendText("\n", false);

        // UUID on second line (indented, grey)
        LLStyle::Params uuid_style;
        uuid_style.color = LLColor4::grey;
        mOutput->appendText(llformat("     %s\n", m.id.asString().c_str()), false, uuid_style);
    }

    mOutput->appendText("\n", false);
    printInfo("TIP: Use 'find <uuid>' to locate/beacon any object\n\n");
    mLineCount += ((parcel_only || region_only) ? 8 : 7) + (count * 2) + 2;
}

// S24: Whois command - look up avatar profile information
void KVDebugConsole::processWhoisCommand(const std::string& args)
{
    LLUUID avatar_id;

    // Try to parse as UUID first
    if (avatar_id.set(args, false))  // false = don't generate warning on parse failure
    {
        // UUID provided - request profile data
        requestAvatarProfile(avatar_id);
    }
    else
    {
        // Search by name - scan nearby avatars
        uuid_vec_t avatar_ids;
        std::vector<LLVector3d> positions;
        LLVector3d my_pos = gAgent.getPositionGlobal();

        // Get all avatars in scene (large radius to catch distant ones)
        LLWorld::getInstance()->getAvatars(&avatar_ids, &positions, my_pos, 1000000.0f);

        std::string search_name = args;
        LLStringUtil::toLower(search_name);

        bool found = false;
        for (size_t i = 0; i < avatar_ids.size(); ++i)
        {
            LLAvatarName av_name;
            if (LLAvatarNameCache::get(avatar_ids[i], &av_name))
            {
                std::string display_name = av_name.getDisplayName();
                std::string username = av_name.getUserName();

                LLStringUtil::toLower(display_name);
                LLStringUtil::toLower(username);

                // Check if search term matches display name or username
                if (display_name.find(search_name) != std::string::npos ||
                    username.find(search_name) != std::string::npos)
                {
                    avatar_id = avatar_ids[i];
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            printError(llformat("✗ Avatar '%s' not found nearby", args.c_str()));
            printInfo("  Try using UUID for avatars not in the current region");
            return;
        }

        requestAvatarProfile(avatar_id);
    }
}

// S24: Request avatar profile and display data when received
void KVDebugConsole::requestAvatarProfile(const LLUUID& avatar_id)
{
    if (!mOutput)
    {
        return;
    }

    // Get avatar name first (display name vs username)
    LLAvatarName av_name;
    bool has_name = LLAvatarNameCache::get(avatar_id, &av_name);

    // Get avatar object if nearby
    LLViewerObject* obj = gObjectList.findObject(avatar_id);
    LLVOAvatar* avatar = obj ? obj->asAvatar() : nullptr;

    // Calculate distance and region info
    LLVector3d my_pos = gAgent.getPositionGlobal();
    LLVector3d target_pos;
    F32 distance = -1.0f;
    std::string region_name = "Unknown";

    if (avatar)
    {
        target_pos = avatar->getPositionGlobal();
        distance = (F32)dist_vec(my_pos, target_pos);
        if (avatar->getRegion())
        {
            region_name = avatar->getRegion()->getName();
        }
    }

    // Display header
    mOutput->appendText(
        "╔═══════════════════════════════════════════════════════════════════════╗\n"
        "║                       AVATAR PROFILE LOOKUP                           ║\n"
        "╚═══════════════════════════════════════════════════════════════════════╝\n"
        "\n",
        false
    );

    // Basic identity
    if (has_name)
    {
        mOutput->appendText(llformat("Avatar:         \"%s\" (%s)\n", 
            av_name.getDisplayName().c_str(), 
            av_name.getUserName().c_str()), false);
    }
    else
    {
        mOutput->appendText(llformat("Avatar:         <Name not cached>\n"), false);
    }

    mOutput->appendText(llformat("UUID:           %s\n", avatar_id.asString().c_str()), false);

    // Location & Distance
    if (distance >= 0.0f)
    {
        mOutput->appendText(llformat("Distance:       %.1fm | Region: %s\n", 
            distance, region_name.c_str()), false);
    }
    else
    {
        mOutput->appendText("Distance:       Not nearby\n", false);
    }

    // Friend status
    bool is_friend = LLAvatarTracker::instance().isBuddy(avatar_id);
    if (is_friend)
    {
        bool is_online = LLAvatarTracker::instance().isBuddyOnline(avatar_id);
        mOutput->appendText(llformat("Friend:         Yes | Status: %s\n", 
            is_online ? "Online" : "Offline"), false);
    }
    else
    {
        mOutput->appendText("Friend:         No\n", false);
    }

    // Mute/Block status
    bool is_muted = LLMuteList::getInstance()->isMuted(avatar_id);
    if (is_muted)
    {
        mOutput->appendText("Muted:          Yes\n", false);
    }

    // Avatar complexity and render info (if nearby)
    if (avatar)
    {
        S32 complexity = avatar->getVisualComplexity();

        mOutput->appendText(llformat("Complexity:     %s\n", 
            LLStringOps::getReadableNumber(complexity).c_str()), false);

        // Avatar render state
        if (avatar->isFullyLoaded())
        {
            mOutput->appendText("Render State:   Fully Loaded\n", false);
        }
        else
        {
            mOutput->appendText("Render State:   Loading...\n", false);
        }
    }

    // Request profile data from server (gentle, cached-friendly query)
    // This uses the same mechanism as opening a profile floater
    LLAvatarPropertiesProcessor::getInstance()->sendAvatarPropertiesRequest(avatar_id);

    // Create a one-shot observer to catch the profile response
    class WhoisProfileObserver : public LLAvatarPropertiesObserver
    {
    public:
        WhoisProfileObserver(const LLUUID& id) : mAvatarId(id) {}

        virtual void processProperties(void* data, EAvatarProcessorType type) override
        {
            if (type == APT_PROPERTIES && data)
            {
                LLAvatarData* avatar_data = static_cast<LLAvatarData*>(data);
                if (avatar_data->avatar_id == mAvatarId)
                {
                    KVDebugConsole* console = LLFloaterReg::findTypedInstance<KVDebugConsole>("kvdebugconsole");
                    if (console && console->mOutput)
                    {
                        // Account Type
                        if (!avatar_data->customer_type.empty())
                        {
                            console->mOutput->appendText(llformat("Account Type:   %s\n", 
                                avatar_data->customer_type.c_str()), false);
                        }

                        // Partner
                        if (avatar_data->partner_id.notNull())
                        {
                            std::string partner_name = "Loading...";
                            LLAvatarName partner_av_name;
                            if (LLAvatarNameCache::get(avatar_data->partner_id, &partner_av_name))
                            {
                                partner_name = partner_av_name.getDisplayName();
                            }
                            console->mOutput->appendText(llformat("Partner:        %s\n", 
                                partner_name.c_str()), false);
                        }

                        // Account Age
                        if (avatar_data->born_on.notNull())
                        {
                            S32 born_year, born_month, born_day;
                            avatar_data->born_on.split(&born_year, &born_month, &born_day);

                            // Calculate age in days
                            LLDate now = LLDate::now();
                            S32 age_days = (S32)((now.secondsSinceEpoch() - avatar_data->born_on.secondsSinceEpoch()) / 86400);
                            S32 age_years = age_days / 365;
                            S32 remaining_days = age_days % 365;

                            console->mOutput->appendText(llformat("Account Age:    %d years, %d days (Rez: %04d-%02d-%02d)\n", 
                                age_years, remaining_days, born_year, born_month, born_day), false);
                        }

                        console->mLineCount += 3;
                    }

                    // Remove ourselves after processing
                    LLAvatarPropertiesProcessor::getInstance()->removeObserver(mAvatarId, this);
                    delete this;
                }
            }
        }

    private:
        LLUUID mAvatarId;
    };

    // Register observer (will self-destruct after receiving data)
    LLAvatarPropertiesProcessor::getInstance()->addObserver(avatar_id, new WhoisProfileObserver(avatar_id));

    if (!has_name)
    {
        printWarning("⚠ Name data not cached - avatar may be out of range");
    }

    mLineCount += 12;  // Base lines, profile data adds more via callback
}

