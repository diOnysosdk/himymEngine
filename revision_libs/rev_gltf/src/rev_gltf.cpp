// cgltf implementation — compiled exactly once here
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "rev_gltf.h"
#include "rev_mesh.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstddef>

// Windows only (matches the rest of the framework)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rev {
namespace gltf {

static bool IsVerboseGltfLoggingEnabled() {
    char env[16] = {};
    DWORD n = GetEnvironmentVariableA("HIMYM_VERBOSE", env, sizeof(env));
    return (n > 0 && env[0] != '0');
}

#define GLTF_LOGV(...) do { if (IsVerboseGltfLoggingEnabled()) printf(__VA_ARGS__); } while (0)

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void SetMaterialDefaults(Material* m) {
    memset(m, 0, sizeof(*m));
    m->base_color[0] = m->base_color[1] = m->base_color[2] = m->base_color[3] = 1.0f;
    m->metallic  = 0.0f;
    m->roughness = 1.0f;
    m->emissive_strength = 1.0f;
}

// Write raw bytes to a file.  Returns true on success.
static bool WriteFile(const char* path, const void* data, size_t size) {
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;
    bool ok = (fwrite(data, 1, size, f) == size);
    fclose(f);
    return ok;
}

// Copy a file from src to dst.  Returns true on success.
static bool CopyFileToDest(const char* src, const char* dst) {
    FILE* fin = nullptr;
    fopen_s(&fin, src, "rb");
    if (!fin) return false;

    fseek(fin, 0, SEEK_END);
    long sz = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    if (sz <= 0) { fclose(fin); return false; }

    void* buf = malloc((size_t)sz);
    if (!buf) { fclose(fin); return false; }
    fread(buf, 1, (size_t)sz, fin);
    fclose(fin);

    bool ok = WriteFile(dst, buf, (size_t)sz);
    free(buf);
    return ok;
}

// Build output path: output_dir/basename
static void BuildOutputPath(char* out, size_t out_size,
                             const char* output_dir, const char* filename) {
    // Strip path from filename to get basename
    const char* base = filename;
    for (const char* p = filename; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(out, out_size, "%s\\%s", output_dir, base);
    // Normalise to backslash on Windows
    for (char* p = out; *p; ++p) if (*p == '/') *p = '\\';
}

// Resolve an external URI relative to the glTF directory
static void ResolveURI(char* out, size_t out_size,
                        const char* gltf_path, const char* uri) {
    // If uri is absolute or a data URI, just use it directly
    if (uri[0] == '/' || uri[0] == '\\' || strstr(uri, "://") || strncmp(uri, "data:", 5) == 0) {
        strncpy_s(out, out_size, uri, _TRUNCATE);
        return;
    }
    // Strip filename from gltf_path to get directory
    char dir[512] = {};
    strncpy_s(dir, gltf_path, _TRUNCATE);
    char* sep = nullptr;
    for (char* p = dir; *p; ++p) if (*p == '/' || *p == '\\') sep = p;
    if (sep) *(sep + 1) = '\0'; else dir[0] = '\0';
    snprintf(out, out_size, "%s%s", dir, uri);
}

// Determine a safe output filename for a cgltf_image
static void ImageOutputName(char* out, size_t out_size,
                              const cgltf_image* img, int index) {
    if (img->uri && img->uri[0] && strncmp(img->uri, "data:", 5) != 0) {
        // Use the basename from the URI
        const char* base = img->uri;
        for (const char* p = img->uri; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
        strncpy_s(out, out_size, base, _TRUNCATE);
    } else if (img->name && img->name[0]) {
        const char* ext = ".png";
        if (img->mime_type) {
            if (strstr(img->mime_type, "jpeg") || strstr(img->mime_type, "jpg")) ext = ".jpg";
            else if (strstr(img->mime_type, "webp")) ext = ".webp";
        }
        snprintf(out, out_size, "%s%s", img->name, ext);
    } else {
        const char* ext = ".png";
        if (img->mime_type) {
            if (strstr(img->mime_type, "jpeg") || strstr(img->mime_type, "jpg")) ext = ".jpg";
        }
        snprintf(out, out_size, "texture_%d%s", index, ext);
    }
}

// Extract one image to output_dir.  Returns true and fills dest_path on success.
static bool ExtractImage(const cgltf_data* data,
                          const cgltf_image* img, int index,
                          const char* gltf_path,
                          const char* output_dir,
                          char* dest_path, size_t dest_path_size) {
    char filename[256] = {};
    ImageOutputName(filename, sizeof(filename), img, index);
    BuildOutputPath(dest_path, dest_path_size, output_dir, filename);

    GLTF_LOGV("[glTF] ExtractImage: output_dir='%s', filename='%s', dest_path='%s'\n",
           output_dir ? output_dir : "(null)", filename, dest_path);

    // Create output directory if needed
    CreateDirectoryA(output_dir, nullptr);

    if (img->buffer_view && img->buffer_view->buffer) {
        // Embedded binary image (in .glb buffer or base64 buffer view)
        GLTF_LOGV("[glTF] Extracting embedded image from buffer_view\n");
        const cgltf_buffer_view* bv = img->buffer_view;
        const unsigned char* src = (const unsigned char*)bv->buffer->data + bv->offset;
        bool ok = WriteFile(dest_path, src, bv->size);
        GLTF_LOGV("[glTF] WriteFile result: %s\n", ok ? "SUCCESS" : "FAILED");
        return ok;
    } else if (img->uri && strncmp(img->uri, "data:", 5) == 0) {
        // Data URI — base64 encoded.  cgltf already decoded the buffer if we
        // called cgltf_load_buffers, but the image itself lives in a buffer_view.
        // If buffer_view is null here, the image data was not loaded — skip.
        GLTF_LOGV("[glTF] Image is data URI but no buffer_view - skipping\n");
        return false;
    } else if (img->uri && img->uri[0]) {
        // External file reference
        GLTF_LOGV("[glTF] Copying external image file: %s\n", img->uri);
        char src_path[512] = {};
        ResolveURI(src_path, sizeof(src_path), gltf_path, img->uri);
        return CopyFileToDest(src_path, dest_path);
    }
    return false;
}

// Fill Material from a cgltf_material
static void FillMaterial(Material* out, const cgltf_material* mat,
                          const cgltf_data* data,
                          const char* gltf_path, const char* output_dir) {
    SetMaterialDefaults(out);
    if (!mat) {
        GLTF_LOGV("[glTF] No material in glTF file\n");
        return;
    }

    if (mat->name) strncpy_s(out->name, mat->name, _TRUNCATE);
    out->double_sided = mat->double_sided != 0;
    
    GLTF_LOGV("[glTF] Material: name='%s' has_pbr=%d\n", 
           mat->name ? mat->name : "(unnamed)", 
           mat->has_pbr_metallic_roughness);

    if (mat->has_pbr_metallic_roughness) {
        const auto& pbr = mat->pbr_metallic_roughness;
        for (int i = 0; i < 4; ++i) out->base_color[i] = pbr.base_color_factor[i];
        out->metallic  = pbr.metallic_factor;
        out->roughness = pbr.roughness_factor;
        
        GLTF_LOGV("[glTF] PBR: base_color_texture.texture=%p\n", (void*)pbr.base_color_texture.texture);
        if (pbr.base_color_texture.texture) {
            GLTF_LOGV("[glTF]      texture->image=%p\n", (void*)pbr.base_color_texture.texture->image);
        }
        GLTF_LOGV("[glTF] Total images in glTF: %zu\n", data->images_count);

        if (output_dir && output_dir[0]) {
            // Base color texture
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                GLTF_LOGV("[glTF] Found base color texture in material\n");
                int img_idx = (int)(pbr.base_color_texture.texture->image - data->images);
                bool extracted = ExtractImage(data, pbr.base_color_texture.texture->image, img_idx,
                             gltf_path, output_dir,
                             out->base_color_texture, sizeof(out->base_color_texture));
                if (extracted) {
                    GLTF_LOGV("[glTF] Extracted base color texture to: %s\n", out->base_color_texture);
                } else {
                    GLTF_LOGV("[glTF] Failed to extract base color texture\n");
                }
            } else {
                GLTF_LOGV("[glTF] No base color texture found in material (texture ptr is NULL)\n");
            }
            // Metallic-roughness texture
            if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image) {
                int img_idx = (int)(pbr.metallic_roughness_texture.texture->image - data->images);
                ExtractImage(data, pbr.metallic_roughness_texture.texture->image, img_idx,
                             gltf_path, output_dir,
                             out->metallic_roughness_texture, sizeof(out->metallic_roughness_texture));
            }
        }
    }

    // Emissive
    for (int i = 0; i < 3; ++i) out->emissive[i] = mat->emissive_factor[i];
    out->emissive_strength = mat->has_emissive_strength ?
        mat->emissive_strength.emissive_strength : 1.0f;

    // Normal texture
    if (output_dir && output_dir[0]) {
        if (mat->normal_texture.texture && mat->normal_texture.texture->image) {
            int img_idx = (int)(mat->normal_texture.texture->image - data->images);
            ExtractImage(data, mat->normal_texture.texture->image, img_idx,
                         gltf_path, output_dir,
                         out->normal_texture, sizeof(out->normal_texture));
        }
        // Emissive texture
        if (mat->emissive_texture.texture && mat->emissive_texture.texture->image) {
            int img_idx = (int)(mat->emissive_texture.texture->image - data->images);
            ExtractImage(data, mat->emissive_texture.texture->image, img_idx,
                         gltf_path, output_dir,
                         out->emissive_texture, sizeof(out->emissive_texture));
        }
    }
}

// Read a float3 attribute from a glTF accessor into dest[3]
static void ReadVec3(const cgltf_accessor* acc, cgltf_size elem, float dest[3]) {
    cgltf_accessor_read_float(acc, elem, dest, 3);
}

// Read a float2 attribute from a glTF accessor into dest[2]
static void ReadVec2(const cgltf_accessor* acc, cgltf_size elem, float dest[2]) {
    cgltf_accessor_read_float(acc, elem, dest, 2);
}

static void Mat4IdentityLocal(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void Mat4Copy(float* dst, const float* src) {
    memcpy(dst, src, sizeof(float) * 16);
}

static void Mat4MultiplyLocal(float* out, const float* a, const float* b) {
    float tmp[16] = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            tmp[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, tmp, sizeof(tmp));
}

static void BuildNodeWorldRecursive(const rev::mesh::Mesh* mesh,
                                    const float* local_matrices,
                                    int node_index,
                                    unsigned char* computed,
                                    float* world_matrices) {
    if (!mesh || !mesh->imported_nodes || node_index < 0 || node_index >= (int)mesh->imported_node_count) return;
    if (computed[node_index]) return;

    const rev::mesh::ImportedNode& node = mesh->imported_nodes[node_index];
    if (node.parent_index >= 0 && node.parent_index < (int)mesh->imported_node_count) {
        BuildNodeWorldRecursive(mesh, local_matrices, node.parent_index, computed, world_matrices);
        Mat4MultiplyLocal(&world_matrices[node_index * 16],
                          &world_matrices[node.parent_index * 16],
                          &local_matrices[node_index * 16]);
    } else {
        Mat4Copy(&world_matrices[node_index * 16], &local_matrices[node_index * 16]);
    }
    computed[node_index] = 1;
}

static void ComposeMatrixFromTRS(float* out,
                                 const float translation[3],
                                 const float rotation[4],
                                 const float scale[3]) {
    float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;

    Mat4IdentityLocal(out);
    out[0] = (1.0f - 2.0f * (yy + zz)) * scale[0];
    out[1] = (2.0f * (xy + wz)) * scale[0];
    out[2] = (2.0f * (xz - wy)) * scale[0];

    out[4] = (2.0f * (xy - wz)) * scale[1];
    out[5] = (1.0f - 2.0f * (xx + zz)) * scale[1];
    out[6] = (2.0f * (yz + wx)) * scale[1];

    out[8] = (2.0f * (xz + wy)) * scale[2];
    out[9] = (2.0f * (yz - wx)) * scale[2];
    out[10] = (1.0f - 2.0f * (xx + yy)) * scale[2];

    out[12] = translation[0];
    out[13] = translation[1];
    out[14] = translation[2];
}

static bool InvertMatrix4x4(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5]  * m[10] * m[15] -
             m[5]  * m[11] * m[14] -
             m[9]  * m[6]  * m[15] +
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] -
             m[13] * m[7]  * m[10];
    inv[4] = -m[4]  * m[10] * m[15] +
              m[4]  * m[11] * m[14] +
              m[8]  * m[6]  * m[15] -
              m[8]  * m[7]  * m[14] -
              m[12] * m[6]  * m[11] +
              m[12] * m[7]  * m[10];
    inv[8] = m[4]  * m[9] * m[15] -
             m[4]  * m[11] * m[13] -
             m[8]  * m[5] * m[15] +
             m[8]  * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];
    inv[12] = -m[4]  * m[9] * m[14] +
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] -
               m[8]  * m[6] * m[13] -
               m[12] * m[5] * m[10] +
               m[12] * m[6] * m[9];
    inv[1] = -m[1]  * m[10] * m[15] +
              m[1]  * m[11] * m[14] +
              m[9]  * m[2] * m[15] -
              m[9]  * m[3] * m[14] -
              m[13] * m[2] * m[11] +
              m[13] * m[3] * m[10];
    inv[5] = m[0]  * m[10] * m[15] -
             m[0]  * m[11] * m[14] -
             m[8]  * m[2] * m[15] +
             m[8]  * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];
    inv[9] = -m[0]  * m[9] * m[15] +
              m[0]  * m[11] * m[13] +
              m[8]  * m[1] * m[15] -
              m[8]  * m[3] * m[13] -
              m[12] * m[1] * m[11] +
              m[12] * m[3] * m[9];
    inv[13] = m[0]  * m[9] * m[14] -
              m[0]  * m[10] * m[13] -
              m[8]  * m[1] * m[14] +
              m[8]  * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];
    inv[2] = m[1]  * m[6] * m[15] -
             m[1]  * m[7] * m[14] -
             m[5]  * m[2] * m[15] +
             m[5]  * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];
    inv[6] = -m[0]  * m[6] * m[15] +
              m[0]  * m[7] * m[14] +
              m[4]  * m[2] * m[15] -
              m[4]  * m[3] * m[14] -
              m[12] * m[2] * m[7] +
              m[12] * m[3] * m[6];
    inv[10] = m[0]  * m[5] * m[15] -
              m[0]  * m[7] * m[13] -
              m[4]  * m[1] * m[15] +
              m[4]  * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];
    inv[14] = -m[0]  * m[5] * m[14] +
               m[0]  * m[6] * m[13] +
               m[4]  * m[1] * m[14] -
               m[4]  * m[2] * m[13] -
               m[12] * m[1] * m[6] +
               m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] +
              m[1] * m[7] * m[10] +
              m[5] * m[2] * m[11] -
              m[5] * m[3] * m[10] -
              m[9] * m[2] * m[7] +
              m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] +
               m[0] * m[7] * m[9] +
               m[4] * m[1] * m[11] -
               m[4] * m[3] * m[9] -
               m[8] * m[1] * m[7] +
               m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 0.000001f) {
        Mat4IdentityLocal(out);
        return false;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
    return true;
}

