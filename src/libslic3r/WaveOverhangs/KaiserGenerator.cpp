///|/ Kaiser LaSO wave-overhang generator — full port of Rieks Kaiser's reference.
///|/
///|/ Original algorithm: Rieks Kaiser (riekskaiser) — https://github.com/riekskaiser/wave_LaSO.
///|/ Original license: MIT. Reimplemented in C++ under AGPL-3.0 compatibility
///|/   (re-implementation, not code derivation).
///|/
///|/ OrcaSlicer port & modifications: Dennis Klappe (dennisklappe).
///|/ Differences from the Python reference (all forced by in-slicer integration;
///|/ none change the generation algorithm):
///|/   - Seed and boundary polygons come from the slicer's layer geometry instead
///|/     of being parsed out of post-processed G-code; their geometric definitions
///|/     match the reference exactly (see generate() below).
///|/   - Multi-region handling per layer — Kaiser's original processed one
///|/     overhang at a time via user prompts.
///|/   - No pin-support placement (out of scope for this phase).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#include "KaiserGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::WaveOverhangs {
namespace {

// Resample a polyline so consecutive vertices are at most `spacing_scaled` apart.
// ClipperLib's jtRound arc_tolerance controls buffer arc smoothness on its own,
// but densifying the seed keeps the subsequent intersection / iterative-offset
// geometry stable on long straight seed segments.
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

void densify_polygons_contour(Polygons &polys, coord_t spacing_scaled)
{
    for (Polygon &p : polys) {
        if (p.points.size() < 2) continue;
        Polyline pl;
        pl.points = p.points;
        pl.points.push_back(p.points.front());
        densify_polyline(pl, spacing_scaled);
        if (pl.points.size() >= 2 && pl.points.front() == pl.points.back())
            pl.points.pop_back();
        p.points = std::move(pl.points);
    }
}

// True iff `p` lies within `tol` (scaled) of any boundary segment. Matches
// Kaiser's `boundary.boundary.distance(Point) < 1e-6`.
bool point_on_any_line(const Point &p, const Lines &boundary_lines, double tol)
{
    const double tol2 = tol * tol;
    for (const Line &l : boundary_lines) {
        if (l.distance_to_squared(p) < tol2)
            return true;
    }
    return false;
}

} // namespace

