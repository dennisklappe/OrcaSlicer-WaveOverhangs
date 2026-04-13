///|/ Kaiser LaSO wave-overhang generator.
///|/
///|/ Original algorithm: Rieks Kaiser (riekskaiser) - https://github.com/riekskaiser/wave_LaSO.
///|/ Original license: MIT. Reimplemented in C++ under AGPL-3.0 compatibility (re-implementation, not code derivation).
///|/
///|/ OrcaSlicer port & modifications: Dennis Klappe (dennisklappe).
///|/ Modifications from Kaiser's original:
///|/   - Auto-detect seed curve from overhang-boundary intersection with lower-slice boundary
///|/     (Kaiser's original required manual seed-curve input).
///|/   - Multi-overhang handling per layer (Kaiser's original processed one region at a time).
///|/   - Pipeline integration (Kaiser's original was a G-code post-processor).
///|/   - Pin-support placement removed (out-of-scope for this phase).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#include "KaiserGenerator.hpp"

#include <algorithm>
#include <cmath>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::WaveOverhangs {
namespace {

// Build the seed curve(s) for a single overhang ExPolygon.
//
// The seed is the "root edge": the portion of the overhang's contour that
// coincides with the lower-slice boundary (i.e. the supported edge from which
// the cantilever grows). We obtain it as the intersection of the overhang's
// boundary polylines with a slightly inflated band around the lower-slice
// polygons - any contour segment that lies within `epsilon` of supported
// material is treated as anchored.
Polylines extract_seed_curves(const ExPolygon &overhang, const Polygons &lower_slices, coord_t epsilon)
{
    if (lower_slices.empty())
        return {};

    // Convert overhang boundary (contour + holes) to polylines.
    Polylines boundary_pls = to_polylines(overhang);
    if (boundary_pls.empty())
        return {};

    // Inflate the lower-slice polygons by epsilon to get a "supported" band.
    Polygons supported_band = expand(lower_slices, float(epsilon), ClipperLib::jtSquare, 0.);
    if (supported_band.empty())
        return {};

    // Clip overhang boundary polylines by the supported band - what remains is
    // the root (anchored) edge.
    Polylines seeds = intersection_pl(boundary_pls, supported_band);

    // Drop degenerate/very-short fragments that would yield a useless offset.
    seeds.erase(
        std::remove_if(seeds.begin(), seeds.end(), [](const Polyline &p) {
            return p.points.size() < 2 || p.length() < scale_(0.05);
        }),
        seeds.end());

    return seeds;
}

// Convert offset-buffer Polygons to centerline Polylines by intersecting their
// boundary with the overhang interior. The buffer of an open seed curve is a
// "stadium"-shaped polygon whose exterior, when clipped to the overhang, gives
// us a polyline running parallel to the seed.
Polylines extract_offset_track(const Polygons &offset_polys, const ExPolygon &overhang)
{
    if (offset_polys.empty())
        return {};

    // The exterior of each offset polygon as a polyline.
    Polylines candidate;
    candidate.reserve(offset_polys.size());
    for (const Polygon &poly : offset_polys) {
        if (poly.points.size() < 2)
            continue;
        Polyline pl;
        pl.points = poly.points;
        pl.points.push_back(poly.points.front()); // close
        candidate.push_back(std::move(pl));
    }

    // Clip to overhang: keep only the portions inside the overhang region.
    Polylines clipped = intersection_pl(candidate, to_polygons(overhang));

    clipped.erase(
        std::remove_if(clipped.begin(), clipped.end(), [](const Polyline &p) {
            return p.points.size() < 2 || p.length() < scale_(0.2);
        }),
        clipped.end());

    return clipped;
}

} // namespace

GenerateResult KaiserGenerator::generate(const ExPolygons   &overhang_area,
                                         const Polygons     &lower_slices_polygons,
                                         const CommonParams &params)
{
    GenerateResult result;
    if (overhang_area.empty() || lower_slices_polygons.empty())
        return result;

    const Flow    wave_flow = params.line_width > 0. ? params.overhang_flow.with_width(float(params.line_width))
                                                     : params.overhang_flow;
    const double  step_mm   = std::max(0.05, params.line_width * (1.0 - std::clamp(this->overlap, 0.0, 0.9)));
    const coord_t step      = std::max<coord_t>(1, coord_t(scale_(step_mm)));
    // Tight band around lower slices to detect supported (root) edge.
    const coord_t seed_epsilon = std::max<coord_t>(SCALED_EPSILON * 4, scale_(0.02));

    // Compute overhang-only region (overhang_area minus lower slices) - this
    // is the area we are actually allowed to deposit Kaiser tracks in.
    BoundingBox bb = get_extents(overhang_area).inflated(SCALED_EPSILON);
    Polygons    optimized_lower = ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                                    lower_slices_polygons, bb);
    Polygons    overhangs_only  = diff(overhang_area, optimized_lower);
    if (overhangs_only.empty())
        return result;

    Polygons covered_total;

    for (const ExPolygon &overhang : union_ex(overhangs_only)) {
        // Per-region max offset distance: bounding box diagonal is a safe upper bound.
        BoundingBox region_bb = get_extents(overhang);
        const coord_t max_extent = static_cast<coord_t>(std::hypot(
            double(region_bb.size().x()), double(region_bb.size().y())));
        if (max_extent <= 0)
            continue;

        // Auto-detect seed curve(s): the contour edges adjacent to lower slices.
        Polylines seeds = extract_seed_curves(overhang, lower_slices_polygons, seed_epsilon);
        if (seeds.empty())
            continue; // No supported edge found; this is a floating island, skip.

        ExtrusionPaths region_paths;
        bool           alternate = false;

        // Iteratively grow the seed outward.
        for (coord_t r = step; r <= max_extent + step; r += step) {
            // Buffer the seed by r. EndType = OpenButt to keep the buffer
            // square at seed endpoints (avoids spurious round caps that could
            // poke beyond the overhang region).
            Polygons offset_buffer = offset(seeds, float(r),
                                            ClipperLib::jtRound, 0.,
                                            ClipperLib::etOpenRound);
            if (offset_buffer.empty())
                break;

            Polylines tracks = extract_offset_track(offset_buffer, overhang);
            if (tracks.empty()) {
                // No intersection with overhang at this offset distance; we've
                // walked off the region. Stop.
                break;
            }

            // Boustrophedon: alternate direction every ring.
            if (alternate) {
                for (Polyline &pl : tracks)
                    pl.reverse();
            }
            alternate = !alternate;

            // Simplify slightly to keep segment count down.
            for (Polyline &pl : tracks)
                pl.simplify(std::min(0.05 * double(step), params.scaled_resolution));

            tracks.erase(
                std::remove_if(tracks.begin(), tracks.end(),
                               [](const Polyline &p) { return p.points.size() < 2; }),
                tracks.end());
            if (tracks.empty())
                continue;

            extrusion_paths_append(region_paths, tracks, erOverhangPerimeter,
                                   wave_flow.mm3_per_mm(), wave_flow.width(), wave_flow.height());
        }

        // Tag and ship.
        if (region_paths.empty())
            continue;

        for (ExtrusionPath &p : region_paths)
            p.wave_overhang = true;

        // Mark the area we covered as consumed (so the caller can subtract it
        // from the infill region). Approximate covered area as a buffer of the
        // emitted polylines at half the line width.
        Polylines covered_pls;
        covered_pls.reserve(region_paths.size());
        for (const ExtrusionPath &p : region_paths)
            covered_pls.push_back(p.polyline);
        Polygons covered = offset(covered_pls, float(wave_flow.scaled_width() / 2),
                                  ClipperLib::jtRound, 0., ClipperLib::etOpenRound);
        append(covered_total, covered);

        result.paths.push_back(std::move(region_paths));
    }

    if (! covered_total.empty())
        result.residual = union_safety_offset(covered_total);

    return result;
}

} // namespace Slic3r::WaveOverhangs
