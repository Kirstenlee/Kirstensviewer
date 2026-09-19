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

#define GLH_EXT_SINGLE_FILE

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

#include "DXStateCache.h"
#include "DXDevice.h"
#include "DXUIBatch.h"
#include <dxgi.h>



bool gDebugSession = false;
bool gClothRipple = false;
bool gHeadlessClient = false;
bool gNonInteractive = false;
bool gDXActive = false; // S24: renamed from gGLActive - see llgl.h

static const std::string HEADLESS_VENDOR_STRING("Kirstens Viewer");
static const std::string HEADLESS_RENDERER_STRING("Headless");
static const std::string HEADLESS_VERSION_STRING("1.0");

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

LLMatrix4 gGLObliqueProjectionInverse;

#define LL_GL_NAME_POOLING 0

std::list<LLGLUpdate*> LLGLUpdate::sGLQ;

#if (LL_WINDOWS)  && !LL_MESA_HEADLESS

#if LL_WINDOWS
// WGL_ARB_create_context
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;

// WGL_AMD_gpu_association
PFNWGLGETGPUIDSAMDPROC                          wglGetGPUIDsAMD = nullptr;
PFNWGLGETGPUINFOAMDPROC                         wglGetGPUInfoAMD = nullptr;
PFNWGLGETCONTEXTGPUIDAMDPROC                    wglGetContextGPUIDAMD = nullptr;
PFNWGLCREATEASSOCIATEDCONTEXTAMDPROC            wglCreateAssociatedContextAMD = nullptr;
PFNWGLCREATEASSOCIATEDCONTEXTATTRIBSAMDPROC     wglCreateAssociatedContextAttribsAMD = nullptr;
PFNWGLDELETEASSOCIATEDCONTEXTAMDPROC            wglDeleteAssociatedContextAMD = nullptr;
PFNWGLMAKEASSOCIATEDCONTEXTCURRENTAMDPROC       wglMakeAssociatedContextCurrentAMD = nullptr;
PFNWGLGETCURRENTASSOCIATEDCONTEXTAMDPROC        wglGetCurrentAssociatedContextAMD = nullptr;
PFNWGLBLITCONTEXTFRAMEBUFFERAMDPROC             wglBlitContextFramebufferAMD = nullptr;

// WGL_EXT_swap_control
PFNWGLSWAPINTERVALEXTPROC    wglSwapIntervalEXT = nullptr;
PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT = nullptr;

#endif

// GL_VERSION_1_2
//PFNGLDRAWRANGEELEMENTSPROC  glDrawRangeElements = nullptr;
//PFNGLTEXIMAGE3DPROC         glTexImage3D = nullptr;
//PFNGLTEXSUBIMAGE3DPROC      glTexSubImage3D = nullptr;
//PFNGLCOPYTEXSUBIMAGE3DPROC  glCopyTexSubImage3D = nullptr;

// GL_VERSION_1_3
PFNGLACTIVETEXTUREPROC               glActiveTexture = nullptr;
PFNGLSAMPLECOVERAGEPROC              glSampleCoverage = nullptr;
PFNGLCOMPRESSEDTEXIMAGE3DPROC        glCompressedTexImage3D = nullptr;
PFNGLCOMPRESSEDTEXIMAGE2DPROC        glCompressedTexImage2D = nullptr;
PFNGLCOMPRESSEDTEXIMAGE1DPROC        glCompressedTexImage1D = nullptr;
PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC     glCompressedTexSubImage3D = nullptr;
PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC     glCompressedTexSubImage2D = nullptr;
PFNGLCOMPRESSEDTEXSUBIMAGE1DPROC     glCompressedTexSubImage1D = nullptr;
PFNGLGETCOMPRESSEDTEXIMAGEPROC       glGetCompressedTexImage = nullptr;
PFNGLCLIENTACTIVETEXTUREPROC         glClientActiveTexture = nullptr;
PFNGLMULTITEXCOORD1DPROC             glMultiTexCoord1d = nullptr;
PFNGLMULTITEXCOORD1DVPROC            glMultiTexCoord1dv = nullptr;
PFNGLMULTITEXCOORD1FPROC             glMultiTexCoord1f = nullptr;
PFNGLMULTITEXCOORD1FVPROC            glMultiTexCoord1fv = nullptr;
PFNGLMULTITEXCOORD1IPROC             glMultiTexCoord1i = nullptr;
PFNGLMULTITEXCOORD1IVPROC            glMultiTexCoord1iv = nullptr;
PFNGLMULTITEXCOORD1SPROC             glMultiTexCoord1s = nullptr;
PFNGLMULTITEXCOORD1SVPROC            glMultiTexCoord1sv = nullptr;
PFNGLMULTITEXCOORD2DPROC             glMultiTexCoord2d = nullptr;
PFNGLMULTITEXCOORD2DVPROC            glMultiTexCoord2dv = nullptr;
PFNGLMULTITEXCOORD2FPROC             glMultiTexCoord2f = nullptr;
PFNGLMULTITEXCOORD2FVPROC            glMultiTexCoord2fv = nullptr;
PFNGLMULTITEXCOORD2IPROC             glMultiTexCoord2i = nullptr;
PFNGLMULTITEXCOORD2IVPROC            glMultiTexCoord2iv = nullptr;
PFNGLMULTITEXCOORD2SPROC             glMultiTexCoord2s = nullptr;
PFNGLMULTITEXCOORD2SVPROC            glMultiTexCoord2sv = nullptr;
PFNGLMULTITEXCOORD3DPROC             glMultiTexCoord3d = nullptr;
PFNGLMULTITEXCOORD3DVPROC            glMultiTexCoord3dv = nullptr;
PFNGLMULTITEXCOORD3FPROC             glMultiTexCoord3f = nullptr;
PFNGLMULTITEXCOORD3FVPROC            glMultiTexCoord3fv = nullptr;
PFNGLMULTITEXCOORD3IPROC             glMultiTexCoord3i = nullptr;
PFNGLMULTITEXCOORD3IVPROC            glMultiTexCoord3iv = nullptr;
PFNGLMULTITEXCOORD3SPROC             glMultiTexCoord3s = nullptr;
PFNGLMULTITEXCOORD3SVPROC            glMultiTexCoord3sv = nullptr;
PFNGLMULTITEXCOORD4DPROC             glMultiTexCoord4d = nullptr;
PFNGLMULTITEXCOORD4DVPROC            glMultiTexCoord4dv = nullptr;
PFNGLMULTITEXCOORD4FPROC             glMultiTexCoord4f = nullptr;
PFNGLMULTITEXCOORD4FVPROC            glMultiTexCoord4fv = nullptr;
PFNGLMULTITEXCOORD4IPROC             glMultiTexCoord4i = nullptr;
PFNGLMULTITEXCOORD4IVPROC            glMultiTexCoord4iv = nullptr;
PFNGLMULTITEXCOORD4SPROC             glMultiTexCoord4s = nullptr;
PFNGLMULTITEXCOORD4SVPROC            glMultiTexCoord4sv = nullptr;
PFNGLLOADTRANSPOSEMATRIXFPROC        glLoadTransposeMatrixf = nullptr;
PFNGLLOADTRANSPOSEMATRIXDPROC        glLoadTransposeMatrixd = nullptr;
PFNGLMULTTRANSPOSEMATRIXFPROC        glMultTransposeMatrixf = nullptr;
PFNGLMULTTRANSPOSEMATRIXDPROC        glMultTransposeMatrixd = nullptr;

// GL_VERSION_1_4
PFNGLBLENDFUNCSEPARATEPROC       glBlendFuncSeparate = nullptr;
PFNGLMULTIDRAWARRAYSPROC         glMultiDrawArrays = nullptr;
PFNGLMULTIDRAWELEMENTSPROC       glMultiDrawElements = nullptr;
PFNGLPOINTPARAMETERFPROC         glPointParameterf = nullptr;
PFNGLPOINTPARAMETERFVPROC        glPointParameterfv = nullptr;
PFNGLPOINTPARAMETERIPROC         glPointParameteri = nullptr;
PFNGLPOINTPARAMETERIVPROC        glPointParameteriv = nullptr;
PFNGLFOGCOORDFPROC               glFogCoordf = nullptr;
PFNGLFOGCOORDFVPROC              glFogCoordfv = nullptr;
PFNGLFOGCOORDDPROC               glFogCoordd = nullptr;
PFNGLFOGCOORDDVPROC              glFogCoorddv = nullptr;
PFNGLFOGCOORDPOINTERPROC         glFogCoordPointer = nullptr;
PFNGLSECONDARYCOLOR3BPROC        glSecondaryColor3b = nullptr;
PFNGLSECONDARYCOLOR3BVPROC       glSecondaryColor3bv = nullptr;
PFNGLSECONDARYCOLOR3DPROC        glSecondaryColor3d = nullptr;
PFNGLSECONDARYCOLOR3DVPROC       glSecondaryColor3dv = nullptr;
PFNGLSECONDARYCOLOR3FPROC        glSecondaryColor3f = nullptr;
PFNGLSECONDARYCOLOR3FVPROC       glSecondaryColor3fv = nullptr;
PFNGLSECONDARYCOLOR3IPROC        glSecondaryColor3i = nullptr;
PFNGLSECONDARYCOLOR3IVPROC       glSecondaryColor3iv = nullptr;
PFNGLSECONDARYCOLOR3SPROC        glSecondaryColor3s = nullptr;
PFNGLSECONDARYCOLOR3SVPROC       glSecondaryColor3sv = nullptr;
PFNGLSECONDARYCOLOR3UBPROC       glSecondaryColor3ub = nullptr;
PFNGLSECONDARYCOLOR3UBVPROC      glSecondaryColor3ubv = nullptr;
PFNGLSECONDARYCOLOR3UIPROC       glSecondaryColor3ui = nullptr;
PFNGLSECONDARYCOLOR3UIVPROC      glSecondaryColor3uiv = nullptr;
PFNGLSECONDARYCOLOR3USPROC       glSecondaryColor3us = nullptr;
PFNGLSECONDARYCOLOR3USVPROC      glSecondaryColor3usv = nullptr;
PFNGLSECONDARYCOLORPOINTERPROC   glSecondaryColorPointer = nullptr;
PFNGLWINDOWPOS2DPROC             glWindowPos2d = nullptr;
PFNGLWINDOWPOS2DVPROC            glWindowPos2dv = nullptr;
PFNGLWINDOWPOS2FPROC             glWindowPos2f = nullptr;
PFNGLWINDOWPOS2FVPROC            glWindowPos2fv = nullptr;
PFNGLWINDOWPOS2IPROC             glWindowPos2i = nullptr;
PFNGLWINDOWPOS2IVPROC            glWindowPos2iv = nullptr;
PFNGLWINDOWPOS2SPROC             glWindowPos2s = nullptr;
PFNGLWINDOWPOS2SVPROC            glWindowPos2sv = nullptr;
PFNGLWINDOWPOS3DPROC             glWindowPos3d = nullptr;
PFNGLWINDOWPOS3DVPROC            glWindowPos3dv = nullptr;
PFNGLWINDOWPOS3FPROC             glWindowPos3f = nullptr;
PFNGLWINDOWPOS3FVPROC            glWindowPos3fv = nullptr;
PFNGLWINDOWPOS3IPROC             glWindowPos3i = nullptr;
PFNGLWINDOWPOS3IVPROC            glWindowPos3iv = nullptr;
PFNGLWINDOWPOS3SPROC             glWindowPos3s = nullptr;
PFNGLWINDOWPOS3SVPROC            glWindowPos3sv = nullptr;

// GL_VERSION_1_5
PFNGLGENQUERIESPROC              glGenQueries = nullptr;
PFNGLDELETEQUERIESPROC           glDeleteQueries = nullptr;
PFNGLISQUERYPROC                 glIsQuery = nullptr;
PFNGLBEGINQUERYPROC              glBeginQuery = nullptr;
PFNGLENDQUERYPROC                glEndQuery = nullptr;
PFNGLGETQUERYIVPROC              glGetQueryiv = nullptr;
PFNGLGETQUERYOBJECTIVPROC        glGetQueryObjectiv = nullptr;
PFNGLGETQUERYOBJECTUIVPROC       glGetQueryObjectuiv = nullptr;
PFNGLBINDBUFFERPROC              glBindBuffer = nullptr;
PFNGLDELETEBUFFERSPROC           glDeleteBuffers = nullptr;
PFNGLGENBUFFERSPROC              glGenBuffers = nullptr;
PFNGLISBUFFERPROC                glIsBuffer = nullptr;
PFNGLBUFFERDATAPROC              glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC           glBufferSubData = nullptr;
PFNGLGETBUFFERSUBDATAPROC        glGetBufferSubData = nullptr;
PFNGLMAPBUFFERPROC               glMapBuffer = nullptr;
PFNGLUNMAPBUFFERPROC             glUnmapBuffer = nullptr;
PFNGLGETBUFFERPARAMETERIVPROC    glGetBufferParameteriv = nullptr;
PFNGLGETBUFFERPOINTERVPROC       glGetBufferPointerv = nullptr;

