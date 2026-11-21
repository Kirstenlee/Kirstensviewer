/**
 * @file DXTypes.h
 * @brief D3D11 renderer basic types and state constants
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

#ifndef DXTYPES_H
#define DXTYPES_H

// S24: DXenum - GL-vocabulary-free U32 enumerant type for the state constants below.
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

// S24: capability-toggle constants for DXStateEnable/DXStateDisable/DXState's "which
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

// S24: layout of the pixels a texture is uploaded from. Auto means "from the component count"
// (1 luminance, 2 luminance-alpha, 3 RGB, 4 RGBA). Replaces the GL internalformat/format/type triple.
enum class DXPixelFormat : U8
{
    Auto = 0,
    Luminance8,
    LuminanceAlpha8,
    Alpha8,     // single channel stored in alpha (e.g. the terrain alpha ramps)
    RGB8,
    RGBA8,
    BGRA8       // R and B swapped (the web plugins' native buffer order)
};

// bytes per pixel of an uncompressed source format (0 for Auto)
inline S32 dxPixelFormatBytes(DXPixelFormat format)
{
    switch (format)
    {
    case DXPixelFormat::Luminance8:
    case DXPixelFormat::Alpha8:
        return 1;
    case DXPixelFormat::LuminanceAlpha8:
        return 2;
    case DXPixelFormat::RGB8:
        return 3;
    case DXPixelFormat::RGBA8:
    case DXPixelFormat::BGRA8:
        return 4;
    default:
        return 0;
    }
}

#endif