// ---------------------------------------------------------------------------
// Animation extraction and evaluation
// ---------------------------------------------------------------------------

// Extract all animations from cgltf_data into our Animation structures
static Animation* ExtractAnimations(const cgltf_data* data, int* out_count) {
    *out_count = 0;
    if (!data || data->animations_count == 0) return nullptr;

    Animation* anims = new Animation[data->animations_count];
    *out_count = (int)data->animations_count;

    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation* src = &data->animations[ai];
        Animation* dst = &anims[ai];
        
        memset(dst, 0, sizeof(Animation));
        if (src->name) strncpy_s(dst->name, src->name, _TRUNCATE);
        
        // Count channels targeting position/rotation/scale (ignore morph weights)
        int valid_channels = 0;
        for (cgltf_size ci = 0; ci < src->channels_count; ++ci) {
            cgltf_animation_path_type path = src->channels[ci].target_path;
            if (path == cgltf_animation_path_type_translation ||
                path == cgltf_animation_path_type_rotation ||
                path == cgltf_animation_path_type_scale) {
                ++valid_channels;
            }
        }
        
        if (valid_channels == 0) continue;
        
        dst->channels = new AnimationChannel[valid_channels];
        dst->channel_count = valid_channels;
        dst->duration = 0.0f;
        
        int channel_idx = 0;
        for (cgltf_size ci = 0; ci < src->channels_count; ++ci) {
            const cgltf_animation_channel* src_chan = &src->channels[ci];
            cgltf_animation_path_type path = src_chan->target_path;
            
            // Skip unsupported paths
            if (path != cgltf_animation_path_type_translation &&
                path != cgltf_animation_path_type_rotation &&
                path != cgltf_animation_path_type_scale) {
                continue;
            }
            
            AnimationChannel* dst_chan = &dst->channels[channel_idx++];
            memset(dst_chan, 0, sizeof(AnimationChannel));
            dst_chan->target_node_index = src_chan->target_node ? (int)cgltf_node_index(data, src_chan->target_node) : -1;
            
            // Map path type
            if (path == cgltf_animation_path_type_translation) {
                dst_chan->path = ANIM_PATH_TRANSLATION;
                dst_chan->components = 3;
            } else if (path == cgltf_animation_path_type_rotation) {
                dst_chan->path = ANIM_PATH_ROTATION;
                dst_chan->components = 4;  // quaternion
            } else if (path == cgltf_animation_path_type_scale) {
                dst_chan->path = ANIM_PATH_SCALE;
                dst_chan->components = 3;
            }
            
            // Map interpolation
            cgltf_animation_sampler* sampler = src_chan->sampler;
            if (!sampler) continue;
            
            switch (sampler->interpolation) {
                case cgltf_interpolation_type_linear: 
                    dst_chan->interpolation = ANIM_INTERP_LINEAR; 
                    break;
                case cgltf_interpolation_type_step: 
                    dst_chan->interpolation = ANIM_INTERP_STEP; 
                    break;
                case cgltf_interpolation_type_cubic_spline: 
                    dst_chan->interpolation = ANIM_INTERP_CUBIC; 
                    break;
                default: 
                    dst_chan->interpolation = ANIM_INTERP_LINEAR; 
                    break;
            }
            
            // Extract keyframe times
            const cgltf_accessor* time_acc = sampler->input;
            const cgltf_accessor* value_acc = sampler->output;
            if (!time_acc || !value_acc) continue;
            
            dst_chan->keyframe_count = (int)time_acc->count;
            dst_chan->times = new float[dst_chan->keyframe_count];
            
            for (int ki = 0; ki < dst_chan->keyframe_count; ++ki) {
                cgltf_accessor_read_float(time_acc, ki, &dst_chan->times[ki], 1);
                // Update animation duration
                if (dst_chan->times[ki] > dst->duration) {
                    dst->duration = dst_chan->times[ki];
                }
            }
            
            // Extract keyframe values
            int value_count = dst_chan->keyframe_count * dst_chan->components;
            dst_chan->values = new float[value_count];
            
            for (int ki = 0; ki < dst_chan->keyframe_count; ++ki) {
                cgltf_accessor_read_float(value_acc, ki, 
                                         &dst_chan->values[ki * dst_chan->components], 
                                         dst_chan->components);
            }
        }
        
        GLTF_LOGV("[glTF] Animation '%s': %d channels, duration=%.2fs\n", 
               dst->name, dst->channel_count, dst->duration);
    }
    
    return anims;
}

