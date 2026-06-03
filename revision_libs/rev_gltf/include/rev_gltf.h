#pragma once
// rev_gltf — glTF 2.0 mesh importer for the HiMYM framework
// Editor-side only: not linked into the final packed intro.
// Wraps cgltf (single-header C99 parser) and outputs rev::mesh::Mesh* objects
// with PBR material data extracted from the glTF document.
//
// Supported features:
//   * .gltf (ASCII JSON) and .glb (binary) files
//   * All mesh nodes in the active scene merged into one Mesh
//   * PBR metallic-roughness material (base color factor, metallic, roughness)
//   * Base color texture, normal texture, metallic-roughness texture extraction
//   * Embedded (base64) and external texture images written to output directory
//   * Multi-material meshes via MaterialSlot index ranges
//   * First imported light position and first imported camera (if present)

#include "rev_mesh.h"
#include <stddef.h>

namespace rev {
namespace gltf {

// ---------------------------------------------------------------------------
// Animation data structures
// ---------------------------------------------------------------------------

enum AnimationPathType {
    ANIM_PATH_TRANSLATION = 0,
    ANIM_PATH_ROTATION    = 1,
    ANIM_PATH_SCALE       = 2
};

enum AnimationInterpolation {
    ANIM_INTERP_LINEAR = 0,
    ANIM_INTERP_STEP   = 1,
    ANIM_INTERP_CUBIC  = 2
};

// A single animation channel animates one property (translation/rotation/scale)
struct AnimationChannel {
    AnimationPathType     path;           // Which property to animate
    AnimationInterpolation interpolation; // How to interpolate between keyframes
    int                   target_node_index; // Source glTF node animated by this channel
    
    float* times;           // Keyframe times (seconds)
    float* values;          // Keyframe values (vec3 for trans/scale, vec4 quaternion for rotation)
    int    keyframe_count;  // Number of keyframes
    int    components;      // 3 for translation/scale, 4 for rotation
};

// An animation is a named collection of channels
struct Animation {
    char               name[128];
    AnimationChannel*  channels;
    int                channel_count;
    float              duration;  // Total animation duration in seconds
};

// ---------------------------------------------------------------------------
// Material data returned from an import
// ---------------------------------------------------------------------------
struct Material {
    char  name[128];

    // PBR factors
    float base_color[4];    // RGBA base color factor (default 1,1,1,1)
    float metallic;         // metallic factor  [0..1]
    float roughness;        // roughness factor [0..1]
    float emissive[3];      // emissive factor  (default 0,0,0)
    bool  double_sided;

    // Texture paths — workspace-relative if textures were extracted, else empty.
    // Check if [0] != '\0' before using.
    char  base_color_texture[512];
    char  normal_texture[512];
    char  metallic_roughness_texture[512];
    char  emissive_texture[512];
};

// ---------------------------------------------------------------------------
// Result of a single import call
// ---------------------------------------------------------------------------
struct ImportResult {
    rev::mesh::Mesh* mesh;          // Merged geometry (all mesh nodes/primitives)
    Material         material;      // Primary material (backward compatibility)
    Material*        materials;     // All materials from the source glTF (nullable)
    int              material_count;
    Animation*       animations;    // Array of animations (nullptr if none)
    int              animation_count;
    bool             has_light;
    float            light_pos[3];
    int              light_node_index;
    bool             has_camera;
    float            camera_pos[3];
    float            camera_target[3];
    float            camera_fov_deg;
    int              camera_node_index;
    bool             ok;
    char             error[256];
};

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Load a .gltf or .glb file.  All mesh primitives in scene mesh nodes are
// merged into a single rev::mesh::Mesh.  Call rev::mesh::UploadToGPU() on the
// result before rendering.
//
// texture_output_dir: if non-null and non-empty, any embedded or externally-
//   referenced texture images are extracted/copied to this directory, and the
//   Material texture paths are set to "<texture_output_dir>/<filename>".
//   Pass nullptr to skip texture extraction.
ImportResult* LoadMesh(const char* gltf_path,
                       const char* texture_output_dir = nullptr);

// Load a .glb file from a memory buffer (packed/embedded builds).
// The buffer must contain a complete, self-contained GLB file.
// If texture_output_dir is provided, embedded textures are extracted there.
ImportResult* LoadMeshFromMemory(const void* data, size_t size,
                                 const char* texture_output_dir = nullptr);

// Free an ImportResult (and the mesh it owns).
void FreeImportResult(ImportResult* result);

// Evaluate an animation at the given time and output the transform.
// time: animation time in seconds (wraps around animation duration for looping)
// out_translation: output 3-component translation vector (can be nullptr)
// out_rotation: output 4-component quaternion (xyzw) (can be nullptr)
// out_scale: output 3-component scale vector (can be nullptr)
void EvaluateAnimation(const Animation* anim, float time,
                       float* out_translation,
                       float* out_rotation,
                       float* out_scale);

// Evaluate only the channels targeting one imported glTF node.
void EvaluateAnimationNodeLocalTransform(const Animation* anim,
                                         int target_node_index,
                                         float time,
                                         float* out_translation,
                                         float* out_rotation,
                                         float* out_scale);

// Convert a quaternion (xyzw) to Euler angles (xyz) in degrees
void QuaternionToEuler(const float* quat, float* euler_degrees);

// Update animation playback time for a mesh (convenience function)
// dt: delta time in seconds
// Returns true if animation is playing, false if no animation or ended (non-looping)
bool UpdateMeshAnimation(rev::mesh::Mesh* mesh, float dt);

// Apply animation transform to mesh cue position/rotation/scale
// rotation_quat: 4-component quaternion (xyzw) from EvaluateAnimation
void ApplyAnimationTransform(float* pos, float* rot_degrees, float* scale,
                             const float* translation, const float* rotation_quat, const float* anim_scale);

// Build per-node delta matrices (current_world * inverse(base_world)) for imported meshes.
// out_matrices must have room for mesh->imported_node_count * 16 floats.
bool BuildAnimatedNodeDeltaMatrices(const rev::mesh::Mesh* mesh,
                                    const Animation* anim,
                                    float time,
                                    float* out_matrices,
                                    int max_nodes);

// Build per-node delta matrices by evaluating all animation clips.
// Useful when imported files store independent object motion in separate clips.
bool BuildAnimatedNodeDeltaMatricesAll(const rev::mesh::Mesh* mesh,
                                       const Animation* anims,
                                       int animation_count,
                                       float time,
                                       bool loop,
                                       float* out_matrices,
                                       int max_nodes);

// Extract all textures from a glTF file to output_dir without loading geometry.
// Returns the number of textures successfully extracted.
int ExtractTextures(const char* gltf_path,
                    const char* output_dir,
                    char        extracted_paths[][512],
                    int         max_paths);

} // namespace gltf
} // namespace rev
