#pragma once

#include "rev_platform.h"
#include "rev_sequence.h"
#include "rev_curve.h"
#include "rev_runtime.h"
#include <cstddef>

namespace rev {
namespace editor {

// Types shared with the runtime (single source of truth in rev_runtime)
using rev::runtime::ColorRGB;
using rev::runtime::ImageCue;
using rev::runtime::AnimatedSpriteCue;
using rev::runtime::PixelCue;
using rev::runtime::PixelEmitterCue;
using rev::runtime::TextCue;
using rev::runtime::ScrollTextCue;
using rev::runtime::TextAnimationConfig;
using rev::runtime::TextRevealConfig;
using rev::runtime::TextExitConfig;
using rev::runtime::TextModifierConfig;
using rev::runtime::kMaxTextAnimationModifiers;
using rev::runtime::TextAnimationUnitCharacter;
using rev::runtime::TextStaggerOrderForward;
using rev::runtime::TextEasingLinear;
using rev::runtime::MusicCue;
using rev::runtime::AudioEffects;
using rev::runtime::MeshCue;
using rev::runtime::LayerPostEffect;
using rev::runtime::AssetShader;
using rev::runtime::TriggerEvent;
using rev::runtime::TriggerTrack;
using rev::runtime::kMaxTriggerTracks;

enum CueType {
    CueTypeShader = 0,
    CueTypeImage = 1,
    CueTypeText = 2,
    CueTypeScrollText = 3,
    CueTypeMusic = 4,
    CueTypeMesh = 5,
    CueTypeAnimatedSprite = 6,
    CueTypePixel = 7,
    CueTypePixelEmitter = 8,
    CueTypePostEffect = 9,
};

enum PostEffectType {
    PostEffectHDR = 0,
    PostEffectACES = 1,
    PostEffectBloom = 2,
    PostEffectVignette = 3,
    PostEffectColorGrading = 4,
    PostEffectFilmGrain = 5,
    PostEffectBlueNoise = 6,
    PostEffectExponentialFog = 7,
    PostEffectFXAA = 8,
    PostEffectChromaticAberration = 9,
    PostEffectCameraShake = 10,
    PostEffectBeatFlash = 11,
    PostEffectFade = 12,
    PostEffectCRTWarp = 13,
    PostEffectScanlines = 14,
    PostEffectLensDistortion = 15,
    PostEffectPaletteCycle = 16,
    PostEffectHeatDistortion = 17,
    PostEffectGlitch = 18,
    PostEffectBloomPulse = 19,
    PostEffectFeedback = 20,
    PostEffectInfiniteZoom = 21,
    PostEffectRecursiveFeedback = 22,
    PostEffectCount = 23,
};

struct PostEffect {
    int type;
    bool enabled;
    int order;
    float intensity;
    float threshold;
    float radius;
    float color[4];
    float start_time;
    float end_time;

    // Curve assignments (-1 = no curve)
    int curve_intensity;
    int curve_threshold;
    int curve_radius;
    int curve_color_r;
    int curve_color_g;
    int curve_color_b;
    int curve_color_a;
    int curve_amount;
};

// Forward declarations
struct EditorContext;
struct ProjectData;
struct SceneBlock;
struct ShaderCue;

struct NoiseSettings {
    int enabled;
    int type;
    float scale;
    float strength;
    float octaves;
    float lacunarity;
    float gain;
    float warp;
    float speed_x;
    float speed_y;
    float seed;
    float contrast;
};

struct NoiseTextureSettings {
    char paths[4][512];
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

    // Virtual 3D coordinates used by procedural shader presets.
    float position_x;
    float position_y;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float motion_x;
    float motion_y;
    float motion_z;

    NoiseSettings noise;
    NoiseTextureSettings noise_textures;
    
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
    int curve_palette_low_r;
    int curve_palette_low_g;
    int curve_palette_low_b;
    int curve_palette_mid_r;
    int curve_palette_mid_g;
    int curve_palette_mid_b;
    int curve_palette_high_r;
    int curve_palette_high_g;
    int curve_palette_high_b;
    int curve_opacity;
    int curve_exposure_ramp;
    int curve_fade_ramp;
};

// (ImageCue, TextCue, MusicCue come from rev_runtime — see using declarations above)

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

    AnimatedSpriteCue* animated_sprite_cues;
    int animated_sprite_cue_count;
    int animated_sprite_cue_capacity;

    PixelCue* pixel_cues;
    int pixel_cue_count;
    int pixel_cue_capacity;

    PixelEmitterCue* pixel_emitter_cues;
    int pixel_emitter_cue_count;
    int pixel_emitter_cue_capacity;
    
