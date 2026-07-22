#pragma once
#include <cstddef>  // size_t
#include "rev_curve.h"

// ============================================================
// rev_runtime — shared cue types and rendering helpers.
// Both the editor preview and the standalone runtime link
// against this lib so every field change propagates once.
// ============================================================

namespace rev {
namespace runtime {

struct AudioEffects {
    int gain_enabled;
    float gain_db;
    int compressor_enabled;
    float compressor_threshold;
    float compressor_ratio;
    float compressor_attack;
    float compressor_release;
    int widener_enabled;
    float widener_amount;
    int eq_enabled;
    float eq_low_db;
    float eq_mid_db;
    float eq_high_db;
};

constexpr int kMaxCurves = 128;
constexpr int kMaxLayerPostEffects = 8;
constexpr int kMaxAssetShaders = 4;

// ------------------------------------------------------------------
// Basic types
// ------------------------------------------------------------------
struct ColorRGB {
    float r, g, b;
};

struct LayerPostEffect {
    int type;
    bool enabled;
    int order;
    int blend_mode;  // 0=alpha, 1=additive, 2=multiply, 3=screen
    float intensity;
    float threshold;
    float radius;
    float color[4];
    float start_time;
    float end_time;
    int curve_intensity;
    int curve_threshold;
    int curve_radius;
    int curve_color_r;
    int curve_color_g;
    int curve_color_b;
    int curve_color_a;
    int curve_amount;
};

struct AssetShader {
    int shader_id;
    bool enabled;
    int order;
    int blend_mode;  // 0=alpha, 1=additive, 2=multiply, 3=screen
    float opacity;
    float speed;
    float intensity;
    float warp;
    float exposure_base;
    float exposure_ramp;
    float fade_base;
    float fade_ramp;
    float palette_low[3];
    float palette_mid[3];
    float palette_high[3];
    float start_time;
    float end_time;
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
    float rotation;         // Rotation in degrees around the image centre
    float opacity;
    int   effect_type;      // 0=none  1=fade_in_out
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int   layer_order;      // Lower value drawn first
    int   blend_mode;       // 0=alpha, 1=additive, 2=multiply, 3=screen
    
    // Curve assignments (-1 = no curve)
    int   curve_x;
    int   curve_y;
    int   curve_scale;
    int   curve_rotation;
    int   curve_opacity;

    int post_effect_count;
    LayerPostEffect post_effects[kMaxLayerPostEffects];
    int shader_count;
    AssetShader shaders[kMaxAssetShaders];
};

// Animated sprite cue (frame-by-frame image sequence)
struct AnimatedSpriteCue {
    char  sprite_name[64];       // Friendly name used by editor/runtime UI
    char  frame_keys_csv[2048];  // Semicolon-separated frame asset keys (filenames)
    char  frame_paths_csv[4096]; // Semicolon-separated frame asset paths
    float x, y;                  // Centre position [0..1] where y=0 is top
    float scale;
    float rotation;              // Rotation in degrees around the sprite centre
    float opacity;
    int   effect_type;           // 0=none  1=fade_in_out
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int   layer_order;
    int   blend_mode;            // 0=alpha, 1=additive, 2=multiply, 3=screen
    float fps;                   // Frames per second
    int   playback_mode;         // 0=loop, 1=once, 2=pingpong
    int   start_frame;           // Initial frame offset

    // Curve assignments (-1 = no curve)
    int   curve_x;
    int   curve_y;
    int   curve_scale;
    int   curve_rotation;
    int   curve_opacity;
    int   curve_frame;

    int post_effect_count;
    LayerPostEffect post_effects[kMaxLayerPostEffects];
    int shader_count;
    AssetShader shaders[kMaxAssetShaders];
};

// Indexed pixel animation cue. The referenced .pix asset owns the palette and frames.
struct PixelCue {
    char asset_key[64];
    char asset_path[512];
    float x, y;
    float scale;
    float rotation;
    float opacity;
    float cue_start;
    float cue_end;
    int layer_order;
    int blend_mode;
    float fps;
    int playback_mode;       // 0=loop, 1=once, 2=pingpong
    int start_frame;
    int palette_offset;
    int palette_cycle_speed;
    int snap_to_pixels;

    int curve_x;
    int curve_y;
    int curve_scale;
    int curve_rotation;
    int curve_opacity;
    int curve_frame;
    int curve_palette_offset;

