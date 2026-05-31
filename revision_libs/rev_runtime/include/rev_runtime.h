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

} // namespace runtime
} // namespace rev
