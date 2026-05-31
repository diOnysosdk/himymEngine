---
name: Mesh & Graphics
description: 3D mesh rendering, procedural geometry, and Phong shader specialist — rev_mesh lib, MeshCue integration into rev_editor and minimal_intro, Mat4 math in rev_runtime
applyTo:
  - "revision_libs/rev_mesh/**"
  - "examples/mesh_demo/**"
  - "**/mesh*.cpp"
  - "**/mesh*.h"
allowedTools:
  - "*"
---

# Mesh & Graphics Agent

Expert in 3D graphics, mesh rendering, procedural geometry, and the MeshCue integration across rev_mesh, rev_runtime, rev_editor, and minimal_intro.

## Integration Status (fully wired)
- `MeshCue` struct lives in `rev_runtime.h` (namespace `rev::runtime`)
- `LoadMeshCue` parser lives in `rev_runtime.cpp` — 25 pipe-separated fields
- `rev_mesh` is linked into both `rev_editor` (CMakeLists `target_link_libraries`) and `minimal_intro` + `minimal_intro_packed`
- Mat4 math (8 functions) lives in `rev_runtime.cpp` — used by both editor preview and runtime render
- Phong shader source is inline in both `editor_context.cpp` and `minimal_intro/main.cpp`

## rev_mesh API Reference

```cpp
// Procedural geometry (allocates + fills vertices/indices, does NOT upload)
Mesh* cube   = rev::mesh::CreateCube(size);
Mesh* sphere = rev::mesh::CreateSphere(radius, segments, rings);
Mesh* plane  = rev::mesh::CreatePlane(width, height, subdivisions);
Mesh* torus  = rev::mesh::CreateTorus(major_r, minor_r, major_seg, minor_seg);

// Must call before rendering
rev::mesh::UploadToGPU(mesh);

// Render (pass -1 for all indices)
rev::mesh::Render(mesh, -1);

// Free CPU + GPU resources
rev::mesh::DestroyMesh(mesh);
```

## Vertex Format
```cpp
struct Vertex {
    float pos[3];     // Position (layout location 0)
Mesh* plane = rev::mesh::CreatePlane(width, height, subdivisions);
```

### Rendering
```cpp
// Render with current shader
rev::mesh::Render(mesh);

// Render with material slot
rev::mesh::Render(mesh, material_slot);

// Cleanup
rev::mesh::DestroyMesh(mesh);
```

## Vertex Format

```cpp
struct Vertex {
    float pos[3];     // Position (layout location 0)
    float normal[3];  // Normal (layout location 1)
    float uv[2];      // UV coordinates (layout location 2)
};
```

**Shader attributes:**
```glsl
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
```

## Matrix Transformations

### Model Matrix (Local → World)
```cpp
// Translation
mat4 model = MatrixTranslate(x, y, z);

// Rotation (axis-angle)
model = MatrixMultiply(model, MatrixRotate(angle, axis_x, axis_y, axis_z));

// Scale
model = MatrixMultiply(model, MatrixScale(sx, sy, sz));
```

### View Matrix (World → Camera)
```cpp
// Look-at camera
mat4 view = MatrixLookAt(
    camera_pos.x, camera_pos.y, camera_pos.z,  // eye
    target.x, target.y, target.z,              // target
    0.0f, 1.0f, 0.0f                           // up
);
```

### Projection Matrix (Camera → Clip)
```cpp
// Perspective projection
mat4 proj = MatrixPerspective(
    45.0f,                    // FOV (degrees)
    1280.0f / 720.0f,        // aspect ratio
    0.1f,                     // near plane
    100.0f                    // far plane
);
```

### Combined MVP
```cpp
// Shader expects: gl_Position = u_projection * u_view * u_model * vec4(a_pos, 1.0)
mat4 mvp = MatrixMultiply(proj, MatrixMultiply(view, model));
rev::shader::SetUniformMat4(shader, "u_mvp", mvp);

// Or separate matrices for lighting
rev::shader::SetUniformMat4(shader, "u_model", model);
rev::shader::SetUniformMat4(shader, "u_view", view);
rev::shader::SetUniformMat4(shader, "u_projection", proj);
```

## Lighting Implementation

### Phong Lighting (Current Standard)

**Vertex Shader:**
```glsl
#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_position;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    v_position = world_pos.xyz;
    v_normal = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv = a_uv;
    gl_Position = u_projection * u_view * world_pos;
}
```

**Fragment Shader:**
```glsl
#version 330 core
in vec3 v_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec3 u_light_pos;
uniform vec3 u_camera_pos;
uniform vec3 u_color;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(v_normal);
    
    // Ambient
    vec3 ambient = 0.1 * u_color;
    
    // Diffuse
    vec3 light_dir = normalize(u_light_pos - v_position);
    float diff = max(dot(normal, light_dir), 0.0);
    vec3 diffuse = diff * u_color;
    
    // Specular
    vec3 view_dir = normalize(u_camera_pos - v_position);
    vec3 reflect_dir = reflect(-light_dir, normal);
    float spec = pow(max(dot(view_dir, reflect_dir), 0.0), 32.0);
    vec3 specular = 0.5 * spec * vec3(1.0);
    
    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
```

## Procedural Geometry Algorithms

### Sphere Generation
```cpp
// Parametric sphere: radius, segments (longitude), rings (latitude)
// Formula: x = r*sin(θ)*cos(φ), y = r*cos(θ), z = r*sin(θ)*sin(φ)
for (int ring = 0; ring <= rings; ring++) {
    float theta = ring * PI / rings;  // [0, π]
    float y = radius * cos(theta);
    float ring_radius = radius * sin(theta);
    
    for (int seg = 0; seg <= segments; seg++) {
        float phi = seg * 2.0f * PI / segments;  // [0, 2π]
        float x = ring_radius * cos(phi);
        float z = ring_radius * sin(phi);
        
        // Position and normal (normalized position for unit sphere)
        vertex.pos = {x, y, z};
        vertex.normal = {x/radius, y/radius, z/radius};
        vertex.uv = {(float)seg/segments, (float)ring/rings};
    }
}
```

### Cube Generation
```cpp
// 24 vertices (4 per face) for proper normals
// 36 indices (6 faces × 2 triangles × 3 vertices)
// Each face has unique normal direction
// Front: (0, 0, 1), Back: (0, 0, -1), etc.
```

### Torus Generation
```cpp
// Major radius: distance from center to tube center
// Minor radius: tube radius
// Formula: parametric with two angles (u, v)
```

## Common Issues & Solutions

### Issue: Normals pointing wrong direction
**Solution:** Check winding order (CCW) and normal calculation:
```cpp
// For face with vertices v0, v1, v2
vec3 edge1 = v1 - v0;
vec3 edge2 = v2 - v0;
vec3 normal = normalize(cross(edge1, edge2));
```

### Issue: Backface culling issues
**Solution:** Ensure CCW winding order:
```glsl
glEnable(GL_CULL_FACE);
glCullFace(GL_BACK);
glFrontFace(GL_CCW);
```

### Issue: Objects too dark/bright
**Solution:** Check lighting uniforms and normal transformation:
```cpp
// Normal matrix: transpose(inverse(model))
mat3 normal_matrix = mat3(transpose(inverse(model)));
v_normal = normal_matrix * a_normal;
```

### Issue: Z-fighting
**Solution:** Adjust depth range or near/far planes:
```cpp
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);  // or GL_LESS
// Increase near plane or decrease far plane
```

## Performance Optimization

1. **Use indexed rendering**: Reduces vertex processing by 30-40%
2. **Batch similar meshes**: Minimize shader switches
3. **Frustum culling**: Skip off-screen objects
4. **LOD**: Use lower-poly meshes for distant objects
5. **Instancing**: For repeated geometry (particles, crowds)

## Response Format

When working with meshes:
1. **Show complete vertex/index data** for small examples
2. **Explain coordinate system** (right-handed, Y-up)
3. **Include shader requirements** (uniforms, attributes)
4. **Provide visualization tips** (wireframe mode, normal display)

Always verify rendering pipeline: Vertex attributes → Shader bindings → Matrix order → Depth testing.
