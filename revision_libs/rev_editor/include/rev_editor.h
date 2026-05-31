#pragma once

#include "rev_platform.h"
#include "rev_sequence.h"
#include "rev_curve.h"
#include <cstddef>

namespace rev {
namespace editor {

// Forward declarations
struct EditorContext;
struct ProjectData;
struct SceneBlock;
struct ShaderCue;
struct ImageCue;
struct TextCue;
struct MusicCue;

// RGB color triplet
struct ColorRGB {
    float r, g, b;
};

// Shader cue data (per shader instance)
struct ShaderCue {
    int shader_scene_id;           // Shader preset ID
    char shader_name[64];          // Friendly name
    
    // Palette colors
    ColorRGB palette_low;
    ColorRGB palette_mid;
    ColorRGB palette_high;
    
    // Parameters
    float speed;
    float intensity;
    float warp;
    float exposure_base;
    float exposure_ramp;
    float fade_base;
    float fade_ramp;
    
    // Timing (scene-relative seconds)
    float cue_start;
    float cue_end;              // -1.0 = implicit scene end
    float fade_in;
    float fade_out;
    
    // Layer controls
    int layer_role;             // 0=background, 1=overlay
    float opacity;
    int blend_mode;             // 0=alpha, 1=additive, 2=multiply, 3=screen
    int layer_order;            // Draw order (lower first)
    
    // Curve assignments (-1 = no curve)
    int curve_speed;
    int curve_intensity;
    int curve_warp;
    int curve_exposure;
    int curve_fade;
};

// Image overlay cue
struct ImageCue {
    char asset_key[64];         // Image filename
    float x, y;                 // Position (0.0-1.0, center)
    float scale;
    float opacity;
    int effect_type;            // 0=none, 1=fade_in_out
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int layer_order;            // Draw order (lower first, 0=default)
};

// Text cue
struct TextCue {
    char text[256];
    char font_name[64];
    float x, y;                 // Position (0.0-1.0, center)
    float size;
    ColorRGB color;
    int effect_type;            // 0=none, 1=fade_in_out, 2=scroll
    float cue_start;
    float cue_end;
    float fade_in_start;
    float fade_in_end;
    float fade_out_start;
    float fade_out_end;
    int layer_order;            // Draw order (lower first, 0=default)
};

// Music cue
struct MusicCue {
    char asset_key[64];         // XM filename
    char asset_path[512];       // Full path
    float cue_start;
    float cue_end;
};

// Scene block (timeline segment)
struct SceneBlock {
    char name[64];              // Scene name (e.g., "Opening")
    float duration;             // Scene length in seconds
    
    // Cue arrays
    ShaderCue* shader_cues;
    int shader_cue_count;
    int shader_cue_capacity;
    
    ImageCue* image_cues;
    int image_cue_count;
    int image_cue_capacity;
    
    TextCue* text_cues;
    int text_cue_count;
    int text_cue_capacity;
    
    MusicCue* music_cues;
    int music_cue_count;
    int music_cue_capacity;
};

// Project data structure
struct ProjectData {
    // Scene blocks (timeline)
    SceneBlock* scenes;
    int scene_count;
    int scene_capacity;
    
    // Animation curves
    rev::curve::Curve* curves;
    int curve_count;
    
    // Project metadata
    char project_path[512];
    char workspace_path[512];
    char assets_path[512];      // Project-specific assets folder
    bool modified;
    
    // Timing
    float total_duration;       // Sum of all scene durations
};

// Editor context (opaque handle)
struct EditorContext {
    void* imgui_context;
    rev::platform::Window* window;
    ProjectData* project;
    
    // UI state
    bool show_timeline;
    bool show_curve_editor;
    bool show_properties;
    bool show_asset_browser;
    bool show_demo;
    
    // Timeline state
    float timeline_zoom;
    float timeline_scroll;
    int selected_scene_index;
    int selected_cue_index;
    int selected_cue_type;      // 0=shader, 1=image, 2=text, 3=music
    
    // Playback
    float current_time;
    bool playing;
    
    // Preview viewport
    bool show_preview;
    unsigned int preview_fbo;        // Framebuffer object
    unsigned int preview_texture;    // Color attachment
    unsigned int preview_depth;      // Depth attachment
    int preview_width;
    int preview_height;
    bool preview_initialized;
    void* preview_shader;            // rev::shader::Program* for fullscreen shader
    void* sprite_shader;             // rev::shader::Program* for sprite rendering
    int preview_current_shader_id;   // Currently compiled shader preset ID (-1 = none)
    
    // Shader modal state
    ShaderCue editing_shader;
    bool shader_modal_open;
    bool shader_modal_request_open;  // Set to true to request opening the modal
    
    // Music modal state
    MusicCue editing_music;
    bool music_modal_open;
    bool music_modal_request_open;
    
    // Image modal state
    ImageCue editing_image;
    bool image_modal_open;
    bool image_modal_request_open;
    
    // Text modal state
    TextCue editing_text;
    bool text_modal_open;
    bool text_modal_request_open;
    
    // Curve editor state
    int selected_curve_index;
    int dragging_point_index;
    bool show_curve_grid;
    
    // Build status
    char build_status_message[512];
    float build_status_timer;

    // Startup working directory — captured at CreateEditor time.
    // Used for all path computations so Windows file-dialog CWD mutations
    // (OFN_NOCHANGEDIR-less dialogs) cannot corrupt relative paths.
    char startup_dir[512];
};

// Lifecycle
EditorContext* CreateEditor(rev::platform::Window* window);
void DestroyEditor(EditorContext* editor);

// Project management
bool LoadProject(EditorContext* editor, const char* path);
bool SaveProject(EditorContext* editor, const char* path);
bool NewProject(EditorContext* editor);
bool ImportFromCues(EditorContext* editor, const char* cues_path);  // Import from cues.txt export format

// Frame lifecycle
void BeginFrame(EditorContext* editor);
void RenderUI(EditorContext* editor);
void EndFrame(EditorContext* editor);

// UI panels
void RenderMenuBar(EditorContext* editor);
void RenderTimeline(EditorContext* editor);
void RenderCurveEditor(EditorContext* editor);
void RenderShaderModal(EditorContext* editor);
void RenderMusicModal(EditorContext* editor);
void RenderImageModal(EditorContext* editor);
void RenderTextModal(EditorContext* editor);
void RenderProperties(EditorContext* editor);
void RenderAssetBrowser(EditorContext* editor);
void RenderPreviewPanel(EditorContext* editor);

// Preview viewport
void InitializePreview(EditorContext* editor, int width, int height);
void CleanupPreview(EditorContext* editor);
void ResizePreview(EditorContext* editor, int width, int height);
void RenderPreviewFrame(EditorContext* editor);
void UpdatePlayback(EditorContext* editor, float delta_time);

// Scene management
int AddScene(EditorContext* editor, const char* name, float duration);
void DeleteScene(EditorContext* editor, int scene_index);
void MoveScene(EditorContext* editor, int from_index, int to_index);
SceneBlock* GetScene(EditorContext* editor, int scene_index);

// Cue management
int AddShaderCue(SceneBlock* scene, const ShaderCue& cue);
int AddImageCue(SceneBlock* scene, const ImageCue& cue);
int AddTextCue(SceneBlock* scene, const TextCue& cue);
int AddMusicCue(SceneBlock* scene, const MusicCue& cue);

void DeleteShaderCue(SceneBlock* scene, int cue_index);
void DeleteImageCue(SceneBlock* scene, int cue_index);
void DeleteTextCue(SceneBlock* scene, int cue_index);
void DeleteMusicCue(SceneBlock* scene, int cue_index);

// Shader presets
void LoadShaderPreset(ShaderCue* cue, int preset_id);
void RandomizeShaderColors(ShaderCue* cue);
void RandomizeShaderValues(ShaderCue* cue);
void ResetShaderValues(ShaderCue* cue);

// Build integration
bool ExportProject(EditorContext* editor, const char* output_path);
bool BuildAndRun(EditorContext* editor);
// Pack assets into the exe (checksum-based, only re-packs changed files), then build and run.
bool PackBuildAndRun(EditorContext* editor);

}  // namespace editor
}  // namespace rev
