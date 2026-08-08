# Packed Runtime Feature Size Matrix

HiMYM packed releases derive compile-time feature flags from the exported cue
rows. `rev_pack` writes both `build/packed_assets.h` for the C++ preprocessor
and `build/packed_features.cmake` for target dependency selection. The editor
reconfigures CMake after packing, then builds `minimal_intro_packed`.

The external-cue `minimal_intro` target remains universal.

## Feature mapping

| Exported content | C++ flag | Optional target dependency |
|---|---|---|
| Music cue | `HIMYM_USE_XM` | `rev_xm` and libxm |
| Pixel cue | `HIMYM_USE_PIXEL` | `rev_pixel` |
| Pixel-emitter cue | `HIMYM_USE_PARTICLES` | `rev_particles` |
| Any mesh cue | `HIMYM_USE_MESH` | `rev_mesh` |
| External mesh with `mesh_type == 4` | `HIMYM_USE_GLTF` | `rev_gltf` and `rev_mesh` |
| Animated-sprite cue | `HIMYM_USE_ANIMATED_SPRITE` | none; prunes runtime code |
| Scrolling-text cue | `HIMYM_USE_SCROLL_TEXT` | none; prunes runtime code |

Particle emitters do not enable mesh by themselves. Mesh attachment projection
is compiled only when the same project also contains mesh cues.

Shader source selection is finer grained than the subsystem flags. The packed
header contains only preset IDs referenced by fullscreen shader cues and by
enabled image, animated-sprite, or pixel asset shaders. Preset 0 is retained as
the deterministic fallback when a project has no shader cue. The universal
47-preset registry remains available to the editor and external-cue runtime,
but is not linked into a current-format packed runtime.

## Controlled raw executable measurements

Measured August 8, 2026 with MSVC Release (`/O1 /GL`, `/LTCG /OPT:REF
/OPT:ICF`). Every row used the same packed Salute header and embedded asset
table; only feature macros and matching CMake dependencies changed. Sizes are
uncompressed PE file sizes and include the identical embedded project data.

| Enabled optional systems | EXE bytes | Change from previous row |
|---|---:|---:|
| XM, pixel, particles, mesh, glTF | 1,601,536 | baseline |
| XM, mesh, glTF | 1,586,688 | -14,848 |
| XM, procedural mesh | 1,513,984 | -72,704 |
| XM only | 1,491,456 | -22,528 |
| None of the five optional systems | 1,448,960 | -42,496 |

Total optional-system reduction in this controlled build: **152,576 bytes**.
Alignment and link-time optimization mean subsystem deltas should not be
treated as perfectly additive across unrelated projects.

For a representative shader/text-only project (`demos/lilbox`), the generated
manifest selected only `rev_curve`, `rev_platform`, `rev_runtime`, and
`rev_shader`. Its packed executable measured 217,600 bytes including its own
embedded cues and text asset. Project executable sizes are not directly
comparable because embedded asset payloads differ.

## Validation evidence

- Shader/text-only packed project: full 30-second playback, exit code 0.
- `demos/3dtest` glTF packed project: full 20-second playback, exit code 0.
- Salute XM/glTF packed project: ran for the 40-second smoke window; it remained
  active because the authored project loops and was then deliberately stopped.
- `text_animation_tests`: PASS.
- `gltf_import_tests tests/fixtures/blender_axis_material.gltf`: PASS.
- `particle_direction_tests`: PASS.
- `packed_feature_manifest_tests`: PASS for empty, shader-only, asset-shader, XM, pixel,
  particle, procedural-mesh, and glTF projects; both generated manifests are
  checked for identical feature selections.
- `tools/test_packed_pipeline.ps1`: PASS for pack, configure, and Release link
  across the same seven feature shapes. Missing required assets and deliberately
  mismatched generated manifests fail as expected.
- Transferred `rectruitro` project: PASS with its original XM path changed to an
  unavailable drive. The packer resolved all 25 assets from the copied project,
  configured the XM-only optional runtime path, and produced a Release executable.
- Windows 10 manual validation by the project author: PASS for `rectruitro`
  editor/standalone visual parity, ESC and Alt+F4 shutdown during playback and
  audio teardown, mixed 2D/3D GL-state and draw ordering, and opaque-before-
  transparent glTF material-slot rendering.
- A second Windows 10 machine exposed resolution-dependent asset placement.
  The runtime now refreshes its physical client dimensions from `WM_SIZE` and
  `GetClientRect`, while rendering remains on the authored 1920x1080 canvas and
  the final output is uniformly scaled into a centered 16:9 viewport. Manual
  retesting on that non-16:9 machine confirmed the corrected composition.
- `tools/test_editor_pipeline.ps1`: PASS for a generated all-cue regression
  project. The headless editor loaded `Salute`, added the missing pixel,
  particle, scroll-text, and post-effect families, saved, reloaded, exported,
  packed 30 assets, enabled all optional feature groups, and linked a
  1,716,736-byte Release runtime.

Visual appearance was presented through the native Win32/OpenGL windows during
the smoke runs. Automated pixel-diff capture is not currently part of the test
harness, so editor/runtime visual parity still requires human observation for
release candidates.

## Repeatable size report

`tools/report_packed_size.ps1` reconfigures the selected build with an MSVC
linker map, rebuilds `minimal_intro_packed`, and writes
`packed_size_report.txt`. It separates unique embedded asset bytes, embedded
cue bytes, and the remaining PE size. The remainder includes code, constants,
imports, headers, alignment, and linker overhead; it is not presented as pure
code size.

