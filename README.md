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

> **⚠️ Experimental / Alpha.** Feedback and test prints welcome. Open an issue with your results.

</div>

---

## What are wave overhangs?

Wave overhangs is a slicing algorithm that lets you print 90‑degree overhangs without support material. Toolpaths are generated recursively based on wave‑propagation theory. Each new ring anchors to the previous one, and the pattern keeps propagating outward until it fills the available space, diffracting around corners and even around holes.

This fork ports the technique into OrcaSlicer and exposes two pluggable generators plus a large tunable parameter space, so people can experiment and find what works for their printer and material.

![Standard vs arc vs wave overhangs](docs/images/fig_2_compare_standard_arc_and_wave_overhangs.png)

> Comparison of standard, arc‑overhang, and wave‑overhang toolpaths. Image from Janis A. Andersons' wave‑overhang research (see [Credits](#credits--research-references)).

---

## Main features

- **Two wave‑overhang algorithms** with an in‑GUI dropdown
  - **Andersons**: port of [stmcculloch/PrusaSlicer‑WaveOverhangs](https://github.com/stmcculloch/PrusaSlicer-WaveOverhangs). Arc‑overhang descendant with narrow‑region splitting and smart/zig‑zag/monotonic pattern selection.
  - **Kaiser LaSO**: C++ reimplementation of [Rieks Kaiser's MSc thesis script](https://github.com/riekskaiser/wave_LaSO). Auto seed‑curve detection, multi‑overhang handling, pipeline‑integrated (no G‑code post‑processing).
- **Dedicated Wave overhangs tab** in Print Settings with grouped sections: General, Geometry, Anchoring, Andersons, Kaiser LaSO, Speed, Cooling, Floor layers, Debug.
- **30+ expert tunables** for experimentation: line spacing, anchor bite, seam mode, spacing mode, direction bias, perimeter overlap, narrow‑split threshold, wavefront advance, discretization, arc resolution, max iterations, authoritative floor layers, and more.
- **Wave‑aware support integration**: supports only generate for overhang areas the wave couldn't cover.
- **G‑code debug markers**: `;WAVE_OVERHANG_CONFIG …` header block and per‑region `;WAVE_OVERHANG_START/END` tags for easy post‑process verification.
- **100% opt‑in**: master toggle off means identical behavior to upstream OrcaSlicer.

---

## Download

Prebuilt binaries for tagged releases on the **[Releases page](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs/releases)**. Linux AppImage, Windows portable zip + installer, macOS universal DMG.

---

## Using wave overhangs

1. Launch the slicer, open a model with an overhang.
2. Go to **Print Settings → Wave overhangs** tab.
3. Toggle **Use wave overhangs (Experimental)** on.
4. Pick an algorithm (Andersons or Kaiser).
5. Slice and inspect the G‑code preview. Wave extrusions appear over detected overhang regions.

> **Simple mode** shows just the master toggle plus the algorithm dropdown.
> Switch to **Advanced** (top‑right mode selector) to tune individual parameters like line spacing, anchor bite, Kaiser LaSO overlap, etc.

For the full reference of every config option with tuning hints, see **[docs/WAVE_OVERHANG_SETTINGS.md](docs/WAVE_OVERHANG_SETTINGS.md)**.

> **Presets are intentionally not shipped yet.** The tunable space is large and we want community test prints to surface what actually works before baking in named bundles. Open an issue with your good-result settings and we'll fold them in.

---

## The algorithms

|  | Andersons (arc‑overhang derived) | Kaiser LaSO |
|---|---|---|
| **Geometric primitive** | Concentric arcs grown from interior seed points, clipped to the overhang boundary | Lateral offsets of a root‑edge seed curve, buffered by `line_width × (1 − overlap)` each ring |
| **Seed** | Interior points where medial‑axis fronts propagate | Curve anchored to the supported edge (auto‑detected in our port as overhang‑boundary ∩ lower‑slice‑boundary) |
| **Propagation** | Radial: arcs fan out | Boustrophedon: parallel rings alternate direction |
| **Research origin** | Wave‑overhang research by Janis A. Andersons (PhD candidate, University of Twente). Arc‑overhang predecessor and PrusaSlicer port by Steven McCulloch (see Credits) | Kaiser's MSc thesis, University of Twente |

Both algorithms have strengths and weaknesses depending on overhang geometry. Try both on your model and compare.

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

---

## Current limitations

- **Experimental.** The tunable space is large (30+ knobs across two algorithms) and most parameter combinations have not been print‑tested yet. Expect rough edges. Please share what works and what doesn't.
- **PLA recommended.** Wave overhangs need each ring to cool and become rigid before the next pass anchors to it. PLA with max part‑cooling works well. PETG, ABS and PC are likely to fail (PETG cools too slowly and delaminates under heavy fan).
- **Warping on larger spans.** Laterally supported overhangs are prone to warping driven by thermal gradients, reheating of earlier layers, and nozzle pressure. Smaller overhangs print cleanly; larger spans may still need traditional supports. See **[docs/LIMITATIONS.md](docs/LIMITATIONS.md)** for the mechanisms and mitigations.
- **Kaiser pin supports are not ported.** Kaiser's original places discrete pin‑support nubs under overhangs. Not planned, since the goal here is fully support‑free overhangs. Use `support_remaining_areas_after_wave_overhangs` with Orca's normal supports if wave can't cover everything.
- **Not yet real‑print tested on Windows/macOS.** CI produces builds for all platforms, but testing has focused on Linux so far.

---

## Credits & research references

**Wave overhang algorithm research**
> **Wave‑inspired path‑planning strategy for support‑free horizontal overhangs in FDM** (paper to be published).
> Reference Python implementation and interactive visualiser: [andersonsjanis/Wave‑overhangs](https://github.com/andersonsjanis/Wave-overhangs).
> Accompanying dataset (placeholder while paper is in review): [10.17632/xhw8xkjyc2.1](https://data.mendeley.com/datasets/xhw8xkjyc2/1).
> Authors: **Janis A. Andersons**, **Solemé Sanchez**, **Tom Vaneker**.

**Arc‑overhang algorithm** (the predecessor wave overhangs builds on)
> Steven McCulloch: [stmcculloch/arc‑overhang](https://github.com/stmcculloch/arc-overhang)

**PrusaSlicer integration of wave overhangs** (what our Andersons port is based on)
> Steven McCulloch: [stmcculloch/PrusaSlicer‑WaveOverhangs](https://github.com/stmcculloch/PrusaSlicer-WaveOverhangs)

**Kaiser LaSO algorithm** (the second algorithm in this fork)
> Rieks Kaiser: [riekskaiser/wave_LaSO](https://github.com/riekskaiser/wave_LaSO)
> *Investigating the warping of Laterally Supported Overhangs in fused deposition modelling, the Python code.*
> Written in the context of Rieks Kaiser's master thesis (Mechanical Engineering, University of Twente).

**OrcaSlicer base**
> OrcaSlicer team: [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)

---

## Contributing

- Open issues for bugs, feature requests, or print failures.
- PRs welcome. Base off `main`.
- When reporting test results, please share: model, algorithm and parameter values used, printer, photos, G‑code snippet.

License: **AGPL‑3.0** (inherited from OrcaSlicer).
