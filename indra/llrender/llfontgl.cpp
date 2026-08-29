/**
 * @file llfontgl.cpp
 * @brief Wrapper around FreeType
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

#include "linden_common.h"

#include "llfontgl.h"

// Linden library includes
#include "llfasttimer.h"
#include "llfontfreetype.h"
#include "llfontbitmapcache.h"
#include "llfontregistry.h"
#include "llgl.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llglslshader.h"
#include "llstl.h"
#include "v4color.h"
#include "glm/gtc/type_ptr.hpp"
#include "lltexture.h"
#include "lldir.h"
#include "llstring.h"

#ifdef DX_RENDER
#include "DXUIBatch.h"
#endif

// Third party library includes
#include <boost/tokenizer.hpp>

#if LL_WINDOWS
#include <Shlobj.h>
#include <Knownfolders.h>
#include <Objbase.h>
#endif // LL_WINDOWS

const S32 BOLD_OFFSET = 1;

// static class members
F32 LLFontGL::sVertDPI = 96.f;
F32 LLFontGL::sHorizDPI = 96.f;
F32 LLFontGL::sScaleX = 1.f;
F32 LLFontGL::sScaleY = 1.f;
S32 LLFontGL::sResolutionGeneration = 0;
bool LLFontGL::sDisplayFont = true ;
std::string LLFontGL::sAppDir;

LLColor4 LLFontGL::sShadowColor(0.f, 0.f, 0.f, 1.f);
LLFontRegistry* LLFontGL::sFontRegistry = NULL;

LLCoordGL LLFontGL::sCurOrigin;
F32 LLFontGL::sCurDepth;
std::vector<std::pair<LLCoordGL, F32> > LLFontGL::sOriginStack;

const F32 PAD_UVY = 0.5f; // half of vertical padding between glyphs in the glyph texture
const F32 DROP_SHADOW_SOFT_STRENGTH = 0.3f;

LLFontGL::LLFontGL()
{
}

LLFontGL::~LLFontGL()
{
}

void LLFontGL::reset()
{
    mFontFreetype->reset(sVertDPI, sHorizDPI);
}

void LLFontGL::destroyGL()
{
    mFontFreetype->destroyGL();
}

bool LLFontGL::loadFace(const std::string& filename, F32 point_size, const F32 vert_dpi, const F32 horz_dpi, S32 weight, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags)
{
    if(mFontFreetype == reinterpret_cast<LLFontFreetype*>(NULL))
    {
        mFontFreetype = new LLFontFreetype;
    }

    return mFontFreetype->loadFace(filename, point_size, vert_dpi, horz_dpi, weight, is_fallback, face_n, hinting, flags);
}

S32 LLFontGL::getNumFaces(const std::string& filename)
{
    if (mFontFreetype == reinterpret_cast<LLFontFreetype*>(NULL))
    {
        mFontFreetype = new LLFontFreetype;
    }

    return mFontFreetype->getNumFaces(filename);
}

S32 LLFontGL::getCacheGeneration() const
{
    const LLFontBitmapCache* font_bitmap_cache = mFontFreetype->getFontBitmapCache();
    return font_bitmap_cache->getCacheGeneration();
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, const LLRect& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
    ShadowType shadow, S32 max_chars, F32* right_x, bool use_ellipses, bool use_color) const
{
    LLRectf rect_float((F32)rect.mLeft, (F32)rect.mTop, (F32)rect.mRight, (F32)rect.mBottom);
    return render(wstr, begin_offset, rect_float, color, halign, valign, style, shadow, max_chars, right_x, use_ellipses, use_color);
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, const LLRectf& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                     ShadowType shadow, S32 max_chars, F32* right_x, bool use_ellipses, bool use_color) const
{
    F32 x = rect.mLeft;
    F32 y = 0.f;

    switch(valign)
    {
    case TOP:
        y = rect.mTop;
        break;
    case VCENTER:
        y = rect.getCenterY();
        break;
    case BASELINE:
    case BOTTOM:
        y = rect.mBottom;
        break;
    default:
        y = rect.mBottom;
        break;
    }
    return render(wstr, begin_offset, x, y, color, halign, valign, style, shadow, max_chars, (S32)rect.getWidth(), right_x, use_ellipses, use_color);
}

// The only seams in LLFontGL that touch rendering (see submitGlyphBatch()/
// submitUnderline() below for the DX_RENDER-vs-GL fences).
void LLFontGL::beginTextRender() const
{
#ifdef DX_RENDER
    // Flush gGL's pending immediate-mode batch (e.g. a widget's own
    // background rect via gl_rect_2d()) so it draws before DXUIBatch's
    // separate, immediate Draw() call for this text - otherwise submission
    // order can diverge and the rect ends up painted over the text.
    gGL.flush();
#endif
    gGL.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);
    gGL.pushUIMatrix();
    gGL.loadUIIdentity();
    // Depth translation, so that floating text appears 'in-world'
    // and is correctly occluded.
    gGL.translatef(0.f, 0.f, sCurDepth);
    // Not guaranteed to be set correctly
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
}

void LLFontGL::endTextRender() const
{
    gGL.popUIMatrix();
}

void LLFontGL::bindGlyphTexture(LLImageGL* font_image) const
{
    gGL.getTexUnit(0)->bind(font_image);
}

void LLFontGL::submitGlyphBatch(const LLVector4a* vertices, const LLVector2* uvs, const LLColor4U* colors, S32 vertex_count) const
{
#ifdef DX_RENDER
    // Uses whatever shader is CURRENTLY bound (gUIProgram, per
    // beginTextRender()'s comment - this class never binds its own shader,
    // mirroring GL's "current program" model).
    if (vertex_count <= 0)
    {
        return;
    }
    // S24 (2026-08-17, task #54): when LLFontVertexBuffer::genBuffers() has
    // a display-list recording open (beginList()/endList()), route through
    // gGL's own immediate-mode path instead of the fast gDXUIBatch path -
    // LLRender::flush()'s existing sBufferDataList capture (genBuffer() +
    // mDXImage, both already backend-agnostic/DX-safe) builds a real,
    // replayable LLVertexBufferData from this call, exactly like it always
    // has for GL. This is the COLD path - recording only happens once per
    // genBuffers() call (i.e. only when the cached render() params actually
    // changed), so the extra LLVertexBuffer allocation cost here is a
    // one-time thing, not a per-frame one. The common case (no recording,
    // every other frame) is completely unchanged below.
    if (gGL.isRecording())
    {
        gGL.begin(LLRender::TRIANGLES);
        gGL.vertexBatchPreTransformed(vertices, uvs, colors, vertex_count);
        gGL.end();
        return;
    }
    if (LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr)
    {
        // S24 (2026-08-16): DXUIBatch's batching key is (shader, topology,
        // alpha_blend, depth) only - it has no idea the MVP is about to
        // change, so two different text strings sharing that same key
        // (e.g. 2D UI text and a world-space HUD nametag both use
        // gUIProgram/TriangleList/alpha-blend/no-depth) can get silently
        // merged into ONE pending batch even though each was positioned
        // assuming its OWN, different transform. Only the LAST-synced MVP
        // before the eventual Draw() actually takes effect - any
        // earlier-pushed text in the same batch gets drawn with the wrong
        // matrix, distorting/misplacing it. The existing pass-boundary
        // flushPending() hooks (llviewerdisplay.cpp, llhudobject.cpp) only
        // guard specific call-site boundaries and don't reach every case;
        // closing it here instead - unconditionally draining any batch that
        // was pending under a DIFFERENT (now-stale) matrix before this
        // string's own vertices/matrix get pushed - guarantees every string
        // of text always draws with its own correct transform regardless of
        // what any particular caller remembers to flush. Narrows the
        // batching granularity to "one draw per text string" instead of
        // "one draw per matching-state run", which is still far fewer
        // draws than the pre-batching one-draw-per-glyph baseline task #211
        // fixed - correctness over squeezing out the last few draw calls.
        gDXUIBatch.flushPending();

        static thread_local std::vector<DXUIVertex> dx_verts;
        dx_verts.resize(vertex_count);
        for (S32 v = 0; v < vertex_count; ++v)
        {
            const F32* p = vertices[v].getF32ptr();
            dx_verts[v].pos[0] = p[0];
            dx_verts[v].pos[1] = p[1];
            dx_verts[v].pos[2] = p[2];
            dx_verts[v].color[0] = colors[v].mV[0];
            dx_verts[v].color[1] = colors[v].mV[1];
            dx_verts[v].color[2] = colors[v].mV[2];
            dx_verts[v].color[3] = colors[v].mV[3];
            dx_verts[v].uv[0] = uvs[v].mV[0];
            dx_verts[v].uv[1] = uvs[v].mV[1];
        }
        gDXUIBatch.push(dx_verts.data(), vertex_count);
        gGL.syncMatrices();
        // Same defensive null-check as LLVertexBuffer::setupVertexBuffer()'s
        // DX_RENDER branch - only null if the bound shader failed to compile.
        if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
        {
            gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
        }
    }
#else
    gGL.begin(LLRender::TRIANGLES);
    {
        gGL.vertexBatchPreTransformed(vertices, uvs, colors, vertex_count);
    }
    gGL.end();
#endif // DX_RENDER
}

void LLFontGL::submitUnderline(F32 x0, F32 x1, F32 y, const LLColor4U& color) const
{
    // color is passed explicitly (text_color/emoji_color) rather than
    // relying on GL's ambient "current color" carry-forward, since each
    // DXUIBatch draw is self-contained under DX_RENDER.
#ifdef DX_RENDER
    // S24 (2026-08-17, task #54): same recording-mode fallback as
    // submitGlyphBatch() above (see its comment) - here it also sidesteps
    // the "DXUIBatch has no line topology" quad-expansion below entirely,
    // replaying as a real LINES draw instead (matching GL's own topology
    // exactly, since this path goes through the general-purpose
    // LLVertexBuffer/DXVertexLayout machinery, which already handles LINES
    // natively - no CPU-side quad expansion needed here, unlike TRIANGLE_FAN).
    if (gGL.isRecording())
    {
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.color4ubv(color.mV);
        gGL.begin(LLRender::LINES);
        gGL.vertex2f(x0, y);
        gGL.vertex2f(x1, y);
        gGL.end();
        return;
    }
    // DXUIBatch has no line topology - represented as a 1-unit-tall filled
    // quad centered on y instead (matching GL's default line width).
    if (LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr)
    {
        // S24 (2026-08-16): same matrix-vs-batching-key gap as
        // submitGlyphBatch() above (see its comment) - an underline shares
        // the same (shader, topology, alpha_blend, depth) signature as
        // glyph quads and plain text-adjacent geometry, so it's just as
        // able to get silently merged with something needing a different
        // transform.
        gDXUIBatch.flushPending();

        constexpr F32 HALF_WIDTH = 0.5f;
        DXUIVertex quad[6];
        auto setv = [&](DXUIVertex& v, F32 x, F32 yy)
        {
            v.pos[0] = x; v.pos[1] = yy; v.pos[2] = 0.f;
            v.color[0] = color.mV[0]; v.color[1] = color.mV[1];
            v.color[2] = color.mV[2]; v.color[3] = color.mV[3];
            v.uv[0] = v.uv[1] = 0.f;
        };
        setv(quad[0], x0, y + HALF_WIDTH);
        setv(quad[1], x1, y + HALF_WIDTH);
        setv(quad[2], x0, y - HALF_WIDTH);
        setv(quad[3], x1, y + HALF_WIDTH);
        setv(quad[4], x1, y - HALF_WIDTH);
        setv(quad[5], x0, y - HALF_WIDTH);

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gDXUIBatch.push(quad, 6);
        gGL.syncMatrices();
        if (ID3DBlob* vsb = shader->mDXVertexShader.getVSBytecode())
        {
            gDXUIBatch.flush(vsb->GetBufferPointer(), vsb->GetBufferSize(), shader->mDXVertexShader.getVS(), shader->mDXPixelShader.getPS(), true, shader->mName.c_str());
        }
    }
#else
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gGL.color4ubv(color.mV);
    gGL.begin(LLRender::LINES);
    gGL.vertex2f(x0, y);
    gGL.vertex2f(x1, y);
    gGL.end();
#endif // DX_RENDER
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                     ShadowType shadow, S32 max_chars, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if(!sDisplayFont) //do not display texts
    {
        return static_cast<S32>(wstr.length());
    }

    if (wstr.empty())
    {
        return 0;
    }

    beginTextRender();

    S32 scaled_max_pixels = max_pixels == S32_MAX ? S32_MAX : llceil((F32)max_pixels * sScaleX);

    // determine which style flags need to be added programmatically by stripping off the
    // style bits that are drawn by the underlying Freetype font
    U8 style_to_add = (style | mFontDescriptor.getStyle()) & ~mFontFreetype->getStyle();

    F32 drop_shadow_strength = 0.f;
    if (shadow != NO_SHADOW)
    {
        F32 luminance;
        color.calcHSL(NULL, NULL, &luminance);
        drop_shadow_strength = clamp_rescale(luminance, 0.35f, 0.6f, 0.f, 1.f);
        if (luminance < 0.35f)
        {
            shadow = NO_SHADOW;
        }
    }

    LLVector2 origin(floorf(sCurOrigin.mX*sScaleX), floorf(sCurOrigin.mY*sScaleY));

    S32 chars_drawn = 0;
    S32 i;
    S32 length;

    if (-1 == max_chars)
    {
        max_chars = length = (S32)wstr.length() - begin_offset;
    }
    else
    {
        length = llmin((S32)wstr.length() - begin_offset, max_chars );
    }

    F32 cur_x, cur_y, cur_render_x, cur_render_y;

    cur_x = ((F32)x * sScaleX) + origin.mV[VX];
    cur_y = ((F32)y * sScaleY) + origin.mV[VY];

    // Offset y by vertical alignment.
    // use unscaled font metrics here
    switch (valign)
    {
    case TOP:
        cur_y -= llceil(mFontFreetype->getAscenderHeight());
        break;
    case BOTTOM:
        cur_y += llceil(mFontFreetype->getDescenderHeight());
        break;
    case VCENTER:
        cur_y -= llceil((llceil(mFontFreetype->getAscenderHeight()) - llceil(mFontFreetype->getDescenderHeight())) / 2.f);
        break;
    case BASELINE:
        // Baseline, do nothing.
        break;
    default:
        break;
    }

    switch (halign)
    {
    case LEFT:
        break;
    case RIGHT:
        cur_x -= llmin(scaled_max_pixels, ll_round(getWidthF32(wstr.c_str(), begin_offset, length) * sScaleX));
        break;
    case HCENTER:
        cur_x -= llmin(scaled_max_pixels, ll_round(getWidthF32(wstr.c_str(), begin_offset, length) * sScaleX)) / 2;
        break;
    default:
        break;
    }

    cur_render_y = cur_y;
    cur_render_x = cur_x;

    F32 start_x = (F32)ll_round(cur_x);

    const LLFontBitmapCache* font_bitmap_cache = mFontFreetype->getFontBitmapCache();

    // S24: this WAS "compute once before the loop", flagged by the
    // original comment below (kept for context) as looking wrong - it is.
    // LLFontBitmapCache::mBitmapWidth/mBitmapHeight are shared across BOTH
    // glyph types (Grayscale and Color use the same two fields, not
    // per-type ones - llfontbitmapcache.h), and nextOpenPos() overwrites
    // them the moment either atlas needs a fresh page (llfontbitmapcache.cpp,
    // "Make a new one" branch). getGlyphInfo() below can trigger exactly
    // that mid-loop (first-ever glyph of a given type this session), which
    // silently invalidates a value captured once up here for every glyph
    // rendered afterward - wrong UV divisors, sampling the wrong region of
    // the (now differently-sized) atlas texture. Confirmed as the root
    // cause of task #254 (SMP color-emoji rendering as garbage under
    // DX_RENDER, specifically in LLFontVertexBuffer-cached callers that
    // bake one render() pass's UVs in permanently - uncached callers
    // self-heal next frame once the atlas size has settled, masking this
    // everywhere else). Moved inside the loop; recomputed per glyph.
    //
    // This looks wrong, value is dynamic.
    // LLFontBitmapCache::nextOpenPos can alter these values when
    // new characters get added to cache, which affects whole string.
    // Todo: Perhaps value should update after symbols were added?

    const S32 LAST_CHARACTER = LLFontFreetype::LAST_CHAR_FULL;

    bool draw_ellipses = false;
    if (use_ellipses)
    {
        // check for too long of a string
        S32 string_width = ll_round(getWidthF32(wstr.c_str(), begin_offset, max_chars) * sScaleX);
        if (string_width > scaled_max_pixels)
        {
            // use four dots for ellipsis width to generate padding
            const LLWString dots(utf8str_to_wstring(std::string("....")));
            scaled_max_pixels = llmax(0, scaled_max_pixels - ll_round(getWidthF32(dots.c_str())));
            draw_ellipses = true;
        }
    }

    const LLFontGlyphInfo* next_glyph = NULL;

    // string can have more than one glyph per char (ex: bold or shadow),
    // make sure that GLYPH_BATCH_SIZE won't end up with half a symbol.
    // See drawGlyph.
    // Ex: with shadows it's 6 glyps per char. 30 fits exactly 5 chars.
    static constexpr S32 GLYPH_BATCH_SIZE = 30;
    static thread_local LLVector4a vertices[GLYPH_BATCH_SIZE * 6];
    static thread_local LLVector2 uvs[GLYPH_BATCH_SIZE * 6];
    static thread_local LLColor4U colors[GLYPH_BATCH_SIZE * 6];

    LLColor4U text_color(color);
    // Preserve the transparency to render fading emojis in fading text (e.g.
    // for the chat console)... HB
    LLColor4U emoji_color(255, 255, 255, text_color.mV[VALPHA]);

    std::pair<EFontGlyphType, S32> bitmap_entry = std::make_pair(EFontGlyphType::Grayscale, -1);
    S32 glyph_count = 0;
    llwchar last_char = wstr[begin_offset];
    for (i = begin_offset; i < begin_offset + length; i++)
    {
        llwchar wch = wstr[i];

        const LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, (!use_color) ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
        }
        if (!fgi)
        {
            LL_ERRS() << "Missing Glyph Info" << LL_ENDL;
            break;
        }
        // S24: recomputed every glyph, not once before the loop - see the
        // comment above the loop for why. getGlyphInfo() just above can have
        // just allocated a fresh atlas page (of either glyph type), so these
        // must reflect the CURRENT (possibly just-changed) atlas size.
        F32 inv_width = 1.f / font_bitmap_cache->getBitmapWidth();
        F32 inv_height = 1.f / font_bitmap_cache->getBitmapHeight();
        // Per-glyph bitmap texture.
        std::pair<EFontGlyphType, S32> next_bitmap_entry = fgi->mBitmapEntry;
        if (next_bitmap_entry != bitmap_entry || last_char != wch)
        {
            // Actually draw the queued glyphs before switching their texture;
            // otherwise the queued glyphs will be taken from wrong textures.
            if (glyph_count > 0)
            {
                submitGlyphBatch(vertices, uvs, colors, glyph_count * 6);
                glyph_count = 0;
            }

            bitmap_entry = next_bitmap_entry;
            LLImageGL* font_image = font_bitmap_cache->getImageGL(bitmap_entry.first, bitmap_entry.second);
            bindGlyphTexture(font_image);
            // S24 (2026-07-23): a diagnostic here (comparing this bind()'s
            // actual bound SRV against LLImageGL::setImage()'s own
            // per-instance call log) traced "no text" to
            // LLImageGL::setSubImage()'s full-image HACK branch calling
            // setImage() - which destroys/recreates the whole DXTexture -
            // on every single glyph addition. Fixed at the source
            // (llimagegl.cpp); this bind() was never the problem.

            // For some reason it's not enough to compare by bitmap_entry.
            // Issue hits emojis, japenese and chinese glyphs, only on first run.
            // Todo: figure it out, there might be a bug with raw image data.
            last_char = wch;
        }

        if ((start_x + scaled_max_pixels) < (cur_x + fgi->mXBearing + fgi->mWidth))
        {
            // Not enough room for this character.
            break;
        }

        // Calculate horizontal offset for tabular numbers (center narrow digits)
        F32 x_offset = 0.0f;
        if (mFontFreetype->getFontWeight() > 0 && fgi->mChar >= '0' && fgi->mChar <= '9' && mFontFreetype->getMaxDigitWidth() > 0.0f)
        {
            // use mXAdvance directly here, since we don't want to get max width instead.
            x_offset = (mFontFreetype->getMaxDigitWidth() - fgi->mXAdvance) * 0.5f;
        }

        // Draw the text at the appropriate location
        //Specify vertices and texture coordinates
        LLRectf uv_rect((fgi->mXBitmapOffset) * inv_width,
                (fgi->mYBitmapOffset + fgi->mHeight + PAD_UVY) * inv_height,
                (fgi->mXBitmapOffset + fgi->mWidth) * inv_width,
                (fgi->mYBitmapOffset - PAD_UVY) * inv_height);
        // snap glyph origin to whole screen pixel
        LLRectf screen_rect((F32)ll_round(cur_render_x + (F32)fgi->mXBearing + x_offset),
                    (F32)ll_round(cur_render_y + (F32)fgi->mYBearing),
                    (F32)ll_round(cur_render_x + (F32)fgi->mXBearing + x_offset) + (F32)fgi->mWidth,
                    (F32)ll_round(cur_render_y + (F32)fgi->mYBearing) - (F32)fgi->mHeight);

        if (glyph_count >= GLYPH_BATCH_SIZE)
        {
            submitGlyphBatch(vertices, uvs, colors, glyph_count * 6);
            glyph_count = 0;
        }

        const LLColor4U& col =
            bitmap_entry.first == EFontGlyphType::Grayscale ? text_color
                                                            : emoji_color;
        drawGlyph(glyph_count, vertices, uvs, colors, screen_rect, uv_rect,
                  col, style_to_add, shadow, drop_shadow_strength);

        chars_drawn++;
        cur_x += mFontFreetype->getXAdvance(fgi);
        cur_y += fgi->mYAdvance;

        llwchar next_char = wstr[i+1];
        if (next_char && (next_char < LAST_CHARACTER))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(next_char, (!use_color) ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }

        // Round after kerning.
        // Must do this to cur_x, not just to cur_render_x, otherwise you
        // will squish sub-pixel kerned characters too close together.
        // For example, "CCCCC" looks bad.
        cur_x = (F32)ll_round(cur_x);
        //cur_y = (F32)ll_round(cur_y);

        cur_render_x = cur_x;
        cur_render_y = cur_y;
    }

    submitGlyphBatch(vertices, uvs, colors, glyph_count * 6);

    if (right_x)
    {
        *right_x = (cur_x - origin.mV[VX]) / sScaleX;
    }

    //FIXME: add underline as glyph?
    if (style_to_add & UNDERLINE)
    {
        F32 descender = (F32)llfloor(mFontFreetype->getDescenderHeight());
        // text_color, not emoji_color (always opaque white, glyph-only) -
        // see submitUnderline()'s comment: previously implicit (GL's
        // immediate-mode "current color" carried forward from whichever
        // glyph was drawn last, which silently became emoji_color for any
        // string ending in an emoji).
        submitUnderline(start_x, cur_x, cur_y - descender, text_color);
    }

    if (draw_ellipses)
    {
        // recursively render ellipses at end of string
        // we've already reserved enough room
        static LLWString elipses_wstr(utf8string_to_wstring(std::string("...")));
        render(elipses_wstr,
                0,
                (cur_x - origin.mV[VX]) / sScaleX, (F32)y,
                color,
                LEFT, valign,
                style_to_add,
                shadow,
                S32_MAX, max_pixels,
                right_x,
                false,
                use_color);
    }

    endTextRender();

    return chars_drawn;
}

S32 LLFontGL::render(const LLWString &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color) const
{
    return render(text, begin_offset, x, y, color, LEFT, BASELINE, NORMAL, NO_SHADOW);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow, S32 max_chars, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color) const
{
    return render(utf8str_to_wstring(text), begin_offset, x, y, color, halign, valign, style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color) const
{
    return renderUTF8(text, begin_offset, (F32)x, (F32)y, color, LEFT, BASELINE, NORMAL, NO_SHADOW);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow) const
{
    return renderUTF8(text, begin_offset, (F32)x, (F32)y, color, halign, valign, style, shadow);
}

// font metrics - override for LLFontFreetype that returns units of virtual pixels
F32 LLFontGL::getAscenderHeight() const
{
    return mFontFreetype->getAscenderHeight() / sScaleY;
}

F32 LLFontGL::getDescenderHeight() const
{
    return mFontFreetype->getDescenderHeight() / sScaleY;
}

S32 LLFontGL::getLineHeight() const
{
    return llceil(mFontFreetype->getAscenderHeight() / sScaleY) + llceil(mFontFreetype->getDescenderHeight() / sScaleY);
}

S32 LLFontGL::getWidth(const std::string& utf8text) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidth(wtext.c_str(), 0, S32_MAX);
}

S32 LLFontGL::getWidth(const llwchar* wchars) const
{
    return getWidth(wchars, 0, S32_MAX);
}

S32 LLFontGL::getWidth(const std::string& utf8text, S32 begin_offset, S32 max_chars) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidth(wtext.c_str(), begin_offset, max_chars);
}

S32 LLFontGL::getWidth(const llwchar* wchars, S32 begin_offset, S32 max_chars) const
{
    F32 width = getWidthF32(wchars, begin_offset, max_chars);
    return ll_round(width);
}

F32 LLFontGL::getWidthF32(const std::string& utf8text) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidthF32(wtext.c_str(), 0, S32_MAX);
}

F32 LLFontGL::getWidthF32(const llwchar* wchars) const
{
    return getWidthF32(wchars, 0, S32_MAX);
}

F32 LLFontGL::getWidthF32(const std::string& utf8text, S32 begin_offset, S32 max_chars) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidthF32(wtext.c_str(), begin_offset, max_chars);
}

F32 LLFontGL::getWidthF32(const llwchar* wchars, S32 begin_offset, S32 max_chars, bool no_padding) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    const S32 LAST_CHARACTER = LLFontFreetype::LAST_CHAR_FULL;

    F32 cur_x = 0;
    const S32 max_index = begin_offset + max_chars;

    const LLFontGlyphInfo* next_glyph = NULL;

    F32 width_padding = 0.f;
    for (S32 i = begin_offset; i < max_index && wchars[i] != 0; i++)
    {
        llwchar wch = wchars[i];

        const LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);
        }

        F32 advance = mFontFreetype->getXAdvance(fgi);

        if (!no_padding)
        {
            // for the last character we want to measure the greater of its width and xadvance values
            // so keep track of the difference between these values for the each character we measure
            // so we can fix things up at the end
            width_padding = llmax(0.f,                                          // always use positive padding amount
                width_padding - advance,                        // previous padding left over after advance of current character
                (F32)(fgi->mWidth + fgi->mXBearing) - advance); // difference between width of this character and advance to next character
        }

        cur_x += advance;
        llwchar next_char = wchars[i+1];

        if (((i + 1) < begin_offset + max_chars)
            && next_char
            && (next_char < LAST_CHARACTER))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(next_char, EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }
        // Round after kerning.
        cur_x = (F32)ll_round(cur_x);
    }

    if (!no_padding)
    {
        // add in extra pixels for last character's width past its xadvance
        cur_x += width_padding;
    }

    return cur_x / sScaleX;
}

void LLFontGL::generateASCIIglyphs()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    for (U32 i = 32; (i < 127); i++)
    {
        mFontFreetype->getGlyphInfo(i, EFontGlyphType::Grayscale);
    }
}

// Returns the max number of complete characters from text (up to max_chars) that can be drawn in max_pixels
S32 LLFontGL::maxDrawableChars(const llwchar* wchars, F32 max_pixels, S32 max_chars, EWordWrapStyle end_on_word_boundary) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (!wchars || !wchars[0] || max_chars == 0)
    {
        return 0;
    }

    llassert(max_pixels >= 0.f);
    llassert(max_chars >= 0);

    bool clip = false;
    F32 cur_x = 0;

    S32 start_of_last_word = 0;
    bool in_word = false;

    // avoid S32 overflow when max_pixels == S32_MAX by staying in floating point
    F32 scaled_max_pixels = max_pixels * sScaleX;
    F32 width_padding = 0.f;

    LLFontGlyphInfo* next_glyph = NULL;

    S32 i;
    for (i=0; (i < max_chars); i++)
    {
        llwchar wch = wchars[i];

        if(wch == 0)
        {
            // Null terminator.  We're done.
            break;
        }

        if (in_word)
        {
            if (iswspace(wch))
            {
                if(wch !=(0x00A0))
                {
                    in_word = false;
                }
            }
            if (iswindividual(wch))
            {
                if (iswpunct(wchars[i+1]))
                {
                    in_word=true;
                }
                else
                {
                    in_word=false;
                    start_of_last_word = i;
                }
            }
        }
        else
        {
            start_of_last_word = i;
            if (!iswspace(wch)||!iswindividual(wch))
            {
                in_word = true;
            }
        }

        LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);

            if (NULL == fgi)
            {
                return 0;
            }
        }

        // account for glyphs that run beyond the starting point for the next glyphs
        width_padding = llmax(  0.f,                                                    // always use positive padding amount
                                width_padding - fgi->mXAdvance,                         // previous padding left over after advance of current character
                                (F32)(fgi->mWidth + fgi->mXBearing) - fgi->mXAdvance);  // difference between width of this character and advance to next character

        cur_x += fgi->mXAdvance;

        // clip if current character runs past scaled_max_pixels (using width_padding)
        if (scaled_max_pixels < cur_x + width_padding)
        {
            clip = true;
            break;
        }

        if (((i+1) < max_chars) && wchars[i+1])
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(wchars[i+1], EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }

        // Round after kerning.
        cur_x = (F32)ll_round(cur_x);
    }

    if( clip )
    {
        switch (end_on_word_boundary)
        {
        case ONLY_WORD_BOUNDARIES:
            i = start_of_last_word;
            break;
        case WORD_BOUNDARY_IF_POSSIBLE:
            if (start_of_last_word != 0)
            {
                i = start_of_last_word;
            }
            break;
        default:
        case ANYWHERE:
            // do nothing
            break;
        }
    }
    return i;
}

S32 LLFontGL::firstDrawableChar(const llwchar* wchars, F32 max_pixels, S32 text_len, S32 start_pos, S32 max_chars) const
{
    if (!wchars || !wchars[0] || max_chars == 0)
    {
        return 0;
    }

    F32 total_width = 0.0;
    S32 drawable_chars = 0;

    F32 scaled_max_pixels = max_pixels * sScaleX;

    S32 start = llmin(start_pos, text_len - 1);
    for (S32 i = start; i >= 0; i--)
    {
        llwchar wch = wchars[i];

        const LLFontGlyphInfo* fgi= mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);

        // last character uses character width, since the whole character needs to be visible
        // other characters just use advance
        F32 width = (i == start)
            ? (F32)(fgi->mWidth + fgi->mXBearing)   // use actual width for last character
            : fgi->mXAdvance;                       // use advance for all other characters

        if( scaled_max_pixels < (total_width + width) )
        {
            break;
        }

        total_width += width;
        drawable_chars++;

        if( max_chars >= 0 && drawable_chars >= max_chars )
        {
            break;
        }

        if ( i > 0 )
        {
            // kerning
            total_width += mFontFreetype->getXKerning(wchars[i-1], wch);
        }

        // Round after kerning.
        total_width = (F32)ll_round(total_width);
    }

    if (drawable_chars == 0)
    {
        return start_pos; // just draw last character
    }
    else
    {
        // if only 1 character is drawable, we want to return start_pos as the first character to draw
        // if 2 are drawable, return start_pos and character before start_pos, etc.
        return start_pos + 1 - drawable_chars;
    }

}

S32 LLFontGL::charFromPixelOffset(const llwchar* wchars, S32 begin_offset, F32 target_x, F32 max_pixels, S32 max_chars, bool round) const
{
    if (!wchars || !wchars[0] || max_chars == 0)
    {
        return 0;
    }

    F32 cur_x = 0;

    target_x *= sScaleX;

    // max_chars is S32_MAX by default, so make sure we don't get overflow
    const S32 max_index = begin_offset + llmin(S32_MAX - begin_offset, max_chars - 1);

    F32 scaled_max_pixels = max_pixels * sScaleX;

    const LLFontGlyphInfo* next_glyph = NULL;

    S32 pos;
    for (pos = begin_offset; pos < max_index; pos++)
    {
        llwchar wch = wchars[pos];
        if (!wch)
        {
            break; // done
        }

        const LLFontGlyphInfo* glyph = next_glyph;
        next_glyph = NULL;
        if(!glyph)
        {
            glyph = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);
        }

        F32 char_width = mFontFreetype->getXAdvance(glyph);

        if (round)
        {
            // Note: if the mouse is on the left half of the character, the pick is to the character's left
            // If it's on the right half, the pick is to the right.
            if (target_x  < cur_x + char_width*0.5f)
            {
                break;
            }
        }
        else if (target_x  < cur_x + char_width)
        {
            break;
        }

        if (scaled_max_pixels < cur_x + char_width)
        {
            break;
        }

        cur_x += char_width;

        if (((pos + 1) < max_index)
            && (wchars[(pos + 1)]))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(wchars[pos + 1], EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(glyph, next_glyph);
        }


        // Round after kerning.
        cur_x = (F32)ll_round(cur_x);
    }

    return llmin(max_chars, pos - begin_offset);
}

const LLFontDescriptor& LLFontGL::getFontDesc() const
{
    return mFontDescriptor;
}

// static
void LLFontGL::initClass(F32 screen_dpi, F32 x_scale, F32 y_scale, const std::string& app_dir, bool create_gl_textures)
{
    sVertDPI = (F32)llfloor(screen_dpi * y_scale);
    sHorizDPI = (F32)llfloor(screen_dpi * x_scale);
    sScaleX = x_scale;
    sScaleY = y_scale;
    sAppDir = app_dir;

    // Font registry init
    if (!sFontRegistry)
    {
        sFontRegistry = new LLFontRegistry(create_gl_textures);
        sFontRegistry->parseFontInfo("fonts.xml");
    }
    else
    {
        sFontRegistry->reset();
    }

    LLFontGL::loadDefaultFonts();
}

void LLFontGL::dumpTextures()
{
    if (mFontFreetype.notNull())
    {
        mFontFreetype->dumpFontBitmaps();
    }
}

// static
void LLFontGL::dumpFonts()
{
    sFontRegistry->dump();
}

// static
void LLFontGL::dumpFontTextures()
{
    sFontRegistry->dumpTextures();
}

// Force standard fonts to get generated up front.
// This is primarily for error detection purposes.
// Don't do this during initClass because it can be slow and we want to get
// the viewer window on screen first. JC
// static
bool LLFontGL::loadDefaultFonts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    bool succ = true;
    succ &= (NULL != getFontSansSerifSmall());
    succ &= (NULL != getFontSansSerif());
    succ &= (NULL != getFontSansSerifBig());
    succ &= (NULL != getFontSansSerifHuge());
    succ &= (NULL != getFontSansSerifBold());
    succ &= (NULL != getFontMonospace());
    return succ;
}

void LLFontGL::loadCommonFonts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    getFont(LLFontDescriptor("SansSerif", "Small", BOLD));
    getFont(LLFontDescriptor("SansSerif", "Large", BOLD));
    getFont(LLFontDescriptor("SansSerif", "Huge", BOLD));
    getFont(LLFontDescriptor("Monospace", "Medium", 0));
}

// static
void LLFontGL::destroyDefaultFonts()
{
    // Remove the actual fonts.
    delete sFontRegistry;
    sFontRegistry = NULL;
}

//static
void LLFontGL::destroyAllGL()
{
    if (sFontRegistry)
    {
        sFontRegistry->destroyGL();
    }
}

// static
U8 LLFontGL::getStyleFromString(const std::string &style)
{
    S32 ret = 0;
    if (style.find("BOLD") != style.npos)
    {
        ret |= BOLD;
    }
    if (style.find("ITALIC") != style.npos)
    {
        ret |= ITALIC;
    }
    if (style.find("UNDERLINE") != style.npos)
    {
        ret |= UNDERLINE;
    }
    return ret;
}

// static
std::string LLFontGL::getStringFromStyle(U8 style)
{
    std::string style_string;
    if (style == NORMAL)
    {
        style_string += "|NORMAL";
    }
    if (style & BOLD)
    {
        style_string += "|BOLD";
    }
    if (style & ITALIC)
    {
        style_string += "|ITALIC";
    }
    if (style & UNDERLINE)
    {
        style_string += "|UNDERLINE";
    }
    return style_string;
}

// static
std::string LLFontGL::nameFromFont(const LLFontGL* fontp)
{
    return fontp->mFontDescriptor.getName();
}


// static
std::string LLFontGL::sizeFromFont(const LLFontGL* fontp)
{
    return fontp->mFontDescriptor.getSize();
}

// static
std::string LLFontGL::nameFromHAlign(LLFontGL::HAlign align)
{
    if (align == LEFT)          return std::string("left");
    else if (align == RIGHT)    return std::string("right");
    else if (align == HCENTER)  return std::string("center");
    else return std::string();
}

// static
LLFontGL::HAlign LLFontGL::hAlignFromName(const std::string& name)
{
    LLFontGL::HAlign gl_hfont_align = LLFontGL::LEFT;
    if (name == "left")
    {
        gl_hfont_align = LLFontGL::LEFT;
    }
    else if (name == "right")
    {
        gl_hfont_align = LLFontGL::RIGHT;
    }
    else if (name == "center")
    {
        gl_hfont_align = LLFontGL::HCENTER;
    }
    //else leave left
    return gl_hfont_align;
}

// static
std::string LLFontGL::nameFromVAlign(LLFontGL::VAlign align)
{
    if (align == TOP)           return std::string("top");
    else if (align == VCENTER)  return std::string("center");
    else if (align == BASELINE) return std::string("baseline");
    else if (align == BOTTOM)   return std::string("bottom");
    else return std::string();
}

// static
LLFontGL::VAlign LLFontGL::vAlignFromName(const std::string& name)
{
    LLFontGL::VAlign gl_vfont_align = LLFontGL::BASELINE;
    if (name == "top")
    {
        gl_vfont_align = LLFontGL::TOP;
    }
    else if (name == "center")
    {
        gl_vfont_align = LLFontGL::VCENTER;
    }
    else if (name == "baseline")
    {
        gl_vfont_align = LLFontGL::BASELINE;
    }
    else if (name == "bottom")
    {
        gl_vfont_align = LLFontGL::BOTTOM;
    }
    //else leave baseline
    return gl_vfont_align;
}

//static
LLFontGL* LLFontGL::getFontEmojiSmall()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Small", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontEmojiMedium()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Medium", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontEmojiLarge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Large", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontEmojiHuge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Huge", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontMonospace()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Monospace","Monospace",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmall()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmallBold()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",BOLD));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmallItalic()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",ITALIC));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerif()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",0));
    return fontp;
}

// static
LLFontGL* LLFontGL::getFontSansSerifMedium()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Medium",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifBig()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Large",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifHuge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Huge",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifBold()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Medium",BOLD));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFont(const LLFontDescriptor& desc)
{
    return sFontRegistry->getFont(desc);
}

//static
LLFontGL* LLFontGL::getFontByName(const std::string& name)
{
    // check for most common fonts first
    if (name == "SANSSERIF")
    {
        return getFontSansSerif();
    }
    else if (name == "SANSSERIF_SMALL")
    {
        return getFontSansSerifSmall();
    }
    else if (name == "SANSSERIF_BIG")
    {
        return getFontSansSerifBig();
    }
    else if (name == "SMALL" || name == "OCRA")
    {
        // *BUG: Should this be "MONOSPACE"?  Do we use "OCRA" anymore?
        // Does "SMALL" mean "SERIF"?
        return getFontMonospace();
    }
    else
    {
        return NULL;
    }
}

//static
LLFontGL* LLFontGL::getFontDefault()
{
    return getFontSansSerif(); // Fallback to sans serif as default font
}


// static
std::string LLFontGL::getFontPathSystem()
{
    auto system_root = LLStringUtil::getenv("SystemRoot");
    if (! system_root.empty())
    {
        std::string fontpath(gDirUtilp->add(system_root, "fonts") + gDirUtilp->getDirDelimiter());
        LL_INFOS() << "from SystemRoot: " << fontpath << LL_ENDL;
        return fontpath;
    }

    wchar_t *pwstr = NULL;
    HRESULT okay = SHGetKnownFolderPath(FOLDERID_Fonts, 0, NULL, &pwstr);
    if (SUCCEEDED(okay) && pwstr)
    {
        std::string fontpath(ll_convert_wide_to_string(pwstr));
        // SHGetKnownFolderPath() contract requires us to free pwstr
        CoTaskMemFree(pwstr);
        LL_INFOS() << "from SHGetKnownFolderPath(): " << fontpath << LL_ENDL;
        return fontpath;
    }

    LL_WARNS() << "Could not determine system fonts path" << LL_ENDL;
    return {};
}


// static
std::string LLFontGL::getFontPathLocal()
{
    std::string local_path;

    // Backup files if we can't load from system fonts directory.
    // We could store this in an end-user writable directory to allow
    // end users to switch fonts.
    if (LLFontGL::sAppDir.length())
    {
        // use specified application dir to look for fonts
        local_path = LLFontGL::sAppDir + "/fonts/";
    }
    else
    {
        // assume working directory is executable directory
        local_path = "./fonts/";
    }
    return local_path;
}

LLFontGL::LLFontGL(const LLFontGL &source)
{
    LL_ERRS() << "Not implemented!" << LL_ENDL;
}

LLFontGL &LLFontGL::operator=(const LLFontGL &source)
{
    LL_ERRS() << "Not implemented" << LL_ENDL;
    return *this;
}

void LLFontGL::renderTriangle(LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, F32 slant_amt) const
{
    S32 index = 0;

    vertex_out[index].set(screen_rect.mRight, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mBottom);
    colors_out[index] = color;
    index++;


    vertex_out[index].set(screen_rect.mRight, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mBottom);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mRight, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mBottom);
    colors_out[index] = color;
}

void LLFontGL::drawGlyph(S32& glyph_count, LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, U8 style, ShadowType shadow, F32 drop_shadow_strength) const
{
    F32 slant_offset;
    slant_offset = ((style & ITALIC) ? ( -mFontFreetype->getAscenderHeight() * 0.2f) : 0.f);

    //FIXME: bold and drop shadow are mutually exclusive only for convenience
    //Allow both when we need them.
    if (style & BOLD)
    {
        for (S32 pass = 0; pass < 2; pass++)
        {
            LLRectf screen_rect_offset = screen_rect;

            screen_rect_offset.translate((F32)(pass * BOLD_OFFSET), 0.f);
            renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_offset, uv_rect, color, slant_offset);
            glyph_count++;
        }
    }
    else if (shadow == DROP_SHADOW_SOFT)
    {
        LLColor4U shadow_color = LLFontGL::sShadowColor;
        shadow_color.mV[VALPHA] = U8(color.mV[VALPHA] * drop_shadow_strength * DROP_SHADOW_SOFT_STRENGTH);
        for (S32 pass = 0; pass < 5; pass++)
        {
            LLRectf screen_rect_offset = screen_rect;

            switch(pass)
            {
            case 0:
                screen_rect_offset.translate(-1.f, -1.f);
                break;
            case 1:
                screen_rect_offset.translate(1.f, -1.f);
                break;
            case 2:
                screen_rect_offset.translate(1.f, 1.f);
                break;
            case 3:
                screen_rect_offset.translate(-1.f, 1.f);
                break;
            case 4:
                screen_rect_offset.translate(0, -2.f);
                break;
            }

            renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_offset, uv_rect, shadow_color, slant_offset);
            glyph_count++;
        }
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
    else if (shadow == DROP_SHADOW)
    {
        LLColor4U shadow_color = LLFontGL::sShadowColor;
        shadow_color.mV[VALPHA] = U8(color.mV[VALPHA] * drop_shadow_strength);
        LLRectf screen_rect_shadow = screen_rect;
        screen_rect_shadow.translate(1.f, -1.f);
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_shadow, uv_rect, shadow_color, slant_offset);
        glyph_count++;
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
    else // normal rendering
    {
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
}
