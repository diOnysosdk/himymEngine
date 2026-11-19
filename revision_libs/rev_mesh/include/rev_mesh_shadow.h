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
bool RenderSpotShadow(ShadowMap* shadow, Mesh* mesh, const float model[16],
                      const float light_position[3], const float light_direction[3],
                      float outer_cone_radians, float light_range);
void BindDirectionalShadow(const ShadowMap* shadow, rev::shader::Program* mesh_program,
                           int texture_unit = 5);

} // namespace mesh
} // namespace rev
