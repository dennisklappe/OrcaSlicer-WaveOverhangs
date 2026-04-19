///|/ Kaiser LaSO wave-overhang generator — full port of Rieks Kaiser's reference.
///|/
///|/ Original algorithm: Rieks Kaiser (riekskaiser) — https://github.com/riekskaiser/wave_LaSO.
///|/ Original license: MIT. Reimplemented in C++ under AGPL-3.0 compatibility
///|/   (re-implementation, not code derivation).
///|/
///|/ OrcaSlicer port & modifications: Dennis Klappe (dennisklappe).
///|/ Differences from the Python reference (forced by in-slicer integration,
///|/ none change the generation algorithm):
///|/   - Seed and boundary polygons come from the slicer's layer geometry
///|/     instead of being parsed from post-processed G-code.
///|/   - Multi-region handling per layer (Kaiser's original processed one
///|/     overhang at a time via user prompts).
///|/   - No pin-support placement (out of scope for this phase).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#ifndef slic3r_WaveOverhangs_KaiserGenerator_hpp_
#define slic3r_WaveOverhangs_KaiserGenerator_hpp_

#include "IGenerator.hpp"

namespace Slic3r::WaveOverhangs {

class KaiserGenerator : public IGenerator {
public:
    // Lateral overlap fraction between successive offset rings. Kaiser's
    // reference uses 0.15 (line 612 of CustomSupportInjector.py).
    double overlap = 0.15;

    GenerateResult generate(const ExPolygons   &overhang_area,
                            const Polygons     &lower_slices_polygons,
                            const CommonParams &params) override;
    const char *name() const override { return "kaiser"; }
};

} // namespace Slic3r::WaveOverhangs

#endif