GenerateResult KaiserGenerator::generate(const ExPolygons   &overhang_area,
                                         const Polygons     &lower_slices_polygons,
                                         const CommonParams &params)
{
    GenerateResult result;
    if (overhang_area.empty() || lower_slices_polygons.empty())
        return result;

    // --- Core Kaiser parameters (names mirror the Python reference) ---
    const double nozzle_mm     = double(params.overhang_flow.nozzle_diameter());
    const double line_width_mm = params.line_width > 0.0 ? params.line_width : nozzle_mm;
    const double overlap_c     = std::clamp(this->overlap, 0.0, 0.9);
    // r = linewidth * (1 - overlap). Distance between successive rings.
    const double r_mm          = line_width_mm * (1.0 - overlap_c);
    const coord_t r_scaled     = std::max<coord_t>(1, coord_t(scale_(r_mm)));
    // Seed = previous-layer outer wall .buffer(-2*nozzlesize).
    const coord_t seed_inset   = coord_t(scale_(2.0 * nozzle_mm));
    // First ring at r/2 (Kaiser line 430).
    const coord_t first_offset = std::max<coord_t>(1, r_scaled / 2);
    const coord_t densify_spacing_scaled = coord_t(scale_(0.1));

    // Placeholder extrusion rate for the ExtrusionPath constructor. The final
    // mm³/mm is set by PerimeterGenerator from wave_overhang_flow_mm3_per_mm
    // (auto-derives to nozzle² when 0), applied uniformly to both algorithms
    // so users get one knob with the same physics model regardless of which
    // generator emitted the rings.
    const double kaiser_mm3_per_mm = nozzle_mm * nozzle_mm;

    const Flow wave_flow = params.line_width > 0. ? params.overhang_flow.with_width(float(line_width_mm))
                                                  : params.overhang_flow;

    // arc_tolerance ≈ r × (1 - cos(π/(2N))). For N=8 (Shapely resolution=8),
    // that's r × 0.019 — matches the Python reference's cap fidelity.
    auto arc_tol = [](coord_t r) { return std::max(1.0, double(r) * 0.019); };

    // Vertex-on-boundary tolerance: small enough to rule out interior vertices,
    // large enough to absorb Clipper's integer-snap around intersection points.
    const double boundary_touch_tol = std::max(double(SCALED_EPSILON) * 8, scale_(0.001));

    // Safety cap on rings; 0 in config means unlimited, we still cap defensively.
    const int cap = std::max(0, params.max_iterations);
    const int hard_ring_cap = cap > 0 ? cap : 10000;

    Polygons covered_total;

    for (const ExPolygon &overhang : union_ex(overhang_area)) {
        if (overhang.contour.points.size() < 3) continue;

        if (params.min_length_mm > 0.0) {
            const coord_t min_len_scaled = coord_t(scale_(params.min_length_mm));
            if (overhang.contour.length() < min_len_scaled) continue;
        }

        // Work-area bbox: the overhang region plus a generous margin so we
        // capture enough of the lower-slice for seed and boundary.
        BoundingBox work_bb = get_extents(overhang);
        work_bb.offset(coord_t(scale_(std::max(5.0, line_width_mm * 10.0))));

        Polygons lower_local = ClipperUtils::clip_clipper_polygons_with_subject_bbox(
            lower_slices_polygons, work_bb);
        if (lower_local.empty()) continue;

        // Seed: previous-layer outer wall shrunk inward by 2*nozzle.
        Polygons seed_polys = shrink(lower_local, float(seed_inset), ClipperLib::jtRound);
        if (seed_polys.empty()) continue;

        // Boundary: current-layer outer wall shrunk inward by r. In the Python
        // reference the current-layer outer wall is captured from g-code; here
        // the current layer's shape in this neighbourhood equals
        // (overhang ∪ lower_local) — the overhang covers the part of the
        // current layer that sits above nothing, the lower_local polys cover
        // the part that sits above supported material.
        Polygons current_layer_local = union_(to_polygons(overhang), lower_local);
        Polygons boundary_polys = shrink(current_layer_local, float(r_scaled), ClipperLib::jtRound);
        if (boundary_polys.empty()) continue;

        densify_polygons_contour(seed_polys, densify_spacing_scaled);

        const Lines boundary_lines = to_lines(boundary_polys);

        // Wave rings naturally grow through the supported region before
        // reaching the overhang (Kaiser's algorithm is built that way: seed
        // inside support, boundary at current-layer wall). Kaiser's Python
        // gets away with it because it's a post-processor that REPLACES the
        // whole overhang layer's perimeters — no double print. We run
        // alongside Orca's normal perimeter generator, so any extrusion we
        // emit over supported material piles on top of a normal perimeter.
        //
        // Pure overhang-only clipping fixed the top-surfaces bug but broke
        // anchoring: the first overhang ring had no previous-ring material
        // to bond to because the equivalent ring in the supported region
        // was clipped out. We need a thin anchor strip — the band of
        // supported material directly adjacent to the overhang edge — so
        // the first overhang ring has something to land on. Far-away top
        // surfaces (not adjacent to any overhang) stay excluded.
        const Polygons overhang_polys = to_polygons(overhang);
        const Polygons overhang_genuine = diff(overhang_polys, lower_slices_polygons);
        if (overhang_genuine.empty()) continue;
        // Anchor band: expand the overhang into the support by ~1 line width,
        // then intersect with the support so we don't leak out of the layer.
        const coord_t anchor_band_scaled = std::max<coord_t>(1, coord_t(scale_(line_width_mm * 1.5)));
        const Polygons anchor_band = intersection(
            expand(overhang_genuine, float(anchor_band_scaled), ClipperLib::jtRound),
            lower_slices_polygons);
        // Final extrusion clip: overhang + adjacent support strip.
        const Polygons overhang_only = union_(overhang_polys, anchor_band);
        if (overhang_only.empty()) continue;

        // --- Initial offset: offsets(seed, r/2) ∩ boundary (Kaiser line 430).
        Polygons current_shape = offset(seed_polys, float(first_offset),
                                        ClipperLib::jtRound, arc_tol(first_offset));
        current_shape = intersection(current_shape, boundary_polys);
        if (current_shape.empty()) continue;

        ExtrusionPaths region_paths;
        Point last_tip{ 0, 0 };
        bool  have_last_tip = false;

        auto total_area = [](const Polygons &ps) {
            double a = 0.;
            for (const Polygon &p : ps) a += std::abs(p.area());
            return a;
        };

        int ring_index = 0;

        while (!current_shape.empty() && ring_index < hard_ring_cap) {
            for (const ExPolygon &ring_ex : union_ex(current_shape)) {
                const Polygon &ring_poly = ring_ex.contour;
                if (ring_poly.points.size() < 3) continue;

                // Close ring: pts[last] == pts[0].
                Points pts = ring_poly.points;
                pts.push_back(ring_poly.points.front());

                std::vector<bool> on_bd(pts.size(), false);
                for (size_t i = 0; i < pts.size(); ++i)
                    on_bd[i] = point_on_any_line(pts[i], boundary_lines, boundary_touch_tol);

                // Alternate direction on odd rings (Kaiser line 467) unless
                // user overrode seam_mode. Aligned → never reverse; Random →
                // per-ring pseudo-random.
                bool reverse_this_ring = (ring_index & 1) != 0;
                switch (params.seam_mode) {
                case SeamMode::Aligned:
                    reverse_this_ring = false; break;
                case SeamMode::Random:
                    reverse_this_ring = (std::hash<int>{}(ring_index) & 1u) != 0u; break;
                case SeamMode::Alternating:
                default:
                    break;
                }
                if (reverse_this_ring) {
                    std::reverse(pts.begin(), pts.end());
                    std::reverse(on_bd.begin(), on_bd.end());
                }

                // Start-point selection (Kaiser line 483): nearest boundary
                // vertex to last_tip. If no boundary vertex on this ring,
                // fall back to nearest vertex of any kind — rare case, only
                // hits tiny rings that never touch the boundary.
                if (have_last_tip && pts.size() >= 2) {
                    const size_t last = pts.size() - 1; // drop duplicate for rotate
                    size_t best_i = SIZE_MAX;
                    double best_d2 = std::numeric_limits<double>::infinity();
                    for (size_t i = 0; i < last; ++i) {
                        if (!on_bd[i]) continue;
                        const double dx = double(pts[i].x()) - double(last_tip.x());
                        const double dy = double(pts[i].y()) - double(last_tip.y());
                        const double d2 = dx * dx + dy * dy;
                        if (d2 < best_d2) { best_d2 = d2; best_i = i; }
                    }
                    if (best_i == SIZE_MAX) {
                        for (size_t i = 0; i < last; ++i) {
                            const double dx = double(pts[i].x()) - double(last_tip.x());
                            const double dy = double(pts[i].y()) - double(last_tip.y());
                            const double d2 = dx * dx + dy * dy;
                            if (d2 < best_d2) { best_d2 = d2; best_i = i; }
                        }
                    }
                    if (best_i != SIZE_MAX && best_i != 0) {
                        pts.pop_back(); on_bd.pop_back();
                        std::rotate(pts.begin(), pts.begin() + best_i, pts.end());
                        std::rotate(on_bd.begin(), on_bd.begin() + best_i, on_bd.end());
                        pts.push_back(pts.front());
                        on_bd.push_back(on_bd.front());
                    }
                }

                // Split ring into extrude runs, skipping segments where BOTH
                // endpoints sit on the current-layer boundary (those are
                // travel moves — the outer wall is printed separately).
                Polyline extrude_run;
                auto flush_run = [&]() {
                    if (extrude_run.points.size() >= 2) {
                        Polylines clipped = intersection_pl(Polylines{ std::move(extrude_run) }, overhang_only);
                        for (Polyline &pl : clipped) {
                            pl.simplify(std::min(0.05 * double(r_scaled), params.scaled_resolution));
                            if (pl.points.size() >= 2) {
                                extrusion_paths_append(region_paths, std::move(pl),
                                                       erOverhangPerimeter, kaiser_mm3_per_mm,
                                                       float(wave_flow.width()), float(wave_flow.height()));
                            }
                        }
                    }
                    extrude_run.points.clear();
                };

                for (size_t i = 1; i < pts.size(); ++i) {
                    const bool travel = on_bd[i-1] && on_bd[i];
                    if (travel) {
                        flush_run();
                    } else {
                        if (extrude_run.points.empty())
                            extrude_run.points.push_back(pts[i-1]);
                        extrude_run.points.push_back(pts[i]);
                    }
                }
                flush_run();

                if (pts.size() >= 2) {
                    last_tip = pts.back();
                    have_last_tip = true;
                }
            }

            // next_shape = (offset(current_shape, r) .buffer(small)) ∩ boundary
            // (Kaiser line 559-561). The tiny positive buffer heals sliver
            // gaps between the outward offset and the clip.
            const double next_arc_tol = arc_tol(r_scaled);
            Polygons next_shape = offset(current_shape, float(r_scaled),
                                         ClipperLib::jtRound, next_arc_tol);
            if (next_shape.empty()) break;
            next_shape = offset(next_shape, float(std::max<coord_t>(1, SCALED_EPSILON)),
                                ClipperLib::jtRound);
            next_shape = intersection(next_shape, boundary_polys);
            if (next_shape.empty()) break;

            // Saturation: if the shape hasn't grown measurably, we've filled
            // the overhang and further iteration won't add new rings.
            if (total_area(next_shape) <= total_area(current_shape) + double(SCALED_EPSILON) * 1000.0)
                break;

            current_shape = std::move(next_shape);
            ++ring_index;
        }

        if (region_paths.empty()) continue;
        for (ExtrusionPath &p : region_paths) p.wave_overhang = true;

        Polylines covered_pls;
        covered_pls.reserve(region_paths.size());
        for (const ExtrusionPath &p : region_paths) covered_pls.push_back(p.polyline);
        Polygons covered = offset(covered_pls, float(wave_flow.scaled_width() / 2),
                                  ClipperLib::jtRound, 0., ClipperLib::etOpenRound);
        append(covered_total, covered);

        result.paths.push_back(std::move(region_paths));
    }

    if (!covered_total.empty())
        result.residual = union_safety_offset(covered_total);

    return result;
}

} // namespace Slic3r::WaveOverhangs
