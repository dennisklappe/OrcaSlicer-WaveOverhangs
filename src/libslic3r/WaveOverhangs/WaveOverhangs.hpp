///|/ Wave overhang generation.
///|/
///|/ Wave overhangs algorithm: Janis A. Andersons (andersonsjanis).
///|/ Builds on arc-overhang algorithm by Steven McCulloch (stmcculloch).
///|/ PrusaSlicer integration: Steven McCulloch.
///|/ Port to OrcaSlicer: Dennis Klappe (dennisklappe).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#ifndef slic3r_WaveOverhangs_hpp_
#define slic3r_WaveOverhangs_hpp_

#include <tuple>
#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::WaveOverhangs {

std::tuple<std::vector<ExtrusionPaths>, Polygons> generate(
    ExPolygons      infill_area,
    const Polygons &lower_slices_polygons,
    int             perimeter_count,
    int             additional_shell_count,
    double          wave_perimeter_overlap,
    double          minimum_wave_width,
    WaveOverhangPattern wave_pattern,
    double          wave_line_spacing,
    double          wave_line_width,
    const Flow     &overhang_flow,
    double          scaled_resolution,
    // Andersons' PropagationParams mirror — see IGenerator.hpp CommonParams.
    double          wavefront_advance         = 0.7,
    double          discretization            = 0.35,
    int             max_iterations            = 0,
    double          min_new_area_mm2          = 0.01,
    int             arc_resolution            = 24,
    bool            use_instead_of_bridges    = false);

} // namespace Slic3r::WaveOverhangs

#endif