    int post_effect_count;
    LayerPostEffect post_effects[kMaxLayerPostEffects];
    int shader_count;
    AssetShader shaders[kMaxAssetShaders];
};

// Particle emitter cue. visual_source is 0 for an image asset and 1 for a
// built-in primitive; primitive_shape is square, circle, triangle, or diamond.
struct PixelEmitterCue {
    char asset_key[64];
    char asset_path[512];
    int visual_source;
    int primitive_shape;
    float primitive_color[4];
    float x, y;
    float scale;
    float rotation;
    float opacity;
    float cue_start;
    float cue_end;
    int layer_order;
    int blend_mode;
    int max_particles;
    float emission_rate;
    int burst_count;
    float duration;
    int loop;
    float start_delay;
    int simulation_space;
    float direction_x;
    float direction_y;
    float cone_angle_degrees;
    float speed_min;
    float speed_max;
    float lifetime_min;
    float lifetime_max;
    float scale_min;
    float scale_max;
    float rotation_min;
    float rotation_max;
    float angular_velocity_min;
    float angular_velocity_max;
    float acceleration_x;
    float acceleration_y;
    float drag;
    unsigned int seed;

    int curve_x;
    int curve_y;
    int curve_scale;
    int curve_rotation;
    int curve_opacity;
    int curve_emission_rate;
    int curve_speed_min;
    int curve_speed_max;
    int curve_lifetime_min;
    int curve_lifetime_max;
    int curve_scale_min;
    int curve_scale_max;

    int post_effect_count;
    LayerPostEffect post_effects[kMaxLayerPostEffects];
    int shader_count;
    AssetShader shaders[kMaxAssetShaders];
};

// Text cue
struct TextCue {
    char     text[256];
    char     font_name[64];
    float    x, y;          // Centre position [0..1] where y=0 is top
    float    size;          // Font size in pixels
    float    rotation;      // Rotation in degrees around each glyph centre
    ColorRGB color;
    int      effect_type;   // 0=none 1=fade_in_out 2=scroll 3=line_by_line 4=typewriter 5=sandstorm
    float    cue_start;
    float    cue_end;
    float    fade_in_start;
    float    fade_in_end;
    float    fade_out_start;
    float    fade_out_end;
    int      layer_order;
    int      blend_mode;    // 0=alpha, 1=additive, 2=multiply, 3=screen
    
    // Curve assignments (-1 = no curve)
    int      curve_x;
    int      curve_y;
    int      curve_size;
    int      curve_rotation;
    int      curve_color_r;
    int      curve_color_g;
    int      curve_color_b;

    // 0 = auto (bake static effects only), 1 = force baked sprite
    int      bake_mode;

    // Optional baked text texture (generated by editor export).
    // When present, runtime can load this image directly instead of rasterizing via GDI+.
    char     baked_asset_key[64];
    char     baked_asset_path[512];

    // Exported glyph atlas used by per-character text rendering.
    char     glyph_atlas_key[64];
    char     glyph_atlas_path[512];
    char     glyph_meta_key[64];
    char     glyph_meta_path[512];
};

// Scroll text cue (dedicated style/preset-driven marquee pipeline)
struct ScrollTextCue {
    char     text[512];
    char     font_name[64];
    float    x, y;          // Anchor in normalized screen space [0..1]
    float    size;          // Font size in pixels
    float    rotation;      // Rotation in degrees around each glyph centre
    ColorRGB color;

    // Timing and layering
    float    cue_start;
    float    cue_end;
    float    fade_in_start;
    float    fade_in_end;
    float    fade_out_start;
    float    fade_out_end;
    float    opacity;
    int      layer_order;
    int      blend_mode;    // 0=alpha, 1=additive, 2=multiply, 3=screen

    // Scroll controls
    int      style_id;      // Preset style selector
    int      direction;     // 0=left, 1=right, 2=up, 3=down
    int      loop_mode;     // 0=loop, 1=clamp
    float    speed;         // Units per second in normalized screen space
    float    wrap_gap;      // Gap before the next repetition
    float    spacing;       // Glyph/word spacing multiplier
    float    slant_deg;     // Italic/slant style hint
    float    wave_amp;      // Optional vertical wobble amount
    float    wave_freq;     // Wobble frequency
    float    wave_length;   // Glyphs per sinus cycle
    float    jitter_amp;    // Per-frame jitter amount
    float    jitter_freq;   // Jitter speed
    float    glow;          // Style intensity helper
    float    shadow;
    float    outline;
    float    chroma_shift;
    float    distortion;

    // Curve assignments (-1 = no curve)
    int      curve_x;
    int      curve_y;
    int      curve_speed;
    int      curve_size;
    int      curve_rotation;
    int      curve_opacity;
    int      curve_color_r;
    int      curve_color_g;
    int      curve_color_b;
    int      curve_wave_amp;
    int      curve_wave_freq;
    int      curve_wave_length;
    int      curve_jitter_amp;
    int      curve_jitter_freq;

    // 0 = auto (dynamic), 1 = force baked sprite
    int      bake_mode;

    // Optional baked scroll texture generated by editor export.
    char     baked_asset_key[64];
    char     baked_asset_path[512];

