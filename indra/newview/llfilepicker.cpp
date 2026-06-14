/**
 * @file llfilepicker.cpp
 * @brief OS-specific file picker
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "llviewerprecompiledheaders.h"

#include "llfilepicker.h"
#include "llworld.h"
#include "llviewerwindow.h"
#include "llkeyboard.h"
#include "lldir.h"
#include "llframetimer.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llwindow.h"   // beforeDialog()

 //
 // Globals
 //

LLFilePicker LLFilePicker::sInstance;

#if LL_WINDOWS
#define SOUND_FILTER L"Sounds (*.wav)\0*.wav\0"
#define IMAGE_FILTER L"Images (*.tga; *.bmp; *.jpg; *.jpeg; *.png)\0*.tga;*.bmp;*.jpg;*.jpeg;*.png\0"
#define ANIM_FILTER L"Animations (*.bvh; *.anim)\0*.bvh;*.anim\0"
#define COLLADA_FILTER L"Scene (*.dae)\0*.dae\0"
#define GLTF_FILTER L"glTF (*.gltf; *.glb)\0*.gltf;*.glb\0"
#define XML_FILTER L"XML files (*.xml)\0*.xml\0"
#define SLOBJECT_FILTER L"Objects (*.slobject)\0*.slobject\0"
#define RAW_FILTER L"RAW files (*.raw)\0*.raw\0"
#define MODEL_FILTER L"Model files (*.dae, *.gltf, *.glb)\0*.dae;*.gltf;*.glb\0"
#define MATERIAL_FILTER L"GLTF Files (*.gltf; *.glb)\0*.gltf;*.glb\0"
#define HDRI_FILTER L"HDRI Files (*.exr)\0*.exr\0"
#define MATERIAL_TEXTURES_FILTER L"GLTF Import (*.gltf; *.glb; *.tga; *.bmp; *.jpg; *.jpeg; *.png)\0*.gltf;*.glb;*.tga;*.bmp;*.jpg;*.jpeg;*.png\0"
#define SCRIPT_FILTER L"Script files (*.lsl)\0*.lsl\0"
#define DICTIONARY_FILTER L"Dictionary files (*.dic; *.xcu)\0*.dic;*.xcu\0"
#endif

//
// Implementation
//
LLFilePicker::LLFilePicker()
	: mCurrentFile(0),
	mLocked(false)

{
	reset();

	mOFN.lStructSize = sizeof(OPENFILENAMEW);
	mOFN.hwndOwner = NULL;  // Set later
	mOFN.hInstance = NULL;
	mOFN.lpstrCustomFilter = NULL;
	mOFN.nMaxCustFilter = 0;
	mOFN.lpstrFile = NULL;                          // set in open and close
	mOFN.nMaxFile = LL_MAX_PATH;
	mOFN.lpstrFileTitle = NULL;
	mOFN.nMaxFileTitle = 0;
	mOFN.lpstrInitialDir = NULL;
	mOFN.lpstrTitle = NULL;
	mOFN.Flags = 0;                                 // set in open and close
	mOFN.nFileOffset = 0;
	mOFN.nFileExtension = 0;
	mOFN.lpstrDefExt = NULL;
	mOFN.lCustData = 0L;
	mOFN.lpfnHook = NULL;
	mOFN.lpTemplateName = NULL;
	mFilesW[0] = '\0';
}

LLFilePicker::~LLFilePicker()
{
	// nothing
}

// utility function to check if access to local file system via file browser
// is enabled and if not, tidy up and indicate we're not allowed to do this.
bool LLFilePicker::check_local_file_access_enabled()
{
	// if local file browsing is turned off, return without opening dialog
	bool local_file_system_browsing_enabled = gSavedSettings.getBOOL("LocalFileSystemBrowsingEnabled");
	if (!local_file_system_browsing_enabled)
	{
		mFiles.clear();
		return false;
	}

	return true;
}

const std::string LLFilePicker::getFirstFile()
{
	mCurrentFile = 0;
	return getNextFile();
}

const std::string LLFilePicker::getNextFile()
{
	if (mCurrentFile >= getFileCount())
	{
		mLocked = false;
		return std::string();
	}
	else
	{
		return mFiles[mCurrentFile++];
	}
}

const std::string LLFilePicker::getCurFile()
{
	if (mCurrentFile >= getFileCount())
	{
		mLocked = false;
		return std::string();
	}
	else
	{
		return mFiles[mCurrentFile];
	}
}

void LLFilePicker::reset()
{
	mLocked = false;
	mFiles.clear();
	mCurrentFile = 0;
}

bool LLFilePicker::setupFilter(ELoadFilter filter)
{
	bool res = true;
	switch (filter)
	{
	case FFLOAD_ALL:
	case FFLOAD_EXE:
		mOFN.lpstrFilter = L"All Files (*.*)\0*.*\0" \
			SOUND_FILTER \
			IMAGE_FILTER \
			ANIM_FILTER \
			MATERIAL_FILTER \
			L"\0";
		break;
	case FFLOAD_WAV:
		mOFN.lpstrFilter = SOUND_FILTER \
			L"\0";
		break;
	case FFLOAD_IMAGE:
		mOFN.lpstrFilter = IMAGE_FILTER \
			L"\0";
		break;
	case FFLOAD_ANIM:
		mOFN.lpstrFilter = ANIM_FILTER \
			L"\0";
		break;
	case FFLOAD_GLTF:
		mOFN.lpstrFilter = GLTF_FILTER \
			L"\0";
		break;
	case FFLOAD_COLLADA:
		mOFN.lpstrFilter = COLLADA_FILTER \
			L"\0";
		break;
	case FFLOAD_XML:
		mOFN.lpstrFilter = XML_FILTER \
			L"\0";
		break;
	case FFLOAD_SLOBJECT:
		mOFN.lpstrFilter = SLOBJECT_FILTER \
			L"\0";
		break;
	case FFLOAD_RAW:
		mOFN.lpstrFilter = RAW_FILTER \
			L"\0";
		break;
	case FFLOAD_MODEL:
		mOFN.lpstrFilter = MODEL_FILTER \
            COLLADA_FILTER \
            MATERIAL_FILTER \
			L"\0";
		break;
	case FFLOAD_MATERIAL:
		mOFN.lpstrFilter = MATERIAL_FILTER \
			L"\0";
		break;
	case FFLOAD_MATERIAL_TEXTURE:
		mOFN.lpstrFilter = MATERIAL_TEXTURES_FILTER \
			MATERIAL_FILTER \
			IMAGE_FILTER \
			L"\0";
		break;
	case FFLOAD_HDRI:
		mOFN.lpstrFilter = HDRI_FILTER \
			L"\0";
		break;
	case FFLOAD_SCRIPT:
		mOFN.lpstrFilter = SCRIPT_FILTER \
			L"\0";
		break;
	case FFLOAD_DICTIONARY:
		mOFN.lpstrFilter = DICTIONARY_FILTER \
			L"\0";
		break;
	default:
		res = false;
		break;
	}
	return res;
}

bool LLFilePicker::getOpenFile(ELoadFilter filter, bool blocking)
{
	if (mLocked)
	{
		return false;
	}
	bool success = false;

	// if local file browsing is turned off, return without opening dialog
	if (!check_local_file_access_enabled())
	{
		return false;
	}

	// don't provide default file selection
	mFilesW[0] = '\0';

	mOFN.hwndOwner = (HWND)gViewerWindow->getPlatformWindow();
	mOFN.lpstrFile = mFilesW;
	mOFN.nMaxFile = SINGLE_FILENAME_BUFFER_SIZE;
	mOFN.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	mOFN.nFilterIndex = 1;

	setupFilter(filter);

	if (blocking)
	{
		// Modal, so pause agent
		send_agent_pause();
	}

	reset();

	// NOTA BENE: hitting the file dialog triggers a window focus event, destroying the selection manager!!
	success = GetOpenFileName(&mOFN);
	if (success)
	{
        std::string filename = ll_convert<std::string>(std::wstring(mFilesW));
		mFiles.push_back(filename);
	}

	if (blocking)
	{
		send_agent_resume();
		// Account for the fact that the app has been stalled.
		LLFrameTimer::updateFrameTime();
	}

	return success;
}

bool LLFilePicker::getOpenFileModeless(ELoadFilter filter,
	void (*callback)(bool, std::vector<std::string>&, void*),
	void* userdata)
{
	// not supposed to be used yet, use LLFilePickerThread
	LL_ERRS() << "NOT IMPLEMENTED" << LL_ENDL;
	return false;
}

bool LLFilePicker::getMultipleOpenFiles(ELoadFilter filter, bool blocking)
{
	if (mLocked)
	{
		return false;
	}
	bool success = false;

	// if local file browsing is turned off, return without opening dialog
	if (!check_local_file_access_enabled())
	{
		return false;
	}

	// don't provide default file selection
	mFilesW[0] = '\0';

	mOFN.hwndOwner = (HWND)gViewerWindow->getPlatformWindow();
	mOFN.lpstrFile = mFilesW;
	mOFN.nFilterIndex = 1;
	mOFN.nMaxFile = FILENAME_BUFFER_SIZE;
	mOFN.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR |
		OFN_EXPLORER | OFN_ALLOWMULTISELECT;

	setupFilter(filter);

	reset();

	if (blocking)
	{
		// Modal, so pause agent
		send_agent_pause();
	}

	// NOTA BENE: hitting the file dialog triggers a window focus event, destroying the selection manager!!
	success = GetOpenFileName(&mOFN); // pauses until ok or cancel.
	if (success)
	{
		// The getopenfilename api doesn't tell us if we got more than
		// one file, so we have to test manually by checking string
		// lengths.
		if (wcslen(mOFN.lpstrFile) > mOFN.nFileOffset) /*Flawfinder: ignore*/
		{
            std::string filename = ll_convert<std::string>(std::wstring(mFilesW));
			mFiles.push_back(filename);
		}
		else
		{
			mLocked = true;
			WCHAR* tptrw = mFilesW;
			std::string dirname;
			while (1)
			{
				if (*tptrw == 0 && *(tptrw + 1) == 0) // double '\0'
					break;
				if (*tptrw == 0)
					tptrw++; // shouldn't happen?
                std::string filename = ll_convert<std::string>(std::wstring(tptrw));
				if (dirname.empty())
					dirname = filename + "\\";
				else
					mFiles.push_back(dirname + filename);
				tptrw += wcslen(tptrw);
			}
		}
	}

	if (blocking)
	{
		send_agent_resume();
	}

	// Account for the fact that the app has been stalled.
	LLFrameTimer::updateFrameTime();
	return success;
}