// Lerp between two float values
static float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Quaternion slerp (spherical linear interpolation)
static void QuatSlerp(const float* q1, const float* q2, float t, float* out) {
    // Compute dot product
    float dot = q1[0] * q2[0] + q1[1] * q2[1] + q1[2] * q2[2] + q1[3] * q2[3];
    
    // If dot < 0, negate q2 to take shorter path
    float q2_copy[4];
    if (dot < 0.0f) {
        dot = -dot;
        q2_copy[0] = -q2[0]; q2_copy[1] = -q2[1];
        q2_copy[2] = -q2[2]; q2_copy[3] = -q2[3];
        q2 = q2_copy;
    }
    
    // If quaternions are very close, use linear interpolation
    if (dot > 0.9995f) {
        out[0] = Lerp(q1[0], q2[0], t);
        out[1] = Lerp(q1[1], q2[1], t);
        out[2] = Lerp(q1[2], q2[2], t);
        out[3] = Lerp(q1[3], q2[3], t);
        // Normalize
        float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
        if (len > 0.0001f) {
            out[0] /= len; out[1] /= len; out[2] /= len; out[3] /= len;
        }
        return;
    }
    
    // Slerp
    float theta = acosf(dot);
    float sin_theta = sinf(theta);
    float w1 = sinf((1.0f - t) * theta) / sin_theta;
    float w2 = sinf(t * theta) / sin_theta;
    
    out[0] = w1 * q1[0] + w2 * q2[0];
    out[1] = w1 * q1[1] + w2 * q2[1];
    out[2] = w1 * q1[2] + w2 * q2[2];
    out[3] = w1 * q1[3] + w2 * q2[3];
}

