/** 
 * @file llgltypes.h
 * @brief LLGL definition
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#ifndef LLGLTYPES_H
#define LLGLTYPES_H

#define MAX_GL_TEXTURE_UNITS 16

typedef U32 LLGLenum;
typedef U32 LLGLuint;
typedef S32 LLGLint;
typedef F32 LLGLfloat;
typedef F64 LLGLdouble;
typedef U8 LLGLboolean;

// S24: DXenum - GL-vocabulary-free U32 alias for LLGLenum, adopted
// opportunistically as files are touched rather than mass-renamed tree-wide.
// LLGLuint/LLGLint/LLGLfloat/LLGLdouble/LLGLboolean are candidates for the
// same treatment later.
typedef U32 DXenum;

// S24: depth/stencil comparison-function constants - GL-vocabulary-free
// equivalents of GL_NEVER/GL_LESS/GL_EQUAL/GL_LEQUAL/GL_GREATER/GL_NOTEQUAL/
// GL_GEQUAL/GL_ALWAYS. Hardcoded to the real OpenGL 1.0 spec values (stable,
// never renumbered by any extension) rather than referencing the GL_* macros
// themselves, so these have no dependency on <GL/gl.h> at all - the actual
// point of this type, not just a naming change. Used as DXDepthTest's
// depth_func parameter (llglstates.h); glDepthFuncToDX() (llgl.cpp) maps
// these to the matching D3D11_COMPARISON_FUNC for this project's reversed-Z
// convention, and static_asserts there guard the values against drift.
constexpr DXenum DX_NEVER    = 0x0200;
constexpr DXenum DX_LESS     = 0x0201;
constexpr DXenum DX_EQUAL    = 0x0202;
constexpr DXenum DX_LEQUAL   = 0x0203;
constexpr DXenum DX_GREATER  = 0x0204;
constexpr DXenum DX_NOTEQUAL = 0x0205;
constexpr DXenum DX_GEQUAL   = 0x0206;
constexpr DXenum DX_ALWAYS   = 0x0207;

// S24: capability-toggle constants for LLGLEnable/LLGLDisable/DXState's "which
// state" argument - same hardcoded-not-aliased treatment as the depth-func
// constants above, for the four capabilities actually wired through to a
// D3D11 state object today (DXStateCache::getBlendState()/getRasterizerState(),
// via applyDXState() in llgl.cpp). GL_POLYGON_OFFSET_FILL/LINE are the next
// candidates for this same treatment, not yet done.
constexpr DXenum DX_BLEND       = 0x0BE2;
constexpr DXenum DX_CULL_FACE   = 0x0B44;
constexpr DXenum DX_SCISSOR_TEST = 0x0C11;
// S24: GL_DEPTH_CLAMP is an ARB/NV extension enum, not core GL 1.0 like the
// other three - but its value was promoted unchanged into core GL 4.5 and is
// what llglheaders.h itself falls back to defining (#define GL_DEPTH_CLAMP
// 0x864F, guarded by #ifndef) when the real header is too old to have it -
// confirmed stable from that same fallback definition, not guessed.
constexpr DXenum DX_DEPTH_CLAMP = 0x864F;

#endif
