# HiMYM Codex Guide

## Mission

HiMYM is a Windows-only C++17 demoscene framework for authoring and playing compact intros and full demos. Optimize decisions in this order:

1. Competition reliability on Windows 11 x64.
2. Executable size and compression friendliness.
3. Deterministic, explicit runtime flow.
4. Rendering capability.
5. Maintainability without engine-style bloat.

Treat current code and CMake files as authoritative. Before relying on material under `PR/`, read `PR/DOCUMENTATION_STATUS.md`; it classifies current, mixed, historical, and legacy documents.

## Repository map

- `revision_libs/rev_runtime/`: shared cue structs, parsers, texture/text helpers, animation, and matrix helpers.
- `revision_libs/rev_editor/`, `examples/editor_app/`: ImGui authoring, preview, project JSON, export, packing, build, and launch.
- `examples/minimal_intro/`: standalone and packed playback.
- `revision_libs/rev_platform/`: Win32 windowing, WGL/OpenGL, input, and timing.
- `revision_libs/rev_shader/`: GLSL compilation and programs.
- `revision_libs/rev_mesh/`, `rev_gltf/`, `rev_pixel/`, `rev_particles/`: visual content systems.
- `revision_libs/rev_xm/`: XM playback through libxm and WinMM.
- `revision_libs/rev_curve/`, `rev_sequence/`: animation, timeline, cues, and triggers.
- `revision_libs/rev_pack/`, `tools/pack_cli.cpp`: embedded-asset generation.
- `PR/`: architecture, API, workflow, and historical documentation.
- `.agents/skills/`: Codex workflows for this repository.
- `.codex/agents/`: Codex custom subagent definitions.

## Core contracts

- Shared cue types belong in `revision_libs/rev_runtime/include/rev_runtime.h`; do not redefine them elsewhere.
- Keep semantics aligned across project JSON, editor load/save, `cues.txt` export/import, runtime parsing, preview, standalone rendering, and packing.
- For a new cue type, follow: shared struct -> parser -> editor ownership/UI -> save/load/export/import -> preview -> runtime -> packer -> validation.
- Initialize every unassigned `curve_*` index to `-1`.
- Same-type cue parameter paste preserves the destination cue's content or
  asset identity and deep-clones assigned curves. Never leave pasted cues
  sharing mutable curve indices with the source cue.
- Preserve deterministic ordering in exports and rendering.
- Interactive menu scenes hold/loop locally until activation or exit. A launched destination returns to the exact originating menu when its scene duration or non-looping XM cue ends; ordinary linear playback remains unchanged.
- Menu animated-sprite references are scene-local indices. Multiple items may instance one cue with independent menu-owned image positions, hit bounds, targets, and selection tint; deletion must clear or reindex references deterministically.
- Each sprite-mode menu item owns a runtime animation clock. Advance only the selected item; deselection freezes its current frame and reselection resumes it, including when several items share one animated-sprite cue.
- Mouse navigation is an explicit per-menu authored option and defaults off for old and new projects; keyboard Up/Down/Enter remains always available.
- Keep packed/release assets embedded-first; filesystem fallback is explicit diagnostics/development behavior.
- Packed builds derive `HIMYM_USE_*` macros and `packed_features.cmake` from exported cue rows. This includes code-only cue families such as animated sprites and scrolling text; scene wipes and menus are code-only packed cue metadata with no optional dependency. Reconfigure after packing so C++ guards and CMake dependencies stay aligned.
- Packed shader sources are project-specific: `rev_pack` emits only fullscreen and enabled asset-shader preset IDs referenced by the exported cues, plus preset 0 as the no-cue fallback. The editor and file-based runtime keep the universal preset registry.
- Packed post-effect GLSL is also project-specific. Collect enabled effect types from global, scene-layer, image, animated-sprite, and pixel rows; preserve the universal shader only for editor/file playback and older packed shader manifests.
- Maintain editor/runtime OpenGL-state parity, especially VAO binding, depth writes, blending, and opaque/transparent ordering.
- Shader-pipeline cue opacity is a compositor contract, not a GLSL contract:
  multiply the evaluated opacity curve by the cue fade envelope in preview and
  runtime, and alpha-composite a partially opaque bottom layer against black.
- Image cues, shader text, and scrolling text use a 1920x1080 authored pixel
  canvas. Scale image dimensions, glyph metrics, pixel speed, and atlas travel
  uniformly by the smaller viewport axis while keeping normalized anchors
  unchanged.
- Shader Text Cue curve slots cover X, Y, glyph height, color RGB, opacity,
  spacing, and scroll speed. Default every slot to `-1`, append them to the
  backward-compatible `cues.txt` row, and evaluate them identically in preview
  and runtime using cue-local time.
