/**
 * @file class1/objects/indexedTextureV.hlsl
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

// GLSL links this against the calling V shader as global "in"/"out" scope
// (texture_index is a real vertex attribute, vary_texture_index is passed to
// the fragment stage). HLSL has no cross-file global linkage - unlike
// weight4/getObjectSkinnedTransform() (where the static global + bridging
// assignment can be prepended anywhere and just needs to exist somewhere
// before use), this file is an *attached utility* positioned after the
// entry file's own main() in concatenation order, so a declaration placed
// here would not be visible to it. DXShader::injectTextureIndexInputs()
// (dxrender/resources/DXShader.cpp) detects this function's real body
// (distinct from nonindexedTextureV.hlsl's no-op) and prepends the
// static int texture_index;/vary_texture_index; declarations to the very
// front of the concatenated source instead - same technique
// injectSkinningInputs() uses for weight4/weight, just done here in C++
// rather than in this file's own text.

void passTextureIndex()
{
    vary_texture_index = texture_index;
}
