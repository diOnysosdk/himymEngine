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

Particle emitters do not enable mesh by themselves. Mesh attachment projection
is compiled only when the same project also contains mesh cues.

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

Visual appearance was presented through the native Win32/OpenGL windows during
the smoke runs. Automated pixel-diff capture is not currently part of the test
harness, so editor/runtime visual parity still requires human observation for
release candidates.

## Release procedure

1. Export the current project to `cues.txt`.
2. Run the editor pack action or `pack_cli` to regenerate both packed manifests.
3. Reconfigure CMake so `packed_features.cmake` updates the target graph.
4. Build `minimal_intro_packed` in Release.
5. Check the dependency list in build output and record the raw EXE size.
6. Run the complete packed production and visually verify it.
7. Apply the chosen executable compressor and record compressed size separately.

