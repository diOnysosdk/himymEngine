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
};

// Lifecycle
Mesh* CreateMesh(uint32_t vertex_count, uint32_t index_count);
void DestroyMesh(Mesh* mesh);

// Data population (call before UploadToGPU)
void SetVertex(Mesh* mesh, uint32_t index, const Vertex& vertex);
void SetIndex(Mesh* mesh, uint32_t index, uint32_t vertex_index);
void AddMaterialSlot(Mesh* mesh, uint32_t start_index, uint32_t count, uint32_t color);

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
