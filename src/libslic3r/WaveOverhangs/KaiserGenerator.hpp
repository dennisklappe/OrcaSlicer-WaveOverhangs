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
#ifndef slic3r_WaveOverhangs_KaiserGenerator_hpp_
#define slic3r_WaveOverhangs_KaiserGenerator_hpp_

#include "IGenerator.hpp"

namespace Slic3r::WaveOverhangs {

class KaiserGenerator : public IGenerator {
public:
    // Lateral overlap fraction between successive offset rings (Kaiser default 0.15).
    double overlap = 0.15;

    GenerateResult generate(const ExPolygons   &overhang_area,
                            const Polygons     &lower_slices_polygons,
                            const CommonParams &params) override;
    const char *name() const override { return "kaiser"; }
};

} // namespace Slic3r::WaveOverhangs

#endif
