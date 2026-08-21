# HiMYM Roadmap

This is the current delivery roadmap for HiMYM. It tracks work against the
live C++ editor, packed runtime, and CMake targets. Historical implementation
plans remain under `PR/` and are not completion checklists for current code.

Priorities are ordered by competition reliability, executable size,
determinism, rendering capability, and then maintainability.

## Now: project-specialized packed runtime

Goal: packed releases contain only the runtime systems used by the exported
project, while `minimal_intro` remains a universal external-cue player.

- [x] Generate deterministic `HIMYM_USE_*` feature flags from packed cue rows.
- [x] Compile out XM playback when no music cue is authored.
- [x] Compile out pixel animation when no pixel cue is authored.
- [x] Compile out particle simulation when no pixel-emitter cue is authored.
- [x] Compile out glTF importing and animation when no external glTF mesh is authored.
- [x] Compile out procedural mesh rendering when no mesh cue is authored.
- [x] Pack only the fullscreen and enabled asset-shader GLSL presets referenced
      by the project, retaining preset 0 solely as the no-cue fallback.
- [x] Author and execute Shadertoy-compatible Image/Buffer A-D pipelines with
      scaled passes, texture inputs, previous-frame feedback, and XM audio data.
- [x] Pack only enabled project-pipeline GLSL and texture channels, preserving
      them during editor asset cleanup and resolving them from the project root.
- [x] Compile out animated-sprite and scrolling-text parsing/rendering when the
      exported project contains no cues from those families.
- [x] Generate packed post-effect GLSL containing only effect branches enabled
      by global, scene-layer, or asset cue stacks.
- [x] Generate matching CMake feature selections and reconfigure after packing
      so unused optional libraries are not packed-target dependencies.
- [x] Add automated packer feature-manifest tests for empty, shader-only, XM,
      pixel, particle, procedural-mesh, and glTF projects.
- [x] Record controlled uncompressed size deltas in `PACKED_RUNTIME_SIZE.md`.
- [x] Record UPX 5.1.1 `--best --lzma` output sizes for the validated
      `rectruitro` project in both GENERAL and INTRO profiles.

Exit criteria:

- Every supported feature combination builds in Release.
- Projects with a feature enabled retain editor/runtime visual and timing parity.
- Disabled systems contribute no code or unwanted Windows imports to the final executable.
- Packed asset discovery remains deterministic and embedded-first.

## Next: release reliability and parity

Goal: make the editor-to-standalone handoff safe enough for competition use.

- [x] Generate a representative regression project covering every cue type.
- [x] Automate load -> save -> reload -> export -> pack -> configure -> build
      checks for that project.
- [x] Automate pack -> configure -> build checks for all packed-runtime feature shapes.
- [x] Smoke-test representative shader/text, glTF, and XM/glTF packed projects.
- [x] Add pipeline failure checks for missing required assets and mismatched manifests.
- [x] Validate a transferred real project with stale absolute asset paths.
- [x] Visually compare the `rectruitro` editor preview and packed standalone runtime.
- [x] Verify ESC and Alt+F4 shutdown during loading, playback, and audio teardown.
- [x] Audit 2D/3D interleaving for VAO, depth-write, blend, and draw-order parity.
- [x] Restore sprite UV state after pipeline compositing and keep text/scroll-text
      atlas orientation identical in editor, standalone, and packed playback.
- [x] Scale shader-text and scrolling-text pixel metrics from the authored
      1920x1080 canvas while preserving normalized placement at smaller runtimes.
- [x] Apply Shadertoy pipeline opacity curves and cue fade envelopes in the
      compositor, including the first/background layer over black.
- [x] Measure clamp-mode scroll travel from glyph extent and authored start so
      text exits the viewport without whitespace padding.
- [x] Validate opaque-before-transparent glTF material-slot rendering.
- [x] Track the actual Win32 client size and use the centered
      letterbox/pillarbox output viewport for the authored 16:9 canvas.
