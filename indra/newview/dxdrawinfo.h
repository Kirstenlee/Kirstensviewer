/**
 * @file dxdrawinfo.h
 * @brief Per-face slice of a draw batch, used by the DX alpha pool to sort and draw faces individually.
 */

#pragma once

class LLFace;

// A face's range inside its batch's vertex and index buffers. The batch keeps its shared state
// (texture, material, shader); each record only says which part of the buffer belongs to one face.
struct DXDrawFace
{
    LLFace* face = nullptr;
    U32     indexOffset = 0;    // first index within the batch's index buffer
    U32     indexCount = 0;
    U32     vertStart = 0;      // first vertex within the batch's vertex buffer
    U32     vertEnd = 0;        // last vertex, inclusive
    F32     alpha = 1.f;        // texture-entry alpha; near 1 means the face is effectively opaque
};
