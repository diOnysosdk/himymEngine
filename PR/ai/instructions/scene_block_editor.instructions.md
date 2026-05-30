---
applyTo: tools/scene_block_editor*.py
description: "Use when editing scene_block_editor modal flows, OBJ/MTL import behavior, style presets, cue export formats, and editor-to-runtime timing/data consistency for image/text/shader curves."
---
# Scene Block Editor Instructions

## Focus
- Keep editor work direct and deterministic.
- Preserve modal workflows and existing project UX.
- Ensure authored changes round-trip: UI -> project data -> export text -> runtime loader -> runtime behavior.
- Keep scene settings, advanced tuning, and nested shader-curve modal behavior consistent with the current editor UX.

## Timing Policy
- Music clock is audio-only.
- Visuals, sequence, image cues, text cues, and shader curves must use one smooth visual timeline and one shared scene-local cue timeline.
- Do not reintroduce music_time-driven visual updates.
- For image overlays, keep `Active Start/End` distinct from `Effect Start/End` in both UI labels and exported data.
- Carry-to-next-scene extends overlay lifetime by one scene hop; preserve the authored semantic for `scene end` and `next scene end` on export.

## Export Policy
- Keep export files stable and readable:
  - assets/scene_data.txt
  - assets/cues.txt
  - assets/image_cues.txt
  - assets/text_cues.txt
  - assets/shader_curves.txt
  - assets/font_config.txt
  - assets/text_config.txt
- Preserve backward-compatible parsing paths when they already exist.
- Prefer explicit failures for malformed authored data in strict runtime paths.
- **Cursor visibility** is authored in `scene_data.txt`:
  - Timeline row ends with `cursor_default`: `hide` or `show`.
  - Each scene row ends with `cursor_override`: `inherit`, `hide`, or `show`.
  - Backward-compatible: absent fields default to `hide` / `inherit` respectively.
- **Image carry** is authored in `image_cues.txt`:
  - Final field `carry_to_next_scene`: `0` or `1`.
  - Older row formats must remain loadable.
- **3D stage timing** exported through `scene3d.txt` should stay continuous across scene changes by default.
  - Only explicit per-scene timing/visibility overrides should produce a scene-local 3D restart window.
  - Per-scene multi-object rows should export as `scene_<id>.object_<n>.*` so several meshes/textures can render together in one scene without inventing a heavier scene graph.
  - Blender OBJ import should preserve lightweight `.meshbin` v4 material slots, diffuse texture paths, and common normal-map paths when available; auto-link the MTL sidecars when present without inventing a heavier authoring system.
- **Text objects** are authored in `text_cues.txt` with kinds:
  - `title_main`, `credits_main`, `scroll_text`, `multiline_text`.
  - Reveal effects for non-scroll text must preserve the authored meaning for `reveal_ltr`, `reveal_fade`, and `reveal_lines`.
  - Scroll style changes (especially `clean_classic`) must reset stale `scroll_*` motion/shading values back to the neutral defaults before applying preset-specific overrides.
- For non-scroll text object placement, keep normalized `x`/`y` as authored anchor coordinates (`0.5`, `0.5` means screen center).
- If the editor shows a semantic end state, export `-1.0` when runtime should resolve the implicit end.
- Keep the `Do It All` workflow explicit and intact: save -> export -> reconfigure `build` when needed -> build -> run.

## Editor UI Rules
- Keep tkinter code explicit; avoid framework-like abstractions.
- If adding controls, wire everything end-to-end.
- Keep labels/hints aligned with runtime semantics.
- Persist user-facing editor state when practical (modal position, last preset, etc.).
- Scene/effect row editing should stay modal.
- Advanced scene tuning belongs in the Scene Settings modal.
- If one modal opens another, manage transient/grab handoff explicitly.

## Validation
- For editor changes: python -m py_compile tools/scene_block_editor.py
- If the OBJ/MTL import or `.meshbin` conversion path changed: python -m py_compile tools/scene_block_editor.py tools/obj_to_meshbin.py
- For editor + runtime pipeline changes:
  - python -m py_compile tools/scene_block_editor.py
  - cmake -S . -B build
  - cmake --build build --config Release
  - cmake -S . -B build -DREV_ENABLE_3D=ON ; cmake --build build --config Release (when the optional 3D path is affected)
