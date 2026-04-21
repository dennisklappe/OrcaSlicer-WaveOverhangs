# Changelog

All notable changes to OrcaSlicer-WaveOverhangs relative to the OrcaSlicer upstream base.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning follows semver with pre-release tags (e.g. `v0.1.0-alpha.1`).

## [Unreleased]

### Changed
- **Unified flow model across Andersons and Kaiser.** `wave_overhang_flow_ratio` replaced with `wave_overhang_flow_mm3_per_mm` (absolute mm³/mm, default `0.16` = `nozzle²` for a 0.4 mm nozzle). Previously Andersons used a layer-height-dependent multiplier while Kaiser had a hardcoded `nozzle²`. Both now read the same config key and apply the same physics model: wave-overhang lines hang in air, so flow depends on nozzle bore and extrusion rate, not on layer height. Breaking change; no profile migration shim (no real users on pre-v0.3 versions to preserve tunings for).

### Fixed
- **`wave_overhang_floor_layers = 0` truly produces zero solid layers above the wave.** Previous fix lived inside `detect_surfaces_type`, which runs before `process_external_surfaces`, `discover_vertical_shells`, and `discover_horizontal_shells` — those passes later re-introduced `stInternalSolid` / `stBottom` surfaces above the wave shadow, so users saw 2 "Internal solid infill" layers in the preview even with N=0. The authoritative promotion has been moved to a new `apply_wave_overhang_floor_layer_authority()` pass that runs AFTER every surface-classification pass, so its result cannot be overwritten.
- **No more `stBottomBridge` above wave strips.** The first layer above a wave was previously classified as `stBottomBridge` and rendered as "Internal bridge" in the preview. That's semantically wrong — the layer sits on top of the wave extrusions (solid material), not air. All N layers in the `wave_overhang_floor_layers` window are now uniformly `stInternalSolid` (regular solid infill).
- **`wave_overhang_floor_layers` is now authoritative, not additive.** Previously the N promoted layers were stacked ON TOP of Orca's default `bottom_shell_layers` (so N=5 silently became N=5+2=7). Now N=5 means *exactly* 5 solid layers above a wave region, and N=0 means zero solid layers (straight to sparse infill). Implementation: record a `wave_overhang_shadow_polygons` mask on every affected layer, then subtract that mask from the bottom-shell seed sets in `discover_vertical_shells` and `discover_horizontal_shells` so they don't propagate further solid layers above the wave footprint.
- **`wave_overhang_min_angle` filter deactivated.** The local `layer_height × tan(angle)` envelope test was over-eager and rejected every legitimate overhang (the incoming strip extends ~one layer-height beyond the support by construction, so it always matched). Orca's upstream `detect_overhang_wall` + `overhang_reverse_threshold` already performs the authoritative slope classification, so we trust that. The config key is preserved (default 0 = no filtering) for profile compatibility and potential future use; tooltip updated accordingly.

## v0.1.0-alpha.1 — first cross-platform alpha

### Added — feature
- **Wave overhangs** end-to-end. Steep cantilevered overhangs printed without supports by generating wave-patterned perimeters that anchor to previous passes.
- **Two algorithms with a dropdown.** Andersons (ported from stmcculloch's PrusaSlicer-WaveOverhangs — arc-overhang descendant with narrow-region splitting and smart/zig-zag/monotonic pattern selection) and Kaiser LaSO (C++ reimplementation of Rieks Kaiser's Python script with auto seed-curve detection and multi-overhang handling).
- **Dedicated Wave overhangs tab** in Print Settings with grouped sections: General · Geometry · Anchoring · Andersons · Kaiser LaSO · Speed · Cooling · Floor layers · Debug.
- **Algorithm-aware gating.** Andersons-only options hidden when Kaiser is selected, and vice versa. Every tunable hidden when the master toggle is off.
- **Floor-layer plumbing.** `wave_overhang_floor_layers` forces N layers above wave regions to be classified as `stBottomBridge` so they get solid fill instead of sparse infill.
- **G-code debug markers.** `;WAVE_OVERHANG_CONFIG` header block listing active wave settings per region, plus `;WAVE_OVERHANG_START/END` tags around wave extrusions. Enabled by default.
- **Public README + full settings reference** at `docs/WAVE_OVERHANG_SETTINGS.md`.
- **GitHub Actions** build triggers for the `wave-overhangs` branch; release workflow on `v*` tags produces Linux AppImage / Windows zip+installer / macOS universal DMG as draft GitHub Releases.

