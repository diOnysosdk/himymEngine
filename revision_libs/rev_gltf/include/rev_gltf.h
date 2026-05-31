#pragma once
// rev_gltf — glTF 2.0 mesh importer for the HiMYM framework
// Editor-side only: not linked into the final packed intro.
// Wraps cgltf (single-header C99 parser) and outputs rev::mesh::Mesh* objects
// with PBR material data extracted from the glTF document.
//
// Supported features:
//   * .gltf (ASCII JSON) and .glb (binary) files
//   * First mesh / all primitives merged into one Mesh
//   * PBR metallic-roughness material (base color factor, metallic, roughness)
//   * Base color texture, normal texture, metallic-roughness texture extraction
//   * Embedded (base64) and external texture images written to output directory
//   * Multi-material meshes via MaterialSlot index ranges

#include "rev_mesh.h"
#include <stddef.h>

namespace rev {
namespace gltf {

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
    rev::mesh::Mesh* mesh;          // Merged geometry (all primitives)
    Material         material;      // First (or primary) material
    bool             ok;
    char             error[256];
};

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Load a .gltf or .glb file.  All mesh primitives in the first mesh node are
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
// Texture extraction is not available in this path.
ImportResult* LoadMeshFromMemory(const void* data, size_t size);

// Free an ImportResult (and the mesh it owns).
void FreeImportResult(ImportResult* result);

// Extract all textures from a glTF file to output_dir without loading geometry.
// Returns the number of textures successfully extracted.
int ExtractTextures(const char* gltf_path,
                    const char* output_dir,
                    char        extracted_paths[][512],
                    int         max_paths);

} // namespace gltf
} // namespace rev
