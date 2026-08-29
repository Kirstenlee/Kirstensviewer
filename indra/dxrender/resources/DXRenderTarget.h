#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>

// Parallel to LLRenderTarget (llrender/llrendertarget.h, a GL FBO wrapper) -
// up to 4 color attachments plus one depth/stencil buffer, matching
// LLRenderTarget's write-side feature set (allocate/addColorAttachment/
// allocateDepth/shareDepthBuffer/bindTarget/clear/flush/release) and its
// bindTarget()/flush() nesting-stack behavior.
//
// The read side is a documented, deliberate gap: LLRenderTarget::
// bindTexture()/getTexture() (sampling a render target as an input texture,
// used by later lighting/post-process passes) has no DX_RENDER equivalent
// yet, since no such pass has been converted so far - only the G-buffer
// write pass (LLDrawPoolSimple) has. getColorSRV()/getDepthSRV() below exist
// for when that need arrives, but nothing currently calls them.
class DXRenderTarget
{
public:
    bool allocate(uint32_t width, uint32_t height, DXGI_FORMAT color_format, bool depth);
    bool addColorAttachment(DXGI_FORMAT color_format); // up to 4 total, matching GL_MAX_COLOR_ATTACHMENTS usage
    bool allocateDepth(); // fixed DXGI_FORMAT_D24_UNORM_S8_UINT, matching this codebase's fixed GL_DEPTH_COMPONENT24 usage
    void shareDepthBuffer(DXRenderTarget& target); // `target` adopts this's depth buffer (does not own it)
    void resize(uint32_t width, uint32_t height); // destroys and recreates all attachments at the new size (D3D11 resources can't resize in place)
    void release();

    // Binds all color attachments (in slot order) + depth (if any) via
    // OMSetRenderTargets and sets the viewport. Deliberately has no bind-
    // stack/"restore previous" logic of its own - LLRenderTarget already
    // has that (mPreviousRT/sBoundTarget), so LLRenderTarget::flush()'s
    // DX_RENDER branch just calls bindTarget() again on whichever
    // LLRenderTarget it finds up its own chain (or bindSwapChainBackBuffer()
    // at the bottom of the stack). Keeping only one stack implementation
    // avoids two parallel, easily-desynced copies of the same state.
    // bind_depth=false binds color attachments only (DSV slot left null) -
    // needed by any pass that writes into this target's color while ALSO
    // sampling this target's (possibly shared) depth buffer as an SRV in
    // the same draw: D3D11 forbids a resource being bound as an OM output
    // and a shader-stage input simultaneously and silently forces the SRV
    // to NULL if so (debug layer: "Resource being set to PS shader
    // resource slot N is still bound on output! Forcing to NULL.") - GL
    // doesn't enforce this the same way, which is why the original GL body
    // never needed an equivalent flag.
    //
    // S24 (2026-08-15): read_only_depth=true binds mReadOnlyDSV instead of
    // mDSV (falls back to mDSV if the read-only view failed to create) -
    // D3D11 explicitly permits a depth-stencil view created with the
    // D3D11_DSV_READ_ONLY_* flags to be bound simultaneously with an SRV on
    // the same underlying resource, which bind_depth=false's SRV-priority
    // approach above can't offer (it sacrifices depth-TESTING entirely,
    // not just the write). Needed for any pass that must depth-test against
    // already-written scene depth (write is never wanted here anyway -
    // LLGLDepthTest's write_enabled=false) while ALSO reading that same
    // depth as an SRV for world-position reconstruction - exactly
    // DXPipeline's local-light pass (pointLightF.hlsl/spotLightF.hlsl's
    // getPosition()/getDepth()), which bind_depth=false left with no depth
    // occlusion against opaque geometry at all (lights bled through walls -
    // found via adversarial review of the "state matches GL" diagnostic,
    // which only compared depth-stencil STATE OBJECTS and was blind to the
    // OM attachment itself being null).
    // S24 (2026-08-17, task #174): clear_color draws from mClearColor
    // (defaults to black - same as the old hardcoded behavior for any
    // target that's never called clearColor()) instead of always hardcoding
    // black. GL's own LLRenderTarget::clear() honors whatever ambient
    // glClearColor(r,g,b,a) the caller set beforehand; D3D11 has no such
    // ambient state, so this class remembers the last color explicitly set
    // via clearColor() below and reuses it on every later clear() call -
    // set it once (e.g. right after allocate()), not before every single
    // clear(). See DXRenderTarget.cpp's clear() for the real user-visible
    // symptom this closes (login-screen strobing from mExposureMap's
    // releaseGLBuffers()/createGLBuffers() cycling clearing to hardcoded
    // black instead of its intended neutral white).
    void bindTarget(bool bind_depth = true, bool read_only_depth = false);
    void clear(bool clear_color, bool clear_depth);