// Evaluate a single animation channel at the given time
static void EvaluateChannel(const AnimationChannel* chan, float time, float* out) {
    if (!chan || !chan->times || !chan->values || chan->keyframe_count == 0) return;
    
    // Find the keyframe interval
    int key0 = 0, key1 = 0;
    float t = 0.0f;
    
    if (time <= chan->times[0]) {
        // Before first keyframe
        key0 = key1 = 0;
        t = 0.0f;
    } else if (time >= chan->times[chan->keyframe_count - 1]) {
        // After last keyframe
        key0 = key1 = chan->keyframe_count - 1;
        t = 0.0f;
    } else {
        // Binary search for interval
        for (int i = 0; i < chan->keyframe_count - 1; ++i) {
            if (time >= chan->times[i] && time < chan->times[i + 1]) {
                key0 = i;
                key1 = i + 1;
                float dt = chan->times[key1] - chan->times[key0];
                t = (dt > 0.0001f) ? ((time - chan->times[key0]) / dt) : 0.0f;
                break;
            }
        }
    }
    
    // Interpolate based on type
    const float* val0 = &chan->values[key0 * chan->components];
    const float* val1 = &chan->values[key1 * chan->components];
    
    if (chan->interpolation == ANIM_INTERP_STEP) {
        // Step: use val0
        for (int i = 0; i < chan->components; ++i) out[i] = val0[i];
    } else if (chan->path == ANIM_PATH_ROTATION) {
        // Quaternion slerp
        QuatSlerp(val0, val1, t, out);
    } else {
        // Linear interpolation for translation/scale
        for (int i = 0; i < chan->components; ++i) {
            out[i] = Lerp(val0[i], val1[i], t);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Internal: build ImportResult from an already-parsed cgltf_data.
// Takes ownership of data (calls cgltf_free).
// gltf_path and texture_output_dir may be null (disables material texture extraction).
// ---------------------------------------------------------------------------
static int MaterialIndexFromPtr(const cgltf_data* data, const cgltf_material* mat) {
    if (!data || !mat || !data->materials) return -1;
    ptrdiff_t idx = mat - data->materials;
    if (idx < 0 || (cgltf_size)idx >= data->materials_count) return -1;
        return (int)idx; // Return the index of the material
}

static void TransformPoint(const float m[16], const float in[3], float out[3]) {
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8]  * in[2] + m[12];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9]  * in[2] + m[13];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14];
}

static void TransformVector(const float m[16], const float in[3], float out[3]) {
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8]  * in[2];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9]  * in[2];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2];
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 0.000001f) {
        out[0] /= len;
        out[1] /= len;
        out[2] /= len;
    }
}

static void GatherSceneMeshNodes(cgltf_node* node, cgltf_node** out_nodes, int max_nodes, int* out_count) {
    if (!node || !out_nodes || !out_count) return;
    if (node->mesh && *out_count < max_nodes) {
        out_nodes[(*out_count)++] = node;
    }
    for (cgltf_size ci = 0; ci < node->children_count; ++ci) {
        GatherSceneMeshNodes(node->children[ci], out_nodes, max_nodes, out_count);
    }
}

static bool FindFirstSceneLight(const cgltf_data* data, cgltf_node* node, float out_pos[3], int* out_node_index) {
    if (!node) return false;
    if (node->light) {
        float m[16] = {};
        cgltf_node_transform_world(node, m);
        out_pos[0] = m[12];
        out_pos[1] = m[13];
        out_pos[2] = m[14];
        if (out_node_index && data) {
            *out_node_index = (int)cgltf_node_index(data, node);
        }
        return true;
    }
    for (cgltf_size ci = 0; ci < node->children_count; ++ci) {
        if (FindFirstSceneLight(data, node->children[ci], out_pos, out_node_index)) {
            return true;
        }
    }
    return false;
}

static bool FindFirstSceneCamera(const cgltf_data* data, cgltf_node* node, float out_pos[3], float out_target[3], float* out_fov_deg, int* out_node_index) {
    if (!node) return false;
    if (node->camera) {
        float m[16] = {};
        cgltf_node_transform_world(node, m);
        out_pos[0] = m[12];
        out_pos[1] = m[13];
        out_pos[2] = m[14];

        // Camera looks along its local -Z axis. Extract column 2 directly to
        // avoid aliasing if caller passes the same array as both in and out.
        // In column-major layout: m[8..10] = Z-axis column (backward); negate for forward.
        float forward[3] = {};
        float forward_local[3] = {0.0f, 0.0f, -1.0f};
        TransformVector(m, forward_local, forward);
        out_target[0] = out_pos[0] + forward[0];
        out_target[1] = out_pos[1] + forward[1];
        out_target[2] = out_pos[2] + forward[2];

        if (out_fov_deg) {
            *out_fov_deg = 45.0f;
            if (node->camera->type == cgltf_camera_type_perspective) {
                *out_fov_deg = node->camera->data.perspective.yfov * 180.0f / 3.14159265f;
            }
        }
        if (out_node_index && data) {
            *out_node_index = (int)cgltf_node_index(data, node);
        }
        return true;
    }
    for (cgltf_size ci = 0; ci < node->children_count; ++ci) {
        if (FindFirstSceneCamera(data, node->children[ci], out_pos, out_target, out_fov_deg, out_node_index)) {
            return true;
        }
    }
    return false;
}

