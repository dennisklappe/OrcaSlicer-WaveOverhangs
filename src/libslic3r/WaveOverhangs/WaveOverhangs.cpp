///|/ Wave overhang generation.
///|/
///|/ Wave overhangs algorithm: Janis A. Andersons (andersonsjanis).
///|/ Builds on arc-overhang algorithm by Steven McCulloch (stmcculloch).
///|/ PrusaSlicer integration: Steven McCulloch.
///|/ Port to OrcaSlicer: Dennis Klappe (dennisklappe).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#include "WaveOverhangs.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <utility>

#include "Algorithm/RegionExpansion.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntity.hpp"
#include "ExPolygon.hpp"
#include "Polyline.hpp"
#include "libslic3r.h"

namespace Slic3r::WaveOverhangs {
namespace {

#define EXTRA_PERIMETER_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.

Polylines reconnect_polylines(const Polylines &polylines, double limit_distance)
{
    if (polylines.empty())
        return polylines;

    std::unordered_map<size_t, Polyline> connected;
    connected.reserve(polylines.size());
    for (size_t i = 0; i < polylines.size(); ++i) {
        if (! polylines[i].empty())
            connected.emplace(i, polylines[i]);
    }

    for (size_t a = 0; a < polylines.size(); ++a) {
        auto base_it = connected.find(a);
        if (base_it == connected.end())
            continue;

        Polyline &base = base_it->second;
        for (size_t b = a + 1; b < polylines.size(); ++b) {
            auto next_it = connected.find(b);
            if (next_it == connected.end())
                continue;

            Polyline &next = next_it->second;
            if ((base.last_point() - next.first_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                base.append(std::move(next));
                connected.erase(next_it);
            } else if ((base.last_point() - next.last_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                base.points.insert(base.points.end(), next.points.rbegin(), next.points.rend());
                connected.erase(next_it);
            } else if ((base.first_point() - next.last_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                next.append(std::move(base));
                base = std::move(next);
                base.reverse();
                connected.erase(next_it);
            } else if ((base.first_point() - next.first_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                base.reverse();
                base.append(std::move(next));
                base.reverse();
                connected.erase(next_it);
            }
        }
    }

    Polylines result;
    result.reserve(connected.size());
    for (auto &entry : connected)
        result.push_back(std::move(entry.second));
    return result;
}

Polylines generate_wave_overhang_seeds(const ExPolygon &boundary, const Polygons &anchoring, const coord_t seed_expansion)
{
    if (anchoring.empty())
        return {};

    Polylines seeds;
    for (const Algorithm::WaveSeed &seed : Algorithm::wave_seeds(to_expolygons(anchoring), ExPolygons{ boundary }, float(seed_expansion), true)) {
        if (seed.boundary == 0 && seed.path.size() >= 2)
            seeds.emplace_back(seed.path);
    }

    if (seeds.empty())
        seeds = intersection_pl(to_polylines(boundary), offset(anchoring, float(seed_expansion), jtRound, 0.));

    return seeds;
}

void tag_wave_overhang_paths(std::vector<ExtrusionPaths> &wave_paths)
{
    for (ExtrusionPaths &region : wave_paths)
        for (ExtrusionPath &path : region)
            path.wave_overhang = true;
}

void append_shell_perimeters(ExtrusionPaths &overhang_region,
                             const Polygons &overhang_to_cover,
                             int             outer_perimeter_count,
                             coord_t         perimeter_spacing,
                             const Flow     &perimeter_flow,
                             double          scaled_resolution)
{
    if (outer_perimeter_count <= 0)
        return;

    Polygons shell_centerline = shrink(overhang_to_cover, std::max<coord_t>(1, perimeter_flow.scaled_width() / 2), jtRound, 0.);
    for (int i = 0; i < outer_perimeter_count && ! shell_centerline.empty(); ++i) {
        Polylines shell_loops = to_polylines(shell_centerline);
        for (Polyline &loop : shell_loops)
            loop.simplify(std::min(0.05 * perimeter_spacing, scaled_resolution));
        shell_loops.erase(
            std::remove_if(shell_loops.begin(), shell_loops.end(), [](const Polyline &loop) { return loop.points.size() < 2; }),
            shell_loops.end());

        if (! shell_loops.empty())
            extrusion_paths_append(overhang_region, shell_loops, erOverhangPerimeter,
                                   perimeter_flow.mm3_per_mm(), perimeter_flow.width(), perimeter_flow.height());

        shell_centerline = shrink(shell_centerline, perimeter_spacing, jtRound, 0.);
    }
}

void append_adaptive_fronts(ExtrusionPaths &overhang_region,
                            const Polygons &remaining_area,
                            const Flow     &wave_flow)
{
    if (remaining_area.empty())
        return;

    for (const ExPolygon &remaining : union_ex(to_expolygons(remaining_area))) {
        ThickPolylines thick_fronts;
        remaining.medial_axis(0., wave_flow.scaled_width(), &thick_fronts);

        for (const ThickPolyline &front : thick_fronts) {
            if (! front.is_valid())
                continue;

            const double width_sum = std::accumulate(front.width.begin(), front.width.end(), 0.0);
            const coord_t average_width = std::max<coord_t>(
                1,
                coord_t(std::lround(width_sum / std::max<size_t>(size_t(1), front.width.size()))));
            const float raw_adaptive_width = unscale<float>(average_width);
            const float min_terminal_width = wave_flow.width() / 3.f;
            if (raw_adaptive_width < min_terminal_width)
                continue;

            const float min_valid_width = std::max(
                unscale<float>(coord_t(1)),
                wave_flow.height() * float(1. - 0.25 * PI) + 1e-4f);
            const float adaptive_width = std::max(min_valid_width, raw_adaptive_width);

            const Flow adapted = wave_flow.with_width(adaptive_width);
            ExtrusionPath path(erOverhangPerimeter, adapted.mm3_per_mm(), adapted.width(), adapted.height());
            path.polyline = Polyline(front.points);
            overhang_region.push_back(std::move(path));
        }
    }
}

} // namespace

std::tuple<std::vector<ExtrusionPaths>, Polygons> generate(
    ExPolygons      infill_area,
    const Polygons &lower_slices_polygons,
    int             perimeter_count,
    int             additional_shell_count,
    double          wave_line_spacing,
    double          wave_line_width,
    const Flow     &overhang_flow,
    double          scaled_resolution)
{
    const coord_t base_spacing       = overhang_flow.scaled_spacing();
    const Flow    wave_flow          = wave_line_width > 0. ? overhang_flow.with_width(float(wave_line_width)) : overhang_flow;
    const coord_t wave_spacing       = std::max<coord_t>(1, wave_line_spacing > 0. ? coord_t(scale_(wave_line_spacing)) : base_spacing);
    const coord_t anchors_size       = std::min(coord_t(scale_(EXTERNAL_INFILL_MARGIN)), base_spacing * (perimeter_count + 1));
    const coord_t seed_expansion     = std::max<coord_t>(1, base_spacing / 10);
    const coord_t shell_inner_edge   = additional_shell_count > 0 ? overhang_flow.scaled_width() + (additional_shell_count - 1) * base_spacing : 0;
    const coord_t trim_inset         = std::max<coord_t>(std::max<coord_t>(1, wave_flow.scaled_width() / 2), shell_inner_edge + wave_flow.scaled_width() / 2);
    const coord_t filled_area_regularization = std::max<coord_t>(1, base_spacing / 2);
    const double  min_area_growth    = 0.05 * double(wave_spacing) * double(wave_spacing);

    BoundingBox infill_area_bb       = get_extents(infill_area).inflated(SCALED_EPSILON);
    Polygons    optimized_lower      = ClipperUtils::clip_clipper_polygons_with_subject_bbox(lower_slices_polygons, infill_area_bb);
    Polygons    overhangs            = diff(infill_area, optimized_lower);

    if (overhangs.empty())
        return {};

    Polygons anchors             = intersection(infill_area, optimized_lower);
    Polygons inset_anchors       = diff(anchors, expand(overhangs, anchors_size + 0.1 * overhang_flow.scaled_width(), EXTRA_PERIMETER_OFFSET_PARAMETERS));
    Polygons inset_overhang_area = diff(infill_area, inset_anchors);

    std::vector<ExtrusionPaths> wave_paths;
    Polygons                    filled_area;

    for (const ExPolygon &overhang : union_ex(to_expolygons(inset_overhang_area))) {
        Polygons overhang_to_cover = to_polygons(overhang);
        Polygons wave_cover_area   = additional_shell_count > 0 ? shrink(overhang_to_cover, shell_inner_edge, jtRound, 0.) : overhang_to_cover;
        Polygons real_overhang     = intersection(wave_cover_area, overhangs);
        if (real_overhang.empty())
            wave_cover_area.clear();

        ExtrusionPaths &overhang_region = wave_paths.emplace_back();

        for (const ExPolygon &wave_cover : union_ex(to_expolygons(wave_cover_area))) {
            Polygons wave_cover_polygons = to_polygons(wave_cover);
            const Polygons &seed_cover_polygons = additional_shell_count > 0 ? overhang_to_cover : wave_cover_polygons;
            const ExPolygon &seed_boundary      = additional_shell_count > 0 ? overhang : wave_cover;
            Polygons anchoring = intersection(expand(seed_cover_polygons, 1.1 * base_spacing, jtRound, 0.), inset_anchors);
            Polylines seeds    = generate_wave_overhang_seeds(seed_boundary, anchoring, seed_expansion);
            if (seeds.empty())
                continue;

            Polygons trim_boundary = shrink(wave_cover_polygons, std::max<coord_t>(1, wave_flow.scaled_width() / 2), jtRound, 0.);
            if (trim_boundary.empty())
                trim_boundary = shrink(wave_cover_polygons, 0.1 * base_spacing);
            if (trim_boundary.empty())
                trim_boundary = wave_cover_polygons;

            const coord_t seed_offset = additional_shell_count > 0 ? shell_inner_edge + seed_expansion : seed_expansion;
            Polygons accumulated_region = intersection(offset(seeds, float(seed_offset), jtRound, 0., ClipperLib::etOpenRound), wave_cover_polygons);
            if (accumulated_region.empty())
                continue;

            double accumulated_area = area(accumulated_region);
            const double wave_cover_area_value = area(wave_cover_polygons);

            for (;;) {
                Polygons next_region = intersection(offset(accumulated_region, float(wave_spacing), jtRound, 0.), wave_cover_polygons);
                if (next_region.empty())
                    break;

                double next_area = area(next_region);
                if (next_area <= accumulated_area + min_area_growth)
                    break;

                const bool is_terminal_step = next_area >= wave_cover_area_value - min_area_growth;
                if (is_terminal_step) {
                    Polygons printed_extent = intersection(
                        offset(accumulated_region, 0.5f * float(wave_flow.scaled_width()), jtRound, 0.),
                        wave_cover_polygons);
                    Polygons remaining_strip = diff(wave_cover_polygons, printed_extent);
                    append_adaptive_fronts(overhang_region, remaining_strip, wave_flow);
                    accumulated_region = std::move(next_region);
                    accumulated_area   = next_area;
                    break;
                }

                Polylines fronts = intersection_pl(to_polylines(next_region), trim_boundary);
                for (Polyline &front : fronts)
                    front.simplify(std::min(0.05 * wave_spacing, scaled_resolution));
                fronts.erase(
                    std::remove_if(fronts.begin(), fronts.end(), [](const Polyline &front) { return front.points.size() < 2; }),
                    fronts.end());
                fronts = reconnect_polylines(fronts, wave_spacing);

                if (! fronts.empty())
                    extrusion_paths_append(overhang_region, fronts, erOverhangPerimeter,
                                           wave_flow.mm3_per_mm(), wave_flow.width(), wave_flow.height());

                accumulated_region = std::move(next_region);
                accumulated_area   = next_area;
            }
        }

        overhang_region.erase(
            std::remove_if(overhang_region.begin(), overhang_region.end(), [](const ExtrusionPath &path) { return path.empty(); }),
            overhang_region.end());
        append_shell_perimeters(overhang_region, overhang_to_cover, additional_shell_count, base_spacing, overhang_flow, scaled_resolution);
        if (! overhang_region.empty())
            append(filled_area, additional_shell_count > 0 ? overhang_to_cover : wave_cover_area);
        if (overhang_region.empty())
            wave_paths.pop_back();
    }

    tag_wave_overhang_paths(wave_paths);
    return { wave_paths, union_safety_offset(closing_ex(filled_area, float(filled_area_regularization), jtRound, 0.)) };
}

} // namespace Slic3r::WaveOverhangs