    // Sets mClearColor (used by every FUTURE clear(true, ...) call on this
    // target, not just this one) and performs an immediate clear right now
    // - same two-in-one contract as before, just no longer one-shot. See
    // the call site in DXPipeline::renderDeferredLighting() for the
    // original reason this exists (a render target that's a stand-in
    // "neutral" input for a not-yet-built pass needs a specific non-black
    // fill, not clear()'s old hardcoded black).
    void clearColor(float r, float g, float b, float a);

    // The DX_RENDER equivalent of GL's "bind FBO 0" - restores the swap
    // chain back buffer + full-window viewport. Static since it doesn't
    // belong to any particular DXRenderTarget instance.
    static void bindSwapChainBackBuffer();

    uint32_t getWidth() const { return mWidth; }
    uint32_t getHeight() const { return mHeight; }
    size_t getNumColorAttachments() const { return mColor.size(); }

    // Read-side accessors - see class comment. Not yet called by anything.
    ID3D11ShaderResourceView* getColorSRV(size_t index) const;
    ID3D11ShaderResourceView* getDepthSRV() const { return mDepthSRV; }

    // S24 (2026-08-17): raw texture accessor - needed by callers doing
    // direct CPU<->GPU pixel transfer (DXReadback::readPixels()/
    // writePixels()) rather than sampling the attachment as a shader input
    // (getColorSRV()'s job). First real caller: KVOpenCL's GPU post-fx
    // effects (kveffects.cpp) - see that file's DX_RENDER branch.
    ID3D11Texture2D* getColorTexture(size_t index) const;

    // S24 (2026-08-26, task #263): depth counterpart to getColorTexture()
    // above, same rationale - direct CPU<->GPU transfer (DXReadback::
    // readDepthPixels()) rather than shader sampling (getDepthSRV()'s job).
    // First real caller: LLViewerWindow::rawSnapshot()'s depth-snapshot path
    // reading pipeline.mRT->deferredScreen directly instead of the removed
    // scratch_space indirection.
    ID3D11Texture2D* getDepthTexture() const { return mDepthTexture; }

private:
    struct Attachment
    {
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    void releaseColorAttachments();
    void releaseDepth();

    std::vector<Attachment> mColor;

    // S24 (2026-08-17, task #174): see clear()/clearColor()'s own comments.
    float mClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    DXGI_FORMAT mDepthFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D* mDepthTexture = nullptr;
    ID3D11DepthStencilView* mDSV = nullptr;
    // S24 (2026-08-15): same underlying mDepthTexture as mDSV, created with
    // D3D11_DSV_READ_ONLY_DEPTH|STENCIL - see bindTarget()'s own comment.
    // May be null (creation failure is non-fatal - bindTarget() falls back
    // to mDSV, same behavior as before this existed).
    ID3D11DepthStencilView* mReadOnlyDSV = nullptr;
    ID3D11ShaderResourceView* mDepthSRV = nullptr;
    // true if this target owns (created) mDepthTexture/mDSV; false if they
    // were adopted from another target via shareDepthBuffer() - release()
    // must not destroy a depth buffer this target doesn't own.
    bool mOwnsDepth = true;

    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
};