    TextCue* text_cues;
    int text_cue_count;
    int text_cue_capacity;

    ScrollTextCue* scroll_text_cues;
    int scroll_text_cue_count;
    int scroll_text_cue_capacity;
    
    MusicCue* music_cues;
    int music_cue_count;
    int music_cue_capacity;

    MeshCue* mesh_cues;
    int mesh_cue_count;
    int mesh_cue_capacity;

        PostEffect* post_effects;
        int post_effect_count;
        int post_effect_capacity;

        LayerPostEffect scene_layer_post_effects[rev::runtime::kMaxLayerPostEffects];
        int scene_layer_post_effect_count;
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

    // Project-level music-synchronised trigger tracks.
    TriggerTrack trigger_tracks[kMaxTriggerTracks];
    int trigger_track_count;
    
    // Project metadata
    char project_path[512];
    char workspace_path[512];
    char assets_path[512];      // Project-specific assets folder
    bool modified;
    
    // Timing
    float total_duration;       // Sum of all scene durations
    bool  loop_intro;           // true = restart timeline when reaching end
    bool  loop_music;           // true = loop active XM cue playback
    bool  music_persist_across_scenes; // true = keep current track across scene cuts unless a different cue track becomes active
    bool  runtime_fullscreen;   // true = launch compiled intro fullscreen
    char  runtime_title[128];   // title shown by the compiled runtime window
    AudioEffects audio_effects;
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
    int selected_cue_type;      // See CueType
    
    // Playback
    float current_time;
    bool playing;
    void* audio_state;          // Editor-owned XM preview state

    // Trigger recorder state.
    bool show_trigger_recorder;
    bool trigger_recording;
    int recording_track_index;
    float recording_bpm;
    float recording_beat_offset;
    float recording_quantize_beats;
    char recording_track_name[64];
    
    // Preview viewport
    bool show_preview;
    unsigned int preview_fbo;        // Framebuffer object
    unsigned int preview_texture;    // Color attachment
    unsigned int preview_depth;      // Depth attachment
    unsigned int post_fbo;           // Post-production target
    unsigned int post_texture;       // Post-production color attachment
    unsigned int post_history_fbo;   // Previous post frame target
    unsigned int post_history_texture;
    unsigned int layer_fbo;
    unsigned int layer_texture;
    bool post_frame_rendered;
    unsigned int preview_vao;        // Dummy VAO required by OpenGL 3.3 core for gl_VertexID draws
    int preview_width;
    int preview_height;
    bool preview_initialized;
    void* preview_shader;            // rev::shader::Program* for fullscreen shader
    void* sprite_shader;             // rev::shader::Program* for sprite rendering
    void* mesh_shader;               // rev::shader::Program* for 3D mesh rendering
    void* post_shader;               // rev::shader::Program* for post-production effects
    int preview_current_shader_id;   // Currently compiled shader preset ID (-1 = none)
    rev::runtime::TextGlyphAtlas preview_text_atlas;
    char preview_text_atlas_font[64];
    float preview_text_atlas_size;
    rev::runtime::TextGlyphAtlas preview_scroll_atlas;
    char preview_scroll_atlas_font[64];
    float preview_scroll_atlas_size;
    
    // Shader modal state
    ShaderCue editing_shader;
    bool shader_modal_open;
    bool shader_modal_request_open;  // Set to true to request opening the modal
    bool shader_modal_asset_mode;
    int shader_modal_asset_cue_type;
    int shader_modal_asset_index;
    AssetShader editing_asset_shader;
    
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

    // Animated sprite modal state
    AnimatedSpriteCue editing_animated_sprite;
    bool animated_sprite_modal_open;
    bool animated_sprite_modal_request_open;

    PixelCue editing_pixel;
    bool pixel_modal_open;
    bool pixel_modal_request_open;

    PixelEmitterCue editing_pixel_emitter;
    bool pixel_emitter_modal_open;
    bool pixel_emitter_modal_request_open;

    // Scroll text modal state
    ScrollTextCue editing_scroll_text;
    bool scroll_text_modal_open;
    bool scroll_text_modal_request_open;
    
    // Installed Windows fonts (for font picker)
    char** installed_fonts;
    int installed_font_count;

    // Mesh modal state
    MeshCue editing_mesh;
    bool mesh_modal_open;
    bool mesh_modal_request_open;

