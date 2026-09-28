---
name: himym-shader-graphics
description: Implement or review HiMYM rendering and visual-content systems. Use for GLSL, rev_shader, shader dispatch/uniforms, rev_mesh, rev_gltf, materials/transparency, rev_pixel, rev_particles, post-effects, asset shaders, OpenGL state bugs, or editor/runtime visual parity.
---

# HiMYM shader and graphics

1. Read `AGENTS.md` and inspect preview and runtime paths.
2. Identify the shader/uniform, vertex layout, cue, asset, and GL-state contract.
3. Apply matching behavior to preview and runtime where both consume it.
4. Preserve opaque-before-transparent ordering, texture alpha, depth-mask rules, VAO rebinding, and blend restoration.
5. Load post-1.1 GL entry points through `wglGetProcAddress`.
6. Declare every WGL-loaded function pointer with `APIENTRY`; x64 tolerates a
   missing annotation while Crinkler x86 can corrupt its stack after repeated
   framebuffer, uniform, VAO, buffer, or post-processing calls.
7. Keep shader dispatch IDs and generated/split shader sources synchronized.
8. Build the affected target and visually verify both normal x64 and optional
   Crinkler x86 artifacts when competition rendering is in scope.

Image-cue source dimensions are authored in 1920x1080 pixels. Apply the shared viewport scale before converting them to normalized sprite size; normalized anchors remain unchanged.

Shadertoy pipeline opacity is applied by the final compositor: use the
evaluated cue opacity times its fade envelope, and alpha-blend a partial bottom
layer over black. Project GLSL is not required to declare an opacity uniform.
For Crinkler-only single-threaded GL loaders, prefer a small guarded direct load
when `std::call_once` introduces Win32 InitOnce/API-set import cycles.

Favor direct, packer-friendly rendering code. Do not add a generic renderer, scene graph, runtime plugin architecture, or heavyweight material system.
