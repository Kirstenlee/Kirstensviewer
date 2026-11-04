/**
 * @file llgl.cpp
 * @brief LLGL implementation
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

// This file sets some global GL parameters, and implements some
// useful functions for GL operations.
//
// S24: DXState/LLGLEnable/LLGLDisable/LLGLCullFace/LLGLUserClipPlane/
// LLGLSquashToFarClip/LLGLUpdate/DXDepthTest's implementations moved to
// core/DXState.cpp; LLGLManager's implementation moved (and renamed to
// DXDeviceInfo, see core/DXDeviceInfo.h) to
// core/DXDeviceInfo.cpp (see project_dxrender_consolidation design notes).
// What's left here: the SkyBox RAII wrapper family (LLGLSPipelineSkyBox/
// DepthTestSkyBox/BlendSkyBox, declared in llglstates.h) and misc
// fail-log/version-string globals that don't belong to either split-out file.

#include "linden_common.h"

#include "boost/tokenizer.hpp"

#include "llsys.h"

#include "llgl.h"
#include "llglstates.h"
#include "llrender.h"

#include "llerror.h"
#include "llerrorcontrol.h"
#include "llquaternion.h"
#include "llmath.h"
#include "m4math.h"
#include "llstring.h"
#include "llstacktrace.h"

#include "llglheaders.h"
#include "llhlslshader.h"

#include "glm/glm.hpp"
#include <glm/gtc/matrix_access.hpp>
#include "glm/gtc/type_ptr.hpp"

#include "lldxhardware.h"

bool gDebugSession = false;
bool gClothRipple = false;
bool gHeadlessClient = false;
bool gNonInteractive = false;
bool gDXActive = false; // S24: renamed from gGLActive - see llgl.h

llofstream gFailLog;

// S24: gl_debug_callback() (glDebugMessageCallback-based GL debug logger)
// removed - GL-only; DX_RENDER's debug layer is DXDevice's sDebugLayerEnabled.

void parse_glsl_version(S32& major, S32& minor);

void ll_init_fail_log(std::string filename)
{
    gFailLog.open(filename.c_str());
}


void ll_fail(std::string msg)
{

    if (gDebugSession)
    {
        std::vector<std::string> lines;

        gFailLog << LLError::utcTime() << " " << msg << std::endl;

        gFailLog << "Stack Trace:" << std::endl;

        ll_get_stack_trace(lines);

        for(size_t i = 0; i < lines.size(); ++i)
        {
            gFailLog << lines[i] << std::endl;
        }

        gFailLog << "End of Stack Trace." << std::endl << std::endl;

        gFailLog.flush();
    }
};

void ll_close_fail_log()
{
    gFailLog.close();
}

#define LL_GL_NAME_POOLING 0

// S24: the glext/glh_extensions PFN*PROC pointer definitions that lived here were
// removed along with DXDeviceInfo::initWGL() (their only assigner, dead under DX_RENDER)
// - see llglheaders.h.

void rotate_quat(LLQuaternion& rotation)
{
    F32 angle_radians, x, y, z;
    rotation.getAngleAxis(&angle_radians, &x, &y, &z);
    gDX.rotatef(angle_radians * RAD_TO_DEG, x, y, z);
}

void flush_glerror()
{
    // S24: no-op under DX_RENDER - glGetError() with no current GL context is undefined
    // behavior, and DX_RENDER never creates one.
}

void clear_glerror()
{
    // S24: no-op under DX_RENDER, same reasoning as flush_glerror() above - glGetError() is
    // unresolved once OpenGL is delinked, so a debug+DX_RENDER build would otherwise fail to
    // link.
}

////////////////////////////////////////////////////////////////////////////////

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string )
{
    // GL_VERSION returns a null-terminated string with the format:
    // <major>.<minor>[.<release>] [<vendor specific>]

    const char* version = (const char*) glGetString(GL_VERSION);
    *major = 0;
    *minor = 0;
    *release = 0;
    vendor_specific->assign("");

    if( !version )
    {
        return;
    }

    version_string->assign(version);

    std::string ver_copy( version );
    S32 len = (S32)strlen( version );   /* Flawfinder: ignore */
    S32 i = 0;
    S32 start;
    // Find the major version
    start = i;
    for( ; i < len; i++ )
    {
        if( '.' == version[i] )
        {
            break;
        }
    }
    std::string major_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(major_str, *major);

    if( '.' == version[i] )
    {
        i++;
    }

    // Find the minor version
    start = i;
    for( ; i < len; i++ )
    {
        if( ('.' == version[i]) || isspace(version[i]) )
        {
            break;
        }
    }
    std::string minor_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(minor_str, *minor);

    // Find the release number (optional)
    if( '.' == version[i] )
    {
        i++;

        start = i;
        for( ; i < len; i++ )
        {
            if( isspace(version[i]) )
            {
                break;
            }
        }

        std::string release_str = ver_copy.substr(start,i-start);
        LLStringUtil::convertToS32(release_str, *release);
    }

    // Skip over any white space
    while( version[i] && isspace( version[i] ) )
    {
        i++;
    }

    // Copy the vendor-specific string (optional)
    if( version[i] )
    {
        vendor_specific->assign( version + i );
    }
}


void parse_glsl_version(S32& major, S32& minor)
{
    // GL_SHADING_LANGUAGE_VERSION returns a null-terminated string with the format:
    // <major>.<minor>[.<release>] [<vendor specific>]

    const char* version = (const char*) glGetString(GL_SHADING_LANGUAGE_VERSION);
    major = 0;
    minor = 0;

    if( !version )
    {
        return;
    }

    std::string ver_copy( version );
    S32 len = (S32)strlen( version );   /* Flawfinder: ignore */
    S32 i = 0;
    S32 start;
    // Find the major version
    start = i;
    for( ; i < len; i++ )
    {
        if( '.' == version[i] )
        {
            break;
        }
    }
    std::string major_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(major_str, major);

    if( '.' == version[i] )
    {
        i++;
    }

    // Find the minor version
    start = i;
    for( ; i < len; i++ )
    {
        if( ('.' == version[i]) || isspace(version[i]) )
        {
            break;
        }
    }
    std::string minor_str = ver_copy.substr(start,i-start);
    LLStringUtil::convertToS32(minor_str, minor);
}

LLGLSPipelineSkyBox::LLGLSPipelineSkyBox()
: mCullFace(DX_CULL_FACE)
, mSquashClip()
{
}

LLGLSPipelineSkyBox::~LLGLSPipelineSkyBox()
{
}

LLGLSPipelineDepthTestSkyBox::LLGLSPipelineDepthTestSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineSkyBox()
, mDepth(depth_test, depth_write, DX_LEQUAL)
{

}

LLGLSPipelineBlendSkyBox::LLGLSPipelineBlendSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineDepthTestSkyBox(depth_test, depth_write)
, mBlend(DX_BLEND)
{
    gDX.setSceneBlendType(LLRender::BT_ALPHA);
}

// Expose desired use of high-performance graphics processor to Optimus driver and to AMD driver
// https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
