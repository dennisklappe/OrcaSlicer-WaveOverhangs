<div align="center">

<picture>
  <img alt="OrcaSlicer-WaveOverhangs" src="resources/images/OrcaSlicer.png" width="15%" height="15%">
</picture>

# OrcaSlicer‑WaveOverhangs

**Print steep overhangs without supports.**
Fork of OrcaSlicer with wave‑pattern overhang printing, two pluggable algorithms, and a rich expert‑mode parameter space.

<br>

[![Download](https://img.shields.io/github/v/release/dennisklappe/OrcaSlicer-WaveOverhangs?include_prereleases&label=Download&color=brightgreen&style=for-the-badge)](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/dennisklappe/OrcaSlicer-WaveOverhangs/build_all.yml?branch=wave-overhangs&label=Build&style=for-the-badge)](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs/actions/workflows/build_all.yml)
[![Stars](https://img.shields.io/github/stars/dennisklappe/OrcaSlicer-WaveOverhangs?style=for-the-badge)](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs/stargazers)
[![License](https://img.shields.io/badge/license-AGPL--3.0-blue?style=for-the-badge)](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/LICENSE.txt)

> **⚠️ Experimental / Alpha.** Feedback and test prints welcome — open an issue with your results.

</div>

---

## Main features

- **Two wave‑overhang algorithms** with an in‑GUI dropdown
  - **Anderson** — port of [stmcculloch/PrusaSlicer‑WaveOverhangs](https://github.com/stmcculloch/PrusaSlicer-WaveOverhangs). Arc‑overhang descendant with narrow‑region splitting and smart/zig‑zag/monotonic pattern selection.
  - **Kaiser LaSO** — C++ reimplementation of [Rieks Kaiser's MSc thesis script](https://github.com/riekskaiser/wave_LaSO). Auto seed‑curve detection, multi‑overhang handling, pipeline‑integrated (no G‑code post‑processing).
- **Recipe presets** — Balanced / Aesthetic / Structural / Fast / Custom auto‑fill the underlying tunables.
- **Dedicated Wave overhangs tab** in Print Settings with grouped sections: General · Geometry · Anchoring · Anderson · Kaiser LaSO · Speed · Cooling · Floor layers · Debug.
- **30+ expert tunables** for experimentation — line spacing, anchor bite, seam mode, spacing mode, direction bias, perimeter overlap, narrow‑split threshold, wavefront advance, discretization, arc resolution, max iterations, and more.
- **Authoritative floor layers** — `N = exactly N` solid layers above a wave region. `N = 0` = zero solid, straight to sparse infill.
- **Wave‑aware support integration** — supports generate only for overhang areas the wave couldn't cover.
- **G‑code debug markers** — `;WAVE_OVERHANG_CONFIG …` header block + per‑region `;WAVE_OVERHANG_START/END` tags for easy post‑process verification.
- **100 % opt‑in** — master toggle off → identical behavior to upstream OrcaSlicer.

---

## Download

Prebuilt binaries for tagged releases → **[Releases page](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs/releases)**. Linux AppImage, Windows portable zip + installer, macOS universal DMG.

---

## Using wave overhangs

1. Launch the slicer, open a model with an overhang.
2. Go to **Print Settings → Wave overhangs** tab.
3. Toggle **Use wave overhangs (Experimental)** on.
4. Pick an algorithm (Anderson or Kaiser).
5. Optionally pick a recipe — **Balanced** is a good starting point.
6. Slice and inspect the G‑code preview — wave extrusions appear over detected overhang regions.

> **Simple mode** shows just the master toggle + algorithm + recipe.
> Switch to **Advanced** (top‑right mode selector) to tune individual parameters — line spacing, anchor bite, Kaiser LaSO overlap, etc.
> Editing any advanced tunable automatically snaps the recipe dropdown to **Custom**.

For the full reference of every config option with tuning hints and exact recipe bundles, see **[docs/WAVE_OVERHANG_SETTINGS.md](docs/WAVE_OVERHANG_SETTINGS.md)**.

---

## The algorithms

|  | Anderson (arc‑overhang derived) | Kaiser LaSO |
|---|---|---|
| **Geometric primitive** | Concentric arcs grown from interior seed points, clipped to the overhang boundary | Lateral offsets of a root‑edge seed curve, buffered by `line_width × (1 − overlap)` each ring |
| **Seed** | Interior points where medial‑axis fronts propagate | Curve anchored to the supported edge (auto‑detected in our port as overhang‑boundary ∩ lower‑slice‑boundary) |
| **Propagation** | Radial: arcs fan out | Boustrophedon: parallel rings alternate direction |
| **Research origin** | Wave‑overhang research by Andersons (PhD candidate, University of Twente) / Sanchez / Vaneker; arc‑overhang predecessor + PrusaSlicer port by Steven McCulloch (see Credits) | Kaiser's MSc thesis, University of Twente |

Both algorithms have strengths and weaknesses depending on overhang geometry — try both on your model and compare.

---

## Building from source

Dependencies are the same as upstream OrcaSlicer: CMake ≥ 3.13, gcc or clang, GTK3, plus the bundled deps under `deps/`. See the upstream [OrcaSlicer build docs](https://github.com/OrcaSlicer/OrcaSlicer/wiki/How-to-build) for the full platform‑by‑platform guide.

```bash
# 1. Build bundled deps
cd deps && mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 2. Build the slicer
cd ../..
mkdir -p build && cd build
cmake .. -DSLIC3R_STATIC=1 -DSLIC3R_GTK=3 -DCMAKE_PREFIX_PATH=$(pwd)/../deps/build/destdir/usr/local
make -j$(nproc)
```

Build fixes for openSUSE Tumbleweed (GMP `lib64` path, gstreamer‑video‑1.0 link) are already committed — no manual patching needed.

---

## Current limitations

- **Kaiser pin supports are not ported.** Kaiser's original places discrete pin‑support nubs under overhangs. Not planned — the goal here is fully support‑free overhangs. Use `support_remaining_areas_after_wave_overhangs` + Orca's normal supports if wave can't cover everything.
- **Linux only, so far** — Windows and macOS builds are produced by CI but haven't been real‑print tested.

---

## Credits & research references

**Wave overhang algorithm research**
> **Wave‑inspired path‑planning strategy for support‑free horizontal overhangs in FDM** — paper to be published.
> Reference Python implementation + interactive visualiser: [andersonsjanis/Wave‑overhangs](https://github.com/andersonsjanis/Wave-overhangs).
> Accompanying dataset (placeholder while paper is in review): [10.17632/xhw8xkjyc2.1](https://data.mendeley.com/datasets/xhw8xkjyc2/1).
> Authors: **Janis A. Andersons**, **Solemé Sanchez**, **Tom Vaneker**.

**Arc‑overhang algorithm** — the predecessor wave overhangs builds on
> Steven McCulloch — [stmcculloch/arc‑overhang](https://github.com/stmcculloch/arc-overhang)

**PrusaSlicer integration of wave overhangs** — what our Anderson port is based on
> Steven McCulloch — [stmcculloch/PrusaSlicer‑WaveOverhangs](https://github.com/stmcculloch/PrusaSlicer-WaveOverhangs)

**Kaiser LaSO algorithm** — the second algorithm in this fork
> Rieks Kaiser — [riekskaiser/wave_LaSO](https://github.com/riekskaiser/wave_LaSO)
> *Investigating the warping of Laterally Supported Overhangs in fused deposition modelling — the Python code.*
> Written in the context of Rieks Kaiser's master thesis (Mechanical Engineering, University of Twente).

**OrcaSlicer base**
> OrcaSlicer team — [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)

---

## Contributing

- Open issues for bugs, feature requests, or print failures.
- PRs welcome — base off `main` (or the `wave-overhangs` branch until merge).
- When reporting test results, please share: model, algorithm/recipe used, printer, photos / G‑code snippet.

License: **AGPL‑3.0** (inherited from OrcaSlicer).