    // Preview mesh cache — avoids reloading glTF every frame.
    // Keyed by asset_path; evicted when the path changes or project reloads.
    static const int kMeshCacheSize = 16;
    struct MeshCacheEntry {
        char     path[512];
        void*    mesh;            // rev::mesh::Mesh*
        uint64_t last_write_time; // File modification timestamp (FILETIME as uint64)
    };
    MeshCacheEntry mesh_cache[kMeshCacheSize];
    int mesh_cache_count;

    // Curve editor state
    int selected_curve_index;
    int dragging_point_index;
    int selected_point_index;          // Currently selected point for editing (persists after drag)
    bool show_curve_grid;
    
    // Curve editor modal state
    bool curve_editor_modal_open;
    bool curve_editor_modal_request_open;
    bool point_properties_modal_open;
    int editing_curve_index;           // Index of curve being edited
    int editing_curve_cue_type;        // See CueType (music has no curve slots)
    int editing_curve_field;           // Which field (custom per cue type)
    char editing_curve_label[64];     // Display name (e.g., "Image X Position")
    
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
void RenderTriggerRecorder(EditorContext* editor);
void RenderCurveEditorModal(EditorContext* editor);
void RenderShaderModal(EditorContext* editor);
void RenderMusicModal(EditorContext* editor);
void RenderImageModal(EditorContext* editor);
void RenderAnimatedSpriteModal(EditorContext* editor);
void RenderPixelModal(EditorContext* editor);
void RenderPixelEmitterModal(EditorContext* editor);
void RenderTextModal (EditorContext* editor);
void RenderScrollTextModal(EditorContext* editor);
void RenderMeshModal (EditorContext* editor);
void RenderProperties(EditorContext* editor);
void RenderAssetBrowser(EditorContext* editor);
void RenderPreviewPanel(EditorContext* editor);

// Preview viewport
void InitializePreview(EditorContext* editor, int width, int height);
void CleanupPreview(EditorContext* editor);
void ResizePreview(EditorContext* editor, int width, int height);
void RenderPreviewFrame(EditorContext* editor);
void UpdatePlayback(EditorContext* editor, float delta_time);
void ReloadEditorAssets(EditorContext* editor);

// Scene management
int AddScene(EditorContext* editor, const char* name, float duration);
void DeleteScene(EditorContext* editor, int scene_index);
void MoveScene(EditorContext* editor, int from_index, int to_index);
SceneBlock* GetScene(EditorContext* editor, int scene_index);

// Cue management
int AddShaderCue(SceneBlock* scene, const ShaderCue& cue);
int AddImageCue (SceneBlock* scene, const ImageCue&  cue);
int AddAnimatedSpriteCue(SceneBlock* scene, const AnimatedSpriteCue& cue);
int AddPixelCue(SceneBlock* scene, const PixelCue& cue);
int AddPixelEmitterCue(SceneBlock* scene, const PixelEmitterCue& cue);
int AddTextCue  (SceneBlock* scene, const TextCue&   cue);
int AddScrollTextCue(SceneBlock* scene, const ScrollTextCue& cue);
int AddMusicCue (SceneBlock* scene, const MusicCue&  cue);
int AddMeshCue  (SceneBlock* scene, const MeshCue&   cue);
int AddPostEffect(SceneBlock* scene, const PostEffect& effect);
void DeletePostEffect(SceneBlock* scene, int effect_index);

void DeleteShaderCue(SceneBlock* scene, int cue_index);
void DeleteImageCue (SceneBlock* scene, int cue_index);
void DeleteAnimatedSpriteCue(SceneBlock* scene, int cue_index);
void DeletePixelCue(SceneBlock* scene, int cue_index);
void DeletePixelEmitterCue(SceneBlock* scene, int cue_index);
void DeleteTextCue  (SceneBlock* scene, int cue_index);
void DeleteScrollTextCue(SceneBlock* scene, int cue_index);
void DeleteMusicCue (SceneBlock* scene, int cue_index);
void DeleteMeshCue  (SceneBlock* scene, int cue_index);

// Shader presets
void LoadShaderPreset(ShaderCue* cue, int preset_id);
void RandomizeShaderColors(ShaderCue* cue);
void RandomizeShaderValues(ShaderCue* cue);
void ResetShaderValues(ShaderCue* cue);

// Build integration
bool ExportProject(EditorContext* editor, const char* output_path);
bool BuildAndRun(EditorContext* editor);
// Export cues and pack referenced assets without invoking a compiler.
bool PackProject(EditorContext* editor);
// Pack assets into the exe (checksum-based, only re-packs changed files), then build and run.
bool PackBuildAndRun(EditorContext* editor);
// Build the packed runtime and copy it as a Windows screen saver (.scr).
bool BuildScreenSaver(EditorContext* editor);

}  // namespace editor
}  // namespace rev