// GL_VERSION_2_0
PFNGLBLENDEQUATIONSEPARATEPROC           glBlendEquationSeparate = nullptr;
PFNGLDRAWBUFFERSPROC                     glDrawBuffers = nullptr;
PFNGLSTENCILOPSEPARATEPROC               glStencilOpSeparate = nullptr;
PFNGLSTENCILFUNCSEPARATEPROC             glStencilFuncSeparate = nullptr;
PFNGLSTENCILMASKSEPARATEPROC             glStencilMaskSeparate = nullptr;
PFNGLATTACHSHADERPROC                    glAttachShader = nullptr;
PFNGLBINDATTRIBLOCATIONPROC              glBindAttribLocation = nullptr;
PFNGLCOMPILESHADERPROC                   glCompileShader = nullptr;
PFNGLCREATEPROGRAMPROC                   glCreateProgram = nullptr;
PFNGLCREATESHADERPROC                    glCreateShader = nullptr;
PFNGLDELETEPROGRAMPROC                   glDeleteProgram = nullptr;
PFNGLDELETESHADERPROC                    glDeleteShader = nullptr;
PFNGLDETACHSHADERPROC                    glDetachShader = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC        glDisableVertexAttribArray = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC         glEnableVertexAttribArray = nullptr;
PFNGLGETACTIVEATTRIBPROC                 glGetActiveAttrib = nullptr;
PFNGLGETACTIVEUNIFORMPROC                glGetActiveUniform = nullptr;
PFNGLGETATTACHEDSHADERSPROC              glGetAttachedShaders = nullptr;
PFNGLGETATTRIBLOCATIONPROC               glGetAttribLocation = nullptr;
PFNGLGETPROGRAMIVPROC                    glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC               glGetProgramInfoLog = nullptr;
PFNGLGETSHADERIVPROC                     glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC                glGetShaderInfoLog = nullptr;
PFNGLGETSHADERSOURCEPROC                 glGetShaderSource = nullptr;
PFNGLGETUNIFORMLOCATIONPROC              glGetUniformLocation = nullptr;
PFNGLGETUNIFORMFVPROC                    glGetUniformfv = nullptr;
PFNGLGETUNIFORMIVPROC                    glGetUniformiv = nullptr;
PFNGLGETVERTEXATTRIBDVPROC               glGetVertexAttribdv = nullptr;
PFNGLGETVERTEXATTRIBFVPROC               glGetVertexAttribfv = nullptr;
PFNGLGETVERTEXATTRIBIVPROC               glGetVertexAttribiv = nullptr;
PFNGLGETVERTEXATTRIBPOINTERVPROC         glGetVertexAttribPointerv = nullptr;
PFNGLISPROGRAMPROC                       glIsProgram = nullptr;
PFNGLISSHADERPROC                        glIsShader = nullptr;
PFNGLLINKPROGRAMPROC                     glLinkProgram = nullptr;
PFNGLSHADERSOURCEPROC                    glShaderSource = nullptr;
PFNGLUSEPROGRAMPROC                      glUseProgram = nullptr;
PFNGLUNIFORM1FPROC                       glUniform1f = nullptr;
PFNGLUNIFORM2FPROC                       glUniform2f = nullptr;
PFNGLUNIFORM3FPROC                       glUniform3f = nullptr;
PFNGLUNIFORM4FPROC                       glUniform4f = nullptr;
PFNGLUNIFORM1IPROC                       glUniform1i = nullptr;
PFNGLUNIFORM2IPROC                       glUniform2i = nullptr;
PFNGLUNIFORM3IPROC                       glUniform3i = nullptr;
PFNGLUNIFORM4IPROC                       glUniform4i = nullptr;
PFNGLUNIFORM1FVPROC                      glUniform1fv = nullptr;
PFNGLUNIFORM2FVPROC                      glUniform2fv = nullptr;
PFNGLUNIFORM3FVPROC                      glUniform3fv = nullptr;
PFNGLUNIFORM4FVPROC                      glUniform4fv = nullptr;
PFNGLUNIFORM1IVPROC                      glUniform1iv = nullptr;
PFNGLUNIFORM2IVPROC                      glUniform2iv = nullptr;
PFNGLUNIFORM3IVPROC                      glUniform3iv = nullptr;
PFNGLUNIFORM4IVPROC                      glUniform4iv = nullptr;
PFNGLUNIFORMMATRIX2FVPROC                glUniformMatrix2fv = nullptr;
PFNGLUNIFORMMATRIX3FVPROC                glUniformMatrix3fv = nullptr;
PFNGLUNIFORMMATRIX4FVPROC                glUniformMatrix4fv = nullptr;
PFNGLVALIDATEPROGRAMPROC                 glValidateProgram = nullptr;
PFNGLVERTEXATTRIB1DPROC                  glVertexAttrib1d = nullptr;
PFNGLVERTEXATTRIB1DVPROC                 glVertexAttrib1dv = nullptr;
PFNGLVERTEXATTRIB1FPROC                  glVertexAttrib1f = nullptr;
PFNGLVERTEXATTRIB1FVPROC                 glVertexAttrib1fv = nullptr;
PFNGLVERTEXATTRIB1SPROC                  glVertexAttrib1s = nullptr;
PFNGLVERTEXATTRIB1SVPROC                 glVertexAttrib1sv = nullptr;
PFNGLVERTEXATTRIB2DPROC                  glVertexAttrib2d = nullptr;
PFNGLVERTEXATTRIB2DVPROC                 glVertexAttrib2dv = nullptr;
PFNGLVERTEXATTRIB2FPROC                  glVertexAttrib2f = nullptr;
PFNGLVERTEXATTRIB2FVPROC                 glVertexAttrib2fv = nullptr;
PFNGLVERTEXATTRIB2SPROC                  glVertexAttrib2s = nullptr;
PFNGLVERTEXATTRIB2SVPROC                 glVertexAttrib2sv = nullptr;
PFNGLVERTEXATTRIB3DPROC                  glVertexAttrib3d = nullptr;
PFNGLVERTEXATTRIB3DVPROC                 glVertexAttrib3dv = nullptr;
PFNGLVERTEXATTRIB3FPROC                  glVertexAttrib3f = nullptr;
PFNGLVERTEXATTRIB3FVPROC                 glVertexAttrib3fv = nullptr;
PFNGLVERTEXATTRIB3SPROC                  glVertexAttrib3s = nullptr;
PFNGLVERTEXATTRIB3SVPROC                 glVertexAttrib3sv = nullptr;
PFNGLVERTEXATTRIB4NBVPROC                glVertexAttrib4Nbv = nullptr;
PFNGLVERTEXATTRIB4NIVPROC                glVertexAttrib4Niv = nullptr;
PFNGLVERTEXATTRIB4NSVPROC                glVertexAttrib4Nsv = nullptr;
PFNGLVERTEXATTRIB4NUBPROC                glVertexAttrib4Nub = nullptr;
PFNGLVERTEXATTRIB4NUBVPROC               glVertexAttrib4Nubv = nullptr;
PFNGLVERTEXATTRIB4NUIVPROC               glVertexAttrib4Nuiv = nullptr;
PFNGLVERTEXATTRIB4NUSVPROC               glVertexAttrib4Nusv = nullptr;
PFNGLVERTEXATTRIB4BVPROC                 glVertexAttrib4bv = nullptr;
PFNGLVERTEXATTRIB4DPROC                  glVertexAttrib4d = nullptr;
PFNGLVERTEXATTRIB4DVPROC                 glVertexAttrib4dv = nullptr;
PFNGLVERTEXATTRIB4FPROC                  glVertexAttrib4f = nullptr;
PFNGLVERTEXATTRIB4FVPROC                 glVertexAttrib4fv = nullptr;
PFNGLVERTEXATTRIB4IVPROC                 glVertexAttrib4iv = nullptr;
PFNGLVERTEXATTRIB4SPROC                  glVertexAttrib4s = nullptr;
PFNGLVERTEXATTRIB4SVPROC                 glVertexAttrib4sv = nullptr;
PFNGLVERTEXATTRIB4UBVPROC                glVertexAttrib4ubv = nullptr;
PFNGLVERTEXATTRIB4UIVPROC                glVertexAttrib4uiv = nullptr;
PFNGLVERTEXATTRIB4USVPROC                glVertexAttrib4usv = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC             glVertexAttribPointer = nullptr;

// GL_VERSION_2_1
PFNGLUNIFORMMATRIX2X3FVPROC glUniformMatrix2x3fv = nullptr;
PFNGLUNIFORMMATRIX3X2FVPROC glUniformMatrix3x2fv = nullptr;
PFNGLUNIFORMMATRIX2X4FVPROC glUniformMatrix2x4fv = nullptr;
PFNGLUNIFORMMATRIX4X2FVPROC glUniformMatrix4x2fv = nullptr;
PFNGLUNIFORMMATRIX3X4FVPROC glUniformMatrix3x4fv = nullptr;
PFNGLUNIFORMMATRIX4X3FVPROC glUniformMatrix4x3fv = nullptr;

