///|/ Wave overhang generator interface (algorithm abstraction).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#ifndef slic3r_WaveOverhangs_IGenerator_hpp_
#define slic3r_WaveOverhangs_IGenerator_hpp_

#include <tuple>
#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::WaveOverhangs {

// Ring spacing mode (uniform constant step vs progressively growing step).
enum class SpacingMode { Uniform, Progressive };

// Inter-ring seam/direction mode (see wave_overhang_seam_mode in PrintConfig).
enum class SeamMode { Alternating, Aligned, Random };

struct CommonParams {
    int         perimeter_count        = 1;
    int         additional_shell_count = 0;
    double      line_spacing           = 0.35;
    double      line_width             = 0.4;
    Flow        overhang_flow;
    double      scaled_resolution      = 1.0;
    SpacingMode spacing_mode           = SpacingMode::Uniform;   // Andersons only.
    SeamMode    seam_mode              = SeamMode::Alternating;  // shared.
    double      min_length_mm          = 0.0;   // mm; skip overhangs whose contour length is below this.
    int         max_iterations         = 0;     // 0 = unlimited; safety cap on main loop (wavefronts for Andersons, rings for Kaiser).
    // Alpha.6 tunables (Andersons only).
    double      perimeter_overlap      = 0.1;   // mm; extend wave propagation toward perimeters.
    double      minimum_wave_width     = 0.7;   // mm; split wave region when a neck is narrower than this.
    WaveOverhangPattern pattern        = WaveOverhangPattern::Smart;
    double      min_new_area           = 0.01;  // mm^2; early-termination threshold on new-area growth.
    bool        use_instead_of_bridges = false; // when true, wave over flat bridgeable spans too.
};

struct GenerateResult {
    std::vector<ExtrusionPaths> paths;     // per-region
    Polygons                    residual;  // covered area (subtracted from infill upstream)
};

class IGenerator {
public:
    virtual ~IGenerator() = default;
    virtual GenerateResult generate(const ExPolygons   &overhang_area,
                                    const Polygons     &lower_slices_polygons,
                                    const CommonParams &params) = 0;
    virtual const char *name() const = 0;
};

} // namespace Slic3r::WaveOverhangs

#endif
