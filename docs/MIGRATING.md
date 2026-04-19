# Migrating from OrcaSlicer, Bambu Studio, or PrusaSlicer

OrcaSlicer-WaveOverhangs uses its own config directory (`OrcaSlicerWaveOverhangs/`) so it can install alongside the stock apps without touching their profiles. That means a fresh install starts with zero printers, filaments, or process profiles — which is a pain if you've already set everything up elsewhere.

The fork detects your existing configs the first time you launch it and offers to copy them over. You can also re-run the flow from **File → Import → Import from other slicer** at any time.

## What gets detected

On launch, the fork probes the standard config root for your OS for each supported source:

| OS | Search root |
|---|---|
| Linux | `$XDG_CONFIG_HOME` (or `~/.config` if unset) |
| macOS | `~/Library/Application Support` |
| Windows | `%APPDATA%` (e.g. `C:\Users\<you>\AppData\Roaming`) |

Under that root it looks for `OrcaSlicer/`, `BambuStudio/`, and `PrusaSlicer/`. Only directories with at least 1 MB or 1 printer profile are shown — tiny stale dirs from uninstalled previous versions are ignored.

## Supported sources

### OrcaSlicer (stock) — full support

Straight recursive copy. Orca-WaveOverhangs and stock Orca share the same codebase and JSON config schema, so every printer / filament / process profile imports losslessly. The fork never overwrites an existing file in its own data dir, so you can re-import safely.

### Bambu Studio — full support

Same mechanism as Orca: near-identical JSON schema (Orca was forked from Bambu Studio), so printers, filaments, and process profiles come over cleanly. Bambu-cloud-specific items (MakerWorld sync, Bambu account tokens) are skipped — you'll sign in to Bambu Connect again if you use it.

### PrusaSlicer — detection only (for now)

PrusaSlicer's config format (`.ini` files under `print/`, `filament/`, `printer/`) differs enough from Orca's JSON tree that a direct copy won't work — it needs a field-by-field translator. The detector still shows the candidate in the dialog with a *"not yet supported"* label; the translator will land in a follow-up release.

If you're on PrusaSlicer and want to try the fork now:
1. Install stock OrcaSlicer, use its built-in *"Import Configs"* for your PrusaSlicer profiles.
2. Launch OrcaSlicer-WaveOverhangs — it'll offer to import the now-existing Orca configs.

## What the import skips

The copier intentionally leaves behind:

- `log/`, `logs/`, `crash_logs/` — runtime noise, regenerated on first launch
- `cache/`, `gcode-preview-cache/`, `tmp/`, `download/` — caches
- `shapes/thumbnails/` — regenerated as needed

Printer / filament / process profile JSON under `user/` and `system/`, plus `appconfig.cfg`, `presets/*`, and anything else that drives slicing behaviour, is copied.

## What if something breaks

Open a GitHub issue with:

- Your OS + slicer you were migrating from
- The candidate row shown in the dialog (source, size, profile count)
- Output of the fork's launch log (`log/` under the new data dir) — relevant lines are tagged `config-import:`

## Prior workaround (no longer needed)

Before the import flow landed you had to either:
- Manually copy `~/.config/OrcaSlicer/` to `~/.config/OrcaSlicerWaveOverhangs/`
- Or rebuild every profile from scratch

The in-app importer does this for you and handles OS-specific paths + Bambu + the upcoming Prusa translator without you needing to remember where configs live.
