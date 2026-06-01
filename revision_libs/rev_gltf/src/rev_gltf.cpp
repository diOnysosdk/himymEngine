// cgltf implementation — compiled exactly once here
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "rev_gltf.h"
#include "rev_mesh.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

// Windows only (matches the rest of the framework)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rev {
namespace gltf {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void SetMaterialDefaults(Material* m) {
    memset(m, 0, sizeof(*m));
    m->base_color[0] = m->base_color[1] = m->base_color[2] = m->base_color[3] = 1.0f;
    m->metallic  = 0.0f;
    m->roughness = 1.0f;
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

    printf("[glTF] ExtractImage: output_dir='%s', filename='%s', dest_path='%s'\n",
           output_dir ? output_dir : "(null)", filename, dest_path);

    // Create output directory if needed
    CreateDirectoryA(output_dir, nullptr);

    if (img->buffer_view && img->buffer_view->buffer) {
        // Embedded binary image (in .glb buffer or base64 buffer view)
        printf("[glTF] Extracting embedded image from buffer_view\n");
        const cgltf_buffer_view* bv = img->buffer_view;
        const unsigned char* src = (const unsigned char*)bv->buffer->data + bv->offset;
        bool ok = WriteFile(dest_path, src, bv->size);
        printf("[glTF] WriteFile result: %s\n", ok ? "SUCCESS" : "FAILED");
        return ok;
    } else if (img->uri && strncmp(img->uri, "data:", 5) == 0) {
        // Data URI — base64 encoded.  cgltf already decoded the buffer if we
        // called cgltf_load_buffers, but the image itself lives in a buffer_view.
        // If buffer_view is null here, the image data was not loaded — skip.
        printf("[glTF] Image is data URI but no buffer_view - skipping\n");
        return false;
    } else if (img->uri && img->uri[0]) {
        // External file reference
        printf("[glTF] Copying external image file: %s\n", img->uri);
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
        printf("[glTF] No material in glTF file\n");
        return;
    }

    if (mat->name) strncpy_s(out->name, mat->name, _TRUNCATE);
    out->double_sided = mat->double_sided != 0;
    
    printf("[glTF] Material: name='%s' has_pbr=%d\n", 
           mat->name ? mat->name : "(unnamed)", 
           mat->has_pbr_metallic_roughness);

    if (mat->has_pbr_metallic_roughness) {
        const auto& pbr = mat->pbr_metallic_roughness;
        for (int i = 0; i < 4; ++i) out->base_color[i] = pbr.base_color_factor[i];
        out->metallic  = pbr.metallic_factor;
        out->roughness = pbr.roughness_factor;
        
        printf("[glTF] PBR: base_color_texture.texture=%p\n", (void*)pbr.base_color_texture.texture);
        if (pbr.base_color_texture.texture) {
            printf("[glTF]      texture->image=%p\n", (void*)pbr.base_color_texture.texture->image);
        }
        printf("[glTF] Total images in glTF: %zu\n", data->images_count);

        if (output_dir && output_dir[0]) {
            // Base color texture
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                printf("[glTF] Found base color texture in material\n");
                int img_idx = (int)(pbr.base_color_texture.texture->image - data->images);
                bool extracted = ExtractImage(data, pbr.base_color_texture.texture->image, img_idx,
                             gltf_path, output_dir,
                             out->base_color_texture, sizeof(out->base_color_texture));
                if (extracted) {
                    printf("[glTF] Extracted base color texture to: %s\n", out->base_color_texture);
                } else {
                    printf("[glTF] Failed to extract base color texture\n");
                }
            } else {
                printf("[glTF] No base color texture found in material (texture ptr is NULL)\n");
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

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Internal: build ImportResult from an already-parsed cgltf_data.
// Takes ownership of data (calls cgltf_free).
// gltf_path and texture_output_dir may be null (disables material texture extraction).
// ---------------------------------------------------------------------------
static ImportResult* BuildFromData(ImportResult* result, cgltf_data* data,
                                    const char* gltf_path,
                                    const char* texture_output_dir) {
    // Find the first mesh (from the default scene or the first mesh in data)
    cgltf_mesh* src_mesh = nullptr;
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size ni = 0; ni < data->scene->nodes_count; ++ni) {
            cgltf_node* node = data->scene->nodes[ni];
            if (node->mesh) { src_mesh = node->mesh; break; }
        }
    }
    if (!src_mesh && data->meshes_count > 0) {
        src_mesh = &data->meshes[0];
    }
    if (!src_mesh) {
        strncpy_s(result->error, "no mesh found in glTF file", _TRUNCATE);
        cgltf_free(data);
        return result;
    }

    // ---------------------------------------------------------------------------
    // Count total vertices and indices across all primitives
    // ---------------------------------------------------------------------------
    uint32_t total_verts   = 0;
    uint32_t total_indices = 0;
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

    if (total_verts == 0 || total_indices == 0) {
        strncpy_s(result->error, "mesh has no usable triangle primitives", _TRUNCATE);
        cgltf_free(data);
        return result;
    }

    // Create the merged mesh
    rev::mesh::Mesh* mesh = rev::mesh::CreateMesh(total_verts, total_indices);
    uint32_t vert_offset  = 0;
    uint32_t idx_offset   = 0;

    // ---------------------------------------------------------------------------
    // Copy vertex data and indices, building one MaterialSlot per primitive
    // ---------------------------------------------------------------------------
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
                case cgltf_attribute_type_texcoord: uv_acc   = prim->attributes[ai].data; break;
                default: break;
            }
        }
        if (!pos_acc) continue;

        uint32_t prim_vert_count = (uint32_t)pos_acc->count;

        // Copy vertices
        for (uint32_t vi = 0; vi < prim_vert_count; ++vi) {
            rev::mesh::Vertex v = {};
            ReadVec3(pos_acc, vi, v.pos);
            if (norm_acc) ReadVec3(norm_acc, vi, v.normal);
            if (uv_acc)   ReadVec2(uv_acc,   vi, v.uv);
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
        uint32_t slot_color = 0xFFFFFFFF; // white
        if (prim->material && prim->material->has_pbr_metallic_roughness) {
            const float* bc = prim->material->pbr_metallic_roughness.base_color_factor;
            uint8_t r = (uint8_t)(bc[0] * 255.0f);
            uint8_t g = (uint8_t)(bc[1] * 255.0f);
            uint8_t b = (uint8_t)(bc[2] * 255.0f);
            uint8_t a = (uint8_t)(bc[3] * 255.0f);
            slot_color = (a << 24) | (b << 16) | (g << 8) | r;
        }
        uint32_t prim_idx_count = idx_offset - prim_idx_start;
        rev::mesh::AddMaterialSlot(mesh, prim_idx_start, prim_idx_count, slot_color);

        vert_offset += prim_vert_count;

        // Fill primary material from first primitive with a material
        if (pi == 0 || (pi > 0 && result->material.name[0] == '\0')) {
            FillMaterial(&result->material, prim->material,
                         data, gltf_path, texture_output_dir);
        }
    }

    result->mesh = mesh;
    result->ok   = true;

    cgltf_free(data);
    return result;
}

// ---------------------------------------------------------------------------

ImportResult* LoadMesh(const char* gltf_path, const char* texture_output_dir) {
    ImportResult* result = new ImportResult{};
    result->ok = false;
    SetMaterialDefaults(&result->material);

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
    delete result;
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