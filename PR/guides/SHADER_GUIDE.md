# HiMYM Shader Authoring Guide

This is the current guide for using fullscreen shaders and Shadertoy-style
multipass pipelines in the C++/ImGui editor, standalone runtime, and packed
runtime.

HiMYM requires OpenGL 3.3 or newer and uses GLSL 330 core. There are two shader
workflows:

1. **Preset shaders** are built into HiMYM and expose the editor's palette,
   speed, intensity, warp, noise, transform, fade, and curve controls.
2. **Shadertoy pipelines** load project-owned GLSL files and support Image,
   Buffer A-D, four channels per pass, texture inputs, feedback, and live XM
   audio data.

Both workflows use a normal shader cue for timing, ordering, blending, and
opacity. A pipeline is assigned to that cue; if it is missing or invalid, HiMYM
uses the cue's selected preset as a fallback.

## Quick Start: Built-In Preset

1. Select a scene in the timeline.
2. Add a shader cue with the shader **+ Cue** button.
3. In **Shader Parameters**, choose a shader under **Select Shader**.
4. Set the cue start/end, layer order, blend mode, fades, and opacity.
5. Adjust palette, speed, intensity, warp, noise, position, rotation, or motion.
6. Close the modal and preview the scene.
7. Use **Do It All** when ready to save, export, pack, build, and run.

Preset 47, **Shadertoy Neon Lattice**, is a useful single-pass compatibility
example.

## Quick Start: Custom GLSL Pipeline

1. Add or edit a shader cue and open **Shadertoy Pipeline**.
2. Click **Create Pipeline**. The new pipeline is assigned to the cue and its
   Image pass is enabled.
3. Open **Image**, then either:
   - click **Browse GLSL** to import a `.glsl`, `.frag`, or `.fs` fragment shader; or
   - click **New / Paste GLSL** to open the built-in source editor and paste
     Shadertoy code directly.
4. Confirm the modal reports **Valid pipeline: 1 enabled pass(es)**.
5. Preview, then use **Do It All** to validate the packed runtime.

The browse button copies the source into the project's `project_assets`
directory and stores a portable `project_assets/...` path. Keep project shader
and texture assets there so another machine and the packer can resolve them.

The built-in source editor starts new files with a GLSL 330-compatible
`mainImage` template. Choose the filename, paste or write the shader, then use
**Save** or **Save & Close**. Closing the source window with its X also saves
pending changes. The source is written into `project_assets`, assigned to the
current pass, and recompiled for preview immediately. Use **Edit GLSL** beside
any assigned source to reopen and modify an imported or previously pasted
shader. **Discard & Close** is the explicit way to leave without saving.

Save the project before creating a source in the built-in editor so HiMYM knows
where its `project_assets` directory belongs.

## Writing a Single-Pass Shader

The easiest portable format is a Shadertoy-style `mainImage` function:

```glsl
#version 330 core

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 p = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    float glow = 0.03 / max(abs(length(p) - 0.45), 0.003);
    vec3 color = glow * (0.5 + 0.5 * cos(iTime + vec3(0.0, 2.0, 4.0)));
    fragColor = vec4(color, 1.0);
}
```

Do not add `main()` when using this form. HiMYM detects `mainImage`, injects the
supported uniforms and output declarations, and creates the GLSL `main()`
bridge. Use `#version 330 core` as the first line. If a source omits the version
or uses a common Shadertoy ES version, HiMYM normalizes it to `#version 330 core`
in memory without modifying the project file; the pipeline modal reports when
this happens. GLSL features that are genuinely incompatible with 330 still
need to be ported manually.

You can instead write a native GLSL fragment shader:

```glsl
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float iTime;
uniform vec3 iResolution;

void main()
{
    vec2 p = (uv * 2.0 - 1.0) *
             vec2(iResolution.x / iResolution.y, 1.0);
    fragColor = vec4(0.5 + 0.5 * cos(iTime + p.xyx + vec3(0, 2, 4)), 1.0);
}
```

Native shaders must declare every input they use. HiMYM supplies the same
runtime values whether the declarations came from the adapter or the source.

## Supported Shadertoy Uniforms

| Uniform | Meaning |
|---|---|
| `vec3 iResolution` | Current pass width, height, and `1.0` |
| `float iTime` | Timeline time in seconds |
| `float iTimeDelta` | Frame delta; editor preview uses deterministic `1/60` |
| `int iFrame` | Rendered frame counter |
| `vec4 iMouse` | Standalone window-relative mouse; editor currently supplies zero |
| `sampler2D iChannel0..3` | The four inputs configured for this pass |

Not currently supplied: `iDate`, `iChannelResolution`, `iChannelTime`,
`iSampleRate`, cubemaps, volume textures, video, webcam, microphone, and
keyboard textures. Replace their uses or declare your own constants when
porting a shader that depends on them.

## Building a Multipass Effect

A pipeline contains five fixed pass slots:

- **Image**: required final result, always executed last.
- **Buffer A-D**: optional intermediate or persistent passes.

For every enabled pass:

1. Enable the pass.
2. Select its GLSL source.
3. Choose a **Resolution Scale** from `0.125` to `1.0`.
4. Configure Channel 0-3.

HiMYM validates dependencies and calculates a deterministic order. A buffer is
rendered before any pass that reads it. Cycles and references to disabled
buffers are rejected and shown in red in the modal.

### Example: Buffer A Feeding Image

Configure **Buffer A** with this source:

```glsl
#version 330 core
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float rings = 0.5 + 0.5 * sin(length(uv - 0.5) * 80.0 - iTime * 5.0);
    fragColor = vec4(vec3(rings), 1.0);
}
```

Then set **Image → Channel 0 → Buffer A** and use:

```glsl
#version 330 core
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float field = texture(iChannel0, uv).r;
    vec3 color = mix(vec3(0.01, 0.02, 0.08), vec3(0.2, 0.8, 1.0), field);
    fragColor = vec4(color, 1.0);
}
```

## Channel Types

Each pass has four independently configured channels:

| Type | Data bound to `iChannelN` |
|---|---|
| **None** | Texture 0; sampling result is undefined/black depending on driver |
| **Texture** | An imported 2D image |
| **Buffer A-D** | That buffer's latest output from the current frame |
| **Self Previous Frame** | This pass's output from the previous frame |
| **Audio Spectrum** | Live Shadertoy-style XM audio texture |

Use **Load Texture** for texture inputs. HiMYM copies supported images into
`project_assets`, loads them in the editor and standalone runtime, and embeds
only referenced channel textures in packed builds.

Channel controls follow the selected type:

- **Texture** offers **Load Texture**, an editable texture path, and **Clear
  Texture**.
- **Buffer A-D** offers shortcuts to load, create/paste, or edit the referenced
  buffer pass's GLSL. Loading source through the shortcut also enables that
  buffer pass.
- **Self Previous Frame** offers the same source actions for the current pass.
- **None** and **Audio Spectrum** do not show unrelated file controls; audio
  reports that it uses the live XM spectrum texture.

Reloading a shader or texture with the same filename invalidates the editor
preview cache, so the updated file appears without restarting the editor.

### Previous-Frame Feedback

To make trails, fluid-like accumulation, or temporal distortion:

1. Enable a Buffer pass.
2. Set one of its channels to **Self Previous Frame**.
3. Sample that channel in the same pass.
4. Feed the buffer into Image or another buffer.

```glsl
#version 330 core
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec2 drift = vec2(0.0015, 0.0);
    vec3 history = texture(iChannel0, uv - drift).rgb * 0.985;
    float pulse = exp(-80.0 * dot(uv - 0.5, uv - 0.5));
    fragColor = vec4(history + pulse * vec3(0.1, 0.4, 1.0), 1.0);
}
```

Feedback uses two textures per enabled pass and swaps them after rendering.
Initial feedback is cleared to transparent black. Seeking or looping does not
currently reset feedback history; reopening/resizing the preview or rebuilding
the pipeline resources does.

### Audio Spectrum and Waveform

**Audio Spectrum** binds a `512 × 2` single-channel texture generated from the
latest 1024 decoded stereo XM frames:

- Row 0: frequency magnitude in `[0,1]`.
- Row 1: waveform mapped from `[-1,1]` to `[0,1]`.

Sample the center of each row:

```glsl
float spectrum = texture(iChannel0, vec2(frequency, 0.25)).r;
float waveform = texture(iChannel0, vec2(position, 0.75)).r * 2.0 - 1.0;
```

Example audio bars:

```glsl
#version 330 core
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float band = floor(uv.x * 64.0) / 64.0;
    float level = texture(iChannel0, vec2(band, 0.25)).r;
    float bar = step(uv.y, level);
    vec3 color = mix(vec3(0.02), vec3(0.2, 0.7, 1.0), bar);
    fragColor = vec4(color, 1.0);
}
```

When no XM is active, the spectrum row is zero and the waveform row represents
silence at `0.5`. Audio analysis and texture upload happen only when a pipeline
actually uses the channel.

## Resolution and Performance

Resolution scale applies independently to every pass. Use it deliberately:

- `1.0`: final Image, sharp patterns, text-like details.
- `0.5`: raymarching, blur, clouds, fluid buffers.
- `0.25` or `0.125`: broad bloom, feedback, displacement, cheap simulation.

An enabled pass owns two RGBA render targets, so its approximate color-buffer
memory is `width × height × 4 × 2` bytes. A 1920×1080 pass at full scale is
about 16 MiB before driver overhead; half scale is about 4 MiB.

For competition reliability:

- Bound raymarch and loop iteration counts at compile time.
- Avoid dynamic indexing patterns that vary across older 3.3 drivers.
- Clamp denominators and ray distances to avoid NaNs and infinities.
- Prefer a lower buffer resolution over a visually invisible loop reduction.
- Test on the target Windows 11 machine and GPU when possible.

## Layering, Timing, and Fallback

The owning shader cue controls when and how the pipeline is composited:

- Cue start/end and scene timing
- Layer role and layer order
- Blend mode and opacity
- Fade-in and fade-out envelope

Pipeline GLSL receives project timeline time, not time reset to the cue start.
Use `iTime` directly for globally synchronized animation. Pipeline files do not
automatically receive preset-only palette, noise, speed, warp, transform, or
curve uniforms; encode controls in GLSL or use timing/layer controls on the cue.

Keep a suitable preset selected on the cue. It is rendered when the assigned
pipeline cannot be loaded, compiled, or validated.

## Saving, Exporting, and Packing

Pipeline data is saved in project JSON. Export writes these deterministic
sections to `cues.txt`:

- `[shader_pipeline_cues]`
- `[shader_pipelines]`
- `[shader_pipeline_passes]`
- `[shader_pipeline_channels]`

The packer scans enabled passes and embeds only referenced GLSL sources and
texture channel assets. After changing exported pipeline data, re-export and
repack before judging `minimal_intro_packed`; packed feature and asset manifests
are generated from the exported cues.

Recommended workflow:

1. Save and preview in the editor.
2. Confirm the pipeline is reported valid.
3. Use **Do It All**, or explicitly Export → Pack → configure/build → Run.
4. Compare editor preview with the standalone or packed runtime.

## Adding a Built-In Preset

Project pipelines need no C++ registry edit. To ship a reusable built-in preset,
add one unique entry to `g_shader_presets` in
`revision_libs/rev_editor/src/shader_presets.cpp`, then rebuild `editor_app` and
`minimal_intro`. The registry is authoritative; never assume the IDs listed in
older documentation are complete.

## Troubleshooting

### Pipeline is red or falls back to the preset

- Ensure Image is enabled and has a source.
- Ensure every enabled Buffer has a source.
- Do not reference a disabled buffer.
- Remove current-frame dependency cycles.
- Use **Self Previous Frame**, not the same Buffer channel, for feedback.

### Shader compiles on Shadertoy but not in HiMYM

- Prefer `#version 330 core`. HiMYM repairs missing or different version
  directives in memory, but it cannot translate incompatible GLSL features.
- Remove unsupported uniforms and input types.
- Remove Shadertoy Common-tab dependencies or copy the shared functions into
  each source that uses them.
- Supply each Buffer/Image tab as its own GLSL file.
- Check the debugger/runtime shader compile log for the exact GLSL error.

### Texture or GLSL works in editor but not packed runtime

- Use **Browse GLSL** or **Browse Texture** so assets are copied into
  `project_assets`.
- Re-export and repack after changing a source path or channel.
- Confirm the referenced file still exists at export time.

### Audio shader is static

- Confirm an XM music cue is active and playing.
- Confirm the pass channel is **Audio Spectrum** and the shader samples the
  correct row.
- Remember that silence is spectrum `0.0` and waveform `0.5`.

### Feedback behaves differently after seeking

Feedback is render-history state, not timeline-derived state. Restart or resize
the preview to clear it when evaluating from a clean initial frame.

## Relevant Source Files

- `revision_libs/rev_runtime/include/rev_runtime.h`: pipeline/channel contract
- `revision_libs/rev_runtime/src/rev_runtime.cpp`: graph validation and audio texture
- `revision_libs/rev_shader/src/shader.cpp`: GLSL compilation and `mainImage` adapter
- `revision_libs/rev_editor/src/editor_modals.cpp`: pipeline authoring UI
- `revision_libs/rev_editor/src/editor_context.cpp`: editor preview execution
- `examples/minimal_intro/main.cpp`: standalone and packed execution
- `revision_libs/rev_pack/src/rev_pack.cpp`: project-specific packed discovery
