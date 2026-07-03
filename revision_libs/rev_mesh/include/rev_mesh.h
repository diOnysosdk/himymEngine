#pragma once

#include <cstdint>

namespace rev {
namespace mesh {

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct MaterialSlot {
    uint32_t start_index;
    uint32_t count;
    uint32_t diffuse_color;  // RGBA packed as uint32
    int      material_index; // Source material index for imported meshes (-1 if unknown)
    uint32_t base_color_texture; // Optional per-slot texture override (0 = none)
    int      source_node_index; // Source glTF node for imported meshes (-1 if unknown)
    float    emissive_color[3];
    float    emissive_strength;
};

struct ImportedNode {
    int   parent_index;
    float base_translation[3];
    float base_rotation[4];
    float base_scale[3];
    float base_world[16];
    float inverse_base_world[16];
};

struct Mesh {
    uint32_t vbo;
    uint32_t ibo;
    uint32_t vao;
    Vertex* vertices;
    uint32_t* indices;
    uint32_t vertex_count;
    uint32_t index_count;
    MaterialSlot* material_slots;
    uint32_t material_slot_count;
    
    // Texture IDs (0 = no texture)
    uint32_t base_color_texture;
    uint32_t normal_texture;
    uint32_t metallic_roughness_texture;
    float    emissive_color[3];
    float    emissive_strength;

    // Imported light placement from glTF (if present)
    bool     has_imported_light;
    float    imported_light_pos[3];
    int      imported_light_node_index;
    // Imported camera placement from glTF (if present)
    bool     has_imported_camera;
    float    imported_camera_pos[3];
    float    imported_camera_target[3];
    float    imported_camera_fov_deg;
    int      imported_camera_node_index;
    ImportedNode* imported_nodes;
    uint32_t      imported_node_count;
    
    // Animation state (runtime only, not set by CreateMesh)
    void*    animation_data;   // Opaque pointer to animation data (cast to rev::gltf::Animation*)
    int      animation_count;
    int      current_animation; // Index of currently playing animation (-1 = none)
    float    animation_time;    // Current playback time in seconds
    float    animation_speed;   // Playback speed multiplier (1.0 = normal)
    bool     animation_loop;    // Loop animation when it reaches the end
};

// Lifecycle
Mesh* CreateMesh(uint32_t vertex_count, uint32_t index_count);
void DestroyMesh(Mesh* mesh);

// Data population (call before UploadToGPU)
void SetVertex(Mesh* mesh, uint32_t index, const Vertex& vertex);
void SetIndex(Mesh* mesh, uint32_t index, uint32_t vertex_index);
void AddMaterialSlot(Mesh* mesh,
                     uint32_t start_index,
                     uint32_t count,
                     uint32_t color,
                     int material_index = -1,
                     uint32_t base_color_texture = 0,
                     int source_node_index = -1,
                     const float* emissive_color = nullptr,
                     float emissive_strength = 1.0f);

// GPU operations
void UploadToGPU(Mesh* mesh);
void Render(Mesh* mesh, int material_slot_index = -1);  // -1 = all slots

// Procedural mesh generation
Mesh* CreateCube(float size);
Mesh* CreateSphere(float radius, int segments);
Mesh* CreatePlane(float width, float height);
Mesh* CreateTorus(float major_radius, float minor_radius, int major_segments, int minor_segments);

}  // namespace mesh
}  // namespace rev
