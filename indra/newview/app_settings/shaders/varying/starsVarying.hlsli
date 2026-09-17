/**
 * @file varying/starsVarying.hlsli
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

// Shared vertex-to-pixel varying struct for class1/deferred/starsV.hlsl/
// starsF.hlsl (the ALM-on variant) - see varying/uiVarying.hlsli's comment
// for why this exists.
//
// S24 (2026-08-29, task #279 "RENDER WOW"): star_seed added - see
// starsV.hlsl's comment for how it's derived and starsF.hlsl's for how it's
// used (per-star color/twinkle variety). screenpos is no longer read
// anywhere (it drove the old twinkle() function's screen-position-based
// flicker, replaced by a real time+seed-based sinusoidal twinkle) - kept
// as-is rather than removed, in case a future GL-side port of this same
// upgrade wants it back for parity; genuinely dead weight otherwise.
// galactic_band (the old per-star recolor approach to the Milky Way band)
// was removed once the dust-haze blob system replaced it; that sprite-blob
// system was itself later replaced by a fully separate procedural noise
// band on the sky dome mesh - see dxdrawpoolwlsky.cpp's
// renderGalacticBandDeferred()/galacticBandV.hlsl.
struct StarsVarying
{
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
    float2 screenpos : TEXCOORD1;
    float star_seed : TEXCOORD2;
};
