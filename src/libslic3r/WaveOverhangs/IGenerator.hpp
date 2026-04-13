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
    // Expert tunables (Kaiser-plumbed; Anderson ignores).
    double      anchor_bite            = 1.0;   // mm; seed-curve pre-offset into supported band.
    SpacingMode spacing_mode           = SpacingMode::Uniform;
    SeamMode    seam_mode              = SeamMode::Alternating;
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
