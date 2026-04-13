///|/ Wave overhangs algorithm: Janis A. Andersons (andersonsjanis).
///|/ Builds on arc-overhang algorithm by Steven McCulloch (stmcculloch).
///|/ PrusaSlicer integration: Steven McCulloch.
///|/ Port to OrcaSlicer: Dennis Klappe (dennisklappe).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#include "AndersonGenerator.hpp"
#include "WaveOverhangs.hpp"

namespace Slic3r::WaveOverhangs {

GenerateResult AndersonGenerator::generate(const ExPolygons   &overhang_area,
                                           const Polygons     &lower_slices_polygons,
                                           const CommonParams &params)
{
    auto [paths, residual] = ::Slic3r::WaveOverhangs::generate(
        overhang_area,
        lower_slices_polygons,
        params.perimeter_count,
        params.additional_shell_count,
        params.line_spacing,
        params.line_width,
        params.overhang_flow,
        params.scaled_resolution);

    GenerateResult r;
    r.paths    = std::move(paths);
    r.residual = std::move(residual);
    return r;
}

} // namespace Slic3r::WaveOverhangs
