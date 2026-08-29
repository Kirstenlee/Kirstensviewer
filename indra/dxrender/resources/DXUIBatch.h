#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>
#include "DXBuffer.h"

// Native D3D11 2D-quad/text batch renderer - see the DXUIBatch plan (project
// memory + the session plan file that introduced it). 2D UI/text never needs
// LLVertexBuffer's general-purpose, up-to-11-input-slot struct-of-arrays
// machinery (built for normals/tangents/skinning weights/texture indices
// that 2D UI never uses) - this is the dedicated, minimal alternative:
// one interleaved vertex format, one input slot, no index buffer.
//
// One interleaved vertex matching interface/uiV.hlsl's VSInput exactly
// (POSITION float3 @0, COLOR0 R8G8B8A8_UNORM @12, TEXCOORD0 float2 @16,
// stride 24 bytes) - see DXVertexLayout::getOrCreateUILayout().
struct DXUIVertex
{
    float pos[3];
    uint8_t color[4];
    float uv[2];
};

// Accepts flat triangle-list geometry - the same shape the existing,
// already-correct GL-era geometry construction code already produces
// (LLFontGL::drawGlyph()/renderTriangle(), llrender2dutils.cpp's
// gl_draw_scaled_rotated_image()/gl_draw_scaled_image_with_border()), so
// callers translate their existing pos/uv/color arrays into DXUIVertex
// directly rather than this class re-deriving a dedup'd vertex+index
// representation from code that's already correct as flat triangles.
// One process-wide instance (gDXUIBatch) - not per-widget, mirroring
// LLRender's own single-instance (gGL) immediate-mode buffer.
// Mirrors the handful of GL primitive topologies llrender2dutils.cpp's
// gGL.begin(X) calls actually use, restricted to what D3D11 natively
// supports with no CPU-side reshaping (see DXRender2DUtils.h's header
// comment for GL_LINE_LOOP/GL_TRIANGLE_FAN, which have no native D3D11
// equivalent and are expanded to LineStrip/TriangleList by the caller
// before reaching push()/flush() at all).
enum class DXUITopology
{
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
};

// S24 (2026-08-16): real cross-primitive batching, replacing the previous
// "draw immediately every single flush() call" design that made 2D UI cost
// scale linearly with widget count (a floater with hundreds of rects/icons/
// text labels meant hundreds of full D3D11 state-replay Draw() calls per
// frame - confirmed root cause of the UI-floater framerate floor). Every
// existing push()+flush(...) call site (21 across llrender2dutils.cpp/
// llfontgl.cpp) is UNCHANGED - flush()'s signature and call pattern stay
// exactly the same; what changed is what it does internally:
//
// flush() now compares the (shader, topology, alpha_blend, depth) state it
// was called with against whatever's currently pending. If it matches (or
// nothing's pending yet), the just-pushed vertices simply extend the
// pending batch - no draw happens. If it differs, the OLD pending batch is
// drawn first (using ITS OWN remembered state), then a new pending batch
// starts with the vertices just pushed for the new state. This turns
// "hundreds of draws, one per primitive" into "a handful of draws, one per
// actual state change" - state changes are rare in practice (per-file
// investigation: alpha_blend is true and depth params are default at every
// one of the 21 existing call sites bar one HUD case, so real-world batches
// are usually keyed on shader+topology alone, i.e. one whole floater's
// worth of same-shader rects/text often merges into ONE draw call).
//
// flushPending() is new: unconditionally draws whatever's pending (if
// anything) right now. This is what every interleaving-hazard hook (scissor
// rect changes, depth-state changes, blend/cull changes, the separate
// LLRender/gGL immediate-mode queue's own flush, texture changes, shader
// bind/unbind, HUD-vs-screen-space pass boundaries) calls BEFORE actually
// changing D3D11 state out from under a still-pending batch - without these
// hooks, deferring flush() would risk silent draw-order corruption. See the
// DXUIBatch batching plan (2026-08-16 session) for the full hazard audit
// and the complete list of hook sites.
class DXUIBatch
{
public:
    // Appends `count` already-built vertices in whatever topology `flush()`
    // will be called with (multiple of 3 for TriangleList, no fixed multiple
    // for strips/lines) - not enforced here, same convention as
    // LLRender::vertexBatchPreTransformed(). Every real call site does
    // exactly one push() immediately followed by one flush() - flush()
    // relies on that pairing to know how many of the tail-end vertices in
    // the shared buffer belong to the primitive it's being called for.
    void push(const DXUIVertex* vertices, size_t count);

    // Same signature/contract as before (see git history for the pre-
    // batching version's full rationale on why vs/ps/depth params are
    // explicit, required arguments rather than trusted ambient state) -
    // internally now defers the actual Draw() until the batch state
    // changes. See this class's top comment.
    void flush(const void* vs_bytecode, size_t vs_bytecode_size, ID3D11VertexShader* vs, ID3D11PixelShader* ps, bool alpha_blend, const char* debug_name = nullptr, DXUITopology topology = DXUITopology::TriangleList,
        bool depth_test = false, bool depth_write = false, D3D11_COMPARISON_FUNC depth_func = D3D11_COMPARISON_LESS_EQUAL);

    // Unconditionally draws whatever's currently pending (no-op if nothing
    // is). Call this before anything outside this class's own push()/
    // flush() pair could change D3D11 pipeline state that a still-pending
    // batch depends on (shader, textures, blend/depth/scissor state,
    // topology) - see this class's top comment for the full hazard list.
    void flushPending();

    // S24 (task #224, diagnostics removed 2026-08-25): batching/deferral was
    // ruled out as the flicker's cause via a live bypass-toggle test - the
    // real bug was LLUIImage's display-list cache (see lluiimage.cpp), now
    // off by default.

private:
    struct BatchState
    {
        ID3D11VertexShader* vs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        const void* vsBytecode = nullptr;
        size_t vsBytecodeSize = 0;
        const char* debugName = nullptr;
        DXUITopology topology = DXUITopology::TriangleList;
        bool alphaBlend = true;
        bool depthTest = false;
        bool depthWrite = true;
        D3D11_COMPARISON_FUNC depthFunc = D3D11_COMPARISON_LESS_EQUAL;

        bool sameDrawState(const BatchState& other) const
        {
            return vs == other.vs && ps == other.ps && topology == other.topology &&
                alphaBlend == other.alphaBlend && depthTest == other.depthTest &&
                depthWrite == other.depthWrite && depthFunc == other.depthFunc;
        }
    };

    // Draws the first `count` vertices of mVertices using `state`, then
    // erases them from the front (any remaining tail vertices - i.e. the
    // ones just pushed under a NEW, different state - shift down to index 0
    // and become the start of the next pending batch).
    void drawAndPop(size_t count, const BatchState& state);

    std::vector<DXUIVertex> mVertices;
    DXBuffer mVertexBuffer;
    size_t mVertexCapacity = 0; // in DXUIVertex units, current GPU buffer size

    bool mHasPending = false;
    BatchState mPendingState;
    size_t mLastPushCount = 0; // vertex count added by the most recent push() call
};

extern DXUIBatch gDXUIBatch;
