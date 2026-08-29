#pragma once
#include <d3d11.h>

// D3D11 equivalent of a GL fence sync object (glFenceSync/glClientWaitSync/
// glDeleteSync) - tracks when the GPU has actually finished executing up to
// a given point in the command stream, as opposed to just CPU submission.
// Deliberately mirrors GL's manual-handle-lifetime style (create/poll/
// release on a raw opaque handle) rather than RAII, since callers already
// track fence lifetime themselves in a queue (see LLViewerStats::
// notifyFrameSubmitted()/checkGPUFrameCompletion() - real GPU-completion-
// based FPS/frametime telemetry, task #98).
class DXQuery
{
public:
    // Creates a new D3D11 event query and issues it (End()) at the current
    // point in the command stream. Returns nullptr on failure (device lost,
    // out of memory, etc.) - non-fatal, caller should just skip tracking
    // this frame.
    static ID3D11Query* issue();

    // Non-blocking check: true once the GPU has completed everything up to
    // and including the matching issue() call. False either means "not
    // signaled yet" or "invalid handle" - callers poll this once per frame
    // rather than stalling the CPU waiting on the GPU.
    static bool isComplete(ID3D11Query* query);

    // Releases a handle returned by issue(). Safe to call with nullptr.
    static void release(ID3D11Query* query);
};
