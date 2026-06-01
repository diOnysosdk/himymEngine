#include "rev_mesh.h"
#include <cstring>
#include <cmath>
#include <vector>

// Forward declare OpenGL functions
typedef unsigned int GLuint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef int GLint;

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_UNSIGNED_INT 0x1405

// Load GL functions
#include <windows.h>
#include <gl/gl.h>  // For glDrawElements from opengl32.dll

extern "C" {
    typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
    typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint array);
    typedef void (*PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
    typedef void (*PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
    typedef void (*PFNGLBUFFERDATAPROC)(GLenum target, ptrdiff_t size, const void* data, GLenum usage);
    typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
    typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, unsigned char normalized, GLsizei stride, const void* pointer);
    typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
    typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
}

// GL function pointers (extensions only - glDrawElements is core 1.1 from opengl32.lib)
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
static PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC glBufferData = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;

// Load GL functions
#include <windows.h>

static void LoadGLFunctions() {
    static bool loaded = false;
    if (loaded) return;
    
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
    
    // Note: glDrawElements is core OpenGL 1.1, loaded from opengl32.lib via <gl/gl.h>
    
    loaded = true;
}

namespace rev {
namespace mesh {

Mesh* CreateMesh(uint32_t vertex_count, uint32_t index_count) {
    Mesh* mesh = new Mesh();
    mesh->vbo = 0;
    mesh->ibo = 0;
    mesh->vao = 0;
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    mesh->vertices = new Vertex[vertex_count];
    mesh->indices = new uint32_t[index_count];
    mesh->material_slots = nullptr;
    mesh->material_slot_count = 0;
    mesh->base_color_texture = 0;
    mesh->normal_texture = 0;
    mesh->metallic_roughness_texture = 0;
    
    memset(mesh->vertices, 0, vertex_count * sizeof(Vertex));
    memset(mesh->indices, 0, index_count * sizeof(uint32_t));
    
    return mesh;
}

void DestroyMesh(Mesh* mesh) {
    if (!mesh) return;
    
    LoadGLFunctions();
    
    if (mesh->vao) {
        glDeleteVertexArrays(1, &mesh->vao);
    }
    if (mesh->vbo) {
        glDeleteBuffers(1, &mesh->vbo);
    }
    if (mesh->ibo) {
        glDeleteBuffers(1, &mesh->ibo);
    }
    
    delete[] mesh->vertices;
    delete[] mesh->indices;
    delete[] mesh->material_slots;
    delete mesh;
}

void SetVertex(Mesh* mesh, uint32_t index, const Vertex& vertex) {
    if (!mesh || index >= mesh->vertex_count) return;
    mesh->vertices[index] = vertex;
}

void SetIndex(Mesh* mesh, uint32_t index, uint32_t vertex_index) {
    if (!mesh || index >= mesh->index_count) return;
    mesh->indices[index] = vertex_index;
}

void AddMaterialSlot(Mesh* mesh, uint32_t start_index, uint32_t count, uint32_t color) {
    if (!mesh) return;
    
    // Reallocate material slots array
    MaterialSlot* new_slots = new MaterialSlot[mesh->material_slot_count + 1];
    if (mesh->material_slots) {
        memcpy(new_slots, mesh->material_slots, mesh->material_slot_count * sizeof(MaterialSlot));
        delete[] mesh->material_slots;
    }
    
    new_slots[mesh->material_slot_count].start_index = start_index;
    new_slots[mesh->material_slot_count].count = count;
    new_slots[mesh->material_slot_count].diffuse_color = color;
    
    mesh->material_slots = new_slots;
    mesh->material_slot_count++;
}

void UploadToGPU(Mesh* mesh) {
    if (!mesh) return;
    
    LoadGLFunctions();
    
    // Generate VAO
    glGenVertexArrays(1, &mesh->vao);
    glBindVertexArray(mesh->vao);
    
    // Generate and upload VBO
    glGenBuffers(1, &mesh->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh->vertex_count * sizeof(Vertex), mesh->vertices, GL_STATIC_DRAW);
    
    // Generate and upload IBO
    glGenBuffers(1, &mesh->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->index_count * sizeof(uint32_t), mesh->indices, GL_STATIC_DRAW);
    
    // Setup vertex attributes
    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    
    // Normal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    
    // UV (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    
    glBindVertexArray(0);
}

void Render(Mesh* mesh, int material_slot_index) {
    if (!mesh || !mesh->vao) return;
    
    LoadGLFunctions();
    
    glBindVertexArray(mesh->vao);
    
    if (material_slot_index < 0) {
        // Render all material slots
        glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, nullptr);
    } else if ((uint32_t)material_slot_index < mesh->material_slot_count) {
        // Render specific material slot
        MaterialSlot& slot = mesh->material_slots[material_slot_index];
        glDrawElements(GL_TRIANGLES, slot.count, GL_UNSIGNED_INT, (void*)(slot.start_index * sizeof(uint32_t)));
    }
    
    glBindVertexArray(0);
}

// Procedural mesh generation
Mesh* CreateCube(float size) {
    float s = size * 0.5f;
    
    Mesh* mesh = CreateMesh(24, 36);  // 6 faces, 4 vertices each, 2 triangles per face
    
    // Front face
    SetVertex(mesh, 0, {{-s, -s, s}, {0, 0, 1}, {0, 0}});
    SetVertex(mesh, 1, {{s, -s, s}, {0, 0, 1}, {1, 0}});
    SetVertex(mesh, 2, {{s, s, s}, {0, 0, 1}, {1, 1}});
    SetVertex(mesh, 3, {{-s, s, s}, {0, 0, 1}, {0, 1}});
    
    // Back face
    SetVertex(mesh, 4, {{s, -s, -s}, {0, 0, -1}, {0, 0}});
    SetVertex(mesh, 5, {{-s, -s, -s}, {0, 0, -1}, {1, 0}});
    SetVertex(mesh, 6, {{-s, s, -s}, {0, 0, -1}, {1, 1}});
    SetVertex(mesh, 7, {{s, s, -s}, {0, 0, -1}, {0, 1}});
    
    // Top face
    SetVertex(mesh, 8, {{-s, s, s}, {0, 1, 0}, {0, 0}});
    SetVertex(mesh, 9, {{s, s, s}, {0, 1, 0}, {1, 0}});
    SetVertex(mesh, 10, {{s, s, -s}, {0, 1, 0}, {1, 1}});
    SetVertex(mesh, 11, {{-s, s, -s}, {0, 1, 0}, {0, 1}});
    
    // Bottom face
    SetVertex(mesh, 12, {{-s, -s, -s}, {0, -1, 0}, {0, 0}});
    SetVertex(mesh, 13, {{s, -s, -s}, {0, -1, 0}, {1, 0}});
    SetVertex(mesh, 14, {{s, -s, s}, {0, -1, 0}, {1, 1}});
    SetVertex(mesh, 15, {{-s, -s, s}, {0, -1, 0}, {0, 1}});
    
    // Right face
    SetVertex(mesh, 16, {{s, -s, s}, {1, 0, 0}, {0, 0}});
    SetVertex(mesh, 17, {{s, -s, -s}, {1, 0, 0}, {1, 0}});
    SetVertex(mesh, 18, {{s, s, -s}, {1, 0, 0}, {1, 1}});
    SetVertex(mesh, 19, {{s, s, s}, {1, 0, 0}, {0, 1}});
    
    // Left face
    SetVertex(mesh, 20, {{-s, -s, -s}, {-1, 0, 0}, {0, 0}});
    SetVertex(mesh, 21, {{-s, -s, s}, {-1, 0, 0}, {1, 0}});
    SetVertex(mesh, 22, {{-s, s, s}, {-1, 0, 0}, {1, 1}});
    SetVertex(mesh, 23, {{-s, s, -s}, {-1, 0, 0}, {0, 1}});
    
    // Indices (2 triangles per face)
    uint32_t indices[] = {
        0,1,2, 0,2,3,      // Front
        4,5,6, 4,6,7,      // Back
        8,9,10, 8,10,11,   // Top
        12,13,14, 12,14,15, // Bottom
        16,17,18, 16,18,19, // Right
        20,21,22, 20,22,23  // Left
    };
    
    for (int i = 0; i < 36; ++i) {
        SetIndex(mesh, i, indices[i]);
    }
    
    AddMaterialSlot(mesh, 0, 36, 0xFFFFFFFF);  // White
    
    return mesh;
}

Mesh* CreateSphere(float radius, int segments) {
    int rings = segments / 2;
    int vertex_count = (rings + 1) * (segments + 1);
    int index_count = rings * segments * 6;
    
    Mesh* mesh = CreateMesh(vertex_count, index_count);
    
    // Generate vertices
    int v_idx = 0;
    for (int r = 0; r <= rings; ++r) {
        float phi = (float)r / rings * 3.14159265359f;
        for (int s = 0; s <= segments; ++s) {
            float theta = (float)s / segments * 2.0f * 3.14159265359f;
            
            float x = radius * sinf(phi) * cosf(theta);
            float y = radius * cosf(phi);
            float z = radius * sinf(phi) * sinf(theta);
            
            float nx = sinf(phi) * cosf(theta);
            float ny = cosf(phi);
            float nz = sinf(phi) * sinf(theta);
            
            float u = (float)s / segments;
            float v = (float)r / rings;
            
            SetVertex(mesh, v_idx++, {{x, y, z}, {nx, ny, nz}, {u, v}});
        }
    }
    
    // Generate indices
    int i_idx = 0;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            int current = r * (segments + 1) + s;
            int next = current + segments + 1;
            
            SetIndex(mesh, i_idx++, current);
            SetIndex(mesh, i_idx++, next);
            SetIndex(mesh, i_idx++, current + 1);
            
            SetIndex(mesh, i_idx++, current + 1);
            SetIndex(mesh, i_idx++, next);
            SetIndex(mesh, i_idx++, next + 1);
        }
    }
    
    AddMaterialSlot(mesh, 0, index_count, 0xFFFFFFFF);
    
    return mesh;
}

Mesh* CreatePlane(float width, float height) {
    Mesh* mesh = CreateMesh(4, 6);
    
    float w = width * 0.5f;
    float h = height * 0.5f;
    
    SetVertex(mesh, 0, {{-w, 0, -h}, {0, 1, 0}, {0, 0}});
    SetVertex(mesh, 1, {{w, 0, -h}, {0, 1, 0}, {1, 0}});
    SetVertex(mesh, 2, {{w, 0, h}, {0, 1, 0}, {1, 1}});
    SetVertex(mesh, 3, {{-w, 0, h}, {0, 1, 0}, {0, 1}});
    
    SetIndex(mesh, 0, 0);
    SetIndex(mesh, 1, 1);
    SetIndex(mesh, 2, 2);
    SetIndex(mesh, 3, 0);
    SetIndex(mesh, 4, 2);
    SetIndex(mesh, 5, 3);
    
    AddMaterialSlot(mesh, 0, 6, 0xFFFFFFFF);
    
    return mesh;
}

Mesh* CreateTorus(float major_radius, float minor_radius, int major_segments, int minor_segments) {
    int vertex_count = (major_segments + 1) * (minor_segments + 1);
    int index_count = major_segments * minor_segments * 6;
    
    Mesh* mesh = CreateMesh(vertex_count, index_count);
    
    // Generate vertices
    int v_idx = 0;
    for (int i = 0; i <= major_segments; ++i) {
        float theta = (float)i / major_segments * 2.0f * 3.14159265359f;
        float cos_theta = cosf(theta);
        float sin_theta = sinf(theta);
        
        for (int j = 0; j <= minor_segments; ++j) {
            float phi = (float)j / minor_segments * 2.0f * 3.14159265359f;
            float cos_phi = cosf(phi);
            float sin_phi = sinf(phi);
            
            float x = (major_radius + minor_radius * cos_phi) * cos_theta;
            float y = minor_radius * sin_phi;
            float z = (major_radius + minor_radius * cos_phi) * sin_theta;
            
            float nx = cos_phi * cos_theta;
            float ny = sin_phi;
            float nz = cos_phi * sin_theta;
            
            float u = (float)i / major_segments;
            float v = (float)j / minor_segments;
            
            SetVertex(mesh, v_idx++, {{x, y, z}, {nx, ny, nz}, {u, v}});
        }
    }
    
    // Generate indices
    int i_idx = 0;
    for (int i = 0; i < major_segments; ++i) {
        for (int j = 0; j < minor_segments; ++j) {
            int current = i * (minor_segments + 1) + j;
            int next = current + minor_segments + 1;
            
            SetIndex(mesh, i_idx++, current);
            SetIndex(mesh, i_idx++, next);
            SetIndex(mesh, i_idx++, current + 1);
            
            SetIndex(mesh, i_idx++, current + 1);
            SetIndex(mesh, i_idx++, next);
            SetIndex(mesh, i_idx++, next + 1);
        }
    }
    
    AddMaterialSlot(mesh, 0, index_count, 0xFFFFFFFF);
    
    return mesh;
}

}  // namespace mesh
}  // namespace rev
