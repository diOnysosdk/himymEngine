---
name: Shader Authoring
description: "Use for GLSL shader work, shader-id dispatch, dyn controls, rev_shader lib changes, Phong mesh shader, and shader runtime contract changes."
---
# Shader Authoring

Use this skill for shader and shader-plumbing work.

## Scope
- `revision_libs/rev_shader/` — shader compilation, program management: `CompileFromSource`, `DestroyProgram`, `UseProgram`
- Inline shader source strings in `editor_context.cpp` and `minimal_intro/main.cpp` — Phong vertex/fragment shaders for mesh rendering
- Any GLSL `.vert`/`.frag`/`.glsl` files under the project

## Non-negotiables
- Keep scene-id dispatch stable unless the runtime/editor contract changes with it.
- Keep uniform names consistent: `u_model`, `u_view`, `u_projection`, `u_light_pos`, `u_view_pos`, `u_color`.
- Keep vertex attrib locations: `a_pos` (0), `a_normal` (1), `a_uv` (2) for Phong shader.
- `glUniformMatrix4fv` and other GL 2.0+ functions are NOT in Windows `<gl/gl.h>`; always load via `wglGetProcAddress`.
- When changing Phong shader source in `editor_context.cpp`, apply the same change to the shader source in `minimal_intro/main.cpp`.

## Phong shader contract
The mesh Phong shader (inline source in both `editor_context.cpp` and `minimal_intro/main.cpp`):
```glsl
// Vertex attribs:
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

// Uniforms:
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform vec3 u_light_pos;
uniform vec3 u_view_pos;
uniform vec4 u_color;
```

## rev_shader API
```cpp
// Compile and link a program from vertex + fragment source strings
void* program = rev::shader::CompileFromSource(vert_src, frag_src);

// Use and destroy
rev::shader::UseProgram(program);
rev::shader::DestroyProgram(program);

// Uniforms set via wglGetProcAddress-loaded glUniform* calls
```

## Read-before-edit targets
- `revision_libs/rev_shader/include/rev_shader.h` — CompileFromSource, UseProgram, DestroyProgram
- `revision_libs/rev_editor/src/editor_context.cpp` — mesh_vertex_shader + mesh_fragment_shader source constants
- `examples/minimal_intro/main.cpp` — mesh_vertex_shader_src + mesh_fragment_shader_src

## Pair with
- `Scene Block Editor` when shader uniform bindings or editor preview rendering changes.
- `Revision Runtime Core` when uniform bindings or scene dispatch change.
- `Revision Build Validation` after any shader/runtime edit.
