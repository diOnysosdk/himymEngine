#pragma once
#include <cstddef>  // size_t

// ============================================================
// rev_runtime — shared cue types and rendering helpers.
// Both the editor preview and the standalone runtime link
// against this lib so every field change propagates once.
// ============================================================

namespace rev {
namespace runtime {

// ------------------------------------------------------------------
// Basic types
// ------------------------------------------------------------------
struct ColorRGB {
    float r, g, b;
};

// ------------------------------------------------------------------
// Cue structs (single source of truth for editor + runtime)
// ------------------------------------------------------------------

// Image overlay cue
struct ImageCue {
    char  asset_key[64];    // Logical name (used by editor to look up path)
    char  asset_path[512];  // Full path on disk (populated by runtime loader)
    float x, y;             // Centre position [0..1] where y=0 is top
    float scale;
    float opacity;
    int   effect_type;      // 0=none  1=fade_in_out
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int   layer_order;      // Lower value drawn first
};

// Text cue
struct TextCue {
    char     text[256];
    char     font_name[64];
    float    x, y;          // Centre position [0..1] where y=0 is top
    float    size;          // Font size in pixels
    ColorRGB color;
    int      effect_type;   // 0=none  1=fade_in_out  2=scroll
    float    cue_start;
    float    cue_end;
    float    fade_in_start;
    float    fade_in_end;
    float    fade_out_start;
    float    fade_out_end;
    int      layer_order;
};

// Music cue
struct MusicCue {
    char  asset_key[64];
    char  asset_path[512];
    float cue_start;
    float cue_end;
};

// 3-D mesh cue
// mesh_type: 0=cube  1=sphere  2=plane  3=torus
// pos/rot/scale: world transform (rot in degrees, Euler XYZ)
// color: RGBA [0..1]
// mesh_size: primary size (cube edge, sphere radius, plane width, torus major_radius)
// mesh_param: secondary param (sphere segment count, plane height, torus minor_radius)
struct MeshCue {
    char  asset_key[64];
    char  asset_path[512];  // file path for mesh_type==4 (glTF/GLB); empty for procedural types
    int   mesh_type;        // 0=Cube 1=Sphere 2=Plane 3=Torus 4=External(glTF/GLB)
    float pos[3];
    float rot[3];
    float scale[3];
    float color[4];         // RGBA base color
    float mesh_size;
    float mesh_param;
    float metallic;         // PBR metallic factor [0..1] (0=dielectric, 1=metal)
    float roughness;        // PBR roughness factor [0..1] (0=glossy, 1=rough)
    int   effect_type;      // 0=none  1=fade_in_out
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int   layer_order;
};

// ------------------------------------------------------------------
// Texture handles
// ------------------------------------------------------------------
struct ImageTexture {
    unsigned int texture_id;
    int          width;
    int          height;
};
typedef ImageTexture TextTexture;

// ------------------------------------------------------------------
// Effect helpers
// ------------------------------------------------------------------

// Returns opacity [0..1] for the given effect and timeline position.
float ComputeEffectOpacity(int   effect_type,
                           float fade_in_start,  float fade_in_end,
                           float fade_out_start, float fade_out_end,
                           float time);

// ------------------------------------------------------------------
// GDI+ texture loaders  (call GdiplusStartup before using these)
// ------------------------------------------------------------------

// Load an image file and upload it to an OpenGL texture.
bool LoadImageTexture(const char* path, ImageTexture* tex);

// Load an image from a memory buffer (for packed-asset builds).
bool LoadImageTextureFromMemory(const unsigned char* data, size_t size,
                                ImageTexture* tex);

// Rasterise text via GDI+ and upload to an OpenGL texture.
bool RenderTextToTexture(const char* text, const char* font_name, float size,
                         float r, float g, float b, TextTexture* tex);

// ------------------------------------------------------------------
// Cue file parsers  (parse the first matching cue from a cues.txt)
// ------------------------------------------------------------------
bool LoadImageCue(const char* cues_path, ImageCue* cue);
bool LoadTextCue (const char* cues_path, TextCue*  cue);
bool LoadMusicCue(const char* cues_path, MusicCue* cue);
bool LoadMeshCue (const char* cues_path, MeshCue*  cue);

// ------------------------------------------------------------------
// 4×4 column-major matrix math (float[16])
// Used by both the editor 3D preview and the runtime mesh renderer.
// ------------------------------------------------------------------
void Mat4Identity    (float* m);
void Mat4Perspective (float* m, float fov_rad, float aspect, float znear, float zfar);
void Mat4LookAt      (float* m, const float eye[3], const float center[3], const float up[3]);
void Mat4Translate   (float* m, float x, float y, float z);
void Mat4RotateEuler (float* m, float rx_deg, float ry_deg, float rz_deg);
void Mat4Scale       (float* m, float sx, float sy, float sz);
void Mat4Multiply    (float* out, const float* a, const float* b);
// Build a full model matrix: translation × rotationXYZ × scale (result into out)
void Mat4Model       (float* out, const float pos[3], const float rot_deg[3], const float scale[3]);

} // namespace runtime
} // namespace rev