// GL_VERSION_3_0
PFNGLCOLORMASKIPROC                              glColorMaski = nullptr;
PFNGLGETBOOLEANI_VPROC                           glGetBooleani_v = nullptr;
PFNGLGETINTEGERI_VPROC                           glGetIntegeri_v = nullptr;
PFNGLENABLEIPROC                                 glEnablei = nullptr;
PFNGLDISABLEIPROC                                glDisablei = nullptr;
PFNGLISENABLEDIPROC                              glIsEnabledi = nullptr;
PFNGLBEGINTRANSFORMFEEDBACKPROC                  glBeginTransformFeedback = nullptr;
PFNGLENDTRANSFORMFEEDBACKPROC                    glEndTransformFeedback = nullptr;
PFNGLBINDBUFFERRANGEPROC                         glBindBufferRange = nullptr;
PFNGLBINDBUFFERBASEPROC                          glBindBufferBase = nullptr;
PFNGLTRANSFORMFEEDBACKVARYINGSPROC               glTransformFeedbackVaryings = nullptr;
PFNGLGETTRANSFORMFEEDBACKVARYINGPROC             glGetTransformFeedbackVarying = nullptr;
PFNGLCLAMPCOLORPROC                              glClampColor = nullptr;
PFNGLBEGINCONDITIONALRENDERPROC                  glBeginConditionalRender = nullptr;
PFNGLENDCONDITIONALRENDERPROC                    glEndConditionalRender = nullptr;
PFNGLVERTEXATTRIBIPOINTERPROC                    glVertexAttribIPointer = nullptr;
PFNGLGETVERTEXATTRIBIIVPROC                      glGetVertexAttribIiv = nullptr;
PFNGLGETVERTEXATTRIBIUIVPROC                     glGetVertexAttribIuiv = nullptr;
PFNGLVERTEXATTRIBI1IPROC                         glVertexAttribI1i = nullptr;
PFNGLVERTEXATTRIBI2IPROC                         glVertexAttribI2i = nullptr;
PFNGLVERTEXATTRIBI3IPROC                         glVertexAttribI3i = nullptr;
PFNGLVERTEXATTRIBI4IPROC                         glVertexAttribI4i = nullptr;
PFNGLVERTEXATTRIBI1UIPROC                        glVertexAttribI1ui = nullptr;
PFNGLVERTEXATTRIBI2UIPROC                        glVertexAttribI2ui = nullptr;
PFNGLVERTEXATTRIBI3UIPROC                        glVertexAttribI3ui = nullptr;
PFNGLVERTEXATTRIBI4UIPROC                        glVertexAttribI4ui = nullptr;
PFNGLVERTEXATTRIBI1IVPROC                        glVertexAttribI1iv = nullptr;
PFNGLVERTEXATTRIBI2IVPROC                        glVertexAttribI2iv = nullptr;
PFNGLVERTEXATTRIBI3IVPROC                        glVertexAttribI3iv = nullptr;
PFNGLVERTEXATTRIBI4IVPROC                        glVertexAttribI4iv = nullptr;
PFNGLVERTEXATTRIBI1UIVPROC                       glVertexAttribI1uiv = nullptr;
PFNGLVERTEXATTRIBI2UIVPROC                       glVertexAttribI2uiv = nullptr;
PFNGLVERTEXATTRIBI3UIVPROC                       glVertexAttribI3uiv = nullptr;
PFNGLVERTEXATTRIBI4UIVPROC                       glVertexAttribI4uiv = nullptr;
PFNGLVERTEXATTRIBI4BVPROC                        glVertexAttribI4bv = nullptr;
PFNGLVERTEXATTRIBI4SVPROC                        glVertexAttribI4sv = nullptr;
PFNGLVERTEXATTRIBI4UBVPROC                       glVertexAttribI4ubv = nullptr;
PFNGLVERTEXATTRIBI4USVPROC                       glVertexAttribI4usv = nullptr;
PFNGLGETUNIFORMUIVPROC                           glGetUniformuiv = nullptr;
PFNGLBINDFRAGDATALOCATIONPROC                    glBindFragDataLocation = nullptr;
PFNGLGETFRAGDATALOCATIONPROC                     glGetFragDataLocation = nullptr;
PFNGLUNIFORM1UIPROC                              glUniform1ui = nullptr;
PFNGLUNIFORM2UIPROC                              glUniform2ui = nullptr;
PFNGLUNIFORM3UIPROC                              glUniform3ui = nullptr;
PFNGLUNIFORM4UIPROC                              glUniform4ui = nullptr;
PFNGLUNIFORM1UIVPROC                             glUniform1uiv = nullptr;
PFNGLUNIFORM2UIVPROC                             glUniform2uiv = nullptr;
PFNGLUNIFORM3UIVPROC                             glUniform3uiv = nullptr;
PFNGLUNIFORM4UIVPROC                             glUniform4uiv = nullptr;
PFNGLTEXPARAMETERIIVPROC                         glTexParameterIiv = nullptr;
PFNGLTEXPARAMETERIUIVPROC                        glTexParameterIuiv = nullptr;
PFNGLGETTEXPARAMETERIIVPROC                      glGetTexParameterIiv = nullptr;
PFNGLGETTEXPARAMETERIUIVPROC                     glGetTexParameterIuiv = nullptr;
PFNGLCLEARBUFFERIVPROC                           glClearBufferiv = nullptr;
PFNGLCLEARBUFFERUIVPROC                          glClearBufferuiv = nullptr;
PFNGLCLEARBUFFERFVPROC                           glClearBufferfv = nullptr;
PFNGLCLEARBUFFERFIPROC                           glClearBufferfi = nullptr;
PFNGLGETSTRINGIPROC                              glGetStringi = nullptr;
PFNGLISRENDERBUFFERPROC                          glIsRenderbuffer = nullptr;
PFNGLBINDRENDERBUFFERPROC                        glBindRenderbuffer = nullptr;
PFNGLDELETERENDERBUFFERSPROC                     glDeleteRenderbuffers = nullptr;
PFNGLGENRENDERBUFFERSPROC                        glGenRenderbuffers = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC                     glRenderbufferStorage = nullptr;
PFNGLGETRENDERBUFFERPARAMETERIVPROC              glGetRenderbufferParameteriv = nullptr;
PFNGLISFRAMEBUFFERPROC                           glIsFramebuffer = nullptr;
PFNGLBINDFRAMEBUFFERPROC                         glBindFramebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC                      glDeleteFramebuffers = nullptr;
PFNGLGENFRAMEBUFFERSPROC                         glGenFramebuffers = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC                  glCheckFramebufferStatus = nullptr;
PFNGLFRAMEBUFFERTEXTURE1DPROC                    glFramebufferTexture1D = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC                    glFramebufferTexture2D = nullptr;
PFNGLFRAMEBUFFERTEXTURE3DPROC                    glFramebufferTexture3D = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC                 glFramebufferRenderbuffer = nullptr;
PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC     glGetFramebufferAttachmentParameteriv = nullptr;
PFNGLGENERATEMIPMAPPROC                          glGenerateMipmap = nullptr;
PFNGLBLITFRAMEBUFFERPROC                         glBlitFramebuffer = nullptr;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC          glRenderbufferStorageMultisample = nullptr;
PFNGLFRAMEBUFFERTEXTURELAYERPROC                 glFramebufferTextureLayer = nullptr;
PFNGLMAPBUFFERRANGEPROC                          glMapBufferRange = nullptr;
PFNGLFLUSHMAPPEDBUFFERRANGEPROC                  glFlushMappedBufferRange = nullptr;
PFNGLBINDVERTEXARRAYPROC                         glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC                      glDeleteVertexArrays = nullptr;
PFNGLGENVERTEXARRAYSPROC                         glGenVertexArrays = nullptr;
PFNGLISVERTEXARRAYPROC                           glIsVertexArray = nullptr;

// GL_VERSION_3_1
PFNGLDRAWARRAYSINSTANCEDPROC         glDrawArraysInstanced = nullptr;
PFNGLDRAWELEMENTSINSTANCEDPROC       glDrawElementsInstanced = nullptr;
PFNGLTEXBUFFERPROC                   glTexBuffer = nullptr;
PFNGLPRIMITIVERESTARTINDEXPROC       glPrimitiveRestartIndex = nullptr;
PFNGLCOPYBUFFERSUBDATAPROC           glCopyBufferSubData = nullptr;
PFNGLGETUNIFORMINDICESPROC           glGetUniformIndices = nullptr;
PFNGLGETACTIVEUNIFORMSIVPROC         glGetActiveUniformsiv = nullptr;
PFNGLGETACTIVEUNIFORMNAMEPROC        glGetActiveUniformName = nullptr;
PFNGLGETUNIFORMBLOCKINDEXPROC        glGetUniformBlockIndex = nullptr;
PFNGLGETACTIVEUNIFORMBLOCKIVPROC     glGetActiveUniformBlockiv = nullptr;
PFNGLGETACTIVEUNIFORMBLOCKNAMEPROC   glGetActiveUniformBlockName = nullptr;
PFNGLUNIFORMBLOCKBINDINGPROC         glUniformBlockBinding = nullptr;

// GL_VERSION_3_2
PFNGLDRAWELEMENTSBASEVERTEXPROC          glDrawElementsBaseVertex = nullptr;
PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC     glDrawRangeElementsBaseVertex = nullptr;
PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC glDrawElementsInstancedBaseVertex = nullptr;
PFNGLMULTIDRAWELEMENTSBASEVERTEXPROC     glMultiDrawElementsBaseVertex = nullptr;
PFNGLPROVOKINGVERTEXPROC                 glProvokingVertex = nullptr;
PFNGLFENCESYNCPROC                       glFenceSync = nullptr;
PFNGLISSYNCPROC                          glIsSync = nullptr;
PFNGLDELETESYNCPROC                      glDeleteSync = nullptr;
PFNGLCLIENTWAITSYNCPROC                  glClientWaitSync = nullptr;
PFNGLWAITSYNCPROC                        glWaitSync = nullptr;
PFNGLGETINTEGER64VPROC                   glGetInteger64v = nullptr;
PFNGLGETSYNCIVPROC                       glGetSynciv = nullptr;
PFNGLGETINTEGER64I_VPROC                 glGetInteger64i_v = nullptr;
PFNGLGETBUFFERPARAMETERI64VPROC          glGetBufferParameteri64v = nullptr;
PFNGLFRAMEBUFFERTEXTUREPROC              glFramebufferTexture = nullptr;
PFNGLTEXIMAGE2DMULTISAMPLEPROC           glTexImage2DMultisample = nullptr;
PFNGLTEXIMAGE3DMULTISAMPLEPROC           glTexImage3DMultisample = nullptr;
PFNGLGETMULTISAMPLEFVPROC                glGetMultisamplefv = nullptr;
PFNGLSAMPLEMASKIPROC                     glSampleMaski = nullptr;

// GL_VERSION_3_3
PFNGLBINDFRAGDATALOCATIONINDEXEDPROC  glBindFragDataLocationIndexed = nullptr;
PFNGLGETFRAGDATAINDEXPROC             glGetFragDataIndex = nullptr;
PFNGLGENSAMPLERSPROC                  glGenSamplers = nullptr;
PFNGLDELETESAMPLERSPROC               glDeleteSamplers = nullptr;
PFNGLISSAMPLERPROC                    glIsSampler = nullptr;
PFNGLBINDSAMPLERPROC                  glBindSampler = nullptr;
PFNGLSAMPLERPARAMETERIPROC            glSamplerParameteri = nullptr;
PFNGLSAMPLERPARAMETERIVPROC           glSamplerParameteriv = nullptr;
PFNGLSAMPLERPARAMETERFPROC            glSamplerParameterf = nullptr;
PFNGLSAMPLERPARAMETERFVPROC           glSamplerParameterfv = nullptr;
PFNGLSAMPLERPARAMETERIIVPROC          glSamplerParameterIiv = nullptr;
PFNGLSAMPLERPARAMETERIUIVPROC         glSamplerParameterIuiv = nullptr;
PFNGLGETSAMPLERPARAMETERIVPROC        glGetSamplerParameteriv = nullptr;
PFNGLGETSAMPLERPARAMETERIIVPROC       glGetSamplerParameterIiv = nullptr;
PFNGLGETSAMPLERPARAMETERFVPROC        glGetSamplerParameterfv = nullptr;
PFNGLGETSAMPLERPARAMETERIUIVPROC      glGetSamplerParameterIuiv = nullptr;
PFNGLQUERYCOUNTERPROC                 glQueryCounter = nullptr;
PFNGLGETQUERYOBJECTI64VPROC           glGetQueryObjecti64v = nullptr;
PFNGLGETQUERYOBJECTUI64VPROC          glGetQueryObjectui64v = nullptr;
PFNGLVERTEXATTRIBDIVISORPROC          glVertexAttribDivisor = nullptr;
PFNGLVERTEXATTRIBP1UIPROC             glVertexAttribP1ui = nullptr;
PFNGLVERTEXATTRIBP1UIVPROC            glVertexAttribP1uiv = nullptr;
PFNGLVERTEXATTRIBP2UIPROC             glVertexAttribP2ui = nullptr;
PFNGLVERTEXATTRIBP2UIVPROC            glVertexAttribP2uiv = nullptr;
PFNGLVERTEXATTRIBP3UIPROC             glVertexAttribP3ui = nullptr;
PFNGLVERTEXATTRIBP3UIVPROC            glVertexAttribP3uiv = nullptr;
PFNGLVERTEXATTRIBP4UIPROC             glVertexAttribP4ui = nullptr;
PFNGLVERTEXATTRIBP4UIVPROC            glVertexAttribP4uiv = nullptr;
PFNGLVERTEXP2UIPROC                   glVertexP2ui = nullptr;
PFNGLVERTEXP2UIVPROC                  glVertexP2uiv = nullptr;
PFNGLVERTEXP3UIPROC                   glVertexP3ui = nullptr;
PFNGLVERTEXP3UIVPROC                  glVertexP3uiv = nullptr;
PFNGLVERTEXP4UIPROC                   glVertexP4ui = nullptr;
PFNGLVERTEXP4UIVPROC                  glVertexP4uiv = nullptr;
PFNGLTEXCOORDP1UIPROC                 glTexCoordP1ui = nullptr;
PFNGLTEXCOORDP1UIVPROC                glTexCoordP1uiv = nullptr;
PFNGLTEXCOORDP2UIPROC                 glTexCoordP2ui = nullptr;
PFNGLTEXCOORDP2UIVPROC                glTexCoordP2uiv = nullptr;
PFNGLTEXCOORDP3UIPROC                 glTexCoordP3ui = nullptr;
PFNGLTEXCOORDP3UIVPROC                glTexCoordP3uiv = nullptr;
PFNGLTEXCOORDP4UIPROC                 glTexCoordP4ui = nullptr;
PFNGLTEXCOORDP4UIVPROC                glTexCoordP4uiv = nullptr;
PFNGLMULTITEXCOORDP1UIPROC            glMultiTexCoordP1ui = nullptr;
PFNGLMULTITEXCOORDP1UIVPROC           glMultiTexCoordP1uiv = nullptr;
PFNGLMULTITEXCOORDP2UIPROC            glMultiTexCoordP2ui = nullptr;
PFNGLMULTITEXCOORDP2UIVPROC           glMultiTexCoordP2uiv = nullptr;
PFNGLMULTITEXCOORDP3UIPROC            glMultiTexCoordP3ui = nullptr;
PFNGLMULTITEXCOORDP3UIVPROC           glMultiTexCoordP3uiv = nullptr;
PFNGLMULTITEXCOORDP4UIPROC            glMultiTexCoordP4ui = nullptr;
PFNGLMULTITEXCOORDP4UIVPROC           glMultiTexCoordP4uiv = nullptr;
PFNGLNORMALP3UIPROC                   glNormalP3ui = nullptr;
PFNGLNORMALP3UIVPROC                  glNormalP3uiv = nullptr;
PFNGLCOLORP3UIPROC                    glColorP3ui = nullptr;
PFNGLCOLORP3UIVPROC                   glColorP3uiv = nullptr;
PFNGLCOLORP4UIPROC                    glColorP4ui = nullptr;
PFNGLCOLORP4UIVPROC                   glColorP4uiv = nullptr;
PFNGLSECONDARYCOLORP3UIPROC           glSecondaryColorP3ui = nullptr;
PFNGLSECONDARYCOLORP3UIVPROC          glSecondaryColorP3uiv = nullptr;