static ImportResult* BuildFromData(ImportResult* result, cgltf_data* data,
                                    const char* gltf_path,
                                    const char* texture_output_dir) {
    if (!data) {
        strncpy_s(result->error, "invalid glTF data", _TRUNCATE);
        cgltf_free(data);
        return result;
    }

    // Warn when Blender exported in Z-up mode (non-standard; glTF 2.0 requires Y-up).
    // The asset.generator string will contain "Blender" but the exporter does NOT embed
    // an "up" field in glTF 2.0 metadata.  The only reliable detection is to check
    // whether every root-node's world matrix has its dominant "up" component along Y.
    // A simpler heuristic: if the file was created by Blender AND the mesh root has a
    // large rotation around X (~±90°), that's the wrong-up-axis sign.
    // For now, just emit a reminder whenever loading from a Blender-generated file so
    // users see it in stdout and can fix their export settings.
    if (data->asset.generator && strstr(data->asset.generator, "Blender")) {
        GLTF_LOGV("[glTF] NOTE: Blender-generated file detected ('%s').\n"
                  "       Ensure 'Up: +Y Up' is selected in Blender's glTF export dialog.\n"
                  "       Exporting with Z-up produces non-standard glTF and will appear\n"
                  "       rotated 90° around X in HiMYM (mesh lies on its side, camera wrong).\n",
                  data->asset.generator);
        // Always print this (not just when verbose) since it is actionable axis guidance.
        printf("[glTF] Blender export detected. Verify 'Up: +Y Up' in export settings.\n");
    }

    const int max_nodes = (int)data->nodes_count;
    cgltf_node** mesh_nodes = nullptr;
    int mesh_node_count = 0;
    if (max_nodes > 0) {
        mesh_nodes = new cgltf_node*[max_nodes];
        memset(mesh_nodes, 0, sizeof(cgltf_node*) * max_nodes);
    }

    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size ni = 0; ni < data->scene->nodes_count; ++ni) {
            GatherSceneMeshNodes(data->scene->nodes[ni], mesh_nodes, max_nodes, &mesh_node_count);
        }
    } else {
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            if (data->nodes[ni].mesh && mesh_node_count < max_nodes) {
                mesh_nodes[mesh_node_count++] = &data->nodes[ni];
            }
        }
    }

    if (mesh_node_count == 0) {
        delete[] mesh_nodes;
        strncpy_s(result->error, "no mesh found in glTF file", _TRUNCATE);
        cgltf_free(data);
        return result;
    }

    // Extract all materials up-front so callers can map slots to textures.
    if (data->materials_count > 0) {
        result->material_count = (int)data->materials_count;
        result->materials = new Material[result->material_count];
        for (int mi = 0; mi < result->material_count; ++mi) {
            FillMaterial(&result->materials[mi], &data->materials[mi], data, gltf_path, texture_output_dir);
        }
        result->material = result->materials[0];
    }

    // Capture the first light position from the active scene (if present).
    result->has_light = false;
    result->light_pos[0] = 0.0f;
    result->light_pos[1] = 0.0f;
    result->light_pos[2] = 0.0f;
        result->light_node_index = -1; // Initialize light node index
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size ni = 0; ni < data->scene->nodes_count; ++ni) {
            if (FindFirstSceneLight(data, data->scene->nodes[ni], result->light_pos, &result->light_node_index)) {
                result->has_light = true;
                break;
            }
        }
    } else {
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            if (data->nodes[ni].light) {
                float m[16] = {};
                cgltf_node_transform_world(&data->nodes[ni], m);
                result->light_pos[0] = m[12];
                result->light_pos[1] = m[13];
                result->light_pos[2] = m[14];
                result->light_node_index = (int)ni;
                result->has_light = true;
                break;
            }
        }
    }

    result->has_camera = false;
    result->camera_pos[0] = 0.0f;
    result->camera_pos[1] = 0.0f;
    result->camera_pos[2] = 5.0f;
    result->camera_target[0] = 0.0f;
    result->camera_target[1] = 0.0f;
    result->camera_target[2] = 0.0f;
    result->camera_fov_deg = 45.0f;
    result->camera_node_index = -1;
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size ni = 0; ni < data->scene->nodes_count; ++ni) {
            if (FindFirstSceneCamera(data, data->scene->nodes[ni], result->camera_pos, result->camera_target, &result->camera_fov_deg, &result->camera_node_index)) {
                result->has_camera = true;
                break;
            }
        }
    } else {
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            if (FindFirstSceneCamera(data, &data->nodes[ni], result->camera_pos, result->camera_target, &result->camera_fov_deg, &result->camera_node_index)) {
                result->has_camera = true;
                break;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Count total vertices and indices across all mesh nodes/primitives
    // ---------------------------------------------------------------------------
    uint32_t total_verts   = 0;
    uint32_t total_indices = 0;
    for (int ni = 0; ni < mesh_node_count; ++ni) {
        cgltf_mesh* src_mesh = mesh_nodes[ni]->mesh;
        if (!src_mesh) continue;
        for (cgltf_size pi = 0; pi < src_mesh->primitives_count; ++pi) {
            cgltf_primitive* prim = &src_mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            // Find POSITION accessor to get vertex count
            for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    total_verts += (uint32_t)prim->attributes[ai].data->count;
                    break;
                }
            }

            if (prim->indices) {
                total_indices += (uint32_t)prim->indices->count;
            } else {
                // Non-indexed: synthesize sequential indices
                for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
                    if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                        total_indices += (uint32_t)prim->attributes[ai].data->count;
                        break;
                    }
                }
            }
        }
    }

    if (total_verts == 0 || total_indices == 0) {
        delete[] mesh_nodes;
        strncpy_s(result->error, "mesh has no usable triangle primitives", _TRUNCATE);
        cgltf_free(data);
        return result;
    }

    // Create the merged mesh
    rev::mesh::Mesh* mesh = rev::mesh::CreateMesh(total_verts, total_indices);
    uint32_t vert_offset  = 0;
    uint32_t idx_offset   = 0;

    // ---------------------------------------------------------------------------
    // Copy vertex data and indices, building one MaterialSlot per primitive.
    // Node world transforms are baked into imported vertex data.
    // ---------------------------------------------------------------------------
    for (int ni = 0; ni < mesh_node_count; ++ni) {
        cgltf_node* src_node = mesh_nodes[ni];
        cgltf_mesh* src_mesh = src_node ? src_node->mesh : nullptr;
        if (!src_mesh) continue;

        float node_world[16] = {};
        cgltf_node_transform_world(src_node, node_world);

        for (cgltf_size pi = 0; pi < src_mesh->primitives_count; ++pi) {
            cgltf_primitive* prim = &src_mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            // Find accessors for this primitive
            const cgltf_accessor* pos_acc    = nullptr;
            const cgltf_accessor* norm_acc   = nullptr;
            const cgltf_accessor* uv_acc     = nullptr;

            for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
                switch (prim->attributes[ai].type) {
                    case cgltf_attribute_type_position: pos_acc  = prim->attributes[ai].data; break;
                    case cgltf_attribute_type_normal:   norm_acc = prim->attributes[ai].data; break;
                    case cgltf_attribute_type_texcoord:
                        if (prim->attributes[ai].index == 0) {
                            uv_acc = prim->attributes[ai].data;
                        }
                        break;
                    default: break;
                }
            }
            if (!pos_acc) continue;

            uint32_t prim_vert_count = (uint32_t)pos_acc->count;

            // Copy vertices
            for (uint32_t vi = 0; vi < prim_vert_count; ++vi) {
                rev::mesh::Vertex v = {};
                float local_pos[3] = {};
                ReadVec3(pos_acc, vi, local_pos);
                TransformPoint(node_world, local_pos, v.pos);

                if (norm_acc) {
                    float local_nrm[3] = {};
                    ReadVec3(norm_acc, vi, local_nrm);
                    TransformVector(node_world, local_nrm, v.normal);
                }
                if (uv_acc) ReadVec2(uv_acc, vi, v.uv);
                rev::mesh::SetVertex(mesh, vert_offset + vi, v);
            }

            // Copy indices (offset by vert_offset)
            uint32_t prim_idx_start = idx_offset;
            if (prim->indices) {
                uint32_t idx_count = (uint32_t)prim->indices->count;
                for (uint32_t ii = 0; ii < idx_count; ++ii) {
                    uint32_t local_idx = (uint32_t)cgltf_accessor_read_index(prim->indices, ii);
                    rev::mesh::SetIndex(mesh, idx_offset + ii, vert_offset + local_idx);
                }
                idx_offset += idx_count;
            } else {
                // Non-indexed: sequential
                for (uint32_t ii = 0; ii < prim_vert_count; ++ii) {
                    rev::mesh::SetIndex(mesh, idx_offset + ii, vert_offset + ii);
                }
                idx_offset += prim_vert_count;
            }

            // Determine material color for this slot
            int material_index = MaterialIndexFromPtr(data, prim->material);
            uint32_t slot_color = 0xFFFFFFFF; // white
            if (material_index >= 0 && material_index < result->material_count) {
                Material& mat = result->materials[material_index];
                uint8_t r = (uint8_t)(mat.base_color[0] * 255.0f);
                uint8_t g = (uint8_t)(mat.base_color[1] * 255.0f);
                uint8_t b = (uint8_t)(mat.base_color[2] * 255.0f);
                uint8_t a = (uint8_t)(mat.base_color[3] * 255.0f);
                if (prim->material && prim->material->alpha_mode != cgltf_alpha_mode_opaque && a == 255) {
                    // Preserve alpha-blended rendering intent even when base_color alpha is 1.
                    a = 254;
                }
                slot_color = (a << 24) | (b << 16) | (g << 8) | r;
            } else if (prim->material && prim->material->has_pbr_metallic_roughness) {
                const float* bc = prim->material->pbr_metallic_roughness.base_color_factor;
                uint8_t r = (uint8_t)(bc[0] * 255.0f);
                uint8_t g = (uint8_t)(bc[1] * 255.0f);
                uint8_t b = (uint8_t)(bc[2] * 255.0f);
                uint8_t a = (uint8_t)(bc[3] * 255.0f);
                if (prim->material->alpha_mode != cgltf_alpha_mode_opaque && a == 255) {
                    a = 254;
                }
                slot_color = (a << 24) | (b << 16) | (g << 8) | r;
            }

            uint32_t prim_idx_count = idx_offset - prim_idx_start;
            int source_node_index = (int)cgltf_node_index(data, src_node);
            float slot_emissive[3] = {0.0f, 0.0f, 0.0f};
            float slot_emissive_strength = 1.0f;
            if (material_index >= 0 && material_index < result->material_count) {
                Material& mat = result->materials[material_index];
                slot_emissive[0] = mat.emissive[0];
                slot_emissive[1] = mat.emissive[1];
                slot_emissive[2] = mat.emissive[2];
                slot_emissive_strength = mat.emissive_strength;
            }
            rev::mesh::AddMaterialSlot(mesh, prim_idx_start, prim_idx_count, slot_color,
                                       material_index, 0, source_node_index,
                                       slot_emissive, slot_emissive_strength);

            vert_offset += prim_vert_count;

            // Fill primary material from the first primitive that has material.
            if (result->material.name[0] == '\0' && prim->material) {
                FillMaterial(&result->material, prim->material,
                             data, gltf_path, texture_output_dir);
            }
        }
    }

    // Keep imported light data on the mesh for renderer fallback behavior.
    mesh->has_imported_light = result->has_light;
    mesh->imported_light_pos[0] = result->light_pos[0];
    mesh->imported_light_pos[1] = result->light_pos[1];
    mesh->imported_light_pos[2] = result->light_pos[2];
    mesh->imported_light_node_index = result->light_node_index;
    mesh->has_imported_camera = result->has_camera;
    mesh->imported_camera_pos[0] = result->camera_pos[0];
    mesh->imported_camera_pos[1] = result->camera_pos[1];
    mesh->imported_camera_pos[2] = result->camera_pos[2];
    mesh->imported_camera_target[0] = result->camera_target[0];
    mesh->imported_camera_target[1] = result->camera_target[1];
    mesh->imported_camera_target[2] = result->camera_target[2];
    mesh->imported_camera_fov_deg = result->camera_fov_deg;
    mesh->imported_camera_node_index = result->camera_node_index;
    if (data->nodes_count > 0) {
        mesh->imported_node_count = (uint32_t)data->nodes_count;
        mesh->imported_nodes = new rev::mesh::ImportedNode[mesh->imported_node_count];
        memset(mesh->imported_nodes, 0, sizeof(rev::mesh::ImportedNode) * mesh->imported_node_count);
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            const cgltf_node* node = &data->nodes[ni];
            rev::mesh::ImportedNode& dst_node = mesh->imported_nodes[ni];
            dst_node.parent_index = node->parent ? (int)cgltf_node_index(data, node->parent) : -1;
            dst_node.base_translation[0] = node->has_translation ? node->translation[0] : 0.0f;
            dst_node.base_translation[1] = node->has_translation ? node->translation[1] : 0.0f;
            dst_node.base_translation[2] = node->has_translation ? node->translation[2] : 0.0f;
            dst_node.base_rotation[0] = node->has_rotation ? node->rotation[0] : 0.0f;
            dst_node.base_rotation[1] = node->has_rotation ? node->rotation[1] : 0.0f;
            dst_node.base_rotation[2] = node->has_rotation ? node->rotation[2] : 0.0f;
            dst_node.base_rotation[3] = node->has_rotation ? node->rotation[3] : 1.0f;
            dst_node.base_scale[0] = node->has_scale ? node->scale[0] : 1.0f;
            dst_node.base_scale[1] = node->has_scale ? node->scale[1] : 1.0f;
            dst_node.base_scale[2] = node->has_scale ? node->scale[2] : 1.0f;
            cgltf_node_transform_world(node, dst_node.base_world);
            InvertMatrix4x4(dst_node.base_world, dst_node.inverse_base_world);
        }
    }

    result->mesh = mesh;
    result->ok   = true;

    // Extract animations before freeing cgltf data
    result->animations = ExtractAnimations(data, &result->animation_count);

    delete[] mesh_nodes;
    cgltf_free(data);
    return result;
}