bool LLFilePicker::getMultipleOpenFilesModeless(ELoadFilter filter,
	void (*callback)(bool, std::vector<std::string>&, void*),
	void* userdata)
{
	// not supposed to be used yet, use LLFilePickerThread
	LL_ERRS() << "NOT IMPLEMENTED" << LL_ENDL;
	return false;
}

bool LLFilePicker::getSaveFile(ESaveFilter filter, const std::string& filename, bool blocking)
{
	if (mLocked)
	{
		return false;
	}
	bool success = false;

	// if local file browsing is turned off, return without opening dialog
	if (!check_local_file_access_enabled())
	{
		return false;
	}

	mOFN.lpstrFile = mFilesW;
	if (!filename.empty())
	{
        std::wstring tstring = ll_convert<std::wstring>(filename);
        wcsncpy(mFilesW, tstring.c_str(), FILENAME_BUFFER_SIZE);    }   /*Flawfinder: ignore*/
	else
	{
		mFilesW[0] = '\0';
	}
	mOFN.hwndOwner = (HWND)gViewerWindow->getPlatformWindow();

	switch (filter)
	{
	case FFSAVE_ALL:
		mOFN.lpstrDefExt = NULL;
		mOFN.lpstrFilter =
			L"All Files (*.*)\0*.*\0" \
			L"WAV Sounds (*.wav)\0*.wav\0" \
			L"Targa, Bitmap Images (*.tga; *.bmp)\0*.tga;*.bmp\0" \
			L"\0";
		break;
	case FFSAVE_WAV:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.wav", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"wav";
		mOFN.lpstrFilter =
			L"WAV Sounds (*.wav)\0*.wav\0" \
			L"\0";
		break;
	case FFSAVE_TGA:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.tga", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"tga";
		mOFN.lpstrFilter =
			L"Targa Images (*.tga)\0*.tga\0" \
			L"\0";
		break;
	case FFSAVE_BMP:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.bmp", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"bmp";
		mOFN.lpstrFilter =
			L"Bitmap Images (*.bmp)\0*.bmp\0" \
			L"\0";
		break;
	case FFSAVE_PNG:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.png", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"png";
		mOFN.lpstrFilter =
			L"PNG Images (*.png)\0*.png\0" \
			L"\0";
		break;
	case FFSAVE_TGAPNG:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.png", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
			//PNG by default
		}
		mOFN.lpstrDefExt = L"png";
		mOFN.lpstrFilter =
			L"PNG Images (*.png)\0*.png\0" \
			L"Targa Images (*.tga)\0*.tga\0" \
			L"\0";
		break;

	case FFSAVE_JPEG:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.jpeg", FILENAME_BUFFER_SIZE);   /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"jpg";
		mOFN.lpstrFilter =
			L"JPEG Images (*.jpg *.jpeg)\0*.jpg;*.jpeg\0" \
			L"\0";
		break;
	case FFSAVE_AVI:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.avi", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"avi";
		mOFN.lpstrFilter =
			L"AVI Movie File (*.avi)\0*.avi\0" \
			L"\0";
		break;
	case FFSAVE_ANIM:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.xaf", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"xaf";
		mOFN.lpstrFilter =
			L"XAF Anim File (*.xaf)\0*.xaf\0" \
			L"\0";
		break;
	case FFSAVE_GLTF:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.gltf", FILENAME_BUFFER_SIZE);   /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"gltf";
		mOFN.lpstrFilter =
			L"glTF Asset File (*.gltf)\0*.gltf\0" \
			L"\0";
		break;
	case FFSAVE_XML:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.xml", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}

		mOFN.lpstrDefExt = L"xml";
		mOFN.lpstrFilter =
			L"XML File (*.xml)\0*.xml\0" \
			L"\0";
		break;
	case FFSAVE_COLLADA:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.collada", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"collada";
		mOFN.lpstrFilter =
			L"COLLADA File (*.collada)\0*.collada\0" \
			L"\0";
		break;
	case FFSAVE_RAW:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.raw", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
		}
		mOFN.lpstrDefExt = L"raw";
		mOFN.lpstrFilter = RAW_FILTER \
			L"\0";
		break;
	case FFSAVE_J2C:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.j2c", FILENAME_BUFFER_SIZE);
		}
		mOFN.lpstrDefExt = L"j2c";
		mOFN.lpstrFilter =
			L"Compressed Images (*.j2c)\0*.j2c\0" \
			L"\0";
		break;
	case FFSAVE_SCRIPT:
		if (filename.empty())
		{
			wcsncpy(mFilesW, L"untitled.lsl", FILENAME_BUFFER_SIZE);
		}
		mOFN.lpstrDefExt = L"txt";
		mOFN.lpstrFilter = L"LSL Files (*.lsl)\0*.lsl\0" L"\0";
		break;
	default:
		return false;
	}

	mOFN.nMaxFile = SINGLE_FILENAME_BUFFER_SIZE;
	mOFN.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;

	reset();

	if (blocking)
	{
		// Modal, so pause agent
		send_agent_pause();
	}

	{
		// NOTA BENE: hitting the file dialog triggers a window focus event, destroying the selection manager!!
		try
		{
			success = GetSaveFileName(&mOFN);
			if (success)
			{
                std::string filename = ll_convert<std::string>(std::wstring(mFilesW));
				mFiles.push_back(filename);
			}
		}
		catch (...)
		{
			LOG_UNHANDLED_EXCEPTION("");
		}
		gKeyboard->resetKeys();
	}

	if (blocking)
	{
		send_agent_resume();
	}

	// Account for the fact that the app has been stalled.
	LLFrameTimer::updateFrameTime();
	return success;
}

bool LLFilePicker::getSaveFileModeless(ESaveFilter filter,
	const std::string& filename,
	void (*callback)(bool, std::string&, void*),
	void* userdata)
{
	// not supposed to be used yet, use LLFilePickerThread
	LL_ERRS() << "NOT IMPLEMENTED" << LL_ENDL;
	return false;
}