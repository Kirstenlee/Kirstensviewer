/**
 * @file llgl.h
 * @brief LLGL definition
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

#ifndef LL_LLGL_H
#define LL_LLGL_H

// This file contains various stuff for handling gl extensions and other gl related stuff.

#include <functional>
#include <string>
#include <unordered_map>
#include <list>

#include "llerror.h"
#include "v4color.h"
#include "llstring.h"
#include "stdtypes.h"
#include "v4math.h"
#include "llplane.h"
#include "llgltypes.h"
#include "llinstancetracker.h"

#include "llglheaders.h"
#include "glm/mat4x4.hpp"

// S24: DXDeviceInfo (GPU/driver capability info, renamed from LLGLManager)
// now lives in
// core/DXDeviceInfo.h, next to DXDevice - re-included here so every existing
// bare #include "llgl.h" site keeps working unchanged. DXState and its RAII
// wrapper family (LLGLEnable/LLGLDisable/LLGLCullFace/LLGLUserClipPlane/
// LLGLSquashToFarClip/LLGLUpdate/etc) now live in core/DXState.h, next to
// DXStateCache, same reasoning. See project_dxrender_consolidation design
// notes.
#include "DXDeviceInfo.h"
#include "DXState.h"

extern bool gDebugGL;
extern bool gDebugSession;
extern llofstream gFailLog;

#define LL_GL_ERRS LL_ERRS("RenderState")

void ll_init_fail_log(std::string filename);

void ll_fail(std::string msg);

void ll_close_fail_log();

class LLQuaternion;
class LLMatrix4;

void rotate_quat(LLQuaternion& rotation);

void flush_glerror(); // Flush GL errors when we know we're handling them correctly.

void clear_glerror();


// stop_glerror is still needed on OS X but has performance implications
// use macro below to conditionally add stop_glerror to non-release builds
// on OS X
#if LL_DARWIN && !LL_RELEASE_FOR_DOWNLOAD
#define STOP_GLERROR stop_glerror()
#else
#define STOP_GLERROR
#endif

#include "llglstates.h"

void init_glstates();

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string );

extern bool gClothRipple;
extern bool gHeadlessClient;
extern bool gNonInteractive;
// S24: renamed from gGLActive; true while llappviewer.cpp actively touches
// the render context. Currently write-only, kept as an invariant marker for
// future DX_RENDER assertions.
extern bool gDXActive;

// S24: the GL_FRAMEBUFFER/GL_RENDERBUFFER/GL_COLOR_ATTACHMENT*/GL_DEPTH24_STENCIL8/
// glGenFramebuffers-etc "_EXT fallback for old SDKs" compat-macro block that used to
// live here (a MAC OSX 10.5->10.6 SDK-transition shim) was removed - confirmed zero
// real usage of any of these tokens anywhere in the tree (grep, not assumed), and
// even if something had used them, the _EXT target symbols these macros resolved to
// are only ever declared inside llglheaders.h's never-compiled LL_DARWIN branch on
// this Windows-only hard fork - the fallback could never have resolved to anything
// real here either way.

#endif // LL_LLGL_H
