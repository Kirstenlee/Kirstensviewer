#include "DXCubeMapFaces.h"

// S24 (2026-08-22, plan item A1 - REVERTED): the re-derivation attempted
// here (matching GL exactly, no negation) broke face SELECTION, not just
// within-face orientation - user-confirmed "jumbled" content, a regression
// from the pre-A1 state where face selection was already known correct.
// Root cause: this table's raw-capture convention feeds directly into
// LLCubeMapArray::sClipToCubeLookVecs/sClipToCubeUpVecs (llcubemaparray.cpp),
// a SEPARATE, independently hand-tuned resample-stage table (shared with
// GL, with its own already-verified bug fix history) that was calibrated
// against the OLD negated convention this table used to have. Changing one
// without correspondingly re-deriving the other produced a real, visible
// mismatch. Reverted to the working (if within-face-orientation-imperfect)
// prior state; re-attempt only after the resample-stage table is properly
// co-derived, not as an independent change.
const float DXCubeMapFaces::sUpVecs[6][3] =
{
    {  0.f,  1.f,  0.f }, // +X
    {  0.f,  1.f,  0.f }, // -X
    {  0.f,  0.f, -1.f }, // +Y
    {  0.f,  0.f,  1.f }, // -Y
    {  0.f,  1.f,  0.f }, // +Z
    {  0.f,  1.f,  0.f }, // -Z
};