// ---------------------------------------------------------------------------

ImportResult* LoadMesh(const char* gltf_path, const char* texture_output_dir) {
    ImportResult* result = new ImportResult{};
    result->ok = false;
    SetMaterialDefaults(&result->material);
    result->materials = nullptr;
    result->material_count = 0;
    result->animations = nullptr;
    result->animation_count = 0;
    result->has_light = false;
    result->light_pos[0] = result->light_pos[1] = result->light_pos[2] = 0.0f;
    result->light_node_index = -1;
    result->has_camera = false;
    result->camera_pos[0] = 0.0f;
    result->camera_pos[1] = 0.0f;
    result->camera_pos[2] = 5.0f;
    result->camera_target[0] = 0.0f;
    result->camera_target[1] = 0.0f;
    result->camera_target[2] = 0.0f;
    result->camera_fov_deg = 45.0f;
    result->camera_node_index = -1;

    if (!gltf_path || !gltf_path[0]) {
        strncpy_s(result->error, "null or empty gltf_path", _TRUNCATE);
        return result;
    }

    cgltf_options opts = {};
    cgltf_data*   data = nullptr;
    cgltf_result  res  = cgltf_parse_file(&opts, gltf_path, &data);
    if (res != cgltf_result_success) {
        snprintf(result->error, sizeof(result->error),
                 "cgltf_parse_file failed: %d", (int)res);
        return result;
    }

    res = cgltf_load_buffers(&opts, data, gltf_path);
    if (res != cgltf_result_success) {
        snprintf(result->error, sizeof(result->error),
                 "cgltf_load_buffers failed: %d", (int)res);
        cgltf_free(data);
        return result;
    }

    return BuildFromData(result, data, gltf_path, texture_output_dir);
}