// GL_VERSION_4_0
PFNGLMINSAMPLESHADINGPROC                glMinSampleShading = nullptr;
PFNGLBLENDEQUATIONIPROC                  glBlendEquationi = nullptr;
PFNGLBLENDEQUATIONSEPARATEIPROC          glBlendEquationSeparatei = nullptr;
PFNGLBLENDFUNCIPROC                      glBlendFunci = nullptr;
PFNGLBLENDFUNCSEPARATEIPROC              glBlendFuncSeparatei = nullptr;
PFNGLDRAWARRAYSINDIRECTPROC              glDrawArraysIndirect = nullptr;
PFNGLDRAWELEMENTSINDIRECTPROC            glDrawElementsIndirect = nullptr;
PFNGLUNIFORM1DPROC                       glUniform1d = nullptr;
PFNGLUNIFORM2DPROC                       glUniform2d = nullptr;
PFNGLUNIFORM3DPROC                       glUniform3d = nullptr;
PFNGLUNIFORM4DPROC                       glUniform4d = nullptr;
PFNGLUNIFORM1DVPROC                      glUniform1dv = nullptr;
PFNGLUNIFORM2DVPROC                      glUniform2dv = nullptr;
PFNGLUNIFORM3DVPROC                      glUniform3dv = nullptr;
PFNGLUNIFORM4DVPROC                      glUniform4dv = nullptr;
PFNGLUNIFORMMATRIX2DVPROC                glUniformMatrix2dv = nullptr;
PFNGLUNIFORMMATRIX3DVPROC                glUniformMatrix3dv = nullptr;
PFNGLUNIFORMMATRIX4DVPROC                glUniformMatrix4dv = nullptr;
PFNGLUNIFORMMATRIX2X3DVPROC              glUniformMatrix2x3dv = nullptr;
PFNGLUNIFORMMATRIX2X4DVPROC              glUniformMatrix2x4dv = nullptr;
PFNGLUNIFORMMATRIX3X2DVPROC              glUniformMatrix3x2dv = nullptr;
PFNGLUNIFORMMATRIX3X4DVPROC              glUniformMatrix3x4dv = nullptr;
PFNGLUNIFORMMATRIX4X2DVPROC              glUniformMatrix4x2dv = nullptr;
PFNGLUNIFORMMATRIX4X3DVPROC              glUniformMatrix4x3dv = nullptr;
PFNGLGETUNIFORMDVPROC                    glGetUniformdv = nullptr;
PFNGLGETSUBROUTINEUNIFORMLOCATIONPROC    glGetSubroutineUniformLocation = nullptr;
PFNGLGETSUBROUTINEINDEXPROC              glGetSubroutineIndex = nullptr;
PFNGLGETACTIVESUBROUTINEUNIFORMIVPROC    glGetActiveSubroutineUniformiv = nullptr;
PFNGLGETACTIVESUBROUTINEUNIFORMNAMEPROC  glGetActiveSubroutineUniformName = nullptr;
PFNGLGETACTIVESUBROUTINENAMEPROC         glGetActiveSubroutineName = nullptr;
PFNGLUNIFORMSUBROUTINESUIVPROC           glUniformSubroutinesuiv = nullptr;
PFNGLGETUNIFORMSUBROUTINEUIVPROC         glGetUniformSubroutineuiv = nullptr;
PFNGLGETPROGRAMSTAGEIVPROC               glGetProgramStageiv = nullptr;
PFNGLPATCHPARAMETERIPROC                 glPatchParameteri = nullptr;
PFNGLPATCHPARAMETERFVPROC                glPatchParameterfv = nullptr;
PFNGLBINDTRANSFORMFEEDBACKPROC           glBindTransformFeedback = nullptr;
PFNGLDELETETRANSFORMFEEDBACKSPROC        glDeleteTransformFeedbacks = nullptr;
PFNGLGENTRANSFORMFEEDBACKSPROC           glGenTransformFeedbacks = nullptr;
PFNGLISTRANSFORMFEEDBACKPROC             glIsTransformFeedback = nullptr;
PFNGLPAUSETRANSFORMFEEDBACKPROC          glPauseTransformFeedback = nullptr;
PFNGLRESUMETRANSFORMFEEDBACKPROC         glResumeTransformFeedback = nullptr;
PFNGLDRAWTRANSFORMFEEDBACKPROC           glDrawTransformFeedback = nullptr;
PFNGLDRAWTRANSFORMFEEDBACKSTREAMPROC     glDrawTransformFeedbackStream = nullptr;
PFNGLBEGINQUERYINDEXEDPROC               glBeginQueryIndexed = nullptr;
PFNGLENDQUERYINDEXEDPROC                 glEndQueryIndexed = nullptr;
PFNGLGETQUERYINDEXEDIVPROC               glGetQueryIndexediv = nullptr;

// GL_VERSION_4_1
PFNGLRELEASESHADERCOMPILERPROC           glReleaseShaderCompiler = nullptr;
PFNGLSHADERBINARYPROC                    glShaderBinary = nullptr;
PFNGLGETSHADERPRECISIONFORMATPROC        glGetShaderPrecisionFormat = nullptr;
PFNGLDEPTHRANGEFPROC                     glDepthRangef = nullptr;
PFNGLCLEARDEPTHFPROC                     glClearDepthf = nullptr;
PFNGLGETPROGRAMBINARYPROC                glGetProgramBinary = nullptr;
PFNGLPROGRAMBINARYPROC                   glProgramBinary = nullptr;
PFNGLPROGRAMPARAMETERIPROC               glProgramParameteri = nullptr;
PFNGLUSEPROGRAMSTAGESPROC                glUseProgramStages = nullptr;
PFNGLACTIVESHADERPROGRAMPROC             glActiveShaderProgram = nullptr;
PFNGLCREATESHADERPROGRAMVPROC            glCreateShaderProgramv = nullptr;
PFNGLBINDPROGRAMPIPELINEPROC             glBindProgramPipeline = nullptr;
PFNGLDELETEPROGRAMPIPELINESPROC          glDeleteProgramPipelines = nullptr;
PFNGLGENPROGRAMPIPELINESPROC             glGenProgramPipelines = nullptr;
PFNGLISPROGRAMPIPELINEPROC               glIsProgramPipeline = nullptr;
PFNGLGETPROGRAMPIPELINEIVPROC            glGetProgramPipelineiv = nullptr;
PFNGLPROGRAMUNIFORM1IPROC                glProgramUniform1i = nullptr;
PFNGLPROGRAMUNIFORM1IVPROC               glProgramUniform1iv = nullptr;
PFNGLPROGRAMUNIFORM1FPROC                glProgramUniform1f = nullptr;
PFNGLPROGRAMUNIFORM1FVPROC               glProgramUniform1fv = nullptr;
PFNGLPROGRAMUNIFORM1DPROC                glProgramUniform1d = nullptr;
PFNGLPROGRAMUNIFORM1DVPROC               glProgramUniform1dv = nullptr;
PFNGLPROGRAMUNIFORM1UIPROC               glProgramUniform1ui = nullptr;
PFNGLPROGRAMUNIFORM1UIVPROC              glProgramUniform1uiv = nullptr;
PFNGLPROGRAMUNIFORM2IPROC                glProgramUniform2i = nullptr;
PFNGLPROGRAMUNIFORM2IVPROC               glProgramUniform2iv = nullptr;
PFNGLPROGRAMUNIFORM2FPROC                glProgramUniform2f = nullptr;
PFNGLPROGRAMUNIFORM2FVPROC               glProgramUniform2fv = nullptr;
PFNGLPROGRAMUNIFORM2DPROC                glProgramUniform2d = nullptr;
PFNGLPROGRAMUNIFORM2DVPROC               glProgramUniform2dv = nullptr;
PFNGLPROGRAMUNIFORM2UIPROC               glProgramUniform2ui = nullptr;
PFNGLPROGRAMUNIFORM2UIVPROC              glProgramUniform2uiv = nullptr;
PFNGLPROGRAMUNIFORM3IPROC                glProgramUniform3i = nullptr;
PFNGLPROGRAMUNIFORM3IVPROC               glProgramUniform3iv = nullptr;
PFNGLPROGRAMUNIFORM3FPROC                glProgramUniform3f = nullptr;
PFNGLPROGRAMUNIFORM3FVPROC               glProgramUniform3fv = nullptr;
PFNGLPROGRAMUNIFORM3DPROC                glProgramUniform3d = nullptr;
PFNGLPROGRAMUNIFORM3DVPROC               glProgramUniform3dv = nullptr;
PFNGLPROGRAMUNIFORM3UIPROC               glProgramUniform3ui = nullptr;
PFNGLPROGRAMUNIFORM3UIVPROC              glProgramUniform3uiv = nullptr;
PFNGLPROGRAMUNIFORM4IPROC                glProgramUniform4i = nullptr;
PFNGLPROGRAMUNIFORM4IVPROC               glProgramUniform4iv = nullptr;
PFNGLPROGRAMUNIFORM4FPROC                glProgramUniform4f = nullptr;
PFNGLPROGRAMUNIFORM4FVPROC               glProgramUniform4fv = nullptr;
PFNGLPROGRAMUNIFORM4DPROC                glProgramUniform4d = nullptr;
PFNGLPROGRAMUNIFORM4DVPROC               glProgramUniform4dv = nullptr;
PFNGLPROGRAMUNIFORM4UIPROC               glProgramUniform4ui = nullptr;
PFNGLPROGRAMUNIFORM4UIVPROC              glProgramUniform4uiv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX2FVPROC         glProgramUniformMatrix2fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX3FVPROC         glProgramUniformMatrix3fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX4FVPROC         glProgramUniformMatrix4fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX2DVPROC         glProgramUniformMatrix2dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX3DVPROC         glProgramUniformMatrix3dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX4DVPROC         glProgramUniformMatrix4dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX2X3FVPROC       glProgramUniformMatrix2x3fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX3X2FVPROC       glProgramUniformMatrix3x2fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX2X4FVPROC       glProgramUniformMatrix2x4fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX4X2FVPROC       glProgramUniformMatrix4x2fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX3X4FVPROC       glProgramUniformMatrix3x4fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX4X3FVPROC       glProgramUniformMatrix4x3fv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX2X3DVPROC       glProgramUniformMatrix2x3dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX3X2DVPROC       glProgramUniformMatrix3x2dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX2X4DVPROC       glProgramUniformMatrix2x4dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX4X2DVPROC       glProgramUniformMatrix4x2dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX3X4DVPROC       glProgramUniformMatrix3x4dv = nullptr;
PFNGLPROGRAMUNIFORMMATRIX4X3DVPROC       glProgramUniformMatrix4x3dv = nullptr;
PFNGLVALIDATEPROGRAMPIPELINEPROC         glValidateProgramPipeline = nullptr;
PFNGLGETPROGRAMPIPELINEINFOLOGPROC       glGetProgramPipelineInfoLog = nullptr;
PFNGLVERTEXATTRIBL1DPROC                 glVertexAttribL1d = nullptr;
PFNGLVERTEXATTRIBL2DPROC                 glVertexAttribL2d = nullptr;
PFNGLVERTEXATTRIBL3DPROC                 glVertexAttribL3d = nullptr;
PFNGLVERTEXATTRIBL4DPROC                 glVertexAttribL4d = nullptr;
PFNGLVERTEXATTRIBL1DVPROC                glVertexAttribL1dv = nullptr;
PFNGLVERTEXATTRIBL2DVPROC                glVertexAttribL2dv = nullptr;
PFNGLVERTEXATTRIBL3DVPROC                glVertexAttribL3dv = nullptr;
PFNGLVERTEXATTRIBL4DVPROC                glVertexAttribL4dv = nullptr;
PFNGLVERTEXATTRIBLPOINTERPROC            glVertexAttribLPointer = nullptr;
PFNGLGETVERTEXATTRIBLDVPROC              glGetVertexAttribLdv = nullptr;
PFNGLVIEWPORTARRAYVPROC                  glViewportArrayv = nullptr;
PFNGLVIEWPORTINDEXEDFPROC                glViewportIndexedf = nullptr;
PFNGLVIEWPORTINDEXEDFVPROC               glViewportIndexedfv = nullptr;
PFNGLSCISSORARRAYVPROC                   glScissorArrayv = nullptr;
PFNGLSCISSORINDEXEDPROC                  glScissorIndexed = nullptr;
PFNGLSCISSORINDEXEDVPROC                 glScissorIndexedv = nullptr;
PFNGLDEPTHRANGEARRAYVPROC                glDepthRangeArrayv = nullptr;
PFNGLDEPTHRANGEINDEXEDPROC               glDepthRangeIndexed = nullptr;
PFNGLGETFLOATI_VPROC                     glGetFloati_v = nullptr;
PFNGLGETDOUBLEI_VPROC                    glGetDoublei_v = nullptr;

// GL_VERSION_4_2
PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC             glDrawArraysInstancedBaseInstance = nullptr;
PFNGLDRAWELEMENTSINSTANCEDBASEINSTANCEPROC           glDrawElementsInstancedBaseInstance = nullptr;
PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC glDrawElementsInstancedBaseVertexBaseInstance = nullptr;
PFNGLGETINTERNALFORMATIVPROC                         glGetInternalformativ = nullptr;
PFNGLGETACTIVEATOMICCOUNTERBUFFERIVPROC              glGetActiveAtomicCounterBufferiv = nullptr;
PFNGLBINDIMAGETEXTUREPROC                            glBindImageTexture = nullptr;
PFNGLMEMORYBARRIERPROC                               glMemoryBarrier = nullptr;
PFNGLTEXSTORAGE1DPROC                                glTexStorage1D = nullptr;
PFNGLTEXSTORAGE2DPROC                                glTexStorage2D = nullptr;
PFNGLTEXSTORAGE3DPROC                                glTexStorage3D = nullptr;
PFNGLDRAWTRANSFORMFEEDBACKINSTANCEDPROC              glDrawTransformFeedbackInstanced = nullptr;
PFNGLDRAWTRANSFORMFEEDBACKSTREAMINSTANCEDPROC        glDrawTransformFeedbackStreamInstanced = nullptr;

// GL_VERSION_4_3
PFNGLCLEARBUFFERDATAPROC                             glClearBufferData = nullptr;
PFNGLCLEARBUFFERSUBDATAPROC                          glClearBufferSubData = nullptr;
PFNGLDISPATCHCOMPUTEPROC                             glDispatchCompute = nullptr;
PFNGLDISPATCHCOMPUTEINDIRECTPROC                     glDispatchComputeIndirect = nullptr;
PFNGLCOPYIMAGESUBDATAPROC                            glCopyImageSubData = nullptr;
PFNGLFRAMEBUFFERPARAMETERIPROC                       glFramebufferParameteri = nullptr;
PFNGLGETFRAMEBUFFERPARAMETERIVPROC                   glGetFramebufferParameteriv = nullptr;
PFNGLGETINTERNALFORMATI64VPROC                       glGetInternalformati64v = nullptr;
PFNGLINVALIDATETEXSUBIMAGEPROC                       glInvalidateTexSubImage = nullptr;
PFNGLINVALIDATETEXIMAGEPROC                          glInvalidateTexImage = nullptr;
PFNGLINVALIDATEBUFFERSUBDATAPROC                     glInvalidateBufferSubData = nullptr;
PFNGLINVALIDATEBUFFERDATAPROC                        glInvalidateBufferData = nullptr;
PFNGLINVALIDATEFRAMEBUFFERPROC                       glInvalidateFramebuffer = nullptr;
PFNGLINVALIDATESUBFRAMEBUFFERPROC                    glInvalidateSubFramebuffer = nullptr;
PFNGLMULTIDRAWARRAYSINDIRECTPROC                     glMultiDrawArraysIndirect = nullptr;
PFNGLMULTIDRAWELEMENTSINDIRECTPROC                   glMultiDrawElementsIndirect = nullptr;
PFNGLGETPROGRAMINTERFACEIVPROC                       glGetProgramInterfaceiv = nullptr;
PFNGLGETPROGRAMRESOURCEINDEXPROC                     glGetProgramResourceIndex = nullptr;
PFNGLGETPROGRAMRESOURCENAMEPROC                      glGetProgramResourceName = nullptr;
PFNGLGETPROGRAMRESOURCEIVPROC                        glGetProgramResourceiv = nullptr;
PFNGLGETPROGRAMRESOURCELOCATIONPROC                  glGetProgramResourceLocation = nullptr;
PFNGLGETPROGRAMRESOURCELOCATIONINDEXPROC             glGetProgramResourceLocationIndex = nullptr;
PFNGLSHADERSTORAGEBLOCKBINDINGPROC                   glShaderStorageBlockBinding = nullptr;
PFNGLTEXBUFFERRANGEPROC                              glTexBufferRange = nullptr;
PFNGLTEXSTORAGE2DMULTISAMPLEPROC                     glTexStorage2DMultisample = nullptr;
PFNGLTEXSTORAGE3DMULTISAMPLEPROC                     glTexStorage3DMultisample = nullptr;
PFNGLTEXTUREVIEWPROC                                 glTextureView = nullptr;
PFNGLBINDVERTEXBUFFERPROC                            glBindVertexBuffer = nullptr;
PFNGLVERTEXATTRIBFORMATPROC                          glVertexAttribFormat = nullptr;
PFNGLVERTEXATTRIBIFORMATPROC                         glVertexAttribIFormat = nullptr;
PFNGLVERTEXATTRIBLFORMATPROC                         glVertexAttribLFormat = nullptr;
PFNGLVERTEXATTRIBBINDINGPROC                         glVertexAttribBinding = nullptr;
PFNGLVERTEXBINDINGDIVISORPROC                        glVertexBindingDivisor = nullptr;
PFNGLDEBUGMESSAGECONTROLPROC                         glDebugMessageControl = nullptr;
PFNGLDEBUGMESSAGEINSERTPROC                          glDebugMessageInsert = nullptr;
PFNGLDEBUGMESSAGECALLBACKPROC                        glDebugMessageCallback = nullptr;
PFNGLGETDEBUGMESSAGELOGPROC                          glGetDebugMessageLog = nullptr;
PFNGLPUSHDEBUGGROUPPROC                              glPushDebugGroup = nullptr;
PFNGLPOPDEBUGGROUPPROC                               glPopDebugGroup = nullptr;
PFNGLOBJECTLABELPROC                                 glObjectLabel = nullptr;
PFNGLGETOBJECTLABELPROC                              glGetObjectLabel = nullptr;
PFNGLOBJECTPTRLABELPROC                              glObjectPtrLabel = nullptr;
PFNGLGETOBJECTPTRLABELPROC                           glGetObjectPtrLabel = nullptr;

// GL_VERSION_4_4
PFNGLBUFFERSTORAGEPROC       glBufferStorage = nullptr;
PFNGLCLEARTEXIMAGEPROC       glClearTexImage = nullptr;
PFNGLCLEARTEXSUBIMAGEPROC    glClearTexSubImage = nullptr;
PFNGLBINDBUFFERSBASEPROC     glBindBuffersBase = nullptr;
PFNGLBINDBUFFERSRANGEPROC    glBindBuffersRange = nullptr;
PFNGLBINDTEXTURESPROC        glBindTextures = nullptr;
PFNGLBINDSAMPLERSPROC        glBindSamplers = nullptr;
PFNGLBINDIMAGETEXTURESPROC   glBindImageTextures = nullptr;
PFNGLBINDVERTEXBUFFERSPROC   glBindVertexBuffers = nullptr;

// GL_VERSION_4_5
PFNGLCLIPCONTROLPROC                                     glClipControl = nullptr;
PFNGLCREATETRANSFORMFEEDBACKSPROC                        glCreateTransformFeedbacks = nullptr;
PFNGLTRANSFORMFEEDBACKBUFFERBASEPROC                     glTransformFeedbackBufferBase = nullptr;
PFNGLTRANSFORMFEEDBACKBUFFERRANGEPROC                    glTransformFeedbackBufferRange = nullptr;
PFNGLGETTRANSFORMFEEDBACKIVPROC                          glGetTransformFeedbackiv = nullptr;
PFNGLGETTRANSFORMFEEDBACKI_VPROC                         glGetTransformFeedbacki_v = nullptr;
PFNGLGETTRANSFORMFEEDBACKI64_VPROC                       glGetTransformFeedbacki64_v = nullptr;
PFNGLCREATEBUFFERSPROC                                   glCreateBuffers = nullptr;
PFNGLNAMEDBUFFERSTORAGEPROC                              glNamedBufferStorage = nullptr;
PFNGLNAMEDBUFFERDATAPROC                                 glNamedBufferData = nullptr;
PFNGLNAMEDBUFFERSUBDATAPROC                              glNamedBufferSubData = nullptr;
PFNGLCOPYNAMEDBUFFERSUBDATAPROC                          glCopyNamedBufferSubData = nullptr;
PFNGLCLEARNAMEDBUFFERDATAPROC                            glClearNamedBufferData = nullptr;
PFNGLCLEARNAMEDBUFFERSUBDATAPROC                         glClearNamedBufferSubData = nullptr;
PFNGLMAPNAMEDBUFFERPROC                                  glMapNamedBuffer = nullptr;
PFNGLMAPNAMEDBUFFERRANGEPROC                             glMapNamedBufferRange = nullptr;
PFNGLUNMAPNAMEDBUFFERPROC                                glUnmapNamedBuffer = nullptr;
PFNGLFLUSHMAPPEDNAMEDBUFFERRANGEPROC                     glFlushMappedNamedBufferRange = nullptr;
PFNGLGETNAMEDBUFFERPARAMETERIVPROC                       glGetNamedBufferParameteriv = nullptr;
PFNGLGETNAMEDBUFFERPARAMETERI64VPROC                     glGetNamedBufferParameteri64v = nullptr;
PFNGLGETNAMEDBUFFERPOINTERVPROC                          glGetNamedBufferPointerv = nullptr;
PFNGLGETNAMEDBUFFERSUBDATAPROC                           glGetNamedBufferSubData = nullptr;
PFNGLCREATEFRAMEBUFFERSPROC                              glCreateFramebuffers = nullptr;
PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC                    glNamedFramebufferRenderbuffer = nullptr;
PFNGLNAMEDFRAMEBUFFERPARAMETERIPROC                      glNamedFramebufferParameteri = nullptr;
PFNGLNAMEDFRAMEBUFFERTEXTUREPROC                         glNamedFramebufferTexture = nullptr;
PFNGLNAMEDFRAMEBUFFERTEXTURELAYERPROC                    glNamedFramebufferTextureLayer = nullptr;
PFNGLNAMEDFRAMEBUFFERDRAWBUFFERPROC                      glNamedFramebufferDrawBuffer = nullptr;
PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC                     glNamedFramebufferDrawBuffers = nullptr;
PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC                      glNamedFramebufferReadBuffer = nullptr;
PFNGLINVALIDATENAMEDFRAMEBUFFERDATAPROC                  glInvalidateNamedFramebufferData = nullptr;
PFNGLINVALIDATENAMEDFRAMEBUFFERSUBDATAPROC               glInvalidateNamedFramebufferSubData = nullptr;
PFNGLCLEARNAMEDFRAMEBUFFERIVPROC                         glClearNamedFramebufferiv = nullptr;
PFNGLCLEARNAMEDFRAMEBUFFERUIVPROC                        glClearNamedFramebufferuiv = nullptr;
PFNGLCLEARNAMEDFRAMEBUFFERFVPROC                         glClearNamedFramebufferfv = nullptr;
PFNGLCLEARNAMEDFRAMEBUFFERFIPROC                         glClearNamedFramebufferfi = nullptr;
PFNGLBLITNAMEDFRAMEBUFFERPROC                            glBlitNamedFramebuffer = nullptr;
PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC                     glCheckNamedFramebufferStatus = nullptr;
PFNGLGETNAMEDFRAMEBUFFERPARAMETERIVPROC                  glGetNamedFramebufferParameteriv = nullptr;
PFNGLGETNAMEDFRAMEBUFFERATTACHMENTPARAMETERIVPROC        glGetNamedFramebufferAttachmentParameteriv = nullptr;
PFNGLCREATERENDERBUFFERSPROC                             glCreateRenderbuffers = nullptr;
PFNGLNAMEDRENDERBUFFERSTORAGEPROC                        glNamedRenderbufferStorage = nullptr;
PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC             glNamedRenderbufferStorageMultisample = nullptr;
PFNGLGETNAMEDRENDERBUFFERPARAMETERIVPROC                 glGetNamedRenderbufferParameteriv = nullptr;
PFNGLCREATETEXTURESPROC                                  glCreateTextures = nullptr;
PFNGLTEXTUREBUFFERPROC                                   glTextureBuffer = nullptr;
PFNGLTEXTUREBUFFERRANGEPROC                              glTextureBufferRange = nullptr;
PFNGLTEXTURESTORAGE1DPROC                                glTextureStorage1D = nullptr;
PFNGLTEXTURESTORAGE2DPROC                                glTextureStorage2D = nullptr;
PFNGLTEXTURESTORAGE3DPROC                                glTextureStorage3D = nullptr;
PFNGLTEXTURESTORAGE2DMULTISAMPLEPROC                     glTextureStorage2DMultisample = nullptr;
PFNGLTEXTURESTORAGE3DMULTISAMPLEPROC                     glTextureStorage3DMultisample = nullptr;
PFNGLTEXTURESUBIMAGE1DPROC                               glTextureSubImage1D = nullptr;
PFNGLTEXTURESUBIMAGE2DPROC                               glTextureSubImage2D = nullptr;
PFNGLTEXTURESUBIMAGE3DPROC                               glTextureSubImage3D = nullptr;
PFNGLCOMPRESSEDTEXTURESUBIMAGE1DPROC                     glCompressedTextureSubImage1D = nullptr;
PFNGLCOMPRESSEDTEXTURESUBIMAGE2DPROC                     glCompressedTextureSubImage2D = nullptr;
PFNGLCOMPRESSEDTEXTURESUBIMAGE3DPROC                     glCompressedTextureSubImage3D = nullptr;
PFNGLCOPYTEXTURESUBIMAGE1DPROC                           glCopyTextureSubImage1D = nullptr;
PFNGLCOPYTEXTURESUBIMAGE2DPROC                           glCopyTextureSubImage2D = nullptr;
PFNGLCOPYTEXTURESUBIMAGE3DPROC                           glCopyTextureSubImage3D = nullptr;
PFNGLTEXTUREPARAMETERFPROC                               glTextureParameterf = nullptr;
PFNGLTEXTUREPARAMETERFVPROC                              glTextureParameterfv = nullptr;
PFNGLTEXTUREPARAMETERIPROC                               glTextureParameteri = nullptr;
PFNGLTEXTUREPARAMETERIIVPROC                             glTextureParameterIiv = nullptr;
PFNGLTEXTUREPARAMETERIUIVPROC                            glTextureParameterIuiv = nullptr;
PFNGLTEXTUREPARAMETERIVPROC                              glTextureParameteriv = nullptr;
PFNGLGENERATETEXTUREMIPMAPPROC                           glGenerateTextureMipmap = nullptr;
PFNGLBINDTEXTUREUNITPROC                                 glBindTextureUnit = nullptr;
PFNGLGETTEXTUREIMAGEPROC                                 glGetTextureImage = nullptr;
PFNGLGETCOMPRESSEDTEXTUREIMAGEPROC                       glGetCompressedTextureImage = nullptr;
PFNGLGETTEXTURELEVELPARAMETERFVPROC                      glGetTextureLevelParameterfv = nullptr;
PFNGLGETTEXTURELEVELPARAMETERIVPROC                      glGetTextureLevelParameteriv = nullptr;
PFNGLGETTEXTUREPARAMETERFVPROC                           glGetTextureParameterfv = nullptr;
PFNGLGETTEXTUREPARAMETERIIVPROC                          glGetTextureParameterIiv = nullptr;
PFNGLGETTEXTUREPARAMETERIUIVPROC                         glGetTextureParameterIuiv = nullptr;
PFNGLGETTEXTUREPARAMETERIVPROC                           glGetTextureParameteriv = nullptr;
PFNGLCREATEVERTEXARRAYSPROC                              glCreateVertexArrays = nullptr;
PFNGLDISABLEVERTEXARRAYATTRIBPROC                        glDisableVertexArrayAttrib = nullptr;
PFNGLENABLEVERTEXARRAYATTRIBPROC                         glEnableVertexArrayAttrib = nullptr;
PFNGLVERTEXARRAYELEMENTBUFFERPROC                        glVertexArrayElementBuffer = nullptr;
PFNGLVERTEXARRAYVERTEXBUFFERPROC                         glVertexArrayVertexBuffer = nullptr;
PFNGLVERTEXARRAYVERTEXBUFFERSPROC                        glVertexArrayVertexBuffers = nullptr;
PFNGLVERTEXARRAYATTRIBBINDINGPROC                        glVertexArrayAttribBinding = nullptr;
PFNGLVERTEXARRAYATTRIBFORMATPROC                         glVertexArrayAttribFormat = nullptr;
PFNGLVERTEXARRAYATTRIBIFORMATPROC                        glVertexArrayAttribIFormat = nullptr;
PFNGLVERTEXARRAYATTRIBLFORMATPROC                        glVertexArrayAttribLFormat = nullptr;
PFNGLVERTEXARRAYBINDINGDIVISORPROC                       glVertexArrayBindingDivisor = nullptr;
PFNGLGETVERTEXARRAYIVPROC                                glGetVertexArrayiv = nullptr;
PFNGLGETVERTEXARRAYINDEXEDIVPROC                         glGetVertexArrayIndexediv = nullptr;
PFNGLGETVERTEXARRAYINDEXED64IVPROC                       glGetVertexArrayIndexed64iv = nullptr;
PFNGLCREATESAMPLERSPROC                                  glCreateSamplers = nullptr;
PFNGLCREATEPROGRAMPIPELINESPROC                          glCreateProgramPipelines = nullptr;
PFNGLCREATEQUERIESPROC                                   glCreateQueries = nullptr;
PFNGLGETQUERYBUFFEROBJECTI64VPROC                        glGetQueryBufferObjecti64v = nullptr;
PFNGLGETQUERYBUFFEROBJECTIVPROC                          glGetQueryBufferObjectiv = nullptr;
PFNGLGETQUERYBUFFEROBJECTUI64VPROC                       glGetQueryBufferObjectui64v = nullptr;
PFNGLGETQUERYBUFFEROBJECTUIVPROC                         glGetQueryBufferObjectuiv = nullptr;
PFNGLMEMORYBARRIERBYREGIONPROC                           glMemoryBarrierByRegion = nullptr;
PFNGLGETTEXTURESUBIMAGEPROC                              glGetTextureSubImage = nullptr;
PFNGLGETCOMPRESSEDTEXTURESUBIMAGEPROC                    glGetCompressedTextureSubImage = nullptr;
PFNGLGETGRAPHICSRESETSTATUSPROC                          glGetGraphicsResetStatus = nullptr;
PFNGLGETNCOMPRESSEDTEXIMAGEPROC                          glGetnCompressedTexImage = nullptr;
PFNGLGETNTEXIMAGEPROC                                    glGetnTexImage = nullptr;
PFNGLGETNUNIFORMDVPROC                                   glGetnUniformdv = nullptr;
PFNGLGETNUNIFORMFVPROC                                   glGetnUniformfv = nullptr;
PFNGLGETNUNIFORMIVPROC                                   glGetnUniformiv = nullptr;
PFNGLGETNUNIFORMUIVPROC                                  glGetnUniformuiv = nullptr;
PFNGLREADNPIXELSPROC                                     glReadnPixels = nullptr;
PFNGLGETNMAPDVPROC                                       glGetnMapdv = nullptr;
PFNGLGETNMAPFVPROC                                       glGetnMapfv = nullptr;
PFNGLGETNMAPIVPROC                                       glGetnMapiv = nullptr;
PFNGLGETNPIXELMAPFVPROC                                  glGetnPixelMapfv = nullptr;
PFNGLGETNPIXELMAPUIVPROC                                 glGetnPixelMapuiv = nullptr;
PFNGLGETNPIXELMAPUSVPROC                                 glGetnPixelMapusv = nullptr;
PFNGLGETNPOLYGONSTIPPLEPROC                              glGetnPolygonStipple = nullptr;
PFNGLGETNCOLORTABLEPROC                                  glGetnColorTable = nullptr;
PFNGLGETNCONVOLUTIONFILTERPROC                           glGetnConvolutionFilter = nullptr;
PFNGLGETNSEPARABLEFILTERPROC                             glGetnSeparableFilter = nullptr;
PFNGLGETNHISTOGRAMPROC                                   glGetnHistogram = nullptr;
PFNGLGETNMINMAXPROC                                      glGetnMinmax = nullptr;
PFNGLTEXTUREBARRIERPROC                                  glTextureBarrier = nullptr;

// GL_VERSION_4_6
PFNGLSPECIALIZESHADERPROC                glSpecializeShader = nullptr;
PFNGLMULTIDRAWARRAYSINDIRECTCOUNTPROC    glMultiDrawArraysIndirectCount = nullptr;
PFNGLMULTIDRAWELEMENTSINDIRECTCOUNTPROC  glMultiDrawElementsIndirectCount = nullptr;
PFNGLPOLYGONOFFSETCLAMPPROC              glPolygonOffsetClamp = nullptr;

#endif

LLGLManager gGLManager;
// S24 - Re-order to match initialisation
LLGLManager::LLGLManager() :
    mInited(false),
    mIsDisabled(false),
    mMaxSamples(0),
    mNumTextureImageUnits(1),
    mMaxSampleMaskWords(0),
    mMaxColorTextureSamples(0),
    mMaxDepthTextureSamples(0),
    mMaxIntegerSamples(0),
    mGLMaxVertexRange(0),
    mGLMaxIndexRange(0),
    mIsAMD(false),
    mIsNVIDIA(false),
    mIsIntel(false),
    mHasRequirements(true),
    mDriverVersionMajor(1),
    mDriverVersionMinor(0),
    mDriverVersionRelease(0),
    mGLVersion(1.0f),
    mGLSLVersionMajor(0),
    mGLSLVersionMinor(0),       
    mVRAM(0)
    
{    
}

//---------------------------------------------------------------------
// Global initialization for GL
//---------------------------------------------------------------------
void LLGLManager::initWGL()
{
    // S24: quiet extension probing, preferring modern FBO/context creation
    // where supported while keeping legacy pbuffer/render_texture available.

    // Try to initialize ARB pixel format support (quiet)
    glh_init_extensions("WGL_ARB_pixel_format");

    // Prefer modern context creation if available
    if (ExtensionExists("WGL_ARB_create_context", gGLHExts.mSysExts))
    {
        GLH_EXT_NAME(wglCreateContextAttribsARB) =
            (PFNWGLCREATECONTEXTATTRIBSARBPROC)GLH_EXT_GET_PROC_ADDRESS("wglCreateContextAttribsARB");
    }

    // AMD per-adapter helpers (quiet)
    mHasAMDAssociations = ExtensionExists("WGL_AMD_gpu_association", gGLHExts.mSysExts);
    if (mHasAMDAssociations)
    {
        GLH_EXT_NAME(wglGetGPUIDsAMD) = (PFNWGLGETGPUIDSAMDPROC)GLH_EXT_GET_PROC_ADDRESS("wglGetGPUIDsAMD");
        GLH_EXT_NAME(wglGetGPUInfoAMD) = (PFNWGLGETGPUINFOAMDPROC)GLH_EXT_GET_PROC_ADDRESS("wglGetGPUInfoAMD");
    }

    // NVX GPU memory info (quiet)
    mHasNVXGpuMemoryInfo = ExtensionExists("GL_NVX_gpu_memory_info", gGLHExts.mSysExts);

    // Vsync / swap control (quiet)
    if (ExtensionExists("WGL_EXT_swap_control", gGLHExts.mSysExts))
    {
        GLH_EXT_NAME(wglSwapIntervalEXT) = (PFNWGLSWAPINTERVALEXTPROC)GLH_EXT_GET_PROC_ADDRESS("wglSwapIntervalEXT");
        GLH_EXT_NAME(wglGetSwapIntervalEXT) = (PFNWGLGETSWAPINTERVALEXTPROC)GLH_EXT_GET_PROC_ADDRESS("wglGetSwapIntervalEXT");
    }

    // Legacy pbuffer/render_texture: probe but do not warn if absent
    glh_init_extensions("WGL_ARB_pbuffer");
    glh_init_extensions("WGL_ARB_render_texture");

    // Single-user-visible message on completion as requested
    LL_WARNS("RenderInit") << "WGL init complete!" << LL_ENDL;
}

// S24: legacy WGL/GL context-init path; unreachable under DX_RENDER
// (LLWindowWin32::switchContext() only calls this from its non-DX_RENDER
// branch). Real equivalent is initGLDX() below. Body removed; name/signature
// kept in case anything still calls this.
bool LLGLManager::initGL()
{
    if (mInited)
    {
        LL_ERRS("RenderInit") << "Calling init on LLGLManager after already initialized!" << LL_ENDL;
    }

    return true;
}