- [x] Confirm the corrected asset placement on the second Windows 10 machine
      and its non-16:9 display.
- [x] Resolve the libxm `xm_tick_envelope` return-path warning.
- [x] Resolve the libxm `load.c` C4333 shift warnings exposed by clean XM builds.
- [x] Add directional scene-entry wipes and interactive music-disc/diskmag
      menus with label or animated-sprite item visuals.
- [x] Hold interactive scenes until selection and return a selected destination
      to its originating menu on scene expiry or non-looping XM completion.
- [x] Support several independently positioned menu items sharing one
      animated-sprite cue without duplicating its frame assets.
- [x] Add 64 KiB, 128 KiB, and custom Crinkler output budgets, preflight locked
      competition outputs, and avoid the mesh InitOnce import cycle on x86.

Exit criteria:

- `editor_app`, `minimal_intro`, and `minimal_intro_packed` build in Release.
- Focused text, glTF, and particle tests pass.
- Representative projects render equivalently in editor preview and standalone playback.
- No stale or filesystem-only asset is required by a packed release.

## Then: size measurement and release profiles

Goal: make size decisions evidence-based and repeatable.

- [x] Produce repeatable linker maps and packed size reports.
- [x] Aggregate linker-map symbol ranges into per-library/subsystem CODE attribution.
- [x] Track unique packed-asset and embedded-cue bytes separately from the
      non-asset executable remainder.
- [x] Establish checked `GENERAL` and `INTRO` Release profiles for normal demos
      and size-limited packed intros.
- [x] Evaluate splitting monolithic library translation units. Linker-map
      auditing confirmed `/Gy /GL /OPT:REF` already removes unused glTF
      entry points, so a physical source split is not currently justified.
- [x] Document the UPX compressor workflow and manually verify the compressed
      INTRO executable's visuals, XM audio, ESC, and Alt+F4 on Windows 10 x64.
- [x] Repeat the release test on another Windows machine and complete the final
      Windows 11 x64 competition-machine compatibility check.

Exit criteria:

- A repeatable command reports raw EXE size, embedded asset size, and compressed size.
- Each release profile has a documented feature set and validation command.
- Size regressions are visible before a competition build is handed off.

## Later: authoring workflow improvements

Goal: improve iteration without expanding the runtime architecture.

- [ ] Improve build/pack progress and error presentation inside the editor.
- [ ] Finish or remove inactive asset-browser actions marked TODO in the current UI.
- [ ] Add clearer packed-feature and estimated-size information to the Build UI.
- [ ] Add project validation that reports unsupported cue combinations before compiling.
- [ ] Keep project JSON, `cues.txt`, preview, runtime parsing, and packing round trips covered.

## Continuing maintenance

- Keep shared cue contracts in `revision_libs/rev_runtime/include/rev_runtime.h`.
- Update current documentation whenever a durable contract changes.
- Preserve deterministic export and render ordering.
- Avoid runtime plugins, scripting, reflection, discovery layers, and unnecessary dependencies.
- Treat `PR/ROADMAP.md` as historical architecture material only.

## Validation commands

```powershell
cmake --build build --config Release --target editor_app
cmake --build build --config Release --target minimal_intro
cmake --build build --config Release --target minimal_intro_packed
cmake --build build --config Release --target text_animation_tests
cmake --build build --config Release --target gltf_import_tests
cmake --build build --config Release --target particle_direction_tests
cmake --build build --config Release --target packed_feature_manifest_tests
.\tools\test_packed_pipeline.ps1
.\tools\test_editor_pipeline.ps1
.\tools\report_packed_size.ps1 -BuildDirectory build\editor_pipeline_test\cmake
.\tools\test_release_profiles.ps1
.\tools\audit_gltf_link_retention.ps1
.\tools\test_compressor_profiles.ps1 -KeepArtifacts
```

Run the focused test executables after building them. Rebuild `pack_cli` or the
editor before validating generated `packed_assets.h` after packer changes.

Last reviewed: August 13, 2026.
