# Project Guidelines

## Scope
- This codebase targets Revision 2026 PC productions with two profiles: Demo and Intro.
- Target platform is Windows only.
- Keep one shared runtime core and profile-specific render/runtime paths where needed.
- Do not add cross-platform abstraction layers, plugin systems, or engine-style architecture.

## Priorities
1. Competition reliability on compo machine.
2. Executable size and compression friendliness (critical for intro profiles).
3. Direct runtime flow and deterministic behavior.
4. Rendering capability.
5. Maintainability without architecture bloat.

## Competition Baseline
- Design around Windows 11 64-bit compo environment.
- Projector target is 1920x1080 at 60 Hz, 16:9.
- ESC or Alt+F4 must terminate immediately.
- Do not rely on extra runtimes/redists being installed.
- Ship all required local dependencies inside release archive.

## Profile Strategy
- Demo profile:
  - Primary runtime path: shared Win32/WGL OpenGL build.
  - Optional compile-time `REV_ENABLE_3D` stage/mesh path when needed.
  - No file size limit, but keep startup and run behavior deterministic and quiet by default.
- Intro profile (64k/128k/256k):
  - Size-first path with strict binary/import discipline.
  - Favor minimal APIs and static data over runtime systems.
  - Treat each byte and each import as budgeted.

## Architecture
- Keep subsystems explicit and static:
  - src/platform
  - src/renderer
  - src/content
  - src/sequence
  - src/audio
  - src/app
- Keep frame flow obvious in main loop:
  - pump messages
  - get/sync time
  - update sequence
  - update content params
  - render
  - present
- Prefer static tables and direct calls over indirection.

## Forbidden Patterns
- No plugin systems, runtime module discovery, DLL probing/loading logic, scripting, reflection, or editor/tool architecture in runtime.
- No fake generic renderer API that hides platform cost.
- No speculative abstractions that increase binary size.

## Audio Rules
- XM-only playback path for both profiles.
- Keep decoder static in executable; no runtime decoder DLL probing.
- Music startup is cue-driven: no authored music cue means silent startup.
- Keep release source deterministic: embedded XM resource is primary, filesystem XM is dev fallback only.

## Asset Sourcing Rules
- Intro/release runtime must behave as embedded-only for authored runtime assets.
- Filesystem asset fallback is diagnostics-only and must stay behind compile-time diagnostics guards.
- Do not silently pick up unreferenced disk assets in intro/release behavior.

## Cursor Rules
- Cursor visibility is authored runtime state, not fade-derived behavior.
- Hide the OS cursor only while the intro/demo window is active and in the foreground.
- Always restore the normal OS cursor on focus loss and shutdown.

## 3D Stage Timing Rules
- Treat global `scene3d.txt` timing as continuous across scene changes by default.
- Only explicit per-scene timing overrides (`scene_<id>.enabled`, `opacity`, `start_s`, `end_s`, `fade_in_s`, `fade_out_s`) should restart the local 3D timing window for that scene.
- Keep `.meshbin` v4 material-slot support lightweight and deterministic; preserve up to 8 OBJ/MTL ranges with diffuse + optional normal-map paths without adding a heavy runtime material system.

## Shader Authoring Source of Truth
- Author shader scene logic in split files under `assets/shader/scenes/*.glsl`.
- Keep shared shader glue in `assets/shader/shader_common.glsl` and `assets/shader/shader_footer.glsl`.
- `assets/shader/fragment.glsl` is generated from split sources by CMake and is the diagnostics/runtime fragment override path.
- Keep scene-id dispatch stable unless both runtime and editor/export semantics are updated together.

## Text Object Semantics
- Treat text assets as explicit authored objects (`title_main`, `credits_main`, `scroll_text`, `multiline_text`).
- Keep Active timing and Effect timing semantics explicit and separate for text cues.
- For non-scroll text cues, normalized coordinates are authored anchor coordinates; `x=0.5`, `y=0.5` should place text at screen center.

## Build and Size Discipline
- Release builds must strip nonessential debug paths.
- Prefer compile-time feature flags for optional dev behavior.
- Minimize dependencies and import surface.
- Keep code and data packer-friendly.
- Keep heavyweight 3D renderer state runtime-owned and off the stack; do not "fix" stack pressure by parking multi-object mesh state in a large static instance that bloats the final PE.
- Use this build command:
  - cmake -S . -B build ; cmake --build build --config Release
- When the optional 3D path, `scene3d` pipeline, or OBJ/MTL import path changes, also verify:
  - cmake -S . -B build -DREV_ENABLE_3D=ON ; cmake --build build --config Release

## Runtime Tuning Knobs
- Keep user-tunable constants grouped near top of implementation files as macros.
- Group knobs by intent, not by scene naming:
  - TIMELINE_*: phase durations, fades, transitions
  - CAMERA_*: offsets, motion, FOV-like controls
  - TEXT_*: startup text/overlay strings and typography
  - COMPOSITE_*: layer ordering, blend amounts, opacity
  - COLOR_*: palette ramps, contrast, fog/atmosphere
  - RHYTHM_*: tempo reference values used by authored timing workflows
- Keep knobs explicit and deterministic; avoid runtime config systems and external config parsing.

## Phase Model
- Do not hardcode docs to a fixed 2-phase show.
- Author productions as a deterministic phase graph (or linear phase timeline) with explicit entry/exit timing.
- Demo profile can use longer scene progression.
- Intro profile should keep transitions compact and byte-efficient.
- Loop timing should derive from music length or deterministic fallback cycle timing.

## Submission Readiness
- Deliver as a single .zip or .rar.
- Include compatibility notes for intended graphics path/settings.
- Set sensible defaults so staff can run without manual tuning.
- Ensure no external downloads or network dependencies.

## C++ Conventions
- Keep code explicit with clear ownership and side effects.
- Functions: PascalCase.
- Types: PascalCase.
- Variables: lower_snake_case.
- Members: trailing underscore.
- Constants: kPascalCase.
- Macros: ALL_CAPS only when necessary.
- Files: snake_case.

## Review Rules
- Prioritize findings by:
  1. binary cost
  2. abstraction cost
  3. runtime/frame-flow clarity
  4. ownership clarity
  5. compression-friendly simplicity
- Reject changes that move the project toward engine/tool bloat.

## Documentation
- `docs/API-REFERENCE.md` — public API for all subsystem headers
- `docs/ARCHITECTURE.md` — subsystem layout, frame loop, startup gate
- `docs/CONTROLS-KNOBS-REFERENCE.md` — all tuning knobs with current defaults
- `docs/SINUS-SCROLL.md` — legacy archival note for retired feature
- `OPENGL-EXPLAINER.md` — WGL bootstrap, shader pipeline, frame loop walkthrough
- `TECH-STACK.md` — dependency overview, build profiles, competition constraints
- When behavior or workflow semantics change, update the relevant docs and the matching `/memories/repo/` note in the same pass when practical.
