#pragma once

// Probe-cube capture convention. The HLSL side is interface/
// cubeFaceConvention.hlsli, and the two must agree.
//
// Face index is D3D11's TextureCube slice order: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
//
// Capture stores each face mirrored in x. LLCoordFrame::lookDir() gives screen
// right = look x up, so the texel at (x, y) holds the world direction
// cubeFaceDir(face, -x, y). For each face, with A and B the sc and tc axes of
// cubeFaceDir(face, sc, tc), the pair below satisfies up = -B and
// look x up = -A. Changing one entry without the other breaks face selection,
// not just within-face orientation.
struct DXCubeMapFaces
{
    // Camera up vector per face.
    static const float sUpVecs[6][3];

    // Camera look direction per face.
    static const float sLookDirs[6][3];
};
