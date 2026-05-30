---
applyTo: src/**
description: "Use when editing the intro runtime frame loop, Win32/WGL behavior, sequence/timeline code, cursor runtime semantics, image/text cue runtime logic, shader curve evaluation, `.meshbin` material/normal-map behavior, and authored-data loader behavior."
---
# Intro Runtime Instructions

## Focus
- Keep the runtime explicit, deterministic, and Windows-only.
- Preserve a clear frame loop and obvious ownership between subsystems.
- Prefer direct fixes in the runtime path over layered abstractions.

## Frame Flow
- Keep `main_win32.cc` readable as the authoritative runtime flow.
- Main-loop order should remain obvious:
  1. pump messages
  2. get/sync visual time
  3. update music playback
  4. update sequence
  5. resolve cursor policy
  6. resolve image/text cues
  7. apply shader curves
  8. render
  9. present

## Timing Policy
- Music time is not the visual driver.
- Music startup is cue-driven: no authored music cue means silent runtime startup.
- Visuals, sequence, cue timing, shader curves, and the optional 3D stage should share one deterministic visual time base.
- For shader dyn speed modulation, keep per-scene phase smooth by integrating shader time on CPU
  (`scene_shader_time += dt * speed * dyn0_factor`) and passing it to GLSL (`a_st`/`b_st`).
- Avoid runtime/shader contracts that recompute phase as absolute-time multiplied by changing
  dyn-speed factors.
- Keep active-window timing and effect-window timing distinct.
- When carry-to-next-scene is involved, be explicit about which time space each comparison uses.
- Global `scene3d` timing should continue across scene changes by default; only explicit per-scene timing overrides should restart the local 3D fade/start window.
- Multi-object 3D support should remain explicit and fixed-size (up to the authored static limit) rather than drifting toward a dynamic scene graph.

## Cursor Policy
- Cursor visibility is authored data, not a fade-derived visual.
- Timeline default plus per-scene override come from `scene_data.txt`.
- Cursor show/hide changes must stay deterministic and immediate.
- Keep requested cursor policy separate from the applied OS cursor state so focus-loss, alt-tab, and shutdown never leave the desktop cursor hidden.

## Cue Policy
- `Active Start/End` controls overlay existence.
- `Effect Start/End` controls animation only.
- Carry-to-next-scene is a one-hop extension, not a generic persistence system.
- Preserve compatibility with older authored rows where already supported.
- Text object kinds are authored as `title_main`, `credits_main`, `scroll_text`, and `multiline_text`.
- For non-scroll text cues, normalized coordinates are anchor coordinates; `x=0.5`, `y=0.5` should resolve to screen center.

## Asset Sourcing Policy
- Intro/release behavior is embedded-only for authored runtime assets.
- Filesystem fallbacks are diagnostics-only and must remain behind `#if REV_ENABLE_DIAGNOSTICS`.

## Build Discipline
- Avoid new dependencies.
- Keep diagnostics behind `#if REV_ENABLE_DIAGNOSTICS`.
- Normal workspace/release builds should stay quiet: no debug console or verbose runtime messaging unless diagnostics are explicitly requested.
- Maintain intro-size discipline and explicit behavior.
- Keep `.meshbin` v4 material-slot / normal-map support lightweight and optional; preserve up to 8 OBJ/MTL ranges without growing a heavy runtime material system.
- Avoid stack-allocating heavyweight renderer state or large mesh/object arrays in the Win32 entry path; keep that storage runtime-owned and explicit (heap or equivalent long-lived owner) so the 3D build stays safe without reintroducing large static PE bloat.

## Validation
- For runtime-only changes:
  - cmake -S . -B build
  - cmake --build build --config Release
- If the optional 3D path, `scene3d` runtime, or `mesh_renderer_gl.cc` changed:
  - cmake -S . -B build -DREV_ENABLE_3D=ON ; cmake --build build --config Release
- For editor/runtime pipeline changes:
  - python -m py_compile tools/scene_block_editor.py
  - cmake -S . -B build
  - cmake --build build --config Release