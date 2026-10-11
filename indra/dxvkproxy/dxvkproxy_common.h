/**
 * @file dxvkproxy_common.h
 * @brief S24 DXVK proxy - shared real-module resolver, included separately
 * into each tiny proxy DLL (d3d11/dxgi); not a library, each proxy gets its
 * own compiled copy.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#pragma once

#include <windows.h>
#include <string>

namespace S24DXVKProxy
{
    // Directory this module itself was loaded from, trailing backslash included.
    inline std::wstring selfDirOf(HINSTANCE selfModule)
    {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(selfModule, buf, MAX_PATH);
        std::wstring path(buf, n);
        size_t slash = path.find_last_of(L'\\');
        return (slash == std::wstring::npos) ? L"" : path.substr(0, slash + 1);
    }

    // Resolves the real module every export forwards to: the bundled DXVK
    // DLL (same file name, in a "dxvk" subfolder beside this module) if the
    // marker file "dxvk\dxvk_enabled" exists beside this module, else the
    // genuine Windows copy. Always loads by an explicit full path - never a
    // bare LoadLibrary(name) - because this proxy is itself already loaded
    // under that same file name, so a bare-name lookup from in here would
    // find itself via the application-directory search order instead of the
    // real target.
    inline HMODULE resolveReal(HINSTANCE selfModule, const wchar_t* dllFileName)
    {
        std::wstring dir = selfDirOf(selfModule);
        std::wstring markerPath = dir + L"dxvk\\dxvk_enabled";

        if (GetFileAttributesW(markerPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring dxvkPath = dir + L"dxvk\\" + dllFileName;
            HMODULE h = LoadLibraryExW(dxvkPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h)
            {
                return h;
            }
            // Bundled copy missing/failed to load - fall through to the
            // genuine Windows one below rather than leaving the viewer with
            // no D3D11/DXGI at all.
        }

        wchar_t sysDir[MAX_PATH];
        UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        std::wstring sysPath = std::wstring(sysDir, n) + L"\\" + dllFileName;
        return LoadLibraryExW(sysPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
}
