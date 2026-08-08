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
- Preserve deterministic ordering in exports and rendering.
- Keep packed/release assets embedded-first; filesystem fallback is explicit diagnostics/development behavior.
- Packed builds derive `HIMYM_USE_*` macros and `packed_features.cmake` from exported cue rows. This includes code-only cue families such as animated sprites and scrolling text, even when they do not change target dependencies. Reconfigure after packing so C++ guards and CMake dependencies stay aligned.
- Packed shader sources are project-specific: `rev_pack` emits only fullscreen and enabled asset-shader preset IDs referenced by the exported cues, plus preset 0 as the no-cue fallback. The editor and file-based runtime keep the universal preset registry.
- Maintain editor/runtime OpenGL-state parity, especially VAO binding, depth writes, blending, and opaque/transparent ordering.
- Load post-OpenGL-1.1 functions with `wglGetProcAddress`.
- Initialize GDI+ before image/text loading and preserve its Windows path and stream-lifetime requirements.

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
```

Run `build\bin\Release\text_animation_tests.exe` for text-animation changes. If `rev_pack` changes, rebuild the editor or `pack_cli` before validating packed output. For current `minimal_intro_packed`, validate generated mesh/glTF selection through `HIMYM_USE_MESH`, `HIMYM_USE_GLTF`, and `build/packed_features.cmake`; the live target no longer uses the historical `REV_ENABLE_3D` option.

Report exact commands and distinguish new failures from stale or environmental failures.

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
