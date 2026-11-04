#include "DXCubeMapFaces.h"

// See DXCubeMapFaces.h for the convention. Each up vector is -B of its face in
// cubeFaceConvention.hlsli, and each look direction is the face's +/-axis.
const float DXCubeMapFaces::sUpVecs[6][3] =
{
    {  0.f,  1.f,  0.f }, // +X
    {  0.f,  1.f,  0.f }, // -X
    {  0.f,  0.f, -1.f }, // +Y
    {  0.f,  0.f,  1.f }, // -Y
    {  0.f,  1.f,  0.f }, // +Z
    {  0.f,  1.f,  0.f }, // -Z
};

const float DXCubeMapFaces::sLookDirs[6][3] =
{
    {  1.f,  0.f,  0.f }, // +X
    { -1.f,  0.f,  0.f }, // -X
    {  0.f,  1.f,  0.f }, // +Y
    {  0.f, -1.f,  0.f }, // -Y
    {  0.f,  0.f,  1.f }, // +Z
    {  0.f,  0.f, -1.f }, // -Z
};
