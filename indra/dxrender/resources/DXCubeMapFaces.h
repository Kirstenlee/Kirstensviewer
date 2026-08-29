#pragma once

// S24 (2026-08-22, plan item A1 - ATTEMPTED AND REVERTED, see
// DXCubeMapFaces.cpp for the full story): tried re-deriving this table to
// match GL's look_upvecs exactly (no negation), on the reasoning that
// Microsoft's documented D3D11 TextureCube face-addressing convention is
// the same table OpenGL uses, so no per-API handedness difference should
// exist here. Live-tested and REJECTED: broke face SELECTION (not just
// within-face orientation), a regression from the working state below -
// user-confirmed "jumbled" content. Root cause: this table's raw-capture
// convention feeds directly into LLCubeMapArray::sClipToCubeLookVecs/
// sClipToCubeUpVecs (llrender/llcubemaparray.cpp), a separate,
// independently hand-tuned resample-stage table that was calibrated
// against THIS table's old (negated) values - changing one without
// co-deriving the other breaks the pair. The underlying reasoning (no
// genuine D3D11/GL addressing difference; a single row-order compensation
// should suffice) may still be correct, but implementing it requires
// re-deriving the resample-stage table in the same pass, not changing this
// one in isolation. Do not re-attempt without also solving that half.
//
// Reverted to the prior, working state below (task #194, 2026-08-13
// original reasoning) until both tables can be co-derived together.
struct DXCubeMapFaces
{
    // Order matches LLCubeMapArray::sTargets / this project's established
    // face-index convention: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
    static const float sUpVecs[6][3];
};
