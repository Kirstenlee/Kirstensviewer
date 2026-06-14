/**
 * @file llapp.cpp
 * @brief Implementation of the LLApp class.
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#include "llapp.h"

#include <cstdlib>

#include "llcommon.h"
#include "llapr.h"
#include "llerrorcontrol.h"
#include "llframetimer.h"
#include "lllivefile.h"
#include "llmemory.h"
#include "llstl.h" // for DeletePointer()
#include "llstring.h"
#include "lleventtimer.h"
#include "stringize.h"
#include "llcleanup.h"
#include "llevents.h"
#include "llsdutil.h"

 // the static application instance
LLApp* LLApp::sApplication = NULL;

// Allows the generation of core files for post mortem under gdb
// and disables crashlogger
bool LLApp::sDisableCrashlogger = false;

// static
// Keeps track of application status
LLScalarCond<LLApp::EAppStatus> LLApp::sStatus{ LLApp::APP_STATUS_STOPPED };
LLAppErrorHandler LLApp::sErrorHandler = NULL;

bool gDisconnected = false;

LLApp::LLApp()
{
	// Set our status to running
	setStatus(APP_STATUS_RUNNING);

	LLCommon::initClass();

	// initialize the options structure. We need to make this an array
	// because the structured data will not auto-allocate if we
	// reference an invalid location with the [] operator.
	mOptions = LLSD::emptyArray();
	LLSD sd;
	for (int i = 0; i < PRIORITY_COUNT; ++i)
	{
		mOptions.append(sd);
	}

	// Make sure we clean up APR when we exit
	// Don't need to do this if we're cleaning up APR in the destructor
	//atexit(ll_cleanup_apr);

	// Set the application to this instance.
	sApplication = this;

	// initialize the buffer to write the minidump filename to
	// (this is used to avoid allocating memory in the crash handler)
	memset(mMinidumpPath, 0, MAX_MINDUMP_PATH_LENGTH);
	mCrashReportPipeStr = L"\\\\.\\pipe\\LLCrashReporterPipe";
}

LLApp::~LLApp()
{
	// reclaim live file memory
	std::for_each(mLiveFiles.begin(), mLiveFiles.end(), DeletePointer());
	mLiveFiles.clear();

	setStopped();

	SUBSYSTEM_CLEANUP_DBG(LLCommon);
}

// static
LLApp* LLApp::instance()
{
	return sApplication;
}

LLSD LLApp::getOption(const std::string& name) const
{
	LLSD rv;
	LLSD::array_const_iterator iter = mOptions.beginArray();
	LLSD::array_const_iterator end = mOptions.endArray();
	for (; iter != end; ++iter)
	{
		rv = (*iter)[name];
		if (rv.isDefined()) break;
	}
	return rv;
}

bool LLApp::parseCommandOptions(int argc, char** argv)
{
	LLSD commands;
	std::string name;
	std::string value;
	for (int ii = 1; ii < argc; ++ii)
	{
		if (argv[ii][0] != '-')
		{
			LL_INFOS() << "Did not find option identifier while parsing token: "
				<< argv[ii] << LL_ENDL;
			return false;
		}
		int offset = 1;
		if (argv[ii][1] == '-') ++offset;
		name.assign(&argv[ii][offset]);
		if (((ii + 1) >= argc) || (argv[ii + 1][0] == '-'))
		{
			// we found another option after this one or we have
			// reached the end. simply record that this option was
			// found and continue.
			int flag = name.compare("logfile");
			if (0 == flag)
			{
				commands[name] = "log";
			}
			else
			{
				commands[name] = true;
			}

			continue;
		}
		++ii;
		value.assign(argv[ii]);

#if LL_WINDOWS
		//Windows changed command line parsing.  Deal with it.
		size_t slen = value.length() - 1;
		size_t start = 0;
		size_t end = slen;
		if (argv[ii][start] == '"')start++;
		if (argv[ii][end] == '"')end--;
		if (start != 0 || end != slen)
		{
			value = value.substr(start, end);
		}
#endif

		commands[name] = value;
	}
	setOptionData(PRIORITY_COMMAND_LINE, commands);
	return true;
}

bool LLApp::parseCommandOptions(int argc, wchar_t** wargv)
{
	LLSD commands;
	std::string name;
	std::string value;
	for (int ii = 1; ii < argc; ++ii)
	{
		if (wargv[ii][0] != '-')
		{
			LL_INFOS() << "Did not find option identifier while parsing token: "
				<< (intptr_t)wargv[ii] << LL_ENDL;
			return false;
		}
		int offset = 1;
		if (wargv[ii][1] == '-') ++offset;

    name.assign(ll_convert_wide_to_string(&wargv[ii][offset]));


		if (((ii + 1) >= argc) || (wargv[ii + 1][0] == '-'))
		{
			// we found another option after this one or we have
			// reached the end. simply record that this option was
			// found and continue.
			int flag = name.compare("logfile");
			if (0 == flag)
			{
				commands[name] = "log";
			}
			else
			{
				commands[name] = true;
			}

			continue;
		}
		++ii;

    value.assign(ll_convert_wide_to_string((wargv[ii])));


		//Windows changed command line parsing.  Deal with it.
		size_t slen = value.length() - 1;
		size_t start = 0;
		size_t end = slen;
		if (wargv[ii][start] == '"')start++;
		if (wargv[ii][end] == '"')end--;
		if (start != 0 || end != slen)
		{
			value = value.substr(start, end);
		}

		commands[name] = value;
	}
	setOptionData(PRIORITY_COMMAND_LINE, commands);
	return true;
}

void LLApp::manageLiveFile(LLLiveFile* livefile)
{
	if (!livefile) return;
	livefile->checkAndReload();
	livefile->addToEventTimer();
	mLiveFiles.push_back(livefile);
}

bool LLApp::setOptionData(OptionPriority level, LLSD data)
{
	if ((level < 0)
		|| (level >= PRIORITY_COUNT)
		|| (data.type() != LLSD::TypeMap))
	{
		return false;
	}
	mOptions[level] = data;
	return true;
}

LLSD LLApp::getOptionData(OptionPriority level)
{
	if ((level < 0) || (level >= PRIORITY_COUNT))
	{
		return LLSD();
	}
	return mOptions[level];
}

void LLApp::stepFrame()
{
	LLFrameTimer::updateFrameTime();
	LLFrameTimer::updateFrameCount();
	LLEventTimer::updateClass();
	mRunner.run();
}

void LLApp::setupErrorHandling(bool second_instance)
{
	// Error handling is done by starting up an error handling thread, which just sleeps and
	// occasionally checks to see if the app is in an error state, and sees if it needs to be run.

	//
	// Start the error handling thread, which is responsible for taking action
	// when the app goes into the APP_STATUS_ERROR state
	//
}

void LLApp::setErrorHandler(LLAppErrorHandler handler)
{
	LLApp::sErrorHandler = handler;
}

// static
void LLApp::runErrorHandler()
{
	if (LLApp::sErrorHandler)
	{
		LLApp::sErrorHandler();
	}

    //LL_INFOS() << "App status now STOPPED" << LL_ENDL;
	LLApp::setStopped();
}

namespace
{

	static std::map<LLApp::EAppStatus, const char*> statusDesc
	{
		{ LLApp::APP_STATUS_RUNNING,  "running" },
		{ LLApp::APP_STATUS_QUITTING, "quitting" },
		{ LLApp::APP_STATUS_STOPPED,  "stopped" },
		{ LLApp::APP_STATUS_ERROR,    "error" }
	};

} // anonymous namespace

// static
void LLApp::setStatus(EAppStatus status)
{
	auto status_it = statusDesc.find(status);
	std::string status_text = status_it != statusDesc.end() ? std::string(status_it->second) : std::to_string(status);
	LL_INFOS() << "status: " << status_text << LL_ENDL;
	// notify everyone waiting on sStatus any time its value changes
	sStatus.set_all(status);

	// This can also happen very late in the application lifecycle -- don't
	// resurrect a deleted LLSingleton
	if (!LLEventPumps::wasDeleted())
	{
		// notify interested parties of status change
		LLEventPumps::instance().obtain("LLApp").post(llsd::map("status", status_text));
	}
}


// static
void LLApp::setError()
{
	// set app status to ERROR so that the LLErrorThread notices
	setStatus(APP_STATUS_ERROR);
}

void LLApp::setDebugFileNames(const std::string& path)
{
	mStaticDebugFileName = path + "static_debug_info.log";
	mDynamicDebugFileName = path + "dynamic_debug_info.log";
}

void LLApp::writeMiniDump()
{
}

// static
void LLApp::setQuitting()
{
	if (!isExiting())
	{
		// If we're already exiting, we don't want to reset our state back to quitting.
		LL_INFOS() << "Setting app state to QUITTING" << LL_ENDL;
		setStatus(APP_STATUS_QUITTING);
	}
}


// static
void LLApp::setStopped()
{
	setStatus(APP_STATUS_STOPPED);
}


// static
bool LLApp::isStopped()
{
	return (APP_STATUS_STOPPED == sStatus.get());
}


// static
bool LLApp::isRunning()
{
	return (APP_STATUS_RUNNING == sStatus.get());
}


// static
bool LLApp::isError()
{
	return (APP_STATUS_ERROR == sStatus.get());
}


// static
bool LLApp::isQuitting()
{
	return (APP_STATUS_QUITTING == sStatus.get());
}

// static
bool LLApp::isExiting()
{
	return isQuitting() || isError();
}

void LLApp::disableCrashlogger()
{
	sDisableCrashlogger = true;
}

// static
bool LLApp::isCrashloggerDisabled()
{
	return sDisableCrashlogger;
}

// static
int LLApp::getPid()
{
	return GetCurrentProcessId();
}

// static
void LLApp::notifyOutOfDiskSpace()
{
	static const U32Seconds min_interval = U32Seconds(60);
	static U32Seconds min_time_to_send = U32Seconds(0);
	U32Seconds now = LLTimer::getTotalTime();
	if (now < min_time_to_send)
		return;

	min_time_to_send = now + min_interval;

	if (LLApp* app = instance())
	{
		app->sendOutOfDiskSpaceNotification();
	}
	else
	{
		LL_WARNS() << "No app instance" << LL_ENDL;
	}
}

// Win32 doesn't support signals. This is used instead.
void LLApp::sendOutOfDiskSpaceNotification()
{
	LL_WARNS() << "Should never be called" << LL_ENDL; // Should be overridden
}