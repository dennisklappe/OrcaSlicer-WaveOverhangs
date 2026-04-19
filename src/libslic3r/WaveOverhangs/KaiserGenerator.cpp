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

// Port of Kaiser's densify_curve(seed, spacing=0.1): resample the polyline
// so consecutive vertices are at most `spacing_scaled` apart. Needed before
// offsetting because ClipperLib's buffer, like Shapely's, approximates the
// round-cap arc by subdividing edges — too-long edges produce visibly faceted
// loops instead of the smooth arcs Kaiser's reference emits.
void densify_polyline(Polyline &pl, coord_t spacing_scaled)
{
    if (pl.points.size() < 2 || spacing_scaled <= 0)
        return;
    Points dense;
    dense.reserve(pl.points.size() * 2);
    dense.push_back(pl.points.front());
    for (size_t i = 1; i < pl.points.size(); ++i) {
        const Point &a = pl.points[i - 1];
        const Point &b = pl.points[i];
        const double dx = double(b.x()) - double(a.x());
        const double dy = double(b.y()) - double(a.y());
        const double len = std::hypot(dx, dy);
        if (len > double(spacing_scaled)) {
            const int n = int(std::floor(len / double(spacing_scaled)));
            for (int k = 1; k <= n; ++k) {
                const double t = double(k) / double(n + 1);
                dense.emplace_back(
                    coord_t(double(a.x()) + dx * t),
                    coord_t(double(a.y()) + dy * t));
            }
        }
        dense.push_back(b);
    }
    pl.points = std::move(dense);
}

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

// Convert offset-buffer Polygons to closed track Polylines. Kaiser's Python
// reference emits the FULL exterior of each offset shape, which straddles both
// supported and overhang regions — the "return" half of the loop lands on the
// supported edge and provides the anchor. Clipping to overhang-only (the old
// behaviour) stripped the anchor and left every ring floating in air.
//
// We take the closed polygon exterior verbatim; containment against a slightly
// expanded anchor mask is done by the caller so the ring's tails can reach
// onto the supported region without running deep into already-perimetered area.
Polylines extract_offset_track(const Polygons &offset_polys, const ExPolygon &overhang, const Polygons &anchor_mask)
{
    if (offset_polys.empty())
        return {};

    Polylines candidate;
    candidate.reserve(offset_polys.size());
    for (const Polygon &poly : offset_polys) {
        if (poly.points.size() < 2)
            continue;
        Polyline pl;
        pl.points = poly.points;
        pl.points.push_back(poly.points.front()); // close the ring
        candidate.push_back(std::move(pl));
    }

    // Allow the ring to live on the overhang OR within the anchor mask (a thin
    // band into the supported region). This preserves the root-anchor half of
    // the loop that Kaiser's post-processor relies on.
    Polygons allowed = anchor_mask.empty() ? to_polygons(overhang) : union_(to_polygons(overhang), anchor_mask);
    Polylines clipped = intersection_pl(candidate, allowed);

    clipped.erase(
        std::remove_if(clipped.begin(), clipped.end(), [](const Polyline &p) {
            return p.points.size() < 2 || p.length() < scale_(0.2);
        }),
        clipped.end());

    return clipped;
}

// Ports Kaiser's Python `closest_point(xlast, ylast, points)` — pick the
// vertex of `pl` nearest to `from`, and (if the polyline is a closed ring)
// rotate it so that vertex is the start. For an open polyline, return it
// unchanged if its first endpoint is already the nearer end, otherwise
// reverse. Matches Kaiser's travel-minimising ring-start behaviour.
void orient_ring_toward(Polyline &pl, const Point &from)
{
    if (pl.points.size() < 2)
        return;
    const bool closed = pl.points.front() == pl.points.back();
    if (closed) {
        // Drop the duplicate last point while rotating, then restore it.
        pl.points.pop_back();
        size_t best_i = 0;
        double best_d2 = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < pl.points.size(); ++i) {
            const double dx = double(pl.points[i].x()) - double(from.x());
            const double dy = double(pl.points[i].y()) - double(from.y());
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_i = i; }
        }
        if (best_i != 0)
            std::rotate(pl.points.begin(), pl.points.begin() + best_i, pl.points.end());
        pl.points.push_back(pl.points.front()); // re-close
    } else {
        const double d2_front = std::pow(double(pl.points.front().x()) - double(from.x()), 2) +
                                std::pow(double(pl.points.front().y()) - double(from.y()), 2);
        const double d2_back  = std::pow(double(pl.points.back().x())  - double(from.x()), 2) +
                                std::pow(double(pl.points.back().y())  - double(from.y()), 2);
        if (d2_back < d2_front)
            pl.reverse();
    }
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
    // Anchor band: thin strip INSIDE the supported region where ring tails are
    // allowed to land for anchoring. Kaiser's post-processor achieves the same
    // effect by deleting original perimeters in the wave region; in our
    // pipeline we instead mark the ring's union as covered so Orca skips its
    // normal perimeters there (see `result.residual` below).
    const coord_t anchor_strip_width = std::max<coord_t>(
        coord_t(scale_(std::max(0.1, params.line_width * 2.0))),
        anchor_bite);

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

        // Build the anchor mask for this region: intersection of the supported
        // lower slice with the overhang's contour-adjacent band. Ring tails that
        // reach into this strip survive the extract_offset_track clip, giving
        // us the root-anchor behaviour from Kaiser's Python reference.
        Polygons anchor_mask;
        {
            BoundingBox bbx = get_extents(overhang).inflated(anchor_strip_width * 2);
            Polygons    lower_local = ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                                          lower_slices_polygons, bbx);
            Polygons    overhang_neighbourhood = expand(to_polygons(overhang), float(anchor_strip_width),
                                                        ClipperLib::jtRound, 0.);
            anchor_mask = intersection(lower_local, overhang_neighbourhood);
        }

        // Auto-detect seed curve(s): the contour edges adjacent to lower slices.
        Polylines seeds = extract_seed_curves(overhang, lower_slices_polygons, seed_epsilon, anchor_bite);
        if (seeds.empty())
            continue; // No supported edge found; this is a floating island, skip.

        // Kaiser's Python densifies each seed to 0.1 mm spacing before
        // offsetting so the round caps produce smooth arcs. Our seeds come
        // from clip operations and often have long straight segments — without
        // densification they'd produce visibly faceted rings that don't match
        // the reference.
        const coord_t densify_spacing = scale_(0.1);
        for (Polyline &pl : seeds)
            densify_polyline(pl, densify_spacing);

        // Optional seed-direction bias: rotate each seed polyline around its centroid.
        if (std::abs(params.direction_bias_deg) > 1e-6) {
            const double ang = params.direction_bias_deg * M_PI / 180.0;
            const double ca  = std::cos(ang);
            const double sa  = std::sin(ang);
            for (Polyline &pl : seeds) {
                if (pl.points.size() < 2)
                    continue;
                // Centroid (simple mean of points).
                double cx = 0, cy = 0;
                for (const Point &p : pl.points) { cx += double(p.x()); cy += double(p.y()); }
                cx /= double(pl.points.size());
                cy /= double(pl.points.size());
                for (Point &p : pl.points) {
                    const double dx = double(p.x()) - cx;
                    const double dy = double(p.y()) - cy;
                    p.x() = coord_t(cx + dx * ca - dy * sa);
                    p.y() = coord_t(cy + dx * sa + dy * ca);
                }
            }
        }

        ExtrusionPaths region_paths;
        // Estimate max ring count for progressive-step normalization.
        const double max_rings_estimate = std::max(4.0, double(max_extent) / double(base_step));
        size_t       ring_index = 0;
        // Tool position tracking. Kaiser's Python `closest_point(xlast, ylast, ...)`
        // picks each ring's start vertex to minimise travel from the previous
        // ring's end; we follow the same heuristic. Seed with the midpoint of
        // the first seed polyline so ring 0 starts near the root edge.
        Point last_tip{ 0, 0 };
        bool  have_last_tip = false;
        if (!seeds.empty() && seeds.front().points.size() >= 2) {
            const size_t mid = seeds.front().points.size() / 2;
            last_tip = seeds.front().points[mid];
            have_last_tip = true;
        }

        // Anchor passes: emit a few extra rings very close to the seed (root edge)
        // before the main wave fan. Each pass is offset by a fraction of line_width
        // so they nest next to the supported edge and bond strongly to the lower layer.
        const int anchor_passes = std::max(0, params.anchor_passes);
        for (int i = 0; i < anchor_passes; ++i) {
            const coord_t anchor_r = coord_t(scale_(std::max(0.01, params.line_width * 0.5 * double(i + 1))));
            // ClipperLib's arc_tolerance ≈ r × (1 − cos(π/(2·N))). Picking N=8
            // (Shapely's default resolution) gives ~0.019 × r and matches the
            // reference's round-cap fidelity.
            const double anchor_arc_tol = std::max(1.0, double(anchor_r) * 0.019);
            Polygons anchor_buffer = offset(seeds, float(anchor_r),
                                            ClipperLib::jtRound, anchor_arc_tol,
                                            ClipperLib::etOpenRound);
            if (anchor_buffer.empty())
                continue;
            Polylines anchor_tracks = extract_offset_track(anchor_buffer, overhang, anchor_mask);
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
            if (have_last_tip) {
                for (Polyline &pl : anchor_tracks)
                    orient_ring_toward(pl, last_tip);
            }
            extrusion_paths_append(region_paths, anchor_tracks, erOverhangPerimeter,
                                   wave_flow.mm3_per_mm(), wave_flow.width(), wave_flow.height());
            if (!anchor_tracks.empty() && anchor_tracks.back().points.size() >= 2) {
                last_tip      = anchor_tracks.back().points.back();
                have_last_tip = true;
            }
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
            const double arc_tol = std::max(1.0, double(r) * 0.019); // matches Shapely resolution=8
            Polygons offset_buffer = offset(seeds, float(r),
                                            ClipperLib::jtRound, arc_tol,
                                            ClipperLib::etOpenRound);
            if (offset_buffer.empty())
                break;

            Polylines tracks = extract_offset_track(offset_buffer, overhang, anchor_mask);
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

            // Kaiser-style seam picking: rotate each closed ring (or flip each
            // open fragment) so it starts nearest to where the previous ring
            // ended. Minimises travel and matches the reference's print order.
            if (have_last_tip) {
                for (Polyline &pl : tracks)
                    orient_ring_toward(pl, last_tip);
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

            // Remember where this ring ended so the next one starts there.
            if (!tracks.empty() && tracks.back().points.size() >= 2) {
                last_tip     = tracks.back().points.back();
                have_last_tip = true;
            }

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