### Added — config options (29 new keys on `PrintRegionConfig`)
Master + algorithm:
- `wave_overhangs` (bool)
- `wave_overhang_algorithm` (enum: Andersons / Kaiser)

Shared tunables:
- `wave_overhang_outer_perimeters` (int)
- `wave_overhang_line_spacing` (float mm)
- `wave_overhang_line_width` (float mm)
- `wave_overhang_print_speed` (float mm/s)
- `wave_overhang_travel_speed` (float mm/s)
- `wave_overhang_fan_speed` (int %)
- `wave_overhang_floor_layers` (int)
- `wave_overhang_min_angle` (float °)
- `wave_overhang_min_length` (float mm)
- `wave_overhang_anchor_bite` (float mm)
- `wave_overhang_anchor_passes` (int)
- `wave_overhang_spacing_mode` (enum: uniform / progressive)
- `wave_overhang_seam_mode` (enum: alternating / aligned / random)
- `wave_overhang_debug_gcode` (bool — default true)
- `support_remaining_areas_after_wave_overhangs` (bool)

Andersons-only:
- `wave_overhang_pattern` (enum: smart / zigzag / monotonic)
- `wave_overhang_perimeter_overlap` (float)
- `wave_overhang_minimum_width` (float mm)
- `wave_overhang_wavefront_advance` (float mm)
- `wave_overhang_discretization` (float mm)
- `wave_overhang_andersons_max_iterations` (int)
- `wave_overhang_min_new_area` (float mm²)
- `wave_overhang_arc_resolution` (int)

Kaiser-only:
- `wave_overhang_laso_overlap` (float)
- `wave_overhang_kaiser_max_rings` (int)
- `wave_overhang_direction_bias` (float °, experimental)

### Fixed — build
- MPFR configure fails on GCC 15 Linux because GMP installs into `lib64/` — patched `deps/MPFR/MPFR.cmake` to pass explicit `--with-gmp-lib=lib64`, `--with-gmp-include`, `--libdir=lib64` on Linux only.
- wxWidgets static media lib needs `gstreamer-video-1.0` on Linux — added to `pkg_check_modules` and `target_link_libraries` in `src/slic3r/CMakeLists.txt`.
- `tmp/` added to `.gitignore` for local scratch.

### Added — plumbing (all previously save-only settings now apply)
- `wave_overhang_travel_speed` — `GCodeWriter::travel_to_*` extended with optional per-move speed; applied when the enclosing path has the wave-overhang flag.
- `wave_overhang_fan_speed` — new `;_WAVE_OVERHANG_FAN_START/END` marker emitted around wave paths and handled in `CoolingBuffer` to force the part-cooling fan percentage.
- `wave_overhang_min_angle` — per-region overhang-steepness estimator at the generator dispatcher; regions shallower than the threshold fall back to Orca's normal overhang path.
- `support_remaining_areas_after_wave_overhangs` — residual polygons (the area the wave couldn't cover) are passed into Orca's support generation as enforcer regions.

### Known limitations (also noted in README)
- Kaiser pin supports: not planned. Goal of this fork is fully support-free overhangs.
- Real-world print validation: Linux only so far (openSUSE Tumbleweed).

### Credits
- Wave overhangs algorithm: Janis A. Andersons (paper to be published; reference Python at [andersonsjanis/Wave-overhangs](https://github.com/andersonsjanis/Wave-overhangs))
- Arc-overhang algorithm and PrusaSlicer integration: Steven McCulloch ([stmcculloch](https://github.com/stmcculloch))
- Kaiser LaSO algorithm: Rieks Kaiser ([riekskaiser/wave_LaSO](https://github.com/riekskaiser/wave_LaSO))
- OrcaSlicer base: [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
