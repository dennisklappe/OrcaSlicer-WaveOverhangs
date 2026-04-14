# OrcaSlicer-WaveOverhangs

Fork of OrcaSlicer adding wave-pattern overhang printing with pluggable algorithms (Anderson/McCulloch + Kaiser LaSO) and extensive expert controls.

> **⚠️ Experimental / Alpha.** Builds and slices, not yet battle-tested on real prints. Feedback + test reports welcome.

---

## Status / Download

Prebuilt binaries for tagged releases → [github.com/dennisklappe/OrcaSlicer-WaveOverhangs/releases](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs/releases). Linux AppImage, Windows portable zip + installer, macOS universal DMG.

---

## What this is

Wave overhangs let you print steep overhangs without supports. Instead of using support structures, the slicer lays down wave-patterned perimeters where each pass anchors to the previous one, extending further out layer by layer.

Two algorithms are available in this fork:

- **Anderson** — port of stmcculloch's PrusaSlicer-WaveOverhangs. Builds on the arc-overhang technique with narrow-region splitting and smart/zig-zag/monotonic pattern selection.
- **Kaiser LaSO** — Lateral Supported Overhangs. A C++ reimplementation of Rieks Kaiser's MSc-thesis Python post-processor, brought inside the slicer pipeline.

The whole feature is 100% opt-in: leave the master toggle off and the fork behaves identically to upstream OrcaSlicer.

---

## The algorithms

|  | Anderson (arc-overhang derived) | Kaiser LaSO |
|---|---|---|
| **Geometric primitive** | Concentric arcs grown from interior seed points, clipped to the overhang boundary | Lateral offsets of a root-edge seed curve, buffered by `line_width × (1 − overlap)` each ring |
| **Seed** | Interior points where medial-axis fronts propagate | Curve anchored to the supported edge (auto-detected in our port as overhang-boundary ∩ lower-slice-boundary) |
| **Propagation** | Radial: arcs fan out | Boustrophedon: parallel rings alternate direction |
| **Research origin** | Andersons / Sanchez / Vaneker wave-overhang work (see below) | Kaiser's MSc thesis, University of Twente |

Both algorithms have strengths and weaknesses depending on the overhang geometry — try both on your model and compare.

---

## Credits & research references