// S24: real DXGI-based equivalent of initGL() above - see llgl.h. Called
// once from LLWindowWin32::switchContext() after initDX11Context() succeeds.
bool LLGLManager::initGLDX()
{
    if (mInited)
    {
        LL_ERRS("RenderInit") << "Calling init on LLGLManager after already initialized!" << LL_ENDL;
    }

    mInited = true;
    mHasRequirements = true;

    // Walk up from the already-created device to its owning adapter (same
    // technique DXSwapChain::create() already uses to find the swap chain's
    // factory) rather than re-enumerating adapters and guessing which one
    // matches - this guarantees we describe the actual adapter in use.
    ID3D11Device* device = gDXDevice.getDevice();
    if (device)
    {
        IDXGIDevice* dxgi_device = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device)) && dxgi_device)
        {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter)
            {
                DXGI_ADAPTER_DESC desc = {};
                if (SUCCEEDED(adapter->GetDesc(&desc)))
                {
                    std::wstring description_w(desc.Description);
                    mGLRenderer = ll_convert_wide_to_string(description_w);
                    LLStringUtil::toUpper(mGLRenderer);

                    mVRAM = (U32)(desc.DedicatedVideoMemory / (1024 * 1024));

                    switch (desc.VendorId)
                    {
                    case 0x10DE: // NVIDIA
                        mGLVendorShort = "NVIDIA";
                        mIsNVIDIA = true;
                        break;
                    case 0x1002: // AMD/ATI
                    case 0x1022:
                        mGLVendorShort = "AMD";
                        mIsAMD = true;
                        break;
                    case 0x8086: // Intel
                        mGLVendorShort = "INTEL";
                        mIsIntel = true;
                        break;
                    default:
                        mGLVendorShort = "MISC";
                        break;
                    }

                    // S24: DXGI has no separate "vendor name" string like
                    // GL_VENDOR - use the PCI-VendorId-derived generic name
                    // from the switch above instead of duplicating the model
                    // string (Description) into both fields.
                    mGLVendor = mGLVendorShort;
                }
                adapter->Release();
            }
            dxgi_device->Release();
        }
    }

    // S24: D3D11 feature-level hardware is capability-equivalent to (or
    // beyond) every GL-version-gated feature-masking check in this
    // codebase; report a real modern version rather than leaving mGLVersion
    // at its 1.0f default, which trips LLFeatureManager::applyBaseMasks()'s
    // "mGLVersion < 3.99f" check into unconditionally applying the "GL3"
    // feature mask.
    mGLVersion = 4.6f;
    mDriverVersionMajor = 4;
    mDriverVersionMinor = 6;
    mDriverVersionRelease = 0;
    mGLSLVersionMajor = 4;
    mGLSLVersionMinor = 60;

    // Real feature-level string for display purposes (e.g. Floater About's
    // "Graphics API" line, llappviewer.cpp) - distinct from mGLVersion above,
    // which is a fixed feature-masking sentinel, not meant to be shown to
    // the user as-is.
    switch (gDXDevice.getFeatureLevel())
    {
    case D3D_FEATURE_LEVEL_11_1: mGLVersionString = "Direct3D 11.1 (Feature Level 11_1)"; break;
    case D3D_FEATURE_LEVEL_11_0: mGLVersionString = "Direct3D 11.0 (Feature Level 11_0)"; break;
    case D3D_FEATURE_LEVEL_10_1: mGLVersionString = "Direct3D 11 (Feature Level 10_1)"; break;
    case D3D_FEATURE_LEVEL_10_0: mGLVersionString = "Direct3D 11 (Feature Level 10_0)"; break;
    default: mGLVersionString = "Direct3D 11"; break;
    }

    mHasCubeMapArray = true;
    mHasTransformFeedback = true;
    mHasDebugOutput = true;
    mHasAnisotropic = true;
    mMaxAnisotropy = 16.f;

    mGLMaxTextureSize = 16384;
    mNumTextureImageUnits = 32;

    LL_INFOS("RenderInit") << "DX_RENDER GPU detection: vendor=" << mGLVendorShort
        << " renderer=" << mGLRenderer << " vram=" << mVRAM << "MB" << LL_ENDL;

    return true;
}

void LLGLManager::getGLInfo(LLSD& info)
{
    if (gHeadlessClient)
    {
        info["GLInfo"]["GLVendor"] = HEADLESS_VENDOR_STRING;
        info["GLInfo"]["GLRenderer"] = HEADLESS_RENDERER_STRING;
        info["GLInfo"]["GLVersion"] = HEADLESS_VERSION_STRING;
        return;
    }
    else
    {
        // S24: use the already-populated fields, not raw glGetString() -
        // there is no GL context to call it against under DX_RENDER.
        info["GLInfo"]["GLVendor"] = mGLVendor;
        info["GLInfo"]["GLRenderer"] = mGLRenderer;
        info["GLInfo"]["GLVersion"] = mGLVersionString;
    }

#if !LL_MESA_HEADLESS
    std::string all_exts = ll_safe_string((const char *)gGLHExts.mSysExts);
    boost::char_separator<char> sep(" ");
    boost::tokenizer<boost::char_separator<char> > tok(all_exts, sep);
    for(boost::tokenizer<boost::char_separator<char> >::iterator i = tok.begin(); i != tok.end(); ++i)
    {
        info["GLInfo"]["GLExtensions"].append(*i);
    }
#endif
}

std::string LLGLManager::getGLInfoString()
{
    std::string info_str;

    if (gHeadlessClient)
    {
        info_str += std::string("GL_VENDOR      ") + HEADLESS_VENDOR_STRING + std::string("\n");
        info_str += std::string("GL_RENDERER    ") + HEADLESS_RENDERER_STRING + std::string("\n");
        info_str += std::string("GL_VERSION     ") + HEADLESS_VERSION_STRING + std::string("\n");
    }
    else
    {
        // S24: same as getGLInfo() above.
        info_str += std::string("GL_VENDOR      ") + mGLVendor + std::string("\n");
        info_str += std::string("GL_RENDERER    ") + mGLRenderer + std::string("\n");
        info_str += std::string("GL_VERSION     ") + mGLVersionString + std::string("\n");
    }

#if !LL_MESA_HEADLESS
    std::string all_exts= ll_safe_string(((const char *)gGLHExts.mSysExts));
    LLStringUtil::replaceChar(all_exts, ' ', '\n');
    info_str += std::string("GL_EXTENSIONS:\n") + all_exts + std::string("\n");
#endif

    return info_str;
}

void LLGLManager::printGLInfoString()
{
    if (gHeadlessClient)
    {
        LL_INFOS("RenderInit") << "GL_VENDOR:     " << HEADLESS_VENDOR_STRING << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_RENDERER:   " << HEADLESS_RENDERER_STRING << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_VERSION:    " << HEADLESS_VERSION_STRING << LL_ENDL;
    }
    else
    {
        // S24: same as getGLInfo() above.
        LL_INFOS("RenderInit") << "GL_VENDOR:     " << mGLVendor << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_RENDERER:   " << mGLRenderer << LL_ENDL;
        LL_INFOS("RenderInit") << "GL_VERSION:    " << mGLVersionString << LL_ENDL;
    }

#if !LL_MESA_HEADLESS
    std::string all_exts= ll_safe_string(((const char *)gGLHExts.mSysExts));
    LLStringUtil::replaceChar(all_exts, ' ', '\n');
    LL_DEBUGS("RenderInit") << "GL_EXTENSIONS:\n" << all_exts << LL_ENDL;
#endif
}

std::string LLGLManager::getRawGLString()
{
    std::string gl_string;
    if (gHeadlessClient)
    {
        gl_string = HEADLESS_VENDOR_STRING + " " + HEADLESS_RENDERER_STRING;
    }
    else
    {
        // S24: same as getGLInfo() above; also feeds
        // LLFeatureManager's RDNA3.5 detection (checkRDNA35()).
        gl_string = mGLVendor + " " + mGLRenderer;
    }
    return gl_string;
}

void LLGLManager::asLLSD(LLSD& info)
{
    // Currently these are duplicates of fields in "system".
    info["gpu_vendor"] = mGLVendorShort;
    info["gpu_version"] = mDriverVersionVendorString;
    info["opengl_version"] = mGLVersionString;

    info["vram"] = LLSD::Integer(mVRAM);

    // OpenGL limits
    info["max_samples"] = mMaxSamples;
    info["num_texture_image_units"] =  mNumTextureImageUnits;
    info["max_sample_mask_words"] = mMaxSampleMaskWords;
    info["max_color_texture_samples"] = mMaxColorTextureSamples;
    info["max_depth_texture_samples"] = mMaxDepthTextureSamples;
    info["max_integer_samples"] = mMaxIntegerSamples;
    info["max_vertex_range"] = mGLMaxVertexRange;
    info["max_index_range"] = mGLMaxIndexRange;
    info["max_texture_size"] = mGLMaxTextureSize;

    // Which vendor
    info["is_ati"] = mIsAMD;  // note, do not rename is_ati to is_amd without coordinating with DW
    info["is_nvidia"] = mIsNVIDIA;
    info["is_intel"] = mIsIntel;

    info["gl_renderer"] = mGLRenderer;
}

// S24: queryAvailableVRAM() removed - zero callers anywhere in the tree (confirmed by grep), and
// its two glGetIntegerv() calls (NVX/ATI GL memory-info extensions) were unguarded raw GL, dead
// code with a live-landmine body. Found in a tree-wide stray-GL sweep.

void LLGLManager::shutdownGL()
{
    if (mInited)
    {
        // S24: unguarded raw glFinish() - a live landmine, not dead code: mInited is set true by
        // the real DX11 init path (initGLDX(), above) and this runs unconditionally on every
        // window close (llwindowwin32.cpp) - found in a tree-wide stray-GL sweep. Nothing to
        // guard it WITH under DX_RENDER either: the DX11 device/swapchain are already fully torn
        // down by the caller before this runs (llwindowwin32.cpp), so there's no outstanding GPU
        // work left to wait on.
#ifndef DX_RENDER
        glFinish();
#endif
        mInited = false;
    }
}

// S24: initExtensions() removed - zero callers anywhere in the tree (confirmed by grep),
// a legacy full-GL function-pointer-table loader (~875 lines of glXxx = GLH_EXT_GET_PROC_ADDRESS(...)
// assignments) with no live D3D11 relevance. Found in a tree-wide stray-GL sweep.

void rotate_quat(LLQuaternion& rotation)
{
    F32 angle_radians, x, y, z;
    rotation.getAngleAxis(&angle_radians, &x, &y, &z);
    gDX.rotatef(angle_radians * RAD_TO_DEG, x, y, z);
}

void flush_glerror()
{
    // S24: glGetError() with no current GL context (DX_RENDER never creates
    // one) is undefined behavior - guard like clear_glerror() below.
#ifndef DX_RENDER
    glGetError();
#endif
}

void clear_glerror()
{
    // S24: gated on DX_RENDER too, not just LL_DEBUG_GL - glGetError() is
    // unresolved once OpenGL is delinked, so a debug+DX_RENDER build would
    // otherwise fail to link.
#if LL_DEBUG_GL && !defined(DX_RENDER)
    glGetError();
    glGetError();
#endif
}

///////////////////////////////////////////////////////////////
//
// DXState
//

// Static members
std::unordered_map<LLGLenum, LLGLboolean> DXState::sStateMap;
LLGLenum DXState::sCullFace = GL_BACK; // OpenGL default

GLboolean LLGLDepthTest::sDepthEnabled = GL_FALSE; // OpenGL default
DXenum LLGLDepthTest::sDepthFunc = GL_LESS; // OpenGL default
GLboolean LLGLDepthTest::sWriteEnabled = GL_TRUE; // OpenGL default

//static
void DXState::initClass()
{
    sStateMap[GL_DITHER] = GL_TRUE;
    // sStateMap[GL_TEXTURE_2D] = GL_TRUE;

    //make sure multisample defaults to disabled
    sStateMap[GL_MULTISAMPLE] = GL_FALSE;
    // S24: unguarded raw glDisable() - a live landmine, not dead code: restoreGL() below (called
    // live from llviewerwindow.cpp on device-lost/restore) always reaches this. GL_MULTISAMPLE
    // isn't one of applyDXState()'s dispatched states either (see that function's own comment -
    // only BLEND/CULL_FACE/SCISSOR_TEST/DEPTH_CLAMP/POLYGON_OFFSET_* have a D3D11 target), so the
    // sStateMap bookkeeping above is all that ever mattered under DX_RENDER; MSAA itself is a
    // swap-chain/render-target sample-count property in D3D11, not a per-draw enable/disable.
    // Found in a tree-wide stray-GL sweep.
#ifndef DX_RENDER
    glDisable(GL_MULTISAMPLE);
#endif
}

//static
void DXState::restoreGL()
{
    sStateMap.clear();
    initClass();
}

// S24: resetTextureStates()/dumpStates() had zero callers anywhere in the tree (confirmed by
// grep, not assumed) - removed rather than left as unreferenced raw-GL-calling dead code
// (resetTextureStates() had 3 unguarded calls: glGetIntegerv/glClientActiveTexture). Unlike
// checkStates() below (kept as a no-op stub - it has ~25 live call sites tree-wide, not worth
// touching for zero behavior change), these had no callers to preserve an interface for.

void DXState::checkStates(GLboolean writeAlpha)
{
    // S24: was a GL-context state validator (glGetIntegerv/glIsEnabled read
    // back against sStateMap) with no D3D11 equivalent - removed rather than
    // left dead behind the gDebugGL guard.
    return;
}

///////////////////////////////////////////////////////////////////////