All-cue regression baseline on August 8, 2026:

| Measurement | Bytes |
|---|---:|
| Release executable | 1,716,736 |
| Unique embedded assets | 1,340,986 |
| Embedded cues | 12,424 |
| Total embedded payload | 1,353,410 |
| Non-asset EXE remainder | 363,326 |
| MSVC linker map | 238,871 |

All optional feature groups were enabled. Per-library symbol aggregation from
the linker map attributes the 203,452-byte CODE segment approximately as:

| Link owner | CODE bytes |
|---|---:|
| `main` runtime orchestration | 78,092 |
| `rev_gltf` | 61,448 |
| libxm (`xm`) | 30,016 |
| `rev_runtime` | 14,616 |
| `rev_mesh` | 5,416 |
| MSVC runtime libraries | 5,164 |
| `rev_particles` | 2,804 |
| `rev_xm` | 2,172 |
| `rev_platform` | 1,256 |
| `rev_shader` | 1,184 |
| `rev_curve` | 700 |
| `rev_pixel` | 584 |

Attribution assigns each symbol range to its MSVC map `Lib:Object` owner.
COMDAT folding means the values are approximate, but they are deterministic
and suitable for tracking subsystem trends between equivalent builds.

## Release profile comparison

`tools/test_release_profiles.ps1` builds the retained all-cue editor-pipeline
fixture twice from identical generated manifests. `GENERAL` favors normal demo
reliability and performance. `INTRO` favors raw size and disables stack
protection, RTTI, and C++ exception unwinding, so it must receive the full
production playback validation before competition use.

Measured August 8, 2026:

| Profile | EXE bytes | Embedded payload | Non-asset remainder | CODE segment |
|---|---:|---:|---:|---:|
| `GENERAL` | 1,773,568 | 1,353,410 | 420,158 | 246,002 |
| `INTRO` | 1,719,296 | 1,353,410 | 365,886 | 202,924 |

The INTRO profile saved **54,272 bytes** (3.06% of the complete executable)
for this fixture. This is a raw, uncompressed comparison; final compressor
results remain project-specific.

## Translation-unit retention audit

The largest source files were checked before physically splitting them. Both
Release profiles enable MSVC function-level linking (`/Gy`), whole-program
optimization (`/GL`), and linker elimination (`/OPT:REF`). In the all-cue
linker maps, `rev_gltf.cpp` retains packed playback entry points such as
`LoadMeshFromMemory` and `BuildAnimatedNodeDeltaMatricesAll`, while the
filesystem/editor-only `LoadMesh`, editor-only `ExtractTextures`, and the
unused single-animation matrix variant are absent.

Therefore the current monolithic glTF source file is not forcing those unused
functions into the packed executable. A physical translation-unit split was
rejected because it produced no additional dead-code boundary beyond the one
already supplied by `/Gy`. Run `tools/audit_gltf_link_retention.ps1` after the
profile harness to verify this assumption against the INTRO map.

## Release procedure

1. Export the current project to `cues.txt`.
2. Run the editor pack action or `pack_cli` to regenerate both packed manifests.
3. Reconfigure CMake so `packed_features.cmake` updates the target graph.
4. Build `minimal_intro_packed` in Release.
5. Check the dependency list in build output and record the raw EXE size.
6. Run the complete packed production and visually verify it.
7. Apply the chosen executable compressor and record compressed size separately.

## Compressor measurements

Measured August 9, 2026 from the retained transferred `rectruitro` manifests.
Both profiles used the identical XM-only packed assets and the same three
referenced shader presets (IDs 0, 5, and 18). UPX 5.1.1 was invoked with
`--best --lzma`, and each result passed `upx -t` integrity validation.

| Profile | Raw EXE | UPX EXE | Bytes removed | Packed/raw |
|---|---:|---:|---:|---:|
| `GENERAL` | 320,000 | 164,864 | 155,136 | 51.52% |
| `INTRO` | 300,544 | 159,232 | 141,312 | 52.98% |

INTRO remains the smaller result, saving 6,144 compressed bytes relative to
GENERAL. Run `tools/test_compressor_profiles.ps1 -KeepArtifacts` to reproduce
the builds, retain both raw and compressed executables, and write
`build/compressor_profiles/compressor_size_report.txt`.

Compared with the previous universal-preset baseline, project-specific shader
packing removed 89,600 raw / 14,336 UPX bytes from GENERAL and 89,088 raw /
14,336 UPX bytes from INTRO. Removing rectruitro's absent animated-sprite and
scrolling-text paths then saved another 8,192 raw / 3,072 UPX bytes from
GENERAL and 6,656 raw / 2,560 UPX bytes from INTRO. The retained executable
contains only the three shader sources required by rectruitro rather than all
47 presets.

The project author completed a full Windows 10 playback of the compressed
INTRO result and confirmed correct visuals, XM audio, ESC shutdown, and
Alt+F4 shutdown. This validates the compressor result on the development
machine; a separate clean-machine check is still tracked for release handoff.

Classic kkrunchy 0.23alpha2 was also acquired from the author's official
distribution and tested. Its executable is x86/PE32, while HiMYM targets
x64/PE32+. It reaches its PE parser but terminates with an internal
`exepacker.cpp` assertion and produces no output. The author identifies missing
x86-64 support, so kkrunchy is not a compatible compressor for the current x64
release target. The local downloaded copy is kept only under ignored
`build/tools/kkrunchy` and is not shipped in the editor release.