// ---------------------------------------------------------------------------

ImportResult* LoadMeshFromMemory(const void* buf, size_t size,
                                  const char* texture_output_dir) {
    ImportResult* result = new ImportResult{};
    result->ok = false;
    SetMaterialDefaults(&result->material);
    result->materials = nullptr;
    result->material_count = 0;
    result->animations = nullptr;
    result->animation_count = 0;
    result->has_light = false;
    result->light_pos[0] = result->light_pos[1] = result->light_pos[2] = 0.0f;
    result->light_node_index = -1;
    result->has_camera = false;
    result->camera_pos[0] = 0.0f;
    result->camera_pos[1] = 0.0f;
    result->camera_pos[2] = 5.0f;
    result->camera_target[0] = 0.0f;
    result->camera_target[1] = 0.0f;
    result->camera_target[2] = 0.0f;
    result->camera_fov_deg = 45.0f;
    result->camera_node_index = -1;

    if (!buf || size == 0) {
        strncpy_s(result->error, "null or empty buffer", _TRUNCATE);
        return result;
    }

    cgltf_options opts = {};
    cgltf_data*   data = nullptr;
    cgltf_result  res  = cgltf_parse(&opts, buf, size, &data);
    if (res != cgltf_result_success) {
        snprintf(result->error, sizeof(result->error),
                 "cgltf_parse failed: %d", (int)res);
        return result;
    }

    // For self-contained GLB all buffers are embedded — pass "" so cgltf
    // does not attempt to resolve external file paths.
    res = cgltf_load_buffers(&opts, data, "");
    if (res != cgltf_result_success) {
        snprintf(result->error, sizeof(result->error),
                 "cgltf_load_buffers failed: %d", (int)res);
        cgltf_free(data);
        return result;
    }

    // For memory loads, we can still extract embedded textures if output_dir is provided.
    // Pass empty string as gltf_path since there's no source file.
    return BuildFromData(result, data, "", texture_output_dir);
}

// ---------------------------------------------------------------------------

void FreeImportResult(ImportResult* result) {
    if (!result) return;
    if (result->mesh) rev::mesh::DestroyMesh(result->mesh);
    delete[] result->materials;
    
    // Free animations
    if (result->animations) {
        for (int ai = 0; ai < result->animation_count; ++ai) {
            Animation* anim = &result->animations[ai];
            if (anim->channels) {
                for (int ci = 0; ci < anim->channel_count; ++ci) {
                    delete[] anim->channels[ci].times;
                    delete[] anim->channels[ci].values;
                }
                delete[] anim->channels;
            }
        }
        delete[] result->animations;
    }
    
    delete result;
}

// ---------------------------------------------------------------------------

void EvaluateAnimation(const Animation* anim, float time,
                       float* out_translation,
                       float* out_rotation,
                       float* out_scale) {
    if (!anim || anim->channel_count == 0) return;
    
    // Loop the animation
    if (anim->duration > 0.0f) {
        time = fmodf(time, anim->duration);
        if (time < 0.0f) time += anim->duration;
    }
    
    // Set defaults
    if (out_translation) {
        out_translation[0] = 0.0f;
        out_translation[1] = 0.0f;
        out_translation[2] = 0.0f;
    }
    if (out_rotation) {
        out_rotation[0] = 0.0f;
        out_rotation[1] = 0.0f;
        out_rotation[2] = 0.0f;
        out_rotation[3] = 1.0f;  // identity quaternion
    }
    if (out_scale) {
        out_scale[0] = 1.0f;
        out_scale[1] = 1.0f;
        out_scale[2] = 1.0f;
    }
    
    // Evaluate each channel
    for (int ci = 0; ci < anim->channel_count; ++ci) {
        const AnimationChannel* chan = &anim->channels[ci];
        
        switch (chan->path) {
            case ANIM_PATH_TRANSLATION:
                if (out_translation) EvaluateChannel(chan, time, out_translation);
                break;
            case ANIM_PATH_ROTATION:
                if (out_rotation) EvaluateChannel(chan, time, out_rotation);
                break;
            case ANIM_PATH_SCALE:
                if (out_scale) EvaluateChannel(chan, time, out_scale);
                break;
        }
    }
}

void EvaluateAnimationNodeLocalTransform(const Animation* anim,
                                         int target_node_index,
                                         float time,
                                         float* out_translation,
                                         float* out_rotation,
                                         float* out_scale) {
    if (!anim || anim->channel_count == 0) return;

    if (anim->duration > 0.0f) {
        time = fmodf(time, anim->duration);
        if (time < 0.0f) time += anim->duration;
    }

    for (int ci = 0; ci < anim->channel_count; ++ci) {
        const AnimationChannel* chan = &anim->channels[ci];
        if (chan->target_node_index != target_node_index) continue;
        switch (chan->path) {
            case ANIM_PATH_TRANSLATION:
                if (out_translation) EvaluateChannel(chan, time, out_translation);
                break;
            case ANIM_PATH_ROTATION:
                if (out_rotation) EvaluateChannel(chan, time, out_rotation);
                break;
            case ANIM_PATH_SCALE:
                if (out_scale) EvaluateChannel(chan, time, out_scale);
                break;
        }
    }
}

// ---------------------------------------------------------------------------

