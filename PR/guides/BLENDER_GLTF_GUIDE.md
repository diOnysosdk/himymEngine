# Blender 5.2 to HiMYM glTF Guide

## Ready-to-use HiMYM setup

Open:

```text
tools/blender/himym_template.blend
```

The template keeps Blender's normal Z-up workspace and contains:

- Metric units with unit scale `1.0`.
- An identity `HIMYM_ROOT`.
- A one-meter scale-reference cube.
- One perspective camera with practical clipping defaults.
- One sun light.
- A 1920×1080 render aspect.

To enable the HiMYM sidebar in any Blender file:

1. Open the **Scripting** workspace.
2. Open `tools/blender/himym_blender.py`.
3. Press **Run Script**.
4. Open the 3D View sidebar (`N`) and select **HiMYM**.
5. Use **Setup HiMYM Scene** or **Export HiMYM GLB**.

The setup operation is non-destructive: it sets metric display units and
creates or resets `HIMYM_ROOT`; it does not rotate or delete existing work.
The export operation synchronizes Blender camera shift and deliberately named
HiMYM Noise nodes into Custom Properties, validates the scene, and invokes the
Blender glTF exporter with the fixed HiMYM options.

For a material Noise Texture, set the node label to exactly `HiMYM Noise` and
link it directly to Principled **Base Color**, **Roughness**, or **Emission
Color**. The helper copies its Scale, Detail, Roughness, and Distortion values
into the compact material extras. Set `himym_noise_strength` on the material
when a blend strength other than `1.0` is needed.

The same helper supports deterministic command-line export:

```powershell
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" `
  --background my_scene.blend `
  --python tools\blender\himym_blender.py -- `
  --export build\my_scene.glb
```

HiMYM consumes glTF 2.0 coordinates as exported. glTF is right-handed, uses
`+Y` as up, and defines cameras as looking along local `-Z`. Do not add a
manual 90-degree correction in HiMYM or pre-rotate the complete Blender scene
to compensate. The Blender glTF exporter owns the Blender Z-up to glTF Y-up
conversion.

## Recommended export

1. Apply or deliberately preserve object transforms in Blender.
2. Export as glTF 2.0 (`.glb` is preferred for portable assets).
3. Include cameras and punctual lights when the HiMYM cue should use them.
4. Enable **Custom Properties** when using HiMYM procedural material settings.
5. Import the result into a mesh cue without adding an axis correction.

HiMYM bakes each imported mesh node's glTF world transform into its vertices.
Normals are transformed with the inverse-transpose, so non-uniform object scale
is supported. Imported cameras retain glTF's local `-Z` forward direction.

Base-color textures honor the glTF texture-coordinate set and
`KHR_texture_transform` offset, rotation, scale, and `texCoord` override emitted
by Blender's Texture Coordinate and Mapping nodes. The transform is baked into
the imported vertices to keep editor and packed playback compact and identical.
Texture wrapping follows the glTF sampler; Blender's Repeat mode therefore
continues to tile when transformed UVs leave the 0-to-1 range.

## PBR material contract

Each glTF primitive keeps its own material slot. HiMYM consumes the core
metallic/roughness fields: base-color factor and texture, metallic factor,
roughness factor, the metallic-roughness texture (roughness in G and metallic
in B), tangent-space normal texture and scale, occlusion texture and strength,
and emissive factor, texture,
and `KHR_materials_emissive_strength`. Base-color and emissive images are
decoded as sRGB; normal and metallic/roughness images are sampled as linear
data. The compact preview and runtime shader uses GGX direct lighting.

Tangents are generated from the transformed positions, inverse-transpose
normals, and UVs. Their handedness is retained for mirrored UV islands and is
corrected when a cue transform changes handedness. This avoids requiring a
second packed tangent asset while supporting non-uniform object transforms.

glTF `OPAQUE`, `MASK`, and `BLEND` are distinct. OPAQUE ignores base-color
alpha, MASK stays in the depth-writing opaque pass and discards below
`alphaCutoff`, and BLEND is drawn after opaque slots with depth writes disabled.

## Imported lights and compact shadows

Enable **Use glTF Light** on a mesh cue to use the first punctual light in the
active glTF scene. Color and authored intensity are retained. Directional
intensity remains linear; point and spot candela-like values use a compact
logarithmic exposure mapping before inverse-square/range attenuation so normal
Blender power values do not immediately saturate the LDR framebuffer. Spot lights additionally apply the glTF
inner/outer cone. Spot lights cast a single perspective 1024x1024 shadow map
using the same compact depth/PCF path as directional lights. A Blender Sun exports as a directional light; HiMYM imports its world
direction and renders a fixed 1024x1024 depth map for that imported mesh. The
shadow pass is automatic, uses 3x3 percentage-closer filtering, and is identical
in editor preview and standalone playback. It deliberately provides self-shadowing
only: geometry from separate mesh cues does not cast into the imported mesh's map.
Additional simultaneous lights, cascades, point-light cubemap shadows, and Blender Eevee/Cycles
shadow settings are outside this compact contract.

When no imported light is selected, the existing neutral key light and a small
constant ambient fallback remain active. HiMYM does not currently import an
HDR environment or generate reflection probes; bake important environment
response into textures when it is required.

## Transform animation

Translation, rotation, and scale channels preserve glTF `LINEAR`, `STEP`, and
`CUBICSPLINE` interpolation. Cubic channels retain their input/value/output
tangent triplets, scale tangents by the keyframe interval during Hermite
evaluation, and normalize quaternion results. Animated node hierarchies,
cameras, lights, and HiMYM attachment sockets use the same evaluated local
transforms. Skinning and morph targets remain outside this compact importer.

## Imported cameras

When **Use glTF Camera** is enabled on a mesh cue, HiMYM uses the first camera
in the active glTF scene. The following compact projection contract is kept in
editor preview, standalone playback, and packed playback:

- Full node transform, including camera roll.
- Perspective or orthographic camera type.
- Perspective vertical field of view.
- Perspective near plane and optional finite far plane. A missing glTF far
  plane uses an infinite perspective projection.
- Optional perspective aspect ratio; otherwise the preview/runtime viewport
  aspect is used.
- Orthographic `xmag`, `ymag`, near plane, and far plane.

Blender lens shift has no core glTF camera field. For the uncommon case where
it is needed, add these Custom Properties to the Blender camera data and enable
**Custom Properties** during export:

```text
himym_camera_shift_x = 0.125
himym_camera_shift_y = -0.25
```

These are normalized projection-center offsets and are clamped to `[-1, 1]`.
They work for perspective and orthographic cameras. HiMYM deliberately does
not import depth of field, focus distance, aperture simulation, sensor
emulation, or other heavyweight Blender camera effects.

## Compact procedural noise material

Core glTF does not serialize an arbitrary Blender shader node graph. HiMYM
therefore reads a small, stable set of Blender material Custom Properties from
the material's glTF `extras` object:

| Blender material Custom Property | Type | Default | Meaning |
|---|---:|---:|---|
| `himym_noise_target` | string or integer | disabled | `base_color`/`1`, `roughness`/`2`, or `emission`/`3` |
| `himym_noise_scale` | float | 5.0 | World-space noise frequency; minimum 0.001 |
| `himym_noise_detail` | float | 2.0 | Octaves, clamped to 1–4 |
| `himym_noise_roughness` | float | 0.5 | Amplitude reduction per octave, clamped to 0–1 |
| `himym_noise_distortion` | float | 0.0 | Domain warp amount |
| `himym_noise_strength` | float | 1.0 | Blend strength, clamped to 0–1 |

Example material properties:

```text
himym_noise_target = "roughness"
himym_noise_scale = 6.5
himym_noise_detail = 3
himym_noise_roughness = 0.4
himym_noise_distortion = 0.25
himym_noise_strength = 0.75
```

The names intentionally describe a HiMYM material rather than promising
pixel-identical Blender Noise Texture output. HiMYM uses bounded four-octave
value noise to keep the shader and packed runtime compact. Use baked image
textures when exact Blender shading is required.

Only material `extras` are consumed. Unknown custom properties are ignored.
The imported values are copied to fixed material-slot fields; Blender APIs and
node-graph interpretation are not part of playback.

## Pixel-emitter attachment sockets

The HiMYM Blender sidebar can create or mark named attachment sockets. A socket
is normally an Empty parented to an object or armature bone so its local
position and orientation can be edited without changing the rig. Use names such
as `FX_Exhaust`, `FX_Hand`, or `FX_Eye`.

Each socket is exported as a glTF node with these extras:

```text
himym_attachment = true
himym_attachment_name = "FX_Exhaust"
himym_direction_axis = "+Z"
```

The direction axis is one of `+X`, `-X`, `+Y`, `-Y`, `+Z`, or `-Z`. The editor
can attach a pixel emitter to the declared socket, add a local XYZ offset, and
override the exported axis. Preview, standalone, and packed playback evaluate
the animated node transform, apply the mesh cue transform, and project the
socket position and axis through the active mesh camera. The current compact
milestone keeps particle simulation in 2D; the socket controls the emitter
origin and spray direction.

Attachment names must be non-empty, unique within an exported scene, and must
not contain the `|` character used by `cues.txt`.

## Regression fixture

`tests/fixtures/blender_axis_material.gltf` contains an asymmetric transformed
mesh, non-uniform scale, a shifted finite perspective camera, and HiMYM noise
properties. Build and run:

```powershell
cmake --build build --config Release --target gltf_import_tests
build\bin\Release\gltf_import_tests.exe tests\fixtures\blender_axis_material.gltf
```

The test verifies transform orientation, inverse-transpose normals, generated
tangent handedness, per-slot PBR and alpha propagation, punctual-light fields, camera
direction and projection fields, finite/infinite perspective matrices,
orthographic projection, cubic animation evaluation, and material-extra parsing without requiring an
OpenGL window.