namespace
{
    // Scoped to GL_BLEND/GL_CULL_FACE/GL_SCISSOR_TEST/GL_DEPTH_CLAMP - see
    // DXStateCache.h for why (the only four states LLGLEnable/LLGLDisable
    // actually toggle in this codebase, confirmed by grep, not guessed).
    // Everything else (GL_STENCIL_TEST, GL_POLYGON_OFFSET_*, ...) still
    // updates sStateMap bookkeeping (in setEnabled(), unconditionally) but
    // has no D3D11 equivalent applied yet - deferred until something that
    // toggles them is converted to DX_RENDER.
    void applyDXState(DXenum state, bool enabled)
    {
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        if (state == GL_BLEND)
        {
            // Blend enable is only one of the three pieces D3D11 bundles
            // into one ID3D11BlendState (see DXStateCache.h) - factors and
            // color write mask live on LLRender, not here. Route through
            // the shared chokepoint rather than building a partial state.
            gDX.applyDXBlendState();
        }
        else if (state == GL_CULL_FACE || state == GL_SCISSOR_TEST || state == GL_DEPTH_CLAMP
            || state == GL_POLYGON_OFFSET_FILL || state == GL_POLYGON_OFFSET_LINE)
        {
            // S24: GL_CULL_FACE/GL_SCISSOR_TEST/GL_DEPTH_CLAMP/
            // GL_POLYGON_OFFSET_FILL/LINE all bundle into the same D3D11
            // rasterizer-state object - toggling any one must read the
            // CURRENT value of the others too (via sStateMap, already
            // updated by setEnabled() above) so it doesn't silently clobber
            // them back to a default. Routed through applyDXRasterizerState()
            // (llrender.cpp), which gathers all dimensions fresh each call,
            // rather than duplicating getRasterizerState()+RSSetState() inline.
            gDX.applyDXRasterizerState();
        }
    }

    // GL depth funcs -> D3D11_COMPARISON_FUNC. GL_NEVER is first in both
    // enumerations but the underlying values aren't contiguous/matching, so
    // this is a real lookup, not an arithmetic remap.
    //
    // S24: LESS/LEQUAL <-> GREATER/GEQUAL are swapped from the "obvious"
    // direct mapping - the depth buffer stores near=1.0/far=0.0 (reversed-Z,
    // see kGLtoDXDepthRemap in llrender.cpp), so GL's "passes if closer"
    // (GL_LESS/GL_LEQUAL) needs D3D11's GREATER/GREATER_EQUAL to match.
    // EQUAL/NOTEQUAL/ALWAYS/NEVER are direction-independent.
    D3D11_COMPARISON_FUNC glDepthFuncToDX(DXenum depth_func)
    {
        switch (depth_func)
        {
        case GL_NEVER:    return D3D11_COMPARISON_NEVER;
        case GL_LESS:     return D3D11_COMPARISON_GREATER;
        case GL_EQUAL:    return D3D11_COMPARISON_EQUAL;
        case GL_LEQUAL:   return D3D11_COMPARISON_GREATER_EQUAL;
        case GL_GREATER:  return D3D11_COMPARISON_LESS;
        case GL_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
        case GL_GEQUAL:   return D3D11_COMPARISON_LESS_EQUAL;
        case GL_ALWAYS:   return D3D11_COMPARISON_ALWAYS;
        default:
            LL_WARNS("RenderState") << "Unmapped GL depth func 0x" << std::hex << depth_func << std::dec << LL_ENDL;
            return D3D11_COMPARISON_GREATER_EQUAL;
        }
    }

    // Mirrors applyDXState() above but for LLGLDepthTest (llglstates.h) -
    // depth-enable/write-enable/depth-func are three independently-set GL
    // toggles (glEnable(GL_DEPTH_TEST)/glDepthMask()/glDepthFunc()) that
    // D3D11 bundles into one ID3D11DepthStencilState (see DXStateCache.h).
    // LLGLDepthTest already tracks all three as one unit internally
    // (sDepthEnabled/sWriteEnabled/sDepthFunc), so unlike blend state there's
    // no separate LLRender-side chokepoint needed - this is called directly
    // from the constructor/destructor with the full combination in hand.
    void applyDXDepthStencilState(GLboolean depth_enabled, GLboolean write_enabled, DXenum depth_func)
    {
        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        D3D11_COMPARISON_FUNC func = glDepthFuncToDX(depth_func);
        ID3D11DepthStencilState* ds = DXStateCache::getDepthStencilState(depth_enabled != GL_FALSE, write_enabled != GL_FALSE, func);
        ctx->OMSetDepthStencilState(ds, 0);
    }
}

DXState::DXState(LLGLenum state, S32 enabled) :
    mState(state), mWasEnabled(false), mIsEnabled(false)
{

    if (mState)
    {
        mWasEnabled = sStateMap[state];
        setEnabled(enabled);
    }
}

void DXState::setEnabled(S32 enabled)
{
    if (!mState)
    {
        return;
    }
    if (enabled == CURRENT_STATE)
    {
        enabled = sStateMap[mState] == GL_TRUE ? ENABLED_STATE : DISABLED_STATE;
    }
    else if (enabled == ENABLED_STATE && sStateMap[mState] != GL_TRUE)
    {
        // S24: flush BEFORE the bookkeeping+state-apply, not after -
        // applyDXState() changes real D3D11 pipeline state immediately, and
        // without flushing first, already-queued CPU-side geometry would be
        // drawn with the new state instead of the one it was built under.
        // gDXUIBatch is a second, independent GPU-submission queue
        // gDX.flush() doesn't reach, so flush it too.
        gDX.flush();
        gDXUIBatch.flushPending();
        sStateMap[mState] = GL_TRUE;
        applyDXState(mState, true);
    }
    else if (enabled == DISABLED_STATE && sStateMap[mState] != GL_FALSE)
    {
        // Same missing-flush issue as the ENABLED_STATE branch above -
        // also flush gDXUIBatch, see its comment there.
        gDX.flush();
        gDXUIBatch.flushPending();
        sStateMap[mState] = GL_FALSE;
        applyDXState(mState, false);
    }
    mIsEnabled = enabled;
}

DXState::~DXState()
{
    if (mState)
    {
#if LL_DEBUG_GL
        // S24: GL-context state validator (glIsEnabled read back against
        // driver state) with no D3D11 equivalent - removed.
#endif

        if (mIsEnabled != mWasEnabled)
        {
            // S24: must pair the sStateMap write with a real applyDXState()
            // call and a preceding flush, same as setEnabled() - otherwise
            // this restore path desyncs bookkeeping from real GPU state
            // (was the root cause of a black-world bug: blend state stuck
            // enabled, never actually toggled).
            gDX.flush();
            sStateMap[mState] = mWasEnabled ? GL_TRUE : GL_FALSE;
            applyDXState(mState, mWasEnabled);
        }
    }
}

LLGLCullFace::LLGLCullFace(LLGLenum face) :
    mPrevFace(DXState::getCullFace())
{
    gDX.cullFace(face);
}

LLGLCullFace::~LLGLCullFace()
{
    gDX.cullFace(mPrevFace);
}

////////////////////////////////////////////////////////////////////////////////

void LLGLManager::initGLStates()
{
    //gl states moved to classes in llglstates.h
    DXState::initClass();
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

LLGLUserClipPlane::LLGLUserClipPlane(const LLPlane& p, const glm::mat4& modelview, const glm::mat4& projection, bool apply)
{
    mApply = apply;

    if (mApply)
    {
        mModelview = modelview;
        mProjection = projection;

        //flip incoming LLPlane to get consistent behavior compared to frustum culling
        setPlane(-p[0], -p[1], -p[2], -p[3]);
    }
}

void LLGLUserClipPlane::disable()
{
    if (mApply)
    {
        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.popMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
    }
    mApply = false;
}

void LLGLUserClipPlane::setPlane(F32 a, F32 b, F32 c, F32 d)
{
    const glm::mat4& P = mProjection;
    const glm::mat4& M = mModelview;

    glm::mat4 invtrans_MVP = glm::transpose(glm::inverse(P * M));
    glm::vec4 oplane(a, b, c, d);
    glm::vec4 cplane = invtrans_MVP * oplane;

    cplane /= fabs(cplane[2]); // Normalize such that depth is not scaled
    cplane[3] -= 1;

    if (cplane[2] < 0)
        cplane *= -1;

    auto suffix = glm::mat4(1.0f);
    suffix[2] = cplane; // Replace the third row with `cplane`

    glm::mat4 newP = suffix * P;

    mProjection = newP;  // Replace gDX's projection matrix manipulation
    mProjectionInverse = glm::transpose(glm::inverse(newP)); // Store the inverse directly
}

LLGLUserClipPlane::~LLGLUserClipPlane()
{
    disable();
}

LLGLDepthTest::LLGLDepthTest(GLboolean depth_enabled, GLboolean write_enabled, DXenum depth_func)
: mPrevDepthEnabled(sDepthEnabled), mPrevDepthFunc(sDepthFunc), mPrevWriteEnabled(sWriteEnabled)
{
    // stop_glerror(); // S24: removed - GPU stall in a hot constructor path (created/destroyed hundreds of times per frame).
    checkState();

    if (!depth_enabled)
    { // always disable depth writes if depth testing is disabled
      // GL spec defines this as a requirement, but some implementations allow depth writes with testing disabled
      // The proper way to write to depth buffer with testing disabled is to enable testing and use a depth_func of GL_ALWAYS
        write_enabled = GL_FALSE;
    }

    // S24: applyDXDepthStencilState() calls OMSetDepthStencilState()
    // immediately - without a flush first, geometry already queued under the
    // OLD depth state would be rasterized with the NEW one once it flushes.
    // LLGLDepthTest backs LLGLSUIDefault (nearly every UI draw call) and is
    // constructed/destroyed hundreds of times per frame, so this is a real,
    // frequent risk.
    if (depth_enabled != sDepthEnabled || depth_func != sDepthFunc || write_enabled != sWriteEnabled)
    {
        // S24: also flush gDXUIBatch's separate pending queue - same
        // missing-flush hazard. Also keeps depth-tested HUD content from
        // merging into the same batch as non-depth-tested screen-space UI.
        gDX.flush();
        gDXUIBatch.flushPending();
        applyDXDepthStencilState(depth_enabled, write_enabled, depth_func);
        sDepthEnabled = depth_enabled;
        sDepthFunc = depth_func;
        sWriteEnabled = write_enabled;
    }
}

LLGLDepthTest::~LLGLDepthTest()
{
    checkState();
    // Same missing-flush issue as the constructor above - see its comment.
    if (sDepthEnabled != mPrevDepthEnabled || sDepthFunc != mPrevDepthFunc || sWriteEnabled != mPrevWriteEnabled)
    {
        gDX.flush();
        gDXUIBatch.flushPending();
        applyDXDepthStencilState(mPrevDepthEnabled, mPrevWriteEnabled, mPrevDepthFunc);
        sDepthEnabled = mPrevDepthEnabled;
        sDepthFunc = mPrevDepthFunc;
        sWriteEnabled = mPrevWriteEnabled;
    }
}

void LLGLDepthTest::checkState()
{
#if LL_DEBUG_GL
    if (gDebugGL)
    {
        GLint func = 0;
        GLboolean mask = GL_FALSE;

        glGetIntegerv(GL_DEPTH_FUNC, &func);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);

        if (glIsEnabled(GL_DEPTH_TEST) != sDepthEnabled ||
            sWriteEnabled != mask ||
            sDepthFunc != func)
        {
            if (gDebugSession)
            {
                gFailLog << "Unexpected depth testing state." << std::endl;
            }
            else
            {
                LL_GL_ERRS << "Unexpected depth testing state." << LL_ENDL;
            }
        }
    }
#endif
}

LLGLSquashToFarClip::LLGLSquashToFarClip()
{
    glm::mat4 proj = get_current_projection();
    setProjectionMatrix(proj, 0);
}

LLGLSquashToFarClip::LLGLSquashToFarClip(const glm::mat4& P, U32 layer)
{
    setProjectionMatrix(P, layer);
}

void LLGLSquashToFarClip::setProjectionMatrix(glm::mat4 projection, U32 layer)
{
    F32 depth = 0.99999f - 0.0001f * layer;

    glm::vec4 P_row_3 = glm::row(projection, 3) * depth;
    projection = glm::row(projection, 2, P_row_3);

    LLRender::eMatrixMode last_matrix_mode = gDX.getMatrixMode();

    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.pushMatrix();
    gDX.loadMatrix(glm::value_ptr(projection));

    gDX.matrixMode(last_matrix_mode);
}

LLGLSquashToFarClip::~LLGLSquashToFarClip()
{
    LLRender::eMatrixMode last_matrix_mode = gDX.getMatrixMode();

    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.popMatrix();

    gDX.matrixMode(last_matrix_mode);
}



LLGLSPipelineSkyBox::LLGLSPipelineSkyBox()
: mCullFace(GL_CULL_FACE)
, mSquashClip()
{
}

LLGLSPipelineSkyBox::~LLGLSPipelineSkyBox()
{
}

LLGLSPipelineDepthTestSkyBox::LLGLSPipelineDepthTestSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineSkyBox()
, mDepth(depth_test ? GL_TRUE : GL_FALSE, depth_write ? GL_TRUE : GL_FALSE, GL_LEQUAL)
{

}

LLGLSPipelineBlendSkyBox::LLGLSPipelineBlendSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineDepthTestSkyBox(depth_test, depth_write)
, mBlend(GL_BLEND)
{
    gDX.setSceneBlendType(LLRender::BT_ALPHA);
}

#if LL_WINDOWS
// Expose desired use of high-performance graphics processor to Optimus driver and to AMD driver
// https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif


