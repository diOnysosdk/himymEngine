# 3D Mesh & glTF Integration Guide

**Complete walkthrough for importing, authoring, and rendering 3D content in HiMYM**

---

## Overview

HiMYM supports full glTF 2.0 mesh import with:
- Multiple mesh nodes and hierarchical transforms
- Per-material texture and color support
- Phong shading with dynamic lighting
- Transparency and alpha blending
- Skeletal animation (basics)
- Material variations per mesh slot

This guide covers authoring meshes, importing via glTF, configuring materials, and runtime integration.

---

## Part 1: Mesh Authoring in Blender

### 1.1 Export Settings

When exporting from Blender to glTF 2.0 (.glb or .gltf):

1. **File → Export → glTF 2.0 (.glb/.gltf)**

2. **Export Settings**:
   - ✅ **Format**: glTF Binary (.glb) recommended for single file
   - ✅ **Include → Geometry**: Meshes, Normals
   - ✅ **Include → Materials**: All materials and textures
   - ✅ **Include → Animations**: If using skeletal animation
   - ✅ **Include → All Bone Influences**: For rigged models
   - ⚠️ **Draco Compression**: Optional (reduces size, adds import time)

3. **Scene Settings**:
   - Active Collection only (if working modularly)
   - Y-up orientation (Blender default)

### 1.2 Material Setup in Blender

For best results in HiMYM, set up materials using **Principled BSDF**:

```
Base Color → RGB or texture
Metallic → 0.0 (for most effects)
Roughness → 0.5-0.8 (higher = duller)
Normal Map → (optional, for detail)
Alpha → For transparency (0.0 = transparent, 1.0 = opaque)
```

**Texture Organization**:
- Diffuse/Color textures: `model_diffuse.png`
- Normal maps: `model_normal.png`
- Roughness: Baked into diffuse or separate
- Emissive: Optional for glow effects

### 1.3 Mesh Organization

Structure your Blender scene hierarchically:

```
Scene
├── Mesh_Body         (main structure)
├── Mesh_Details      (secondary elements)
└── Light_Key         (lighting info, optional)
```

**Why**: HiMYM loads all mesh nodes and combines them per-frame, so organizing by semantic meaning helps with later tuning.

---

## Part 2: Asset Pipeline

### 2.1 Folder Structure

Place 3D assets in `assets/imported/`:

```
assets/imported/
├── mymodel.glb                 # Exported glTF file
├── mymodel/
│   ├── textures/
│   │   ├── color.png
│   │   ├── normal.png
│   │   └── roughness.png
│   └── blenderfiles/
│       └── mymodel.blend       # Source file
```

### 2.2 Convert to Runtime Format

HiMYM uses `.meshbin` format (custom binary) for runtime efficiency.

**Tool**: `tools/obj_to_meshbin.py` or use editor export:

```powershell
# In editor: Assets → Import Mesh
# Select mymodel.glb → "Convert & Import"

# Or command line:
python tools/obj_to_meshbin.py \
    --input assets/imported/mymodel.glb \
    --output assets/meshes/mymodel.meshbin \
    --merge-nodes
```

**Flags**:
- `--merge-nodes`: Combine all mesh nodes into single vertex buffer (fast, no hierarchy)
- `--keep-nodes`: Preserve node structure for per-node transforms (slower, more control)
- `--optimize`: Stripe indices and reorder vertices for cache efficiency

### 2.3 Verify Output

Check the generated `.meshbin`:

```powershell
# Test render in editor
.\build\bin\Release\mesh_demo.exe assets/meshes/mymodel.meshbin
```

Expected: Model renders with default lighting, no textures yet.

---

## Part 3: Material Configuration

### 3.1 Material Slots

Each mesh can have multiple material slots. In glTF, this is per-primitive (mesh sub-section).

**Default Material Slot** (if no texture):
- Diffuse Color: White (1, 1, 1)
- Specular: 0.2
- Shininess: 32

### 3.2 Texture Binding

In editor or via code:

**Editor (ImGui Material Modal)**:
1. Select mesh in timeline
2. Click "Materials..." button
3. For each slot:
   - Choose texture from browser
   - Adjust color multiplier (RGB)
   - Set roughness/metallic if supported

**Runtime Code**:
```cpp
auto mesh = rev::mesh::LoadMesh("assets/meshes/mymodel.meshbin");
rev::mesh::SetMaterialTexture(mesh, slot_index, "assets/textures/color.png");
rev::mesh::SetMaterialColor(mesh, slot_index, {1.0f, 0.8f, 0.6f}); // Tint
```

### 3.3 Transparency Handling

**Per-Slot Alpha**:
- Opaque slots (α=1.0): Rendered first (solid geometry pass)
- Transparent slots (α<1.0): Rendered last (blended)

**In Material Config**:
```json
{
  "slot": 0,
  "color": [1.0, 1.0, 1.0, 1.0],
  "texture": "assets/textures/diffuse.png",
  "normalMap": "assets/textures/normal.png",
  "roughness": 0.5,
  "metallic": 0.0,
  "alpha": 0.8
}
```

---

## Part 4: Lighting Setup

### 4.1 Phong Shading Model

HiMYM uses Phong shading with:
- **Ambient**: Global light (default: gray ~0.3)
- **Diffuse**: Main directional light
- **Specular**: Shininess highlights

### 4.2 Default Lighting

If no custom light specified, HiMYM uses imported light from Blender:

```cpp
// Exported from Blender scene
Light position: {3.0, 5.0, 4.0}
Light intensity: 1.0
Light color: {1.0, 1.0, 1.0}
```

### 4.3 Dynamic Lighting (Runtime)

Control lighting via curves or code:

**Timeline Cue** (scene lighting):
```json
{
  "type": "mesh",
  "meshbin": "assets/meshes/mymodel.meshbin",
  "light": {
    "position": [3.0, 5.0, 4.0],
    "intensity": 1.0,
    "color": [1.0, 0.9, 0.8]
  },
  "rotation": [0.0, 0.5, 0.0]
}
```

**Curve-Driven Lighting** (animating light intensity over time):
```cpp
float intensity_curve = rev::curve::Sample(intensity_anim, time);
rev::mesh::SetLightIntensity(mesh, intensity_curve);
```

### 4.4 Ambient Lighting

Adjust global ambient brightness:

```cpp
glm::vec3 ambient = {0.3f, 0.3f, 0.3f};
rev::mesh::SetAmbientLight(ambient);
```

Higher ambient = brighter shadows, more uniform look.
Lower ambient = more contrast, deeper shadows.

---

## Part 5: Transforms & Animation

### 5.1 Static Positioning

Place mesh in scene with transform:

```cpp
glm::mat4 transform = glm::identity<glm::mat4>();
transform = glm::translate(transform, {0.0f, 0.0f, -5.0f}); // Move back
transform = glm::rotate(transform, glm::radians(45.0f), {0.0f, 1.0f, 0.0f}); // Rotate
transform = glm::scale(transform, {2.0f, 2.0f, 2.0f}); // Scale 2x

rev::mesh::SetTransform(mesh, transform);
rev::mesh::Render(mesh, view_projection);
```

### 5.2 Curve-Driven Animation

Animate mesh using timeline curves:

```cpp
// In update loop
float rotation_y = rev::curve::Sample(rotate_y_curve, current_time);
float scale_factor = rev::curve::Sample(scale_curve, current_time);
glm::vec3 position = rev::curve::SampleVec3(position_curve, current_time);

glm::mat4 transform = glm::identity<glm::mat4>();
transform = glm::translate(transform, position);
transform = glm::rotate(transform, rotation_y, {0.0f, 1.0f, 0.0f});
transform = glm::scale(transform, {scale_factor, scale_factor, scale_factor});

rev::mesh::SetTransform(mesh, transform);
```

### 5.3 Skeletal Animation (Basic)

If mesh has bones (rigged in Blender):

```cpp
auto mesh = rev::mesh::LoadMesh("assets/meshes/rigged_model.meshbin");
auto animation = rev::mesh::LoadAnimation(mesh, 0); // Load first animation

// Play animation
rev::mesh::PlayAnimation(mesh, animation, time_seconds);
rev::mesh::Render(mesh, view_projection);
```

**Current Support**:
- Linear bone interpolation
- No animation blending yet
- Single active animation per mesh

---

## Part 6: Editor Workflow

### 6.1 Import Mesh in Editor

1. **Assets Panel** → "Import Mesh"
2. Browse to `.glb` or `.gltf` file
3. Choose conversion options:
   - Merge nodes (faster) vs Keep nodes (control)
   - Optimize for GPU
4. Select output location (`assets/meshes/`)
5. Click "Import"

### 6.2 Add Mesh to Timeline

1. Create new scene in timeline
2. Click "+ Cue" → "Mesh"
3. Select `.meshbin` from dropdown
4. Configure:
   - Position (X, Y, Z)
   - Rotation (Euler angles)
   - Scale
   - Materials (colors, textures)

### 6.3 Animate with Curves

1. Select mesh cue in timeline
2. Click "Curves..." button
3. Create curves for:
   - `rotation_y` - Spin around Y axis
   - `scale` - Size pulse
   - `light_intensity` - Lighting change
4. Click "Apply & Preview"

### 6.4 Compose Multiple Meshes

Layer meshes in same timeline:

```
Timeline
├── Scene 1 (0s-5s)
│   ├── Mesh: background.meshbin (static)
│   ├── Mesh: character.meshbin (animated)
│   └── Text: "Scene 1 Title"
├── Scene 2 (5s-10s)
│   ├── Mesh: environment.meshbin (rotating)
│   └── Shader: plasma.glsl (overlay)
```

---

## Part 7: Best Practices

### 7.1 Performance Tips

1. **Vertex Count**: Keep under 100k vertices per mesh (desktop target)
2. **Texture Size**: 1024x1024 or 2048x2048 max (memory budget)
3. **Merge Nodes**: Use `--merge-nodes` if you don't need per-node control
4. **LOD (Level of Detail)**: Create simplified versions for distant camera
   - `model_high.meshbin` - Full detail
   - `model_low.meshbin` - Reduced geometry

### 7.2 Quality Tips

1. **Normal Maps**: Use for surface detail without extra geometry
2. **Specular Highlights**: Adjust roughness per material for realism
3. **Ambient Occlusion**: Bake into textures for depth
4. **Consistent Scale**: Use Blender units = World units (1m = 1 unit)

### 7.3 Memory & Size

Track mesh sizes for intro compression:

- Simple mesh (5k verts, 512px texture): ~100 KB
- Complex mesh (100k verts, 2048px texture): ~1-2 MB
- Full scene (5 meshes): ~3-5 MB (uncompressed)

Use kkrunchy or Crinkler for final intro packaging.

### 7.4 Debugging

**Visual Debugging**:
```powershell
# Run mesh_demo standalone
.\build\bin\Release\mesh_demo.exe assets/meshes/mymodel.meshbin

# Renders mesh with default lighting and camera rotation
```

**Log Mesh Info**:
```cpp
auto mesh = rev::mesh::LoadMesh("assets/meshes/mymodel.meshbin");
rev::mesh::LogMeshInfo(mesh);  // Prints vertex/index count, materials
```

---

## Part 8: Common Workflows

### 8.1 Replace Mesh Material

Change color without re-exporting:

```cpp
auto mesh = rev::mesh::LoadMesh("assets/meshes/box.meshbin");
rev::mesh::SetMaterialColor(mesh, 0, {1.0f, 0.0f, 0.0f}); // Red
rev::mesh::Render(mesh, view_projection);
```

### 8.2 Rotate Mesh Over Time

Animate continuous rotation:

```cpp
// In main loop
float time = rev::platform::GetTime();
glm::mat4 rotation = glm::rotate(glm::identity<glm::mat4>(), 
                                 time * glm::radians(45.0f), 
                                 {0.0f, 1.0f, 0.0f});
rev::mesh::SetTransform(mesh, rotation);
rev::mesh::Render(mesh, view_projection);
```

### 8.3 Fade Mesh In/Out

Use opacity curves:

```cpp
float fade = rev::curve::Sample(fade_in_curve, current_time);  // 0.0 to 1.0
glm::vec4 color_with_alpha = {1.0f, 1.0f, 1.0f, fade};
rev::mesh::SetMaterialColor(mesh, 0, color_with_alpha);
```

### 8.4 Layer Transparent Mesh Over Shader

Composite mesh on top of fullscreen effect:

```cpp
// Render shader fullscreen
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
rev::shader::Use(shader);
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

// Then render mesh with blending
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
rev::mesh::Render(mesh, view_projection);
glDisable(GL_BLEND);
```

---

## Part 9: Troubleshooting

### "Mesh is all black"
- Check ambient light: `rev::mesh::SetAmbientLight({0.3f, 0.3f, 0.3f})`
- Verify normals exported from Blender
- Adjust light position closer to mesh

### "Texture not showing"
- Verify `.meshbin` was created with `--keep-textures` flag
- Check texture path is correct in material config
- Test with editor texture browser

### "Mesh has holes or wrong normals"
- Re-export from Blender with "Include → Normals" enabled
- Check for double-sided faces (may need to recalculate normals in Blender)
- Verify mesh is manifold (no floating vertices)

### "Animation plays too fast/slow"
- Check curve timing in timeline (should be 0.0-1.0 over animation duration)
- Verify `time` parameter passed to `PlayAnimation()` is in seconds

### "Memory explosion with large texture"
- Reduce texture size: 4096x4096 → 2048x2048
- Use PNG compression or WebP if supported
- Consider texture atlasing (combine multiple textures)

---

## Part 10: Example: Complete Mesh Scene

**project.json** (editor project):
```json
{
  "scenes": [
    {
      "name": "Rotating Box",
      "duration": 5.0,
      "cues": [
        {
          "type": "mesh",
          "meshbin": "assets/meshes/box.meshbin",
          "position": [0.0, 0.0, -5.0],
          "rotation": [0.0, 0.5, 0.0],
          "scale": [1.0, 1.0, 1.0],
          "materials": [
            {
              "slot": 0,
              "color": [1.0, 0.8, 0.6, 1.0],
              "texture": "assets/textures/wood.png"
            }
          ]
        }
      ]
    }
  ]
}
```

**Runtime Code** (`src/main.cpp`):
```cpp
#include "rev_runtime.h"

int main() {
    auto ctx = rev::CreateContext(1920, 1080, true, "Mesh Demo");
    
    auto mesh = rev::mesh::LoadMesh("assets/meshes/box.meshbin");
    rev::mesh::SetMaterialColor(mesh, 0, {1.0f, 0.8f, 0.6f, 1.0f});
    
    double start_time = rev::platform::GetTime();
    
    while (rev::UpdateContext(ctx)) {
        float time = rev::platform::GetTime() - start_time;
        
        // Rotate
        glm::mat4 rotation = glm::rotate(glm::identity<glm::mat4>(),
                                        time * glm::radians(90.0f),
                                        {0.0f, 1.0f, 0.0f});
        rev::mesh::SetTransform(mesh, rotation);
        
        // Render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        rev::mesh::Render(mesh, ctx->view_projection);
        
        rev::PresentContext(ctx);
        
        if (time > 5.0f) break;
    }
    
    rev::mesh::UnloadMesh(mesh);
    rev::DestroyContext(ctx);
    return 0;
}
```

---

## Resources

- **[glTF 2.0 Specification](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html)** - Official format spec
- **[Blender glTF Export](https://docs.blender.org/manual/en/latest/addons/import_export/scene_gltf2.html)** - Blender documentation
- **[rev_mesh API](../architecture/API_REFERENCE.md#rev_mesh)** - Full C++ reference
- **[rev_gltf API](../architecture/API_REFERENCE.md#rev_gltf)** - glTF parser reference

---

**Last Updated**: June 2026  
**Framework**: HiMYM v6.0  
**Status**: Complete