    // Exported glyph atlas used by dynamic scroll rendering.
    char     glyph_atlas_key[64];
    char     glyph_atlas_path[512];
    char     glyph_meta_key[64];
    char     glyph_meta_path[512];
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
    float emissive_color[3]; // Emissive tint/multiplier
    float emissive_strength; // Emissive intensity multiplier
    float fov_deg;          // Per-cue camera FOV in degrees (defaults to 45)
    int   cull_mode;        // 0=off, 1=back, 2=front
    int   use_imported_light;
    int   use_imported_camera;
    int   effect_type;      // 0=none  1=fade_in_out
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int   layer_order;
    
    // Curve assignments (-1 = no curve)
    int   curve_pos_x;
    int   curve_pos_y;
    int   curve_pos_z;
    int   curve_rot_x;
    int   curve_rot_y;
    int   curve_rot_z;
    int   curve_scale_x;
    int   curve_scale_y;
    int   curve_scale_z;
    int   curve_color_r;
    int   curve_color_g;
    int   curve_color_b;
    int   curve_color_a;
    int   curve_mesh_size;
    int   curve_metallic;
    int   curve_roughness;
    int   curve_fov;
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

struct TextGlyph {
    int   codepoint;
    float u0, v0, u1, v1;
    float width, height;
    float advance;
    float bearing_y;
};

struct TextGlyphAtlas {
    unsigned int texture_id;
    int          width;
    int          height;
    float        font_size;
    float        line_height;
    TextGlyph    glyphs[128];
};

struct TextEffectFrame {
    char  text[256];
    float offset_x;      // Normalized screen-space offset in cue space [0..1]
    float offset_y;      // Normalized screen-space offset in cue space [0..1]
    float opacity_mul;   // Additional multiplier on top of base effect opacity
};

// ------------------------------------------------------------------
// Effect helpers
// ------------------------------------------------------------------

// Returns opacity [0..1] for the given effect and timeline position.
float ComputeEffectOpacity(int   effect_type,
                           float fade_in_start,  float fade_in_end,
                           float fade_out_start, float fade_out_end,
                           float time);

// Build per-frame text content and transform modifiers for advanced text effects.
// Returns false when no visible text should be drawn for this frame.
bool BuildTextEffectFrame(const TextCue* cue, float time, TextEffectFrame* out);

// ------------------------------------------------------------------
// GDI+ texture loaders  (call GdiplusStartup before using these)
// ------------------------------------------------------------------

// Load an image file and upload it to an OpenGL texture.
bool LoadImageTexture(const char* path, ImageTexture* tex);

// Load an image from a memory buffer (for packed-asset builds).
bool LoadImageTextureFromMemory(const unsigned char* data, size_t size,
                                ImageTexture* tex);

// Load a specific animation frame from an image. Frame 0 is the first frame.
bool LoadImageTextureFrame(const char* path, int frame_index, ImageTexture* tex);
bool LoadImageTextureFrameFromMemory(const unsigned char* data, size_t size,
                                     int frame_index, ImageTexture* tex);
int GetImageFrameCount(const char* path);
int GetImageFrameCountFromMemory(const unsigned char* data, size_t size);

// Rasterise text via GDI+ and upload to an OpenGL texture.
bool RenderTextToTexture(const char* text, const char* font_name, float size,
                         float r, float g, float b, TextTexture* tex);

// Build a white-alpha atlas for printable ASCII glyphs. Color is supplied at draw time.
bool CreateTextGlyphAtlas(const char* font_name, float size, TextGlyphAtlas* atlas);

// Distance, in normalized screen coordinates, for one complete scroll cycle.
float ComputeScrollTextTravel(const TextGlyphAtlas* atlas, const char* text,
                              int direction, float size_scale, float spacing,
                              float wrap_gap, float viewport_width,
                              float viewport_height);
bool SaveTextGlyphAtlas(const char* font_name, float size,
                        const char* image_path, const char* metadata_path);
bool LoadTextGlyphAtlasFromMemory(const unsigned char* image_data, size_t image_size,
                                  const unsigned char* metadata_data, size_t metadata_size,
                                  TextGlyphAtlas* atlas);
void DestroyTextGlyphAtlas(TextGlyphAtlas* atlas);
const TextGlyph* FindTextGlyph(const TextGlyphAtlas* atlas, unsigned int codepoint);

// ------------------------------------------------------------------
// Cue file parsers  (parse the first matching cue from a cues.txt)
// ------------------------------------------------------------------
bool LoadImageCue(const char* cues_path, ImageCue* cue);
bool LoadAnimatedSpriteCue(const char* cues_path, AnimatedSpriteCue* cue);
bool LoadTextCue (const char* cues_path, TextCue*  cue);
bool LoadMusicCue(const char* cues_path, MusicCue* cue);
bool LoadMeshCue (const char* cues_path, MeshCue*  cue);

// Load all curves from cues.txt into the provided array (max kMaxCurves curves)
// Returns the number of curves loaded
int LoadCurves(const char* cues_path, curve::Curve* curves, int max_curves);

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