**Wave overhang algorithm research**
> **Wave-inspired path-planning strategy for support-free horizontal overhangs in FDM** — paper to be published.
> Reference Python implementation + interactive visualiser: [andersonsjanis/Wave-overhangs](https://github.com/andersonsjanis/Wave-overhangs).
> Accompanying dataset (placeholder while paper is in review): [10.17632/xhw8xkjyc2.1](https://data.mendeley.com/datasets/xhw8xkjyc2/1).
> Authors: **Janis A. Andersons**, **Solemé Sanchez**, **Tom Vaneker**

**Arc-overhang algorithm** (predecessor that wave overhangs builds on)
> Steven McCulloch — https://github.com/stmcculloch/arc-overhang

**PrusaSlicer integration of wave overhangs** (what our Anderson port ports from)
> Steven McCulloch — https://github.com/stmcculloch/PrusaSlicer-WaveOverhangs
> "Wave overhangs algorithm developed and tested by Janis A. Andersons, Solemé Sanchez, and Tom Vaneker. Improves upon the Arc Overhang algorithm by Steven McCulloch."

**Kaiser LaSO algorithm** (the second algorithm available in this fork)
> Rieks Kaiser — https://github.com/riekskaiser/wave_LaSO
> "Investigating the warping of Laterally Supported Overhangs in fused deposition modelling; The python code.
> This code was written in the context of Rieks Kaiser's master thesis, for the study Mechanical Engineering at the University of Twente."

**OrcaSlicer base**
> OrcaSlicer team — https://github.com/OrcaSlicer/OrcaSlicer

---

## Features added in this fork

- Ported Anderson wave-overhang algorithm from stmcculloch's PrusaSlicer fork into OrcaSlicer's `PerimeterGenerator` pipeline.
- Synced upstream stmcculloch's alpha.6 improvements: narrow-region splitting, smart/zig-zag/monotonic pattern selection, perimeter-overlap margin, support-fallback for unfilled areas.
- Added Kaiser LaSO algorithm as a C++ reimplementation of his Python post-processor — **auto-detects seed curves** from overhang-boundary ∩ lower-slice-boundary (Kaiser's original needed manual seed input), handles **multiple overhang regions per layer** independently, and runs **inside the slicer pipeline** instead of post-processing G-code.
- Algorithm dropdown + pluggable generator interface — future algorithms slot in without touching integration points.
- Recipe system (Balanced / Aesthetic / Structural / Fast / Custom) that auto-fills parameter bundles.
- Dedicated **Wave overhangs** page in Print Settings with grouped sections: General · Geometry · Anchoring · Kaiser LaSO · Speed · Cooling · Floor layers · Debug.
- Floor-layers plumbing — actual surface-classification override so the layers above wave regions get proper `stBottomBridge` solid fill.
- 20+ expert config parameters (`min-angle`, `anchor-bite`, `spacing-mode`, `seam-mode`, `direction-bias`, `debug-gcode`, `min-length`, `kaiser-max-rings`, `anchor-passes`, `perimeter-overlap`, `narrow-split-threshold`, …).
- Launcher + hidapi stub for openSUSE Tumbleweed (works around libmanette/hidapi 0.14 crash in the WebKit-based config wizard).

---

## Building

Dependencies are the same as upstream OrcaSlicer: CMake ≥ 3.13, gcc or clang, GTK3, plus the bundled deps under `deps/`. See the upstream [OrcaSlicer build docs](https://github.com/OrcaSlicer/OrcaSlicer/wiki/How-to-build) for the full platform-by-platform guide.

On **openSUSE Tumbleweed** there's a specific `lib64` path fix already patched in `deps/MPFR/MPFR.cmake` and a system `gstreamer-video-1.0` link in `src/slic3r/CMakeLists.txt` — these are already committed; no manual patching needed.

Build steps (Linux):

```bash
# 1. Build bundled deps
cd deps
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 2. Build the slicer
cd ../..
mkdir -p build && cd build
cmake .. \
  -DSLIC3R_STATIC=1 \
  -DSLIC3R_GTK=3 \
  -DCMAKE_PREFIX_PATH=$(pwd)/../deps/build/destdir/usr/local
make -j$(nproc)
```

---

## Using wave overhangs

1. Launch the slicer, open a model with an overhang.
2. Go to **Print Settings → Wave overhangs** tab.
3. Toggle **Use wave overhangs (Experimental)** on.
4. Pick an algorithm (Anderson or Kaiser).
5. Optionally pick a recipe (Balanced is a good starting point) — picking a named recipe auto-fills the individual tunables.
6. Slice and inspect the G-code preview — wave extrusions will appear over detected overhang regions.

> Simple mode exposes the master toggle + algorithm + recipe. Switch to **Advanced** (top-right mode selector) if you want to tune individual parameters like line spacing, anchor bite, or the Kaiser LaSO overlap. Editing any advanced tunable automatically snaps the recipe dropdown to "Custom".

For a full reference of every config option with tuning hints and the exact recipe bundles, see [docs/WAVE_OVERHANG_SETTINGS.md](docs/WAVE_OVERHANG_SETTINGS.md).

---

## Current limitations

- **Kaiser pin supports are not ported.** Kaiser's original places discrete pin-support nubs under overhangs. Not planned — the goal here is fully support-free overhangs; use `support_remaining_areas_after_wave_overhangs` + Orca's normal supports if wave can't cover everything.
- **Not tested on Windows / macOS** — only Linux (openSUSE Tumbleweed) so far.

---

## Contributing

- Open issues for bugs, feature requests, or print failures.
- PRs welcome — base off `main` (or the `wave-overhangs` branch until merge).
- When reporting test results, please share: model, algorithm/recipe used, printer, and photos / G-code snippet.
- License: **AGPL-3.0** (inherited from OrcaSlicer).
