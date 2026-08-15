#pragma once

namespace rev { namespace shader { struct Program; } }

namespace rev {
namespace mesh {

struct Mesh;

struct ShadowMap {
    unsigned int framebuffer;
    unsigned int depth_texture;
    rev::shader::Program* depth_program;
    float light_matrix[16];
    bool ready;
};

bool CreateShadowMap(ShadowMap* shadow);
void DestroyShadowMap(ShadowMap* shadow);
bool RenderDirectionalShadow(ShadowMap* shadow, Mesh* mesh,
                             const float model[16], const float light_direction[3]);
void BindDirectionalShadow(const ShadowMap* shadow, rev::shader::Program* mesh_program,
                           int texture_unit = 3);

} // namespace mesh
} // namespace rev
