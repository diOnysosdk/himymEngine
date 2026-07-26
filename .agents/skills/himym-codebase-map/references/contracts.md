# HiMYM contract map

## Source-of-truth order

1. Current headers and implementations.
2. Current CMake target definitions.
3. Root `AGENTS.md` and `.github/copilot-instructions.md`.
4. `PR/architecture/` and current guides.
5. Older summaries and copied agent material.

## End-to-end authored data

For every cue/property, locate its shared defaults, editor ownership, JSON save/load, `cues.txt` export/import, runtime parser, preview consumer, standalone consumer, packed-asset discovery, cleanup scanning, tests, and build targets.

## High-risk invariants

- `curve_* == -1` means unassigned.
- Asset paths must survive project transfer and packing.
- Preview/runtime must agree on timing, fades, curves, triggers, alpha, layers, and materials.
- Mixed glTF materials require per-slot behavior, texture alpha, and opaque-before-transparent rendering.
- 2D/3D interleaving requires deliberate VAO, depth, and blend restoration.
- GDI+ has initialization, separator, and stream-lifetime requirements.
- Windows `<gl/gl.h>` exposes only OpenGL 1.1 declarations.

Do not assume older library/cue inventories are exhaustive. Inspect current animated-sprite, pixel, particle, advanced text, post-effect, asset-shader, trigger, glTF, and packed-asset support.