- Load post-OpenGL-1.1 functions with `wglGetProcAddress`.
- Declare every WGL-loaded OpenGL function pointer with `APIENTRY`. The normal
  x64 ABI can hide a missing calling convention, but Crinkler's x86 runtime
  will corrupt the stack and may flicker, render black, or crash.
- Initialize GDI+ before image/text loading and preserve its Windows path and stream-lifetime requirements.
- Competition-size builds are optional Win32 artifacts and never replace the
  supported x64 `minimal_intro_packed.exe`. Build and preserve x64 first.
- Crinkler consumes ordinary COFF, not MSVC LTCG objects, and ignores embedded
  `/DEFAULTLIB` directives. Keep `/GL` and `/LTCG` out of its tree, disable IPO
  in dependencies such as libxm, and pass required imports such as WinMM explicitly.
- Scope Crinkler reuse layouts to the current packed manifests. Regenerate the
  layout when assets or feature flags change, and keep every initialized part
  below Crinkler's 64 KiB uncompressed limit.
- The editor exposes 64 KiB, 128 KiB, and custom final-size budgets separately
  from Crinkler's per-part 64 KiB limit. Refuse to link while the existing
  competition executable is locked, with an actionable close-process error.
- Avoid `std::call_once` in Crinkler-linked graphics libraries when its Win32
  InitOnce forwarding creates import cycles; competition playback is
  single-threaded, so a direct guarded loader is sufficient there.

## Runtime constraints

- Keep frame flow obvious: messages -> time/audio sync -> sequence/cues -> animation -> render -> present.
- Do not introduce plugins, scripting, reflection, runtime discovery, generic renderer layers, or speculative abstractions.
- Prefer fixed-size/static data and direct calls where appropriate for intro builds.
- Avoid exceptions, RTTI, iostream-heavy paths, inheritance-heavy designs, and unnecessary allocation in size-sensitive code.
- ESC and Alt+F4 must terminate promptly.
- XM playback remains static and cue-driven; do not add decoder DLL probing.

## Working method

Before changing cross-layer behavior:

1. Read the applicable skill under `.agents/skills/`.
2. Read `PR/DOCUMENTATION_STATUS.md` before using older project documentation.
3. Inspect the current struct, writer/exporter, parser, preview consumer, and runtime consumer.
4. Make the smallest coherent change across affected layers.
5. Update current documentation and its status entry when a durable contract changes.
6. Run proportional validation.

Preserve unrelated user changes in a dirty worktree.

## Build and validation

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Prefer focused targets during iteration:

```powershell
cmake --build build --config Release --target editor_app
cmake --build build --config Release --target minimal_intro
cmake --build build --config Release --target minimal_intro_packed
cmake --build build --config Release --target text_animation_tests
cmake --build build --config Release --target packed_feature_manifest_tests
.\tools\test_editor_pipeline.ps1
.\tools\build_crinkler_competition.ps1 -CompetitionMode FAST
```

Run `build\bin\Release\text_animation_tests.exe` for text-animation changes. If `rev_pack` changes, rebuild the editor or `pack_cli` before validating packed output. For current `minimal_intro_packed`, validate generated mesh/glTF selection through `HIMYM_USE_MESH`, `HIMYM_USE_GLTF`, and `build/packed_features.cmake`; the live target no longer uses the historical `REV_ENABLE_3D` option.

Report exact commands and distinguish new failures from stale or environmental failures.
For competition changes, validate both PE architectures and smoke-test shader
pipelines in the x86 artifact; an x64-only render check cannot expose calling-
convention defects.

## Style

- Types/functions: `PascalCase`
- Variables: `lower_snake_case`
- Members: trailing underscore
- Constants: `kPascalCase`
- Macros: `ALL_CAPS` only when necessary
- Files: `snake_case`

Keep ownership and side effects explicit. Favor compact, direct C++ over generalized machinery.

## Code review rules

Prioritize:

1. Correctness and competition reliability.
2. Editor/export/runtime mismatch.
3. Binary and dependency cost.
4. OpenGL state or resource-lifetime errors.
5. Determinism and ownership clarity.
6. Missing validation or documentation parity.

Use file/line references. Avoid purely stylistic findings without concrete risk.

## Codex routing

Use `$himym-codebase-map`, `$himym-runtime`, `$himym-editor`, `$himym-shader-graphics`, `$himym-build-validation`, and `$himym-director` when applicable.

Custom agents in `.codex/agents/` support explicitly requested subagent work. Parallelize bounded read-only exploration/review/validation when requested; serialize overlapping editor/runtime contract edits.
