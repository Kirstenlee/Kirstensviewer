/**
 * @file llvowlsky.cpp
 * @brief LLVOWLSky class implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "pipeline.h"

#include "llvowlsky.h"
#include "llsky.h"
#include "lldrawpoolwlsky.h"
#include "llface.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llsettingssky.h"
#include "lldxlinewidth.h"

constexpr U32 MIN_SKY_DETAIL = 8;
constexpr U32 MAX_SKY_DETAIL = 180;

// Bumped from the original 1000 for RenderStarDensity headroom - density is a shader-side visibility
// cull against the baked field (starsF.hlsl), so a bigger field gives the slider real range instead
// of just thinning an already-sparse sky. The last NUM_NEBULA_PATCHES slots of that field are
// repurposed as soft nebula blobs (see initStars()/updateStarGeometry()) rather than points.
//
// The galactic dust band itself is NOT part of this sprite field - see dxdrawpoolwlsky.cpp's
// renderGalacticBandDeferred()/galacticBandV.hlsl for the current (procedural, dome-mesh-based)
// implementation. An earlier sprite-scatter attempt lived here (NUM_DUST_PATCHES, elongated
// streak billboards) but read as isolated dots/streaks rather than a continuous haze - removed
// rather than kept dead/disabled.
constexpr U32 NUM_STARS = 2500;
constexpr U32 NUM_NEBULA_PATCHES = 8;
constexpr U32 NUM_REAL_STARS = NUM_STARS - NUM_NEBULA_PATCHES;

// Shooting stars are a small CPU-tracked pool, unrelated to the baked star field above - see
// updateShootingStars().
constexpr U32 MAX_SHOOTING_STARS = 4;

inline U32 LLVOWLSky::getNumStacks(void)
{
    return llmin(MAX_SKY_DETAIL, llmax(MIN_SKY_DETAIL, gSavedSettings.getU32("WLSkyDetail")));
}

inline U32 LLVOWLSky::getNumSlices(void)
{
    return 2 * llmin(MAX_SKY_DETAIL, llmax(MIN_SKY_DETAIL, gSavedSettings.getU32("WLSkyDetail")));
}

inline U32 LLVOWLSky::getStripsNumVerts(void)
{
    return (getNumStacks() - 1) * getNumSlices();
}

inline U32 LLVOWLSky::getStripsNumIndices(void)
{
    return 2 * ((getNumStacks() - 2) * (getNumSlices() + 1)) + 1 ;
}

inline U32 LLVOWLSky::getStarsNumVerts(void)
{
    return NUM_STARS;
}

inline U32 LLVOWLSky::getStarsNumIndices(void)
{
    return NUM_STARS;
}

LLVOWLSky::LLVOWLSky(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
    : LLStaticViewerObject(id, pcode, regionp, true)
{
    initStars();
}

void LLVOWLSky::idleUpdate(LLAgent &agent, const F64 &time)
{
    // NOT a reliable per-frame hook: LLVOWLSky::isActive() hardcodes false, and
    // LLViewerObjectList's idle dispatch only calls idleUpdate() on active-list objects.
    // updateShootingStars() is instead driven from the DX_RENDER draw path
    // (dxdrawpoolwlsky.cpp's renderShootingStarsDeferred()), matching how this sky subsystem already
    // computes its own per-frame time (sStarTime etc.) without relying on idle ticks.
}

bool LLVOWLSky::isActive(void) const
{
    return false;
}

LLDrawable * LLVOWLSky::createDrawable(LLPipeline * pipeline)
{
    pipeline->allocDrawable(this);

    //LLDrawPoolWLSky *poolp = static_cast<LLDrawPoolWLSky *>(
        gPipeline.getPool(LLDrawPool::POOL_WL_SKY);

    mDrawable->setRenderType(LLPipeline::RENDER_TYPE_WL_SKY);

    return mDrawable;
}

inline F32 calcPhi(const U32 &i, const F32 &reciprocal_num_stacks)
{
    // promote to double for all math
    // Demos: \pi/8*\left(1-((1-x^{4})*(1-x^{4}))\right)\ \left\{0<x\le1\right\}

    // t^4 shaping
    F32 t = float(i) * reciprocal_num_stacks; //SL-16127: remove: / float(getNumStacks());

    // ^4 the parameter of the tesselation to bias things toward 0 (the dome's apex)
    t *= t;
    t *= t;

    // horizon bias
    t = 1.f - t;
    t = t * t;
    t = 1.f - t;

    return (F_PI / 8.f) * t;
}

void LLVOWLSky::resetVertexBuffers()
{
    mStripsVerts.clear();
    mStarsVerts = nullptr;
    mFsSkyVerts = nullptr;

    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
}

void LLVOWLSky::cleanupGL()
{
    mStripsVerts.clear();
    mStarsVerts = nullptr;
    mFsSkyVerts = nullptr;

    LLDrawPoolWLSky::cleanupGL();
}

void LLVOWLSky::restoreGL()
{
    LLDrawPoolWLSky::restoreGL();
    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
}

bool LLVOWLSky::updateGeometry(LLDrawable* drawable)
{
    LLStrider<LLVector3>    vertices;
    LLStrider<LLVector2>    texCoords;
    LLStrider<U16>          indices;

    if (mFsSkyVerts.isNull())
    {
        mFsSkyVerts = new LLVertexBuffer(LLDrawPoolWLSky::ADV_ATMO_SKY_VERTEX_DATA_MASK);

        if (!mFsSkyVerts->allocateBuffer(4, 6))
        {
            LL_WARNS() << "Failed to allocate Vertex Buffer on full screen sky update" << LL_ENDL;
        }

        bool success = mFsSkyVerts->getVertexStrider(vertices)
                    && mFsSkyVerts->getTexCoord0Strider(texCoords)
                    && mFsSkyVerts->getIndexStrider(indices);

        if(!success)
        {
            LL_ERRS() << "Failed updating WindLight fullscreen sky geometry." << LL_ENDL;
        }

        *vertices++ = LLVector3(-1.0f, -1.0f, 0.0f);
        *vertices++ = LLVector3( 1.0f, -1.0f, 0.0f);
        *vertices++ = LLVector3(-1.0f,  1.0f, 0.0f);
        *vertices++ = LLVector3( 1.0f,  1.0f, 0.0f);

        *texCoords++ = LLVector2(0.0f, 0.0f);
        *texCoords++ = LLVector2(1.0f, 0.0f);
        *texCoords++ = LLVector2(0.0f, 1.0f);
        *texCoords++ = LLVector2(1.0f, 1.0f);

        *indices++ = 0;
        *indices++ = 1;
        *indices++ = 2;
        *indices++ = 1;
        *indices++ = 3;
        *indices++ = 2;

        mFsSkyVerts->unmapBuffer();
    }

    {
        const F32 dome_radius = LLEnvironment::instance().getCurrentSky()->getDomeRadius();

        const U32 max_buffer_bytes = gSavedSettings.getS32("RenderMaxVBOSize")*1024;
        const U32 data_mask = LLDrawPoolWLSky::SKY_VERTEX_DATA_MASK;
        const U32 max_verts = max_buffer_bytes / LLVertexBuffer::calcVertexSize(data_mask);

        const U32 total_stacks = getNumStacks();

        const U32 verts_per_stack = getNumSlices();

        // each seg has to have one more row of verts than it has stacks
        // then round down
        const U32 stacks_per_seg = (max_verts - verts_per_stack) / verts_per_stack;

        // round up to a whole number of segments
        const U32 strips_segments = (total_stacks+stacks_per_seg-1) / stacks_per_seg;

        mStripsVerts.resize(strips_segments, NULL);

#if RELEASE_SHOW_DEBUG
        LL_INFOS() << "WL Skydome strips in " << strips_segments << " batches." << LL_ENDL;

        LLTimer timer;
        timer.start();
#endif

        for (U32 i = 0; i < strips_segments ;++i)
        {
            LLVertexBuffer * segment = new LLVertexBuffer(LLDrawPoolWLSky::SKY_VERTEX_DATA_MASK);
            mStripsVerts[i] = segment;

            U32 num_stacks_this_seg = stacks_per_seg;
            if ((i == strips_segments - 1) && (total_stacks % stacks_per_seg) != 0)
            {
                // for the last buffer only allocate what we'll use
                num_stacks_this_seg = total_stacks % stacks_per_seg;
            }

            // figure out what range of the sky we're filling
            const U32 begin_stack = i * stacks_per_seg;
            const U32 end_stack = begin_stack + num_stacks_this_seg;
            llassert(end_stack <= total_stacks);

            const U32 num_verts_this_seg = verts_per_stack * (num_stacks_this_seg+1);
            llassert(num_verts_this_seg <= max_verts);

            const U32 num_indices_this_seg = 1+num_stacks_this_seg*(2+2*verts_per_stack);
            llassert(num_indices_this_seg * sizeof(U16) <= max_buffer_bytes);

            bool allocated = segment->allocateBuffer(num_verts_this_seg, num_indices_this_seg);
#if RELEASE_SHOW_WARNS
            if( !allocated )
            {
                LL_WARNS() << "Failed to allocate Vertex Buffer on update to "
                    << num_verts_this_seg << " vertices and "
                    << num_indices_this_seg << " indices" << LL_ENDL;
            }
#else
            (void) allocated;
#endif

            // lock the buffer
            bool success = segment->getVertexStrider(vertices)
                && segment->getTexCoord0Strider(texCoords)
                && segment->getIndexStrider(indices);

#if RELEASE_SHOW_DEBUG
            if(!success)
            {
                LL_ERRS() << "Failed updating WindLight sky geometry." << LL_ENDL;
            }
#else
            (void) success;
#endif

            // fill it
            buildStripsBuffer(begin_stack, end_stack, vertices, texCoords, indices, dome_radius, verts_per_stack, total_stacks);

            // and unlock the buffer
            segment->unmapBuffer();
        }

#if RELEASE_SHOW_DEBUG
        LL_INFOS() << "completed in " << llformat("%.2f", timer.getElapsedTimeF32().value()) << "seconds" << LL_ENDL;
#endif
    }

    updateStarColors();
    updateStarGeometry(drawable);

    LLPipeline::sCompiles++;

    return true;
}

void LLVOWLSky::drawStars(void)
{
    //  render the stars as a sphere centered at viewer camera
    if (mStarsVerts.notNull())
    {
        mStarsVerts->setBuffer();
        // Must be *6, not *4: each star is 2 triangles (6 verts, see updateStarGeometry()'s per-star
        // write loop). Nebula patches live in the highest-indexed slots, so any undercount here drops
        // them first.
        mStarsVerts->drawArrays(LLRender::TRIANGLES, 0, getStarsNumVerts()*6);
    }
}

void LLVOWLSky::drawShootingStars(void)
{
    // Unlike the static star field, this pool's positions change every frame while active, so the
    // (tiny) buffer is rebuilt here rather than in updateGeometry().
    U32 active_count = 0;
    for (const auto& s : mShootingStars)
    {
        if (s.mActive) ++active_count;
    }

    if (active_count == 0)
    {
        return;
    }

    if (!updateShootingStarGeometry())
    {
        return;
    }

    if (mShootingStarVerts.notNull())
    {
        mShootingStarVerts->setBuffer();
        mShootingStarVerts->drawArrays(LLRender::TRIANGLES, 0, active_count*6);
    }
}

void LLVOWLSky::drawFsSky(void)
{
    if (mFsSkyVerts.isNull())
    {
        updateGeometry(mDrawable);
    }

    LLGLDisable disable_blend(GL_BLEND);

    mFsSkyVerts->setBuffer();
    mFsSkyVerts->drawRange(LLRender::TRIANGLES, 0, mFsSkyVerts->getNumVerts() - 1, mFsSkyVerts->getNumIndices(), 0);
    gPipeline.addTrianglesDrawn(mFsSkyVerts->getNumIndices());
    LLVertexBuffer::unbind();
}

void LLVOWLSky::drawGalacticBandQuad(void)
{
    if (mFsSkyVerts.isNull())
    {
        updateGeometry(mDrawable);
    }

    mFsSkyVerts->setBuffer();
    mFsSkyVerts->drawRange(LLRender::TRIANGLES, 0, mFsSkyVerts->getNumVerts() - 1, mFsSkyVerts->getNumIndices(), 0);
    gPipeline.addTrianglesDrawn(mFsSkyVerts->getNumIndices());
    LLVertexBuffer::unbind();
}

void LLVOWLSky::drawDome(void)
{
    if (mStripsVerts.empty())
    {
        updateGeometry(mDrawable);
    }

    LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE);

    std::vector< LLPointer<LLVertexBuffer> >::const_iterator strips_vbo_iter, end_strips;
    end_strips = mStripsVerts.end();
    for(strips_vbo_iter = mStripsVerts.begin(); strips_vbo_iter != end_strips; ++strips_vbo_iter)
    {
        LLVertexBuffer * strips_segment = strips_vbo_iter->get();

        strips_segment->setBuffer();

        strips_segment->drawRange(
            LLRender::TRIANGLE_STRIP,
            0, strips_segment->getNumVerts()-1, strips_segment->getNumIndices(),
            0);
        gPipeline.addTrianglesDrawn(strips_segment->getNumIndices());
    }

    LLVertexBuffer::unbind();
}

// Stylized approximations of well-known constellation asterism shapes, hand-encoded as 2D offsets in
// an arbitrary local unit around each anchor direction - not precise astronomical RA/Dec data, placed
// by eye for recognizability. See LLVOWLSky::initStars() for how these get placed on the dome and
// folded into the existing per-star twinkle/color/flare system.
//
// `colorT` is an authored spectral-type color, on the same [0,1] scale starsF.hlsl's
// starColorFromSeed() uses (0=red giant, ~0.16-0.34=orange, ~0.34-0.55=warm white, >0.55=blue-white) -
// approximate real spectral classes for each constellation's brightest/most notable stars (e.g.
// Betelgeuse red, Rigel blue) rather than the fully random per-seed hue every other star gets.
struct LLConstellationStar { F32 x, y, colorT; };
// Index pair into a constellation's own star array, for the optional connector-line overlay
// (RenderConstellationLines) - NOT simply "connect consecutive array indices", real asterisms
// branch/close (e.g. the Big Dipper's bowl is a closed loop with the handle branching off one
// corner, not a straight chain).
struct LLConstellationEdge { U32 a, b; };
struct LLConstellationDef
{
    LLVector3 anchor; // rough placement direction (need not be normalized)
    const LLConstellationStar* stars;
    U32 count;
    const LLConstellationEdge* edges;
    U32 edgeCount;
};

// Local tangent-plane scale shared by the constellation placement loop and the anchored Orion
// Nebula placement below - kept in sync deliberately rather than duplicated as a magic number.
constexpr F32 CONSTELLATION_SCALE = 0.18f;

static const LLConstellationStar kBigDipperStars[] = {
    // bowl: Dubhe (orange K giant, outer pointer star), Merak/Phecda/Megrez (white A)
    {-1.00f, 0.30f, 0.20f}, {-1.00f,-0.20f, 0.50f}, {-0.50f,-0.30f, 0.50f}, {-0.40f, 0.25f, 0.55f},
    // handle: Alioth/Mizar (white A), Alkaid (blue-white B3)
    { 0.10f, 0.10f, 0.55f}, { 0.60f, 0.35f, 0.60f}, { 1.00f, 0.15f, 0.70f}
};
static const LLConstellationStar kOrionStars[] = {
    // shoulders: Betelgeuse (red supergiant), Bellatrix (blue-white giant)
    {-0.50f, 0.80f, 0.03f}, { 0.50f, 0.75f, 0.80f},
    // belt: Mintaka/Alnilam/Alnitak, all hot blue-white O/B stars
    {-0.20f, 0.10f, 0.78f}, { 0.00f, 0.05f, 0.78f}, { 0.20f, 0.00f, 0.78f},
    // feet: Saiph (blue-white B), Rigel (blue supergiant, brightest in Orion)
    {-0.45f,-0.80f, 0.85f}, { 0.50f,-0.75f, 0.90f}
};
static const LLConstellationStar kCassiopeiaStars[] = {
    // Shedar (orange K giant), Caph/Ruchbah (white F/A), Gamma Cas/Segin (blue-white B)
    {-1.00f, 0.00f, 0.20f}, {-0.50f, 0.40f, 0.55f}, { 0.00f, 0.00f, 0.65f}, { 0.50f, 0.50f, 0.55f}, { 1.00f, 0.10f, 0.70f}
};
static const LLConstellationStar kSouthernCrossStars[] = {
    // Gacrux (red giant, notable exception in an otherwise blue-white cross), Acrux/Mimosa/Delta Crucis (blue-white B)
    { 0.00f, 1.00f, 0.08f}, { 0.00f,-1.00f, 0.80f}, {-0.70f,-0.10f, 0.85f}, { 0.60f, 0.20f, 0.75f}, { 0.05f, 0.05f, 0.50f}
};
static const LLConstellationStar kScorpiusStars[] = {
    // Antares (red supergiant, the scorpion's "heart"), rest of the curving tail is hot blue-white
    {-1.00f, 0.60f, 0.04f}, {-0.70f, 0.50f, 0.70f}, {-0.40f, 0.30f, 0.75f}, {-0.10f, 0.10f, 0.78f},
    { 0.20f,-0.10f, 0.78f}, { 0.40f,-0.40f, 0.80f}, { 0.50f,-0.70f, 0.82f}, { 0.35f,-0.95f, 0.85f}
};
static const LLConstellationStar kCygnusStars[] = {
    // Deneb (blue-white supergiant, tail), Sadr/wings (blue-white), Albireo (famous gold+blue binary - shown gold, the head)
    { 0.00f, 1.20f, 0.85f}, { 0.00f, 0.00f, 0.75f}, { 0.00f,-1.00f, 0.25f}, {-0.80f, 0.10f, 0.70f}, { 0.80f, 0.15f, 0.70f}
};
static const LLConstellationStar kLeoStars[] = {
    // Regulus (blue-white B, brightest), Algieba (orange K giant), Denebola (white A, tail)
    {-0.90f, 0.10f, 0.65f}, {-0.60f, 0.50f, 0.22f}, {-0.20f, 0.60f, 0.55f}, { 0.10f, 0.35f, 0.60f}, { 0.05f,-0.10f, 0.65f}, { 0.60f,-0.15f, 0.55f}
};
static const LLConstellationStar kLyraStars[] = {
    // Vega (brilliant blue-white A0, dominates the constellation), rest are far fainter white/blue-white
    { 0.00f, 0.60f, 0.85f}, {-0.15f, 0.00f, 0.60f}, { 0.15f, 0.05f, 0.60f}, { 0.10f,-0.30f, 0.55f}, {-0.10f,-0.28f, 0.55f}
};

// Star-index connectivity per shape (see LLConstellationEdge's comment) - array indices are
// local to each constellation's own star array above, not global star-buffer indices.
static const LLConstellationEdge kBigDipperEdges[] = {
    {0,1},{1,2},{2,3},{3,0}, // bowl, closed loop
    {3,4},{4,5},{5,6}        // handle, branches off the bowl corner nearest it
};
static const LLConstellationEdge kOrionEdges[] = {
    {0,2},{1,4},   // shoulders down to the nearer belt star
    {2,3},{3,4},   // belt
    {2,5},{4,6}    // belt down to the nearer foot
};
static const LLConstellationEdge kCassiopeiaEdges[] = { {0,1},{1,2},{2,3},{3,4} }; // the W/M zigzag
static const LLConstellationEdge kSouthernCrossEdges[] = {
    {0,1},       // main vertical bar (Gacrux to Acrux)
    {2,4},{4,3}  // horizontal arm, via the near-center 5th star
};
static const LLConstellationEdge kScorpiusEdges[] = { {0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7} }; // curving tail
static const LLConstellationEdge kCygnusEdges[] = {
    {0,1},{1,2}, // tail-center-head (the "Northern Cross" long axis)
    {1,3},{1,4}  // wings
};
static const LLConstellationEdge kLeoEdges[] = { {0,1},{1,2},{2,3},{3,4},{4,5} }; // sickle + body chain
static const LLConstellationEdge kLyraEdges[] = {
    {0,1},           // Vega down to the small parallelogram
    {1,2},{2,3},{3,4},{4,1} // parallelogram, closed loop
};

static const LLConstellationDef kConstellations[] = {
    { LLVector3( 0.55f, 0.35f, 0.55f), kBigDipperStars,    sizeof(kBigDipperStars)/sizeof(kBigDipperStars[0]),    kBigDipperEdges,    sizeof(kBigDipperEdges)/sizeof(kBigDipperEdges[0]) },
    { LLVector3(-0.60f, 0.20f, 0.45f), kOrionStars,        sizeof(kOrionStars)/sizeof(kOrionStars[0]),            kOrionEdges,        sizeof(kOrionEdges)/sizeof(kOrionEdges[0]) },
    { LLVector3( 0.10f,-0.60f, 0.60f), kCassiopeiaStars,   sizeof(kCassiopeiaStars)/sizeof(kCassiopeiaStars[0]),  kCassiopeiaEdges,   sizeof(kCassiopeiaEdges)/sizeof(kCassiopeiaEdges[0]) },
    { LLVector3(-0.30f,-0.50f, 0.35f), kSouthernCrossStars,sizeof(kSouthernCrossStars)/sizeof(kSouthernCrossStars[0]), kSouthernCrossEdges, sizeof(kSouthernCrossEdges)/sizeof(kSouthernCrossEdges[0]) },
    { LLVector3( 0.65f,-0.35f, 0.30f), kScorpiusStars,     sizeof(kScorpiusStars)/sizeof(kScorpiusStars[0]),      kScorpiusEdges,     sizeof(kScorpiusEdges)/sizeof(kScorpiusEdges[0]) },
    { LLVector3(-0.70f,-0.15f, 0.55f), kCygnusStars,       sizeof(kCygnusStars)/sizeof(kCygnusStars[0]),          kCygnusEdges,       sizeof(kCygnusEdges)/sizeof(kCygnusEdges[0]) },
    { LLVector3( 0.25f, 0.65f, 0.40f), kLeoStars,          sizeof(kLeoStars)/sizeof(kLeoStars[0]),                kLeoEdges,          sizeof(kLeoEdges)/sizeof(kLeoEdges[0]) },
    { LLVector3(-0.15f, 0.60f, 0.65f), kLyraStars,         sizeof(kLyraStars)/sizeof(kLyraStars[0]),              kLyraEdges,         sizeof(kLyraEdges)/sizeof(kLyraEdges[0]) },
};
// Index of Orion within kConstellations[], used by the anchored Orion Nebula placement below -
// asserted against a name match isn't practical for a plain array, so this is kept adjacent and
// commented rather than magic.
constexpr U32 ORION_CONSTELLATION_INDEX = 1;

// Constellation stars are always placed first, filling indices [0, getConstellationTotalStars()) -
// used by updateStarGeometry() to size-boost exactly that range. Computed instead of hardcoded so it
// can't drift out of sync with kConstellations above.
static U32 getConstellationTotalStars()
{
    U32 total = 0;
    for (const auto& def : kConstellations)
    {
        total += def.count;
    }
    return total;
}

void LLVOWLSky::initStars()
{
    const F32 DISTANCE_TO_STARS = LLEnvironment::instance().getCurrentSky()->getDomeRadius();

    // Initialize star map
    mStarVertices.resize(getStarsNumVerts());
    mStarColors.resize(getStarsNumVerts());
    mStarIntensities.resize(getStarsNumVerts());

    std::vector<LLVector3>::iterator v_p = mStarVertices.begin();
    std::vector<LLColor4>::iterator v_c = mStarColors.begin();
    std::vector<F32>::iterator v_i = mStarIntensities.begin();

    U32 i = 0;

    // "Real Constellations" style fills the front of the field with the hand-authored shapes above,
    // brighter/steadier than the random background fill, then falls through to the same random scatter
    // as every other style for the rest. Existing per-star systems (twinkle/color/flare, density cull,
    // nebula patches) keep working unchanged - this only changes WHERE some stars land.
    if (gSavedSettings.getS32("RenderSkyStyle") == 1)
    {
        for (const auto& def : kConstellations)
        {
            LLVector3 anchor = def.anchor;
            anchor.normVec();
            LLVector3 left = anchor % LLVector3(0.f, 0.f, 1.f);
            left.normVec();
            LLVector3 up = anchor % left;

            for (U32 s = 0; s < def.count && i < getStarsNumVerts(); ++s, ++i)
            {
                LLVector3 pos = anchor
                    + left * (def.stars[s].x * CONSTELLATION_SCALE)
                    + up   * (def.stars[s].y * CONSTELLATION_SCALE);
                pos.normVec();
                *v_p = pos * DISTANCE_TO_STARS;

                *v_i = 0.95f; // steadier/brighter than the background field
                // VRED carries the authored spectral-type colorT for constellation stars
                // (starsF.hlsl reads it back in place of the usual random-seed hue) - every
                // other star (background field, nebula patches) still gets a decorative
                // near-white VRED, so this repurposing only affects constellation stars.
                v_c->mV[VRED]   = def.stars[s].colorT;
                v_c->mV[VGREEN] = 1.f;
                // VBLUE in [0.30,0.45] flags a constellation star as exempt from the density cull -
                // every other star (background field and nebula patches) uses [0.75,1.0], so this
                // range is unambiguous. starsF.hlsl detects it and reconstructs a normal-looking blue
                // tint from star_seed, so the flag has no visible side effect.
                v_c->mV[VBLUE]  = 0.30f + ll_frand() * 0.15f;
                v_c->mV[VALPHA] = 1.f;
                v_c->clamp();

                v_p++;
                v_c++;
                v_i++;
            }
        }

        // The Orion Nebula (M42) sits just below Orion's belt, in the "sword" - anchor one of the
        // NUM_NEBULA_PATCHES slots there instead of letting it land randomly like the rest. Recomputes
        // Orion's own local tangent frame (not stored above) since this runs after that loop exits.
        if (i < getStarsNumVerts())
        {
            const LLConstellationDef& orion = kConstellations[ORION_CONSTELLATION_INDEX];
            LLVector3 anchor = orion.anchor;
            anchor.normVec();
            LLVector3 left = anchor % LLVector3(0.f, 0.f, 1.f);
            left.normVec();
            LLVector3 up = anchor % left;

            // Belt stars sit at local y~0.05-0.10; the sword hangs further below (more negative y).
            LLVector3 pos = anchor + up * (-0.55f * CONSTELLATION_SCALE);
            pos.normVec();
            *v_p = pos * DISTANCE_TO_STARS;

            *v_i = 1.f;
            v_c->mV[VRED]   = 0.95f;
            v_c->mV[VGREEN] = 0.f; // nebula flag (VGREEN==0), see initStars()'s comment below
            v_c->mV[VBLUE]  = 0.85f;
            v_c->mV[VALPHA] = 1.f;
            v_c->clamp();

            v_p++;
            v_c++;
            v_i++;
            ++i;
        }
    }

    for (; i < getStarsNumVerts(); ++i)
    {
        v_p->mV[VX] = ll_frand() - 0.5f;
        v_p->mV[VY] = ll_frand() - 0.5f;

        // we only want stars on the top half of the dome!

        v_p->mV[VZ] = ll_frand()/2.f;

        v_p->normVec();
        *v_p *= DISTANCE_TO_STARS;

        *v_i = llmin((F32)pow(ll_frand(),2.f) + 0.1f, 1.f);
        v_c->mV[VRED]   = 0.75f + ll_frand() * 0.25f ;
        v_c->mV[VGREEN] = 1.f ;
        v_c->mV[VBLUE]  = 0.75f + ll_frand() * 0.25f ;
        v_c->mV[VALPHA] = 1.f;

        // The last NUM_NEBULA_PATCHES slots of the baked field are soft blobs, not
        // point stars. VGREEN==1.0 always for a real star (set above, never touched again), so 0.0
        // here is an unambiguous flag - starsF.hlsl reads it back via vertex_color.g.
        if (i >= NUM_REAL_STARS)
        {
            v_c->mV[VGREEN] = 0.f;
        }

        v_c->clamp();
        v_p++;
        v_c++;
        v_i++;
    }
}

void LLVOWLSky::drawConstellationLines(void)
{
    if (gSavedSettings.getS32("RenderSkyStyle") != 1 || !gSavedSettings.getBOOL("RenderConstellationLines"))
    {
        return;
    }

    if (mStarVertices.size() < getConstellationTotalStars())
    {
        return;
    }

    // Cool-white "star chart" overlay - unobtrusive but real; additive blend against a dark sky
    // needs more than a token alpha to actually register.
    LLColor4 line_color(0.55f, 0.70f, 0.85f, 0.65f);

    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gDX.begin(LLRender::TRIANGLES);

    U32 base = 0;
    for (const auto& def : kConstellations)
    {
        for (U32 e = 0; e < def.edgeCount; ++e)
        {
            const LLVector3& a = mStarVertices[base + def.edges[e].a];
            const LLVector3& b = mStarVertices[base + def.edges[e].b];

            LLVector3 seg = b - a;
            F32 seg_length = seg.length();
            if (seg_length < 0.001f)
            {
                continue;
            }
            seg *= 1.f / seg_length;

            // Dot/gap sized as a fraction of this edge's own length, so the pattern scales sensibly
            // regardless of the dome's absolute radius or how far apart any given pair of stars is,
            // rather than a fixed meter size (which would be meaningless at star-dome distances).
            F32 dot_length = seg_length * 0.015f;
            F32 gap_length = seg_length * 0.025f;
            F32 pattern_length = dot_length + gap_length;
            S32 num_dots = (S32)(seg_length / pattern_length);

            for (S32 d = 0; d <= num_dots; ++d)
            {
                F32 start_dist = d * pattern_length;
                if (start_dist >= seg_length)
                {
                    break;
                }
                F32 end_dist = llmin(start_dist + dot_length, seg_length);
                LLVector3 dot_start = a + seg * start_dist;
                LLVector3 dot_end = a + seg * end_dist;
                dxLineWidth(dot_start, dot_end, 1.1f, line_color);
            }
        }
        base += def.count;
    }

    gDX.end();
}

void LLVOWLSky::buildStripsBuffer(U32 begin_stack,
                                  U32 end_stack,
                                  LLStrider<LLVector3> & vertices,
                                  LLStrider<LLVector2> & texCoords,
                                  LLStrider<U16> & indices,
                                  const F32 dome_radius,
                                  const U32& num_slices,
                                  const U32& num_stacks)
{
    U32 i, j;
    F32 phi0, theta, x0, y0, z0;
    const F32 reciprocal_num_stacks = 1.f / num_stacks;

    llassert(end_stack <= num_stacks);

    // stacks are iterated one-indexed since phi(0) was handled by the fan above
#if NEW_TESS
    for(i = begin_stack; i <= end_stack; ++i)
#else
    for(i = begin_stack + 1; i <= end_stack+1; ++i)
#endif
    {
        phi0 = calcPhi(i, reciprocal_num_stacks);

        for(j = 0; j < num_slices; ++j)
        {
            theta = F_TWO_PI * (float(j) / float(num_slices));

            // standard transformation from  spherical to
            // rectangular coordinates
            x0 = sin(phi0) * cos(theta);
            y0 = cos(phi0);
            z0 = sin(phi0) * sin(theta);

#if NEW_TESS
            *vertices++ = LLVector3(x0 * dome_radius, y0 * dome_radius, z0 * dome_radius);
#else
            if (i == num_stacks-2)
            {
                *vertices++ = LLVector3(x0*dome_radius, y0*dome_radius-1024.f*2.f, z0*dome_radius);
            }
            else if (i == num_stacks-1)
            {
                *vertices++ = LLVector3(0, y0*dome_radius-1024.f*2.f, 0);
            }
            else
            {
                *vertices++     = LLVector3(x0 * dome_radius, y0 * dome_radius, z0 * dome_radius);
            }
#endif

            // generate planar uv coordinates
            // note: x and z are transposed in order for things to animate
            // correctly in the global coordinate system where +x is east and
            // +y is north
            *texCoords++    = LLVector2((-z0 + 1.f) / 2.f, (-x0 + 1.f) / 2.f);
        }
    }

    //build triangle strip...
    *indices++ = 0 ;

    S32 k = 0 ;
    for(i = 1; i <= end_stack - begin_stack; ++i)
    {
        *indices++ = i * num_slices + k ;

        k = (k+1) % num_slices ;
        for(j = 0; j < num_slices ; ++j)
        {
            *indices++ = (i-1) * num_slices + k ;
            *indices++ = i * num_slices + k ;

            k = (k+1) % num_slices ;
        }

        if((--k) < 0)
        {
            k = num_slices - 1 ;
        }

        *indices++ = i * num_slices + k ;
    }
}

void LLVOWLSky::updateStarColors()
{
    std::vector<LLColor4>::iterator v_c = mStarColors.begin();
    std::vector<F32>::iterator v_i = mStarIntensities.begin();
    std::vector<LLVector3>::iterator v_p = mStarVertices.begin();

    const F32 var = 0.15f;
    const F32 min = 0.5f; //0.75f;
    //const F32 sunclose_max = 0.6f;
    //const F32 sunclose_range = 1 - sunclose_max;

    //F32 below_horizon = - llmin(0.0f, gSky.mVOSkyp->getToSunLast().mV[2]);
    //F32 brightness_factor = llmin(1.0f, below_horizon * 20);

    static S32 swap = 0;
    swap++;

    if ((swap % 2) == 1)
    {
        F32 intensity;                      //  max intensity of each star
        U32 x;
        for (x = 0; x < getStarsNumVerts(); ++x)
        {
            //F32 sundir_factor = 1;
            LLVector3 tostar = *v_p;
            tostar.normVec();
            //const F32 how_close_to_sun = tostar * gSky.mVOSkyp->getToSunLast();
            //if (how_close_to_sun > sunclose_max)
            //{
            //  sundir_factor = (1 - how_close_to_sun) / sunclose_range;
            //}
            intensity = *(v_i);
            F32 alpha = v_c->mV[VALPHA] + (ll_frand() - 0.5f) * var * intensity;
            if (alpha < min * intensity)
            {
                alpha = min * intensity;
            }
            if (alpha > intensity)
            {
                alpha = intensity;
            }
            //alpha *= brightness_factor * sundir_factor;

            alpha = llclamp(alpha, 0.f, 1.f);
            v_c->mV[VALPHA] = alpha;
            v_c++;
            v_i++;
            v_p++;
        }
    }
}

bool LLVOWLSky::updateStarGeometry(LLDrawable *drawable)
{
    LLStrider<LLVector3> verticesp;
    LLStrider<LLColor4U> colorsp;
    LLStrider<LLVector2> texcoordsp;

    if (mStarsVerts.isNull())
    {
        mStarsVerts = new LLVertexBuffer(LLDrawPoolWLSky::STAR_VERTEX_DATA_MASK);
        if (!mStarsVerts->allocateBuffer(getStarsNumVerts()*6, 0))
        {
            LL_WARNS() << "Failed to allocate Vertex Buffer for Sky to " << getStarsNumVerts() * 6 << " vertices" << LL_ENDL;
        }
    }

    bool success = mStarsVerts->getVertexStrider(verticesp)
        && mStarsVerts->getColorStrider(colorsp)
        && mStarsVerts->getTexCoord0Strider(texcoordsp);

    if(!success)
    {
        LL_ERRS() << "Failed updating star geometry." << LL_ENDL;
    }

    // *TODO: fix LLStrider with a real prefix increment operator so it can be
    // used as a model of OutputIterator. -Brad
    // std::copy(mStarVertices.begin(), mStarVertices.end(), verticesp);

    if (mStarVertices.size() < getStarsNumVerts())
    {
        LL_ERRS() << "Star reference geometry insufficient." << LL_ENDL;
    }

    for (U32 vtx = 0; vtx < getStarsNumVerts(); ++vtx)
    {
        LLVector3 at = mStarVertices[vtx];
        at.normVec();
        LLVector3 left = at%LLVector3(0,0,1);
        LLVector3 up = at%left;

        // Nebula slots (flagged via VGREEN==0 in initStars()) get a much larger billboard - starsF.hlsl
        // renders them as a soft radial-gradient blob instead of a point sprite, needing real screen
        // coverage. Checked via the actual flag rather than "vtx >= NUM_REAL_STARS" alone - the
        // anchored Orion Nebula star (Real Constellations style) sits earlier in the array, outside
        // that index range, but is still a real nebula-flagged slot.
        bool is_nebula_slot = (vtx >= NUM_REAL_STARS) || (mStarColors[vtx].mV[VGREEN] < 0.5f);
        bool is_constellation_star = (gSavedSettings.getS32("RenderSkyStyle") == 1)
            && (vtx < getConstellationTotalStars());
        // Starry Night boosts every star's billboard (not just a front-loaded subset): a normal 16-36
        // unit quad is far too small at typical viewing distance for starsF.hlsl's ring/duotone recolor
        // (computed per-quad) to register legibly. The size is tuned to leave genuine empty margin
        // before the quad's hard edge, since the falloff terminates inside the quad rather than at it.
        bool is_starry_night = gSavedSettings.getS32("RenderSkyStyle") == 2;

        F32 sc = is_nebula_slot
            ? (260.0f + ll_frand() * 220.0f)
            : is_constellation_star
                ? (48.0f + ll_frand() * 60.0f)
                : is_starry_night
                    ? (180.0f + ll_frand() * 260.0f)
                    : (16.0f + ll_frand() * 20.0f);
        left *= sc;
        up *= sc;

        *(verticesp++)  = mStarVertices[vtx];
        *(verticesp++) = mStarVertices[vtx]+up;
        *(verticesp++) = mStarVertices[vtx]+left+up;
        *(verticesp++)  = mStarVertices[vtx];
        *(verticesp++) = mStarVertices[vtx]+left+up;
        *(verticesp++) = mStarVertices[vtx]+left;

        *(texcoordsp++) = LLVector2(1,0);
        *(texcoordsp++) = LLVector2(1,1);
        *(texcoordsp++) = LLVector2(0,1);
        *(texcoordsp++) = LLVector2(1,0);
        *(texcoordsp++) = LLVector2(0,1);
        *(texcoordsp++) = LLVector2(0,0);

        *(colorsp++)    = LLColor4U(mStarColors[vtx]);
        *(colorsp++)    = LLColor4U(mStarColors[vtx]);
        *(colorsp++)    = LLColor4U(mStarColors[vtx]);
        *(colorsp++)    = LLColor4U(mStarColors[vtx]);
        *(colorsp++)    = LLColor4U(mStarColors[vtx]);
        *(colorsp++)    = LLColor4U(mStarColors[vtx]);
    }

    mStarsVerts->unmapBuffer();
    return true;
}

// Spawn/age/expire logic for the shooting-star pool, called once per frame from the DX_RENDER draw
// path (dxdrawpoolwlsky.cpp), not idleUpdate() (see idleUpdate()'s own comment). Does not touch the
// GPU - updateShootingStarGeometry() rebuilds the tiny vertex buffer from whatever this leaves in
// mShootingStars.
void LLVOWLSky::updateShootingStars(F32 dt)
{
    if (mShootingStars.empty())
    {
        mShootingStars.resize(MAX_SHOOTING_STARS);
    }

    // age/expire existing
    for (auto& s : mShootingStars)
    {
        if (!s.mActive)
        {
            continue;
        }
        s.mAge += dt;
        if (s.mAge >= s.mLifetime)
        {
            s.mActive = false;
        }
    }

    if (!gSavedSettings.getBOOL("RenderShootingStars"))
    {
        return;
    }

    F32 per_minute = llmax(0.f, gSavedSettings.getF32("RenderShootingStarFrequency"));
    if (per_minute <= 0.f)
    {
        return;
    }

    mNextShootingStarCheck -= dt;
    if (mNextShootingStarCheck > 0.f)
    {
        return;
    }

    // schedule the next check on an exponential (Poisson-process) delay so
    // the frequency slider reads as an honest average rate rather than a
    // fixed metronome tick
    F32 mean_interval_s = 60.0f / per_minute;
    mNextShootingStarCheck = mean_interval_s * llmax(0.05f, -logf(llmax(0.0001f, ll_frand())));

    // find a free pool slot - if the pool's full, just skip this spawn
    // (equivalent to a very-high-frequency slider self-limiting at
    // MAX_SHOOTING_STARS concurrent streaks, which is the intended cap)
    for (auto& s : mShootingStars)
    {
        if (s.mActive)
        {
            continue;
        }

        const F32 DISTANCE_TO_STARS = LLEnvironment::instance().getCurrentSky()->getDomeRadius();

        LLVector3 origin(ll_frand() - 0.5f, ll_frand() - 0.5f, ll_frand() * 0.4f + 0.05f);
        origin.normVec();
        origin *= DISTANCE_TO_STARS;

        LLVector3 at = origin;
        at.normVec();
        LLVector3 tangent_a = at % LLVector3(0.f, 0.f, 1.f);
        tangent_a.normVec();
        LLVector3 tangent_b = at % tangent_a;

        F32 dir_angle = ll_frand() * F_TWO_PI;
        LLVector3 dir = tangent_a * cosf(dir_angle) + tangent_b * sinf(dir_angle);
        dir.normVec();

        s.mOrigin = origin;
        s.mDir = dir;
        s.mTotalDistance = 900.0f + ll_frand() * 700.0f;
        s.mTrailLength = 120.0f + ll_frand() * 100.0f;
        s.mLifetime = 0.6f + ll_frand() * 0.5f;
        s.mAge = 0.f;
        s.mActive = true;
        break;
    }
}

bool LLVOWLSky::updateShootingStarGeometry()
{
    LLStrider<LLVector3> verticesp;
    LLStrider<LLColor4U> colorsp;
    LLStrider<LLVector2> texcoordsp;

    if (mShootingStarVerts.isNull())
    {
        mShootingStarVerts = new LLVertexBuffer(LLDrawPoolWLSky::STAR_VERTEX_DATA_MASK);
        if (!mShootingStarVerts->allocateBuffer(MAX_SHOOTING_STARS*6, 0))
        {
            LL_WARNS() << "Failed to allocate Vertex Buffer for shooting stars" << LL_ENDL;
            return false;
        }
    }

    bool success = mShootingStarVerts->getVertexStrider(verticesp)
        && mShootingStarVerts->getColorStrider(colorsp)
        && mShootingStarVerts->getTexCoord0Strider(texcoordsp);

    if (!success)
    {
        LL_WARNS() << "Failed updating shooting star geometry." << LL_ENDL;
        return false;
    }

    for (const auto& s : mShootingStars)
    {
        if (!s.mActive)
        {
            continue;
        }

        F32 progress = llclamp(s.mAge / s.mLifetime, 0.f, 1.f);
        F32 head_dist = s.mTotalDistance * progress;
        F32 tail_dist = llmax(0.f, head_dist - s.mTrailLength);

        LLVector3 head = s.mOrigin + s.mDir * head_dist;
        LLVector3 tail = s.mOrigin + s.mDir * tail_dist;

        LLVector3 at = s.mOrigin;
        at.normVec();
        LLVector3 side = s.mDir % at;
        side.normVec();
        side *= 6.0f + (s.mTrailLength * 0.01f);

        // fade in over the first 10% of life, out over the last 25% -
        // texcoord0.x carries head(1)->tail(0) position along the streak,
        // texcoord0.y carries the fade-in/out envelope for starsShootingF.hlsl
        F32 envelope = llmin(progress / 0.10f, (1.f - progress) / 0.25f);
        envelope = llclamp(envelope, 0.f, 1.f);

        LLColor4U tint(255, 255, 255, (U8)(255.f * envelope));

        *(verticesp++) = head + side;
        *(verticesp++) = head - side;
        *(verticesp++) = tail - side;
        *(verticesp++) = head + side;
        *(verticesp++) = tail - side;
        *(verticesp++) = tail + side;

        *(texcoordsp++) = LLVector2(1.f, 1.f);
        *(texcoordsp++) = LLVector2(1.f, 0.f);
        *(texcoordsp++) = LLVector2(0.f, 0.f);
        *(texcoordsp++) = LLVector2(1.f, 1.f);
        *(texcoordsp++) = LLVector2(0.f, 0.f);
        *(texcoordsp++) = LLVector2(0.f, 1.f);

        *(colorsp++) = tint;
        *(colorsp++) = tint;
        *(colorsp++) = tint;
        *(colorsp++) = tint;
        *(colorsp++) = tint;
        *(colorsp++) = tint;
    }

    mShootingStarVerts->unmapBuffer();
    return true;
}