void QuaternionToEuler(const float* quat, float* euler_degrees) {
    // quat = [x, y, z, w]
    float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    
    // Roll (x-axis rotation)
    float sinr_cosp = 2.0f * (w * x + y * z);
    float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
    euler_degrees[0] = atan2f(sinr_cosp, cosr_cosp) * (180.0f / 3.14159265f);
    
    // Pitch (y-axis rotation)
    float sinp = 2.0f * (w * y - z * x);
    if (fabsf(sinp) >= 1.0f)
        euler_degrees[1] = copysignf(90.0f, sinp); // Use 90 degrees if out of range
    else
        euler_degrees[1] = asinf(sinp) * (180.0f / 3.14159265f);
    
    // Yaw (z-axis rotation)
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    euler_degrees[2] = atan2f(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
}

bool UpdateMeshAnimation(rev::mesh::Mesh* mesh, float dt) {
    if (!mesh || mesh->current_animation < 0 || mesh->current_animation >= mesh->animation_count) {
        return false;
    }
    
    if (!mesh->animation_data) return false;
    
    Animation* anims = (Animation*)mesh->animation_data;
    Animation* anim = &anims[mesh->current_animation];
    
    // Update time
    mesh->animation_time += dt * mesh->animation_speed;
    
    // Handle looping
    if (anim->duration > 0.0f) {
        if (mesh->animation_loop) {
            mesh->animation_time = fmodf(mesh->animation_time, anim->duration);
            if (mesh->animation_time < 0.0f) mesh->animation_time += anim->duration;
        } else {
            // Clamp to duration if not looping
            if (mesh->animation_time >= anim->duration) {
                mesh->animation_time = anim->duration;
                return false;  // Animation ended
            }
        }
    }
    
    return true;
}

void ApplyAnimationTransform(float* pos, float* rot_degrees, float* scale,
                             const float* translation, const float* rotation_quat, const float* anim_scale) {
    // Add translation
    if (translation) {
        pos[0] += translation[0];
        pos[1] += translation[1];
        pos[2] += translation[2];
    }
    
    // Convert quaternion to Euler and add to rotation
    if (rotation_quat) {
        float euler[3];
        QuaternionToEuler(rotation_quat, euler);
        rot_degrees[0] += euler[0];
        rot_degrees[1] += euler[1];
        rot_degrees[2] += euler[2];
    }
    
    // Multiply scale
    if (anim_scale) {
        scale[0] *= anim_scale[0];
        scale[1] *= anim_scale[1];
        scale[2] *= anim_scale[2];
    }
}

bool BuildAnimatedNodeDeltaMatrices(const rev::mesh::Mesh* mesh,
                                    const Animation* anim,
                                    float time,
                                    float* out_matrices,
                                    int max_nodes) {
    if (!mesh || !mesh->imported_nodes || mesh->imported_node_count == 0 || !out_matrices) return false;
    if ((int)mesh->imported_node_count > max_nodes) return false;

    float* local_matrices = new float[mesh->imported_node_count * 16];
    float* world_matrices = new float[mesh->imported_node_count * 16];
    unsigned char* computed = new unsigned char[mesh->imported_node_count];
    memset(computed, 0, mesh->imported_node_count);
    for (uint32_t ni = 0; ni < mesh->imported_node_count; ++ni) {
        const rev::mesh::ImportedNode& node = mesh->imported_nodes[ni];
        float translation[3] = { node.base_translation[0], node.base_translation[1], node.base_translation[2] };
        float rotation[4] = { node.base_rotation[0], node.base_rotation[1], node.base_rotation[2], node.base_rotation[3] };
        float scale[3] = { node.base_scale[0], node.base_scale[1], node.base_scale[2] };
        if (anim) {
            EvaluateAnimationNodeLocalTransform(anim, (int)ni, time, translation, rotation, scale);
        }

        ComposeMatrixFromTRS(&local_matrices[ni * 16], translation, rotation, scale);
    }

    for (uint32_t ni = 0; ni < mesh->imported_node_count; ++ni) {
        BuildNodeWorldRecursive(mesh, local_matrices, (int)ni, computed, world_matrices);
        Mat4MultiplyLocal(&out_matrices[ni * 16], &world_matrices[ni * 16], mesh->imported_nodes[ni].inverse_base_world);
    }

    delete[] computed;
    delete[] local_matrices;
    delete[] world_matrices;
    return true;
}

bool BuildAnimatedNodeDeltaMatricesAll(const rev::mesh::Mesh* mesh,
                                       const Animation* anims,
                                       int animation_count,
                                       float time,
                                       bool loop,
                                       float* out_matrices,
                                       int max_nodes) {
    if (!mesh || !mesh->imported_nodes || mesh->imported_node_count == 0 || !out_matrices) return false;
    if ((int)mesh->imported_node_count > max_nodes) return false;

    float* local_matrices = new float[mesh->imported_node_count * 16];
    float* world_matrices = new float[mesh->imported_node_count * 16];
    unsigned char* computed = new unsigned char[mesh->imported_node_count];
    memset(computed, 0, mesh->imported_node_count);

    // Use one active track for evaluation. Combining all tracks by overwriting
    // channels causes incorrect transforms when files contain multiple actions.
    const Animation* active_anim = nullptr;
    if (anims && animation_count > 0) {
        int active_index = mesh->current_animation;
        if (active_index < 0 || active_index >= animation_count) active_index = 0;
        active_anim = &anims[active_index];
    }

    for (uint32_t ni = 0; ni < mesh->imported_node_count; ++ni) {
        const rev::mesh::ImportedNode& node = mesh->imported_nodes[ni];
        float translation[3] = { node.base_translation[0], node.base_translation[1], node.base_translation[2] };
        float rotation[4] = { node.base_rotation[0], node.base_rotation[1], node.base_rotation[2], node.base_rotation[3] };
        float scale[3] = { node.base_scale[0], node.base_scale[1], node.base_scale[2] };

        if (active_anim) {
            float anim_time = time;
            if (active_anim->duration > 0.0f) {
                if (loop) {
                    anim_time = fmodf(anim_time, active_anim->duration);
                    if (anim_time < 0.0f) anim_time += active_anim->duration;
                } else {
                    if (anim_time < 0.0f) anim_time = 0.0f;
                    if (anim_time > active_anim->duration) anim_time = active_anim->duration;
                }
            }
            EvaluateAnimationNodeLocalTransform(active_anim, (int)ni, anim_time, translation, rotation, scale);
        }

        ComposeMatrixFromTRS(&local_matrices[ni * 16], translation, rotation, scale);
    }

    for (uint32_t ni = 0; ni < mesh->imported_node_count; ++ni) {
        BuildNodeWorldRecursive(mesh, local_matrices, (int)ni, computed, world_matrices);
        Mat4MultiplyLocal(&out_matrices[ni * 16], &world_matrices[ni * 16], mesh->imported_nodes[ni].inverse_base_world);
    }

    delete[] computed;
    delete[] local_matrices;
    delete[] world_matrices;
    return true;
}

// ---------------------------------------------------------------------------

int ExtractTextures(const char* gltf_path,
                    const char* output_dir,
                    char        extracted_paths[][512],
                    int         max_paths) {
    if (!gltf_path || !output_dir) return 0;

    cgltf_options opts = {};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&opts, gltf_path, &data) != cgltf_result_success) return 0;
    cgltf_load_buffers(&opts, data, gltf_path);

    CreateDirectoryA(output_dir, nullptr);

    int count = 0;
    for (cgltf_size i = 0; i < data->images_count && count < max_paths; ++i) {
        char dest[512] = {};
        if (ExtractImage(data, &data->images[i], (int)i,
                         gltf_path, output_dir, dest, sizeof(dest))) {
            if (extracted_paths)
                strncpy_s(extracted_paths[count], dest, _TRUNCATE);
            ++count;
        }
    }

    cgltf_free(data);
    return count;
}

} // namespace gltf
} // namespace rev