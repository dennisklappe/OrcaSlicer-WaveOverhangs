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
#include <functional>

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
Polylines extract_seed_curves(const ExPolygon &overhang, const Polygons &lower_slices, coord_t epsilon, coord_t anchor_bite)
{
    if (lower_slices.empty())
        return {};

    // Convert overhang boundary (contour + holes) to polylines.
    Polylines boundary_pls = to_polylines(overhang);
    if (boundary_pls.empty())
        return {};

    // Inflate the lower-slice polygons by epsilon (+anchor bite) to get a "supported" band.
    // anchor_bite pre-extends the band into the supported region so seeds reach further
    // in before the first offset ring is emitted.
    const coord_t band_infl = epsilon + std::max<coord_t>(0, anchor_bite);
    Polygons supported_band = expand(lower_slices, float(band_infl), ClipperLib::jtSquare, 0.);
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
    const double  base_step_mm = std::max(0.05, params.line_width * (1.0 - std::clamp(this->overlap, 0.0, 0.9)));
    const coord_t base_step    = std::max<coord_t>(1, coord_t(scale_(base_step_mm)));
    // Tight band around lower slices to detect supported (root) edge.
    const coord_t seed_epsilon = std::max<coord_t>(SCALED_EPSILON * 4, scale_(0.02));
    // Anchor-bite: how far seed extraction pre-extends into the supported region.
    const coord_t anchor_bite  = std::max<coord_t>(0, coord_t(scale_(std::max(0.0, params.anchor_bite))));

    // Compute overhang-only region (overhang_area minus lower slices) - this
    // is the area we are actually allowed to deposit Kaiser tracks in.
    BoundingBox bb = get_extents(overhang_area).inflated(SCALED_EPSILON);
    Polygons    optimized_lower = ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                                    lower_slices_polygons, bb);
    Polygons    overhangs_only  = diff(overhang_area, optimized_lower);
    if (overhangs_only.empty())
        return result;

    Polygons covered_total;

    // Scaled minimum contour-length filter: skip tiny overhangs.
    const coord_t min_contour_len = params.min_length_mm > 0.0
                                        ? coord_t(scale_(params.min_length_mm))
                                        : coord_t(0);

    for (const ExPolygon &overhang : union_ex(overhangs_only)) {
        if (min_contour_len > 0 && overhang.contour.length() < min_contour_len)
            continue;
        // Per-region max offset distance: bounding box diagonal is a safe upper bound.
        BoundingBox region_bb = get_extents(overhang);
        const coord_t max_extent = static_cast<coord_t>(std::hypot(
            double(region_bb.size().x()), double(region_bb.size().y())));
        if (max_extent <= 0)
            continue;

        // Auto-detect seed curve(s): the contour edges adjacent to lower slices.
        Polylines seeds = extract_seed_curves(overhang, lower_slices_polygons, seed_epsilon, anchor_bite);
        if (seeds.empty())
            continue; // No supported edge found; this is a floating island, skip.

        ExtrusionPaths region_paths;
        // Estimate max ring count for progressive-step normalization.
        const double max_rings_estimate = std::max(4.0, double(max_extent) / double(base_step));
        size_t       ring_index = 0;

        // Anchor passes: emit a few extra rings very close to the seed (root edge)
        // before the main wave fan. Each pass is offset by a fraction of line_width
        // so they nest next to the supported edge and bond strongly to the lower layer.
        const int anchor_passes = std::max(0, params.anchor_passes);
        for (int i = 0; i < anchor_passes; ++i) {
            const coord_t anchor_r = coord_t(scale_(std::max(0.01, params.line_width * 0.5 * double(i + 1))));
            Polygons anchor_buffer = offset(seeds, float(anchor_r),
                                            ClipperLib::jtRound, 0.,
                                            ClipperLib::etOpenRound);
            if (anchor_buffer.empty())
                continue;
            Polylines anchor_tracks = extract_offset_track(anchor_buffer, overhang);
            if (anchor_tracks.empty())
                continue;
            for (Polyline &pl : anchor_tracks)
                pl.simplify(params.scaled_resolution);
            anchor_tracks.erase(
                std::remove_if(anchor_tracks.begin(), anchor_tracks.end(),
                               [](const Polyline &p) { return p.points.size() < 2; }),
                anchor_tracks.end());
            if (anchor_tracks.empty())
                continue;
            extrusion_paths_append(region_paths, anchor_tracks, erOverhangPerimeter,
                                   wave_flow.mm3_per_mm(), wave_flow.width(), wave_flow.height());
        }

        // Kaiser safety cap on total rings (0 = unlimited).
        const int kaiser_max_rings = std::max(0, params.kaiser_max_rings);

        // Iteratively grow the seed outward.
        for (coord_t r = 0; r <= max_extent + base_step; ) {
            if (kaiser_max_rings > 0 && int(ring_index) >= kaiser_max_rings)
                break;
            // Compute this ring's step based on spacing_mode.
            coord_t step;
            if (params.spacing_mode == SpacingMode::Progressive) {
                // 70% of base step at root, growing to ~130% at tip.
                const double f = 0.7 + 0.6 * double(ring_index) / max_rings_estimate;
                step = std::max<coord_t>(1, coord_t(double(base_step) * f));
            } else {
                step = base_step;
            }
            r += step;
            if (r > max_extent + base_step)
                break;
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

            // Ring-direction logic driven by seam_mode.
            bool reverse_this_ring = false;
            switch (params.seam_mode) {
            case SeamMode::Alternating:
                // Boustrophedon: alternate direction every ring.
                reverse_this_ring = (ring_index & 1u) != 0u;
                break;
            case SeamMode::Aligned:
                // Keep every ring in the same direction.
                reverse_this_ring = false;
                break;
            case SeamMode::Random:
                // Deterministic per-ring pseudo-random choice (hash of ring index).
                reverse_this_ring = (std::hash<size_t>{}(ring_index) & 1u) != 0u;
                break;
            }
            if (reverse_this_ring) {
                for (Polyline &pl : tracks)
                    pl.reverse();
            }

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

            ++ring_index;
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
