#include "rev_editor.h"
#include "rev_shader.h"
#include "rev_pack.h"
#include "rev_mesh.h"
#include "rev_gltf.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <windows.h>
#include <gl/gl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

// OpenGL constants not in gl.h
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// NOTE: This file requires Dear ImGui to be fully functional
// See revision_libs/rev_editor/README.md for setup instructions

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

// Forward declare the ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Message callback for rev_platform
static long long ImGuiMessageCallback(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
    return ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wparam, (LPARAM)lparam);
}

// Shader presets
struct ShaderPreset {
    int id;
    const char* name;
    const char* description;
};

static const ShaderPreset g_shader_presets[] = {
    {0, "Plasma Vibrant", "Colorful plasma effect with smooth gradients"},
    {1, "Tunnel Neon", "3D tunnel with neon lighting"},
    {2, "Raymarcher SDF", "Raymarched signed distance fields"},
    {3, "Fractal Mandelbrot", "Classic Mandelbrot fractal zoom"},
    {4, "Voronoi Cells", "Cellular voronoi patterns"},
    {5, "Wave Distortion", "Sine wave distortion field"},
    {6, "Particle System", "GPU particle simulation"},
    {7, "Starfield", "3D starfield with motion blur"},
    {8, "Glow Orbs", "Glowing orb metaballs"},
    {9, "Matrix Rain", "Matrix-style digital rain"},
};

static const int g_shader_preset_count = sizeof(g_shader_presets) / sizeof(g_shader_presets[0]);

namespace rev {
namespace editor {

EditorContext* CreateEditor(rev::platform::Window* window) {
    EditorContext* editor = new EditorContext();
    editor->window = window;
    editor->imgui_context = nullptr;
    editor->project = nullptr;
    editor->show_timeline = true;
    editor->show_curve_editor = true;
    editor->show_properties = true;
    editor->show_asset_browser = false;
    editor->show_demo = false;
    editor->timeline_zoom = 1.0f;
    editor->timeline_scroll = 0.0f;
    editor->selected_scene_index = -1;
    editor->selected_cue_index = -1;
    editor->selected_cue_type = 0;
    editor->current_time = 0.0f;
    editor->playing = false;
    editor->show_preview = true;
    editor->preview_fbo = 0;
    editor->preview_texture = 0;
    editor->preview_depth = 0;
    editor->preview_vao = 0;
    editor->preview_width = 1920;
    editor->preview_height = 1080;
    editor->preview_initialized = false;
    editor->preview_shader = nullptr;
    editor->sprite_shader = nullptr;
    editor->preview_current_shader_id = -1;
    editor->shader_modal_open = false;
    editor->shader_modal_request_open = false;
    editor->music_modal_open = false;
    editor->music_modal_request_open = false;
    editor->image_modal_open = false;
    editor->image_modal_request_open = false;
    editor->text_modal_open = false;
    editor->text_modal_request_open = false;
    editor->mesh_modal_open = false;
    editor->mesh_modal_request_open = false;
    memset(&editor->editing_mesh, 0, sizeof(editor->editing_mesh));
    editor->editing_mesh.scale[0] = editor->editing_mesh.scale[1] = editor->editing_mesh.scale[2] = 1.0f;
    editor->mesh_shader = nullptr;
    editor->selected_curve_index = -1;
    editor->dragging_point_index = -1;
    editor->show_curve_grid = true;
    editor->build_status_message[0] = '\0';
    editor->build_status_timer = 0.0f;

    // Capture startup working directory before any file dialog can mutate it
    GetCurrentDirectoryA(sizeof(editor->startup_dir), editor->startup_dir);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(window->hwnd);
    ImGui_ImplOpenGL3_Init("#version 330");
    
    // Register message callback for ImGui input handling
    rev::platform::SetMessageCallback(window, ImGuiMessageCallback);
    
    // Style
    ImGui::StyleColorsDark();
    
    // Create empty project
    editor->project = new ProjectData();
    editor->project->scenes = nullptr;
    editor->project->scene_count = 0;
    editor->project->scene_capacity = 0;
    
    // Allocate fixed curve array (max 32 curves)
    editor->project->curves = new rev::curve::Curve[32];
    for (int i = 0; i < 32; ++i) {
        editor->project->curves[i].points = nullptr;
        editor->project->curves[i].point_count = 0;
        editor->project->curves[i].capacity = 0;
    }
    editor->project->curve_count = 0;
    
    editor->project->modified = false;
    editor->project->total_duration = 0.0f;
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    memset(editor->project->workspace_path, 0, sizeof(editor->project->workspace_path));
    memset(editor->project->assets_path, 0, sizeof(editor->project->assets_path));
    
    return editor;
}

void DestroyEditor(EditorContext* editor) {
    if (!editor) return;
    
    // Unregister message callback
    rev::platform::SetMessageCallback(editor->window, nullptr);
    
    // Cleanup project
    if (editor->project) {
        // Clean up scenes
        for (int i = 0; i < editor->project->scene_count; ++i) {
            SceneBlock* scene = &editor->project->scenes[i];
            delete[] scene->shader_cues;
            delete[] scene->image_cues;
            delete[] scene->text_cues;
            delete[] scene->music_cues;
            delete[] scene->mesh_cues;
        }
        delete[] editor->project->scenes;
        
        // Clean up curves
        for (int i = 0; i < editor->project->curve_count; ++i) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
        }
        delete[] editor->project->curves;
        
        delete editor->project;
    }
    
    // Cleanup preview
    CleanupPreview(editor);
    
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    delete editor;
}

bool LoadProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return false;
    
    // Clear existing project
    NewProject(editor);
    
    char line[1024];
    SceneBlock* current_scene = nullptr;
    bool in_scenes = false;
    bool in_shader_cues = false;
    bool in_image_cues = false;
    bool in_text_cues = false;
    bool in_music_cues = false;
    bool in_mesh_cues = false;
    bool in_curves = false;
    bool in_curve_points = false;
    
    ShaderCue current_shader_cue = {};
    ImageCue current_image_cue = {};
    TextCue current_text_cue = {};
    MusicCue current_music_cue = {};
    MeshCue current_mesh_cue = {};
    current_mesh_cue.scale[0] = current_mesh_cue.scale[1] = current_mesh_cue.scale[2] = 1.0f;    rev::curve::Curve* current_curve = nullptr;
    
    char scene_name[64] = {};
    float scene_duration = 0.0f;
    bool has_name = false;
    bool has_duration = false;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
        
        // Detect sections
        if (strstr(start, "\"scenes\":")) {
            in_scenes = true;
            continue;
        }
        if (strstr(start, "\"curves\":")) {
            in_scenes = false;
            in_curves = true;
            continue;
        }
        
        // Scene name and duration detection (inside scenes array)
        if (in_scenes && strstr(start, "\"name\":")) {
            if (sscanf_s(start, "\"name\": \"%63[^\"]\"", scene_name, (unsigned)sizeof(scene_name)) == 1) {
                has_name = true;
            }
        }
        if (in_scenes && strstr(start, "\"duration\":")) {
            if (sscanf_s(start, "\"duration\": %f", &scene_duration) == 1) {
                has_duration = true;
            }
        }
        
        // When we have both name and duration, create the scene
        if (in_scenes && has_name && has_duration) {
            int idx = AddScene(editor, scene_name, scene_duration);
            current_scene = GetScene(editor, idx);
            has_name = false;
            has_duration = false;
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = false;
        }
        
        // Section detection
        if (strstr(start, "\"shader_cues\":")) {
            in_shader_cues = true;
            in_image_cues = false;
            in_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"image_cues\":")) {
            in_shader_cues = false;
            in_image_cues = true;
            in_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"text_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = true;
            in_music_cues = false;
        } else if (strstr(start, "\"music_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_music_cues = true;
            in_mesh_cues = false;
        } else if (strstr(start, "\"mesh_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = true;
        } else if (strstr(start, "\"curves\":")) {
            in_curves = true;
        }
        
        // Parse shader cue fields
        if (in_shader_cues && current_scene) {
            if (strstr(start, "\"shader_name\":")) {
                sscanf_s(start, "\"shader_name\": \"%63[^\"]\"", current_shader_cue.shader_name, (unsigned)sizeof(current_shader_cue.shader_name));
            } else if (strstr(start, "\"shader_scene_id\":")) {
                sscanf_s(start, "\"shader_scene_id\": %d", &current_shader_cue.shader_scene_id);
            } else if (strstr(start, "\"palette_low\":")) {
                sscanf_s(start, "\"palette_low\": [%f, %f, %f]",
                    &current_shader_cue.palette_low.r,
                    &current_shader_cue.palette_low.g,
                    &current_shader_cue.palette_low.b);
            } else if (strstr(start, "\"palette_mid\":")) {
                sscanf_s(start, "\"palette_mid\": [%f, %f, %f]",
                    &current_shader_cue.palette_mid.r,
                    &current_shader_cue.palette_mid.g,
                    &current_shader_cue.palette_mid.b);
            } else if (strstr(start, "\"palette_high\":")) {
                sscanf_s(start, "\"palette_high\": [%f, %f, %f]",
                    &current_shader_cue.palette_high.r,
                    &current_shader_cue.palette_high.g,
                    &current_shader_cue.palette_high.b);
            } else if (strstr(start, "\"speed\":")) {
                sscanf_s(start, "\"speed\": %f", &current_shader_cue.speed);
            } else if (strstr(start, "\"intensity\":")) {
                sscanf_s(start, "\"intensity\": %f", &current_shader_cue.intensity);
            } else if (strstr(start, "\"warp\":")) {
                sscanf_s(start, "\"warp\": %f", &current_shader_cue.warp);
            } else if (strstr(start, "\"exposure_base\":")) {
                sscanf_s(start, "\"exposure_base\": %f", &current_shader_cue.exposure_base);
            } else if (strstr(start, "\"exposure_ramp\":")) {
                sscanf_s(start, "\"exposure_ramp\": %f", &current_shader_cue.exposure_ramp);
            } else if (strstr(start, "\"fade_base\":")) {
                sscanf_s(start, "\"fade_base\": %f", &current_shader_cue.fade_base);
            } else if (strstr(start, "\"fade_ramp\":")) {
                sscanf_s(start, "\"fade_ramp\": %f", &current_shader_cue.fade_ramp);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_shader_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_shader_cue.cue_end);
            } else if (strstr(start, "\"fade_in\":")) {
                sscanf_s(start, "\"fade_in\": %f", &current_shader_cue.fade_in);
            } else if (strstr(start, "\"fade_out\":")) {
                sscanf_s(start, "\"fade_out\": %f", &current_shader_cue.fade_out);
            } else if (strstr(start, "\"layer_role\":")) {
                sscanf_s(start, "\"layer_role\": %d", &current_shader_cue.layer_role);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_shader_cue.opacity);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_shader_cue.blend_mode);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_shader_cue.layer_order);
            } else if (strstr(start, "\"curve_speed\":")) {
                sscanf_s(start, "\"curve_speed\": %d", &current_shader_cue.curve_speed);
            } else if (strstr(start, "\"curve_intensity\":")) {
                sscanf_s(start, "\"curve_intensity\": %d", &current_shader_cue.curve_intensity);
            } else if (strstr(start, "\"curve_warp\":")) {
                sscanf_s(start, "\"curve_warp\": %d", &current_shader_cue.curve_warp);
            } else if (strstr(start, "\"curve_exposure\":")) {
                sscanf_s(start, "\"curve_exposure\": %d", &current_shader_cue.curve_exposure);
            } else if (strstr(start, "\"curve_fade\":")) {
                sscanf_s(start, "\"curve_fade\": %d", &current_shader_cue.curve_fade);
            } else if (start[0] == '}' && current_shader_cue.shader_name[0] != '\0') {
                // End of shader cue object - add it
                printf("[LoadProject] Loaded shader cue: name='%s' id=%d start=%.2f end=%.2f\n",
                       current_shader_cue.shader_name, current_shader_cue.shader_scene_id,
                       current_shader_cue.cue_start, current_shader_cue.cue_end);
                AddShaderCue(current_scene, current_shader_cue);
                memset(&current_shader_cue, 0, sizeof(current_shader_cue));
            }
        }
        
        // Parse image cue fields
        if (in_image_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                sscanf_s(start, "\"asset_key\": \"%127[^\"]\"", current_image_cue.asset_key, (unsigned)sizeof(current_image_cue.asset_key));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_image_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_image_cue.y);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": %f", &current_image_cue.scale);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_image_cue.opacity);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_image_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_image_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_image_cue.cue_end);
            } else if (strstr(start, "\"effect_start\":")) {
                sscanf_s(start, "\"effect_start\": %f", &current_image_cue.fade_in_start);
            } else if (strstr(start, "\"effect_end\":")) {
                sscanf_s(start, "\"effect_end\": %f", &current_image_cue.fade_in_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_image_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_image_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_image_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_image_cue.fade_out_end);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_image_cue.layer_order);
            } else if (start[0] == '}' && current_image_cue.asset_key[0] != '\0') {
                // End of image cue object - add it
                printf("[LoadProject] Loaded image cue: %s pos=(%.2f,%.2f) scale=%.2f\n",
                       current_image_cue.asset_key, current_image_cue.x, current_image_cue.y, current_image_cue.scale);
                AddImageCue(current_scene, current_image_cue);
                memset(&current_image_cue, 0, sizeof(current_image_cue));
            }
        }

        // Parse text cue fields
        if (in_text_cues && current_scene) {
            if (strstr(start, "\"text\":")) {
                sscanf_s(start, "\"text\": \"%255[^\"]\"", current_text_cue.text, (unsigned)sizeof(current_text_cue.text));
            } else if (strstr(start, "\"font_name\":")) {
                sscanf_s(start, "\"font_name\": \"%63[^\"]\"", current_text_cue.font_name, (unsigned)sizeof(current_text_cue.font_name));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_text_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_text_cue.y);
            } else if (strstr(start, "\"size\":")) {
                sscanf_s(start, "\"size\": %f", &current_text_cue.size);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f]",
                    &current_text_cue.color.r, &current_text_cue.color.g, &current_text_cue.color.b);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_text_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_text_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_text_cue.cue_end);
            } else if (strstr(start, "\"effect_start\":")) {
                sscanf_s(start, "\"effect_start\": %f", &current_text_cue.fade_in_start);
            } else if (strstr(start, "\"effect_end\":")) {
                sscanf_s(start, "\"effect_end\": %f", &current_text_cue.fade_in_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_text_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_text_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_text_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_text_cue.fade_out_end);
            } else if (start[0] == '}' && current_text_cue.text[0] != '\0') {
                AddTextCue(current_scene, current_text_cue);
                memset(&current_text_cue, 0, sizeof(current_text_cue));
            }
        }

        // Parse music cue fields
        if (in_music_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                sscanf_s(start, "\"asset_key\": \"%63[^\"]\"", current_music_cue.asset_key, (unsigned)sizeof(current_music_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                sscanf_s(start, "\"asset_path\": \"%511[^\"]\"", current_music_cue.asset_path, (unsigned)sizeof(current_music_cue.asset_path));
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_music_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_music_cue.cue_end);
            } else if (start[0] == '}' && current_music_cue.asset_key[0] != '\0') {
                printf("[LoadProject] Loaded music cue: %s path=%s\n",
                       current_music_cue.asset_key, current_music_cue.asset_path);
                AddMusicCue(current_scene, current_music_cue);
                memset(&current_music_cue, 0, sizeof(current_music_cue));
            }
        }

        // Parse mesh cue fields
        if (in_mesh_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                sscanf_s(start, "\"asset_key\": \"%63[^\"]\"", current_mesh_cue.asset_key, (unsigned)sizeof(current_mesh_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                sscanf_s(start, "\"asset_path\": \"%511[^\"]\"", current_mesh_cue.asset_path, (unsigned)sizeof(current_mesh_cue.asset_path));
            } else if (strstr(start, "\"mesh_type\":")) {
                sscanf_s(start, "\"mesh_type\": %d", &current_mesh_cue.mesh_type);
            } else if (strstr(start, "\"pos\":")) {
                sscanf_s(start, "\"pos\": [%f, %f, %f]", &current_mesh_cue.pos[0], &current_mesh_cue.pos[1], &current_mesh_cue.pos[2]);
            } else if (strstr(start, "\"rot\":")) {
                sscanf_s(start, "\"rot\": [%f, %f, %f]", &current_mesh_cue.rot[0], &current_mesh_cue.rot[1], &current_mesh_cue.rot[2]);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": [%f, %f, %f]", &current_mesh_cue.scale[0], &current_mesh_cue.scale[1], &current_mesh_cue.scale[2]);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f, %f]", &current_mesh_cue.color[0], &current_mesh_cue.color[1], &current_mesh_cue.color[2], &current_mesh_cue.color[3]);
            } else if (strstr(start, "\"mesh_size\":")) {
                sscanf_s(start, "\"mesh_size\": %f", &current_mesh_cue.mesh_size);
            } else if (strstr(start, "\"mesh_param\":")) {
                sscanf_s(start, "\"mesh_param\": %f", &current_mesh_cue.mesh_param);
            } else if (strstr(start, "\"metallic\":")) {
                sscanf_s(start, "\"metallic\": %f", &current_mesh_cue.metallic);
            } else if (strstr(start, "\"roughness\":")) {
                sscanf_s(start, "\"roughness\": %f", &current_mesh_cue.roughness);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_mesh_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_mesh_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_mesh_cue.cue_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_mesh_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_mesh_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_mesh_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_mesh_cue.fade_out_end);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_mesh_cue.layer_order);
            } else if (start[0] == '}' && current_mesh_cue.asset_key[0] != '\0') {
                AddMeshCue(current_scene, current_mesh_cue);
                MeshCue blank = {};
                blank.scale[0] = blank.scale[1] = blank.scale[2] = 1.0f;
                blank.roughness = 0.5f;
                current_mesh_cue = blank;
            }
        }

        // Parse curve points
        if (in_curves && strstr(start, "\"points\":")) {
            in_curve_points = true;
            if (editor->project->curve_count < 32) {
                current_curve = &editor->project->curves[editor->project->curve_count++];
                *current_curve = rev::curve::CreateCurve();
            }
        } else if (in_curve_points && current_curve && strstr(start, "{\"t\":")) {
            float t, v, in_ease, out_ease;
            char mode[32];
            if (sscanf_s(start, "{\"t\": %f, \"v\": %f, \"in_ease\": %f, \"out_ease\": %f, \"mode\": \"%31[^\"]\"",
                &t, &v, &in_ease, &out_ease, mode, (unsigned)sizeof(mode)) == 5) {
                
                rev::curve::EaseMode ease_mode = rev::curve::EaseMode::Linear;
                if (strcmp(mode, "ease_in") == 0) ease_mode = rev::curve::EaseMode::EaseIn;
                else if (strcmp(mode, "ease_out") == 0) ease_mode = rev::curve::EaseMode::EaseOut;
                else if (strcmp(mode, "ease_in_out") == 0) ease_mode = rev::curve::EaseMode::EaseInOut;
                else if (strcmp(mode, "smoothstep") == 0) ease_mode = rev::curve::EaseMode::Smoothstep;
                else if (strcmp(mode, "hold") == 0) ease_mode = rev::curve::EaseMode::Hold;
                
                rev::curve::AddPoint(*current_curve, t, v, ease_mode);
                current_curve->points[current_curve->point_count - 1].in_ease = in_ease;
                current_curve->points[current_curve->point_count - 1].out_ease = out_ease;
            }
        } else if (in_curve_points && strstr(start, "]")) {
            in_curve_points = false;
            current_curve = nullptr;
        }
    }
    
    fclose(f);
    
    strncpy_s(editor->project->project_path, path, sizeof(editor->project->project_path) - 1);
    
    // Set workspace_path to the directory containing the project file
    strncpy_s(editor->project->workspace_path, path, sizeof(editor->project->workspace_path) - 1);
    // Find last slash/backslash and truncate to get directory
    char* last_slash = strrchr(editor->project->workspace_path, '\\');
    if (!last_slash) last_slash = strrchr(editor->project->workspace_path, '/');
    if (last_slash) *last_slash = '\0';
    
    printf("[LoadProject] Workspace path set to: %s\n", editor->project->workspace_path);
    
    // Create project-specific assets folder path
    // Extract project name from path (filename without extension)
    char project_name[256] = {0};
    const char* filename_start = strrchr(path, '\\');
    if (!filename_start) filename_start = strrchr(path, '/');
    filename_start = filename_start ? filename_start + 1 : path;
    
    // Copy filename and remove extension
    strncpy_s(project_name, filename_start, sizeof(project_name) - 1);
    char* dot = strrchr(project_name, '.');
    if (dot) *dot = '\0';
    
    // Create assets folder path: workspace_path\{project_name}_assets
    snprintf(editor->project->assets_path, sizeof(editor->project->assets_path),
             "%s\\%s_assets", editor->project->workspace_path, project_name);
    
    // Create the directory if it doesn't exist
    CreateDirectoryA(editor->project->assets_path, NULL);
    
    printf("[LoadProject] Assets path set to: %s\n", editor->project->assets_path);
    
    editor->project->modified = false;
    
    return true;
}

bool SaveProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, path, "w");
    if (!f) return false;
    
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"workspace_path\": \"%s\",\n", editor->project->workspace_path);
    fprintf(f, "  \"total_duration\": %.3f,\n", editor->project->total_duration);
    fprintf(f, "  \"scenes\": [\n");
    
    // Save scenes
    for (int s = 0; s < editor->project->scene_count; ++s) {
        SceneBlock* scene = &editor->project->scenes[s];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", scene->name);
        fprintf(f, "      \"duration\": %.3f,\n", scene->duration);
        
        // Shader cues
        fprintf(f, "      \"shader_cues\": [\n");
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            fprintf(f, "        {\n");
            fprintf(f, "          \"shader_name\": \"%s\",\n", cue->shader_name);
            fprintf(f, "          \"shader_scene_id\": %d,\n", cue->shader_scene_id);
            fprintf(f, "          \"palette_low\": [%.3f, %.3f, %.3f],\n", 
                cue->palette_low.r, cue->palette_low.g, cue->palette_low.b);
            fprintf(f, "          \"palette_mid\": [%.3f, %.3f, %.3f],\n",
                cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b);
            fprintf(f, "          \"palette_high\": [%.3f, %.3f, %.3f],\n",
                cue->palette_high.r, cue->palette_high.g, cue->palette_high.b);
            fprintf(f, "          \"speed\": %.3f,\n", cue->speed);
            fprintf(f, "          \"intensity\": %.3f,\n", cue->intensity);
            fprintf(f, "          \"warp\": %.3f,\n", cue->warp);
            fprintf(f, "          \"exposure_base\": %.3f,\n", cue->exposure_base);
            fprintf(f, "          \"exposure_ramp\": %.3f,\n", cue->exposure_ramp);
            fprintf(f, "          \"fade_base\": %.3f,\n", cue->fade_base);
            fprintf(f, "          \"fade_ramp\": %.3f,\n", cue->fade_ramp);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in\": %.3f,\n", cue->fade_in);
            fprintf(f, "          \"fade_out\": %.3f,\n", cue->fade_out);
            fprintf(f, "          \"layer_role\": %d,\n", cue->layer_role);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"curve_speed\": %d,\n", cue->curve_speed);
            fprintf(f, "          \"curve_intensity\": %d,\n", cue->curve_intensity);
            fprintf(f, "          \"curve_warp\": %d,\n", cue->curve_warp);
            fprintf(f, "          \"curve_exposure\": %d,\n", cue->curve_exposure);
            fprintf(f, "          \"curve_fade\": %d\n", cue->curve_fade);
            fprintf(f, "        }%s\n", (i < scene->shader_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Image cues
        fprintf(f, "      \"image_cues\": [\n");
        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", cue->asset_key);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"scale\": %.3f,\n", cue->scale);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f\n", cue->fade_out_end);
            fprintf(f, "        }%s\n", (i < scene->image_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Text cues
        fprintf(f, "      \"text_cues\": [\n");
        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            fprintf(f, "        {\n");
            fprintf(f, "          \"text\": \"%s\",\n", cue->text);
            fprintf(f, "          \"font_name\": \"%s\",\n", cue->font_name);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"size\": %.3f,\n", cue->size);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f],\n",
                cue->color.r, cue->color.g, cue->color.b);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f\n", cue->fade_out_end);
            fprintf(f, "        }%s\n", (i < scene->text_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Music cues
        fprintf(f, "      \"music_cues\": [\n");
        for (int i = 0; i < scene->music_cue_count; ++i) {
            MusicCue* cue = &scene->music_cues[i];
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", cue->asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n", cue->asset_path);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f\n", cue->cue_end);
            fprintf(f, "        }%s\n", (i < scene->music_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Mesh cues
        fprintf(f, "      \"mesh_cues\": [\n");
        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n",   cue->asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n",  cue->asset_path);
            fprintf(f, "          \"mesh_type\": %d,\n",        cue->mesh_type);
            fprintf(f, "          \"pos\": [%.3f, %.3f, %.3f],\n",   cue->pos[0],   cue->pos[1],   cue->pos[2]);
            fprintf(f, "          \"rot\": [%.3f, %.3f, %.3f],\n",   cue->rot[0],   cue->rot[1],   cue->rot[2]);
            fprintf(f, "          \"scale\": [%.3f, %.3f, %.3f],\n", cue->scale[0], cue->scale[1], cue->scale[2]);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f, %.3f],\n", cue->color[0], cue->color[1], cue->color[2], cue->color[3]);
            fprintf(f, "          \"mesh_size\": %.3f,\n",  cue->mesh_size);
            fprintf(f, "          \"mesh_param\": %.3f,\n", cue->mesh_param);
            fprintf(f, "          \"metallic\": %.3f,\n",   cue->metallic);
            fprintf(f, "          \"roughness\": %.3f,\n",  cue->roughness);
            fprintf(f, "          \"effect_type\": %d,\n",  cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n",  cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n",    cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n",  cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n",    cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n",   cue->fade_out_end);
            fprintf(f, "          \"layer_order\": %d\n",   cue->layer_order);
            fprintf(f, "        }%s\n", (i < scene->mesh_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ]\n");
        
        fprintf(f, "    }%s\n", (s < editor->project->scene_count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n");
    
    // Save curves
    fprintf(f, "  \"curves\": [\n");
    for (int c = 0; c < editor->project->curve_count; ++c) {
        rev::curve::Curve* curve = &editor->project->curves[c];
        fprintf(f, "    {\n");
        fprintf(f, "      \"points\": [\n");
        
        for (int p = 0; p < curve->point_count; ++p) {
            rev::curve::Point* pt = &curve->points[p];
            
            const char* mode_str = "linear";
            switch (pt->mode) {
                case rev::curve::EaseMode::Linear: mode_str = "linear"; break;
                case rev::curve::EaseMode::EaseIn: mode_str = "ease_in"; break;
                case rev::curve::EaseMode::EaseOut: mode_str = "ease_out"; break;
                case rev::curve::EaseMode::EaseInOut: mode_str = "ease_in_out"; break;
                case rev::curve::EaseMode::Smoothstep: mode_str = "smoothstep"; break;
                case rev::curve::EaseMode::Hold: mode_str = "hold"; break;
            }
            
            fprintf(f, "        {\"t\": %.3f, \"v\": %.3f, \"in_ease\": %.3f, \"out_ease\": %.3f, \"mode\": \"%s\"}%s\n",
                pt->t, pt->v, pt->in_ease, pt->out_ease, mode_str,
                (p < curve->point_count - 1) ? "," : ""
            );
        }
        
        fprintf(f, "      ]\n");
        fprintf(f, "    }%s\n", (c < editor->project->curve_count - 1) ? "," : "");
    }
    fprintf(f, "  ]\n");
    
    fprintf(f, "}\n");
    
    fclose(f);
    
    strncpy_s(editor->project->project_path, path, sizeof(editor->project->project_path) - 1);

    // Derive workspace_path (directory of the project file)
    strncpy_s(editor->project->workspace_path, path, sizeof(editor->project->workspace_path) - 1);
    char* last_slash = strrchr(editor->project->workspace_path, '\\');
    if (!last_slash) last_slash = strrchr(editor->project->workspace_path, '/');
    if (last_slash) *last_slash = '\0';

    // Derive assets_path: {workspace}\{project_name}_assets
    char project_name[256] = {};
    const char* fn = strrchr(path, '\\');
    if (!fn) fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    strncpy_s(project_name, fn, sizeof(project_name) - 1);
    char* dot = strrchr(project_name, '.');
    if (dot) *dot = '\0';
    snprintf(editor->project->assets_path, sizeof(editor->project->assets_path),
             "%s\\%s_assets", editor->project->workspace_path, project_name);
    CreateDirectoryA(editor->project->assets_path, NULL);

    editor->project->modified = false;
    
    return true;
}

bool NewProject(EditorContext* editor) {
    if (!editor) return false;
    
    // Clean up existing scenes
    for (int i = 0; i < editor->project->scene_count; ++i) {
        SceneBlock* scene = &editor->project->scenes[i];
        delete[] scene->shader_cues;
        delete[] scene->image_cues;
        delete[] scene->text_cues;
        delete[] scene->music_cues;
        delete[] scene->mesh_cues;
    }
    delete[] editor->project->scenes;
    editor->project->scenes = nullptr;
    editor->project->scene_count = 0;
    editor->project->scene_capacity = 0;
    
    // Clean up curves
    if (editor->project->curves) {
        for (int i = 0; i < editor->project->curve_count; ++i) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
        }
        editor->project->curve_count = 0;
    }
    
    editor->project->total_duration = 0.0f;
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    memset(editor->project->workspace_path, 0, sizeof(editor->project->workspace_path));
    memset(editor->project->assets_path, 0, sizeof(editor->project->assets_path));
    editor->project->modified = false;
    
    return true;
}

void BeginFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame start
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    // Update playback
    ImGuiIO& io = ImGui::GetIO();
    UpdatePlayback(editor, io.DeltaTime);
    
    // Render preview frame
    if (editor->show_preview && editor->preview_initialized) {
        RenderPreviewFrame(editor);
    }
}

void RenderUI(EditorContext* editor) {
    if (!editor) return;
    
    // Create dockspace over main viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);
    
    // Create the actual dockspace
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    
    ImGui::End();
    
    RenderMenuBar(editor);
    
    // Handle keyboard shortcuts
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        // Ctrl+S - Save
        const char* path = (editor->project->project_path[0] != '\0') 
            ? editor->project->project_path 
            : "project.json";
        if (SaveProject(editor, path)) {
            strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                     "Project saved successfully!", _TRUNCATE);
            editor->build_status_timer = 3.0f;
        }
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        // Ctrl+O - Open with dialog
        OPENFILENAMEA ofn = {};
        char filepath[260] = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (HWND)editor->window->hwnd;
        ofn.lpstrFile = filepath;
        ofn.nMaxFile = sizeof(filepath);
        ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        
        if (GetOpenFileNameA(&ofn)) {
            if (LoadProject(editor, filepath)) {
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                         "Project loaded!", _TRUNCATE);
                editor->build_status_timer = 3.0f;
            }
        }
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        // Ctrl+N - New
        NewProject(editor);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                 "New project created", _TRUNCATE);
        editor->build_status_timer = 3.0f;
    }
    
    if (editor->show_timeline) {
        RenderTimeline(editor);
    }
    
    if (editor->show_properties) {
        RenderProperties(editor);
    }
    
    if (editor->show_curve_editor) {
        RenderCurveEditor(editor);
    }
    
    if (editor->show_asset_browser) {
        RenderAssetBrowser(editor);
    }
    
    if (editor->shader_modal_open || editor->shader_modal_request_open) {
        RenderShaderModal(editor);
    }
    
    if (editor->music_modal_open || editor->music_modal_request_open) {
        RenderMusicModal(editor);
    }
    
    if (editor->image_modal_open || editor->image_modal_request_open) {
        RenderImageModal(editor);
    }
    
    if (editor->text_modal_open || editor->text_modal_request_open) {
        RenderTextModal(editor);
    }

    if (editor->mesh_modal_open || editor->mesh_modal_request_open) {
        RenderMeshModal(editor);
    }
    
    if (editor->show_preview) {
        RenderPreviewPanel(editor);
    }
    
    if (editor->show_demo) {
        // ImGui::ShowDemoWindow(&editor->show_demo);  // Requires imgui_demo.cpp
    }
    
    // Show build status notification
    if (editor->build_status_timer > 0.0f) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 window_pos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 50.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.9f);
        
        if (ImGui::Begin("BuildStatus", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | 
            ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", editor->build_status_message);
            ImGui::End();
        }
        
        editor->build_status_timer -= io.DeltaTime;
    }
}

void EndFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame end and render
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Returns the workspace-root-relative path for the cues file next to the project:
//   intros/testintro/cues.txt
// Returns false if the project hasn't been saved yet (no workspace_path).
static bool GetProjectCuesPath(EditorContext* editor, char* out, size_t out_size) {
    if (!editor->project->workspace_path[0] || !editor->project->project_path[0])
        return false;
    // Use startup_dir (not GetCurrentDirectoryA) so Windows file-dialog CWD
    // mutations cannot corrupt the result.
    const char* cwd = editor->startup_dir;
    size_t cwd_len = strlen(cwd);
    const char* wp = editor->project->workspace_path;
    char rel_dir[512] = {};
    if (cwd_len > 0 && _strnicmp(wp, cwd, cwd_len) == 0 &&
        (wp[cwd_len] == '\\' || wp[cwd_len] == '/' || wp[cwd_len] == '\0')) {
        if (wp[cwd_len] == '\0')
            strncpy_s(rel_dir, ".", sizeof(rel_dir) - 1);
        else
            strncpy_s(rel_dir, wp + cwd_len + 1, sizeof(rel_dir) - 1);
    } else {
        strncpy_s(rel_dir, wp, sizeof(rel_dir) - 1);
    }
    for (char* p = rel_dir; *p; ++p) if (*p == '\\') *p = '/';
    snprintf(out, out_size, "%s/cues.txt", rel_dir);
    return true;
}

void RenderMenuBar(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) { 
                NewProject(editor); 
            }
            if (ImGui::MenuItem("Open", "Ctrl+O")) { 
                // Win32 file dialog
                OPENFILENAMEA ofn = {};
                char filepath[260] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = (HWND)editor->window->hwnd;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                
                if (GetOpenFileNameA(&ofn)) {
                    if (LoadProject(editor, filepath)) {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Project loaded!", _TRUNCATE);
                        editor->build_status_timer = 3.0f;
                    } else {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Load failed!", _TRUNCATE);
                        editor->build_status_timer = 5.0f;
                    }
                }
            }
            if (ImGui::MenuItem("Import from cues.txt")) {
                // Win32 file dialog for cues.txt
                OPENFILENAMEA ofn = {};
                char filepath[260] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = (HWND)editor->window->hwnd;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "Cues Files\0*.txt\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                
                if (GetOpenFileNameA(&ofn)) {
                    if (ImportFromCues(editor, filepath)) {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Imported from cues.txt!", _TRUNCATE);
                        editor->build_status_timer = 3.0f;
                    } else {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Import failed!", _TRUNCATE);
                        editor->build_status_timer = 5.0f;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) { 
                const char* path = (editor->project->project_path[0] != '\0') 
                    ? editor->project->project_path 
                    : "project.json";
                if (SaveProject(editor, path)) {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                             "Project saved successfully!", _TRUNCATE);
                    editor->build_status_timer = 3.0f;
                } else {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                             "Failed to save project!", _TRUNCATE);
                    editor->build_status_timer = 5.0f;
                }
            }
            if (ImGui::MenuItem("Save As")) { 
                // Win32 save dialog
                OPENFILENAMEA ofn = {};
                char filepath[260] = "project.json";
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = (HWND)editor->window->hwnd;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrDefExt = "json";
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
                
                if (GetSaveFileNameA(&ofn)) {
                    if (SaveProject(editor, filepath)) {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Saved!", _TRUNCATE);
                        editor->build_status_timer = 3.0f;
                    } else {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Save failed!", _TRUNCATE);
                        editor->build_status_timer = 5.0f;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) { editor->window->should_close = true; }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Timeline", nullptr, &editor->show_timeline);
            ImGui::MenuItem("Properties", nullptr, &editor->show_properties);
            ImGui::MenuItem("Curve Editor", nullptr, &editor->show_curve_editor);
            ImGui::MenuItem("Asset Browser", nullptr, &editor->show_asset_browser);
            ImGui::MenuItem("Preview", nullptr, &editor->show_preview);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &editor->show_demo);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Export Project")) {
                char cues_path[512] = {};
                if (GetProjectCuesPath(editor, cues_path, sizeof(cues_path)))
                    ExportProject(editor, cues_path);
                else {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                             "Save the project first!", _TRUNCATE);
                    editor->build_status_timer = 4.0f;
                }
            }
            if (ImGui::MenuItem("Build and Run", "F5")) { 
                BuildAndRun(editor); 
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Pack, Build and Run")) {
                PackBuildAndRun(editor);
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

void RenderTimeline(EditorContext* editor) {
    if (!editor) return;
    
    if (ImGui::Begin("Timeline", &editor->show_timeline)) {
        // Top controls
        ImGui::Text("Total Duration: %.2fs | Scenes: %d", 
                    editor->project->total_duration, 
                    editor->project->scene_count);
        
        ImGui::SliderFloat("Zoom", &editor->timeline_zoom, 0.1f, 10.0f);
        ImGui::SameLine();
        
        if (ImGui::Button("+ Scene")) {
            AddScene(editor, "New Scene", 10.0f);
        }
        
        ImGui::Separator();
        
        // Scene list
        for (int i = 0; i < editor->project->scene_count; ++i) {
            SceneBlock* scene = &editor->project->scenes[i];
            
            ImGui::PushID(i);
            
            bool selected = (editor->selected_scene_index == i);
            if (ImGui::Selectable(scene->name, selected, 0, ImVec2(0, 0))) {
                editor->selected_scene_index = i;
                editor->selected_cue_index = -1;
            }
            
            ImGui::SameLine();
            ImGui::Text("%.2fs", scene->duration);
            
            // Show cue counts
            if (scene->shader_cue_count > 0 || scene->image_cue_count > 0 || 
                scene->text_cue_count > 0 || scene->music_cue_count > 0) {
                ImGui::Indent();
                if (scene->shader_cue_count > 0) {
                    ImGui::Text("  Shaders: %d", scene->shader_cue_count);
                }
                if (scene->image_cue_count > 0) {
                    ImGui::Text("  Images: %d", scene->image_cue_count);
                }
                if (scene->text_cue_count > 0) {
                    ImGui::Text("  Text: %d", scene->text_cue_count);
                }
                if (scene->music_cue_count > 0) {
                    ImGui::Text("  Music: %d", scene->music_cue_count);
                }
                ImGui::Unindent();
            }
            
            ImGui::PopID();
        }
        
        ImGui::Separator();
        
        // Bottom controls
        if (editor->selected_scene_index >= 0) {
            if (ImGui::Button("Delete Scene")) {
                DeleteScene(editor, editor->selected_scene_index);
                editor->selected_scene_index = -1;
            }
        }
        
        ImGui::End();
    }
}

void RenderProperties(EditorContext* editor) {
    if (!editor) return;
    
    if (ImGui::Begin("Properties", &editor->show_properties)) {
        if (editor->selected_scene_index >= 0 && 
            editor->selected_scene_index < editor->project->scene_count) {
            SceneBlock* scene = &editor->project->scenes[editor->selected_scene_index];
            
            ImGui::Text("Scene: %s", scene->name);
            ImGui::Separator();
            
            // Scene properties
            char name_buf[64];
            strncpy_s(name_buf, sizeof(name_buf), scene->name, _TRUNCATE);
            if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
                strncpy_s(scene->name, sizeof(scene->name), name_buf, _TRUNCATE);
                editor->project->modified = true;
            }
            
            float duration = scene->duration;
            if (ImGui::InputFloat("Duration", &duration, 0.1f, 1.0f)) {
                if (duration > 0.0f) {
                    editor->project->total_duration -= scene->duration;
                    scene->duration = duration;
                    editor->project->total_duration += duration;
                    editor->project->modified = true;
                }
            }
            
            ImGui::Separator();
            
            // Display existing cues
            if (scene->shader_cue_count > 0) {
                ImGui::Text("Shader Cues:");
                for (int i = 0; i < scene->shader_cue_count; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::Button(scene->shader_cues[i].shader_name)) {
                        editor->editing_shader = scene->shader_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = 0;
                        editor->shader_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteShaderCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
                ImGui::Separator();
            }
            
            // Cue actions
            if (ImGui::Button("+ Shader Cue")) {
                ShaderCue cue = {};
                LoadShaderPreset(&cue, 0);  // Default to first preset (Plasma Vibrant)
                int new_index = AddShaderCue(scene, cue);
                // Immediately open modal for the new shader
                editor->editing_shader = scene->shader_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 0;
                editor->shader_modal_request_open = true;
                editor->project->modified = true;
            }
            
            if (ImGui::Button("+ Image Cue")) {
                ImageCue cue = {};
                cue.x = 0.5f;
                cue.y = 0.5f;
                cue.scale = 1.0f;
                cue.opacity = 1.0f;
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                cue.layer_order = 0;
                int new_index = AddImageCue(scene, cue);
                editor->editing_image = scene->image_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 1;  // image
                editor->image_modal_request_open = true;
                editor->project->modified = true;
            }
            
            if (ImGui::Button("+ Text Cue")) {
                TextCue cue = {};
                strncpy_s(cue.text, sizeof(cue.text), "New Text", _TRUNCATE);
                strncpy_s(cue.font_name, sizeof(cue.font_name), "Arial", _TRUNCATE);
                cue.x = 0.5f;
                cue.y = 0.5f;
                cue.size = 48.0f;
                cue.color = {1.0f, 1.0f, 1.0f};
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                cue.layer_order = 0;
                int new_index = AddTextCue(scene, cue);
                editor->editing_text = scene->text_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 2;  // text
                editor->text_modal_request_open = true;
                editor->project->modified = true;
            }
            
            if (ImGui::Button("+ Music Cue")) {
                MusicCue cue = {};
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                int new_index = AddMusicCue(scene, cue);
                editor->editing_music = scene->music_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 3;  // music
                editor->music_modal_request_open = true;
                editor->project->modified = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("+ Mesh Cue")) {
                MeshCue cue = {};
                cue.mesh_type  = 0;
                cue.mesh_size  = 1.0f;
                cue.mesh_param = 16.0f;
                cue.scale[0]   = cue.scale[1] = cue.scale[2] = 1.0f;
                cue.color[0]   = cue.color[1] = cue.color[2] = cue.color[3] = 1.0f;
                cue.metallic   = 0.0f;
                cue.roughness  = 0.5f;
                cue.cue_start  = 0.0f;
                cue.cue_end    = scene->duration;
                snprintf(cue.asset_key, sizeof(cue.asset_key), "mesh_%d", scene->mesh_cue_count);
                int new_index = AddMeshCue(scene, cue);
                editor->editing_mesh = scene->mesh_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 4;
                editor->mesh_modal_request_open = true;
                editor->project->modified = true;
            }
            
            ImGui::Separator();
            
            // Display existing image cues
            if (scene->image_cue_count > 0) {
                ImGui::Text("Image Cues:");
                for (int i = 0; i < scene->image_cue_count; ++i) {
                    ImGui::PushID(2000 + i);  // unique ID offset
                    const char* display_name = scene->image_cues[i].asset_key[0] != '\0' 
                        ? scene->image_cues[i].asset_key 
                        : "(no image)";
                    if (ImGui::Button(display_name)) {
                        editor->editing_image = scene->image_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = 1;
                        editor->image_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteImageCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
            }
            
            // Display existing text cues
            if (scene->text_cue_count > 0) {
                ImGui::Text("Text Cues:");
                for (int i = 0; i < scene->text_cue_count; ++i) {
                    ImGui::PushID(3000 + i);  // unique ID offset
                    const char* display_text = scene->text_cues[i].text[0] != '\0' 
                        ? scene->text_cues[i].text 
                        : "(no text)";
                    if (ImGui::Button(display_text)) {
                        editor->editing_text = scene->text_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = 2;
                        editor->text_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteTextCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
            }
            
            // Display existing music cues
            if (scene->music_cue_count > 0) {
                ImGui::Text("Music Cues:");
                for (int i = 0; i < scene->music_cue_count; ++i) {
                    ImGui::PushID(1000 + i);  // unique ID offset
                    const char* display_name = scene->music_cues[i].asset_key[0] != '\0' 
                        ? scene->music_cues[i].asset_key 
                        : "(no file)";
                    if (ImGui::Button(display_name)) {
                        editor->editing_music = scene->music_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = 3;
                        editor->music_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteMusicCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
            }

            // Display existing mesh cues
            if (scene->mesh_cue_count > 0) {
                static const char* mesh_type_names[] = {"Cube","Sphere","Plane","Torus","glTF"};
                ImGui::Text("Mesh Cues:");
                for (int i = 0; i < scene->mesh_cue_count; ++i) {
                    ImGui::PushID(5000 + i);
                    MeshCue* mc = &scene->mesh_cues[i];
                    const char* type_str = (mc->mesh_type >= 0 && mc->mesh_type < 5)
                        ? mesh_type_names[mc->mesh_type] : "Mesh";
                    char label[80];
                    snprintf(label, sizeof(label), "%s (%s)", mc->asset_key[0] ? mc->asset_key : "mesh", type_str);
                    if (ImGui::Button(label)) {
                        editor->editing_mesh = *mc;
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = 4;
                        editor->mesh_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteMeshCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
            }
        } else {
            ImGui::Text("No scene selected");
            ImGui::Text("Select a scene from the timeline");
        }
        
        ImGui::End();
    }
}

void RenderAssetBrowser(EditorContext* editor) {
    if (!editor) return;
    
    if (ImGui::Begin("Asset Browser", &editor->show_asset_browser)) {
        ImGui::Text("Assets Folder");
        ImGui::Separator();
        
        // Categories
        if (ImGui::CollapsingHeader("Music (.xm)", ImGuiTreeNodeFlags_DefaultOpen)) {
            WIN32_FIND_DATAA find_data;
            HANDLE h_find = FindFirstFileA("assets/*.xm", &find_data);
            
            if (h_find != INVALID_HANDLE_VALUE) {
                do {
                    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (ImGui::Selectable(find_data.cFileName)) {
                            // TODO: Use this file
                        }
                    }
                } while (FindNextFileA(h_find, &find_data));
                FindClose(h_find);
            } else {
                ImGui::TextDisabled("No .xm files found");
            }
        }
        
        if (ImGui::CollapsingHeader("Images (.png, .jpg)", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool found_any = false;
            
            // PNG files
            WIN32_FIND_DATAA find_data;
            HANDLE h_find = FindFirstFileA("assets/*.png", &find_data);
            
            if (h_find != INVALID_HANDLE_VALUE) {
                found_any = true;
                do {
                    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (ImGui::Selectable(find_data.cFileName)) {
                            // TODO: Use this file
                        }
                    }
                } while (FindNextFileA(h_find, &find_data));
                FindClose(h_find);
            }
            
            // JPG files
            h_find = FindFirstFileA("assets/*.jpg", &find_data);
            
            if (h_find != INVALID_HANDLE_VALUE) {
                found_any = true;
                do {
                    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (ImGui::Selectable(find_data.cFileName)) {
                            // TODO: Use this file
                        }
                    }
                } while (FindNextFileA(h_find, &find_data));
                FindClose(h_find);
            }
            
            if (!found_any) {
                ImGui::TextDisabled("No image files found");
            }
        }
        
        if (ImGui::CollapsingHeader("Shaders (.glsl, .vert, .frag)")) {
            bool found_any = false;
            
            const char* patterns[] = {"assets/*.glsl", "assets/*.vert", "assets/*.frag"};
            
            for (int i = 0; i < 3; ++i) {
                WIN32_FIND_DATAA find_data;
                HANDLE h_find = FindFirstFileA(patterns[i], &find_data);
                
                if (h_find != INVALID_HANDLE_VALUE) {
                    found_any = true;
                    do {
                        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            if (ImGui::Selectable(find_data.cFileName)) {
                                // TODO: Use this file
                            }
                        }
                    } while (FindNextFileA(h_find, &find_data));
                    FindClose(h_find);
                }
            }
            
            if (!found_any) {
                ImGui::TextDisabled("No shader files found");
            }
        }
        
        if (ImGui::CollapsingHeader("All Files")) {
            WIN32_FIND_DATAA find_data;
            HANDLE h_find = FindFirstFileA("assets/*.*", &find_data);
            
            if (h_find != INVALID_HANDLE_VALUE) {
                do {
                    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        ImGui::BulletText("%s", find_data.cFileName);
                    }
                } while (FindNextFileA(h_find, &find_data));
                FindClose(h_find);
            } else {
                ImGui::TextDisabled("No files found");
            }
        }
        
        ImGui::End();
    }
}

void RenderCurveEditor(EditorContext* editor) {
    if (!editor) return;
    
    if (ImGui::Begin("Curve Editor", &editor->show_curve_editor)) {
        // Curve selection
        ImGui::Text("Curves: %d", editor->project->curve_count);
        
        if (ImGui::Button("+ New Curve")) {
            // Resize curve array if needed
            if (editor->project->curve_count >= 32) {
                ImGui::Text("Maximum curves reached");
            } else {
                editor->project->curves[editor->project->curve_count] = rev::curve::CreateCurve();
                rev::curve::AddPoint(editor->project->curves[editor->project->curve_count], 0.0f, 0.0f);
                rev::curve::AddPoint(editor->project->curves[editor->project->curve_count], 1.0f, 1.0f);
                editor->selected_curve_index = editor->project->curve_count;
                editor->project->curve_count++;
                editor->project->modified = true;
            }
        }
        
        ImGui::SameLine();
        if (editor->selected_curve_index >= 0 && ImGui::Button("Delete Curve")) {
            if (editor->selected_curve_index < editor->project->curve_count) {
                rev::curve::DestroyCurve(editor->project->curves[editor->selected_curve_index]);
                // Shift remaining curves
                for (int i = editor->selected_curve_index; i < editor->project->curve_count - 1; ++i) {
                    editor->project->curves[i] = editor->project->curves[i + 1];
                }
                editor->project->curve_count--;
                editor->selected_curve_index = -1;
                editor->project->modified = true;
            }
        }
        
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &editor->show_curve_grid);
        
        // Curve list
        if (editor->project->curve_count > 0) {
            ImGui::Text("Select Curve:");
            for (int i = 0; i < editor->project->curve_count; ++i) {
                ImGui::PushID(i);
                char label[32];
                sprintf_s(label, sizeof(label), "Curve %d (%d pts)", i, editor->project->curves[i].point_count);
                if (ImGui::Selectable(label, editor->selected_curve_index == i)) {
                    editor->selected_curve_index = i;
                }
                ImGui::PopID();
            }
        }
        
        ImGui::Separator();
        
        // Curve canvas
        if (editor->selected_curve_index >= 0 && editor->selected_curve_index < editor->project->curve_count) {
            rev::curve::Curve* curve = &editor->project->curves[editor->selected_curve_index];
            
            // Canvas setup
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            canvas_size.y = (canvas_size.y > 300.0f) ? canvas_size.y : 300.0f;
            
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            
            // Background
            draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                                     IM_COL32(40, 40, 40, 255));
            
            // Grid
            if (editor->show_curve_grid) {
                const int grid_lines = 10;
                for (int i = 0; i <= grid_lines; ++i) {
                    float x = canvas_pos.x + (canvas_size.x / grid_lines) * i;
                    float y = canvas_pos.y + (canvas_size.y / grid_lines) * i;
                    draw_list->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y),
                                      IM_COL32(60, 60, 60, 255));
                    draw_list->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_pos.x + canvas_size.x, y),
                                      IM_COL32(60, 60, 60, 255));
                }
            }
            
            // Border
            draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                              IM_COL32(100, 100, 100, 255));
            
            // Draw curve line
            if (curve->point_count > 1) {
                const int segments = 100;
                for (int i = 0; i < segments; ++i) {
                    float t0 = (float)i / segments;
                    float t1 = (float)(i + 1) / segments;
                    float v0 = rev::curve::Evaluate(*curve, t0);
                    float v1 = rev::curve::Evaluate(*curve, t1);
                    
                    ImVec2 p0 = ImVec2(canvas_pos.x + t0 * canvas_size.x,
                                      canvas_pos.y + canvas_size.y - v0 * canvas_size.y);
                    ImVec2 p1 = ImVec2(canvas_pos.x + t1 * canvas_size.x,
                                      canvas_pos.y + canvas_size.y - v1 * canvas_size.y);
                    
                    draw_list->AddLine(p0, p1, IM_COL32(100, 200, 255, 255), 2.0f);
                }
            }
            
            // Draw and interact with control points
            ImGui::SetCursorScreenPos(canvas_pos);
            ImGui::InvisibleButton("canvas", canvas_size);
            bool is_hovered = ImGui::IsItemHovered();
            bool is_active = ImGui::IsItemActive();
            ImVec2 mouse_pos = ImGui::GetMousePos();
            
            // Add point on double-click
            if (is_hovered && ImGui::IsMouseDoubleClicked(0)) {
                float t = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
                float v = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
                t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
                v = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
                rev::curve::AddPoint(*curve, t, v, rev::curve::EaseMode::Linear);
                rev::curve::SortPoints(*curve);
                editor->project->modified = true;
            }
            
            // Draw control points
            for (int i = 0; i < curve->point_count; ++i) {
                rev::curve::Point* pt = &curve->points[i];
                ImVec2 point_pos = ImVec2(canvas_pos.x + pt->t * canvas_size.x,
                                         canvas_pos.y + canvas_size.y - pt->v * canvas_size.y);
                
                float point_radius = 6.0f;
                bool point_hovered = (mouse_pos.x - point_pos.x) * (mouse_pos.x - point_pos.x) +
                                    (mouse_pos.y - point_pos.y) * (mouse_pos.y - point_pos.y) < point_radius * point_radius;
                
                // Start dragging
                if (point_hovered && ImGui::IsMouseClicked(0)) {
                    editor->dragging_point_index = i;
                }
                
                // Drag point
                if (editor->dragging_point_index == i && ImGui::IsMouseDragging(0)) {
                    pt->t = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
                    pt->v = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
                    pt->t = (pt->t < 0.0f) ? 0.0f : ((pt->t > 1.0f) ? 1.0f : pt->t);
                    pt->v = (pt->v < 0.0f) ? 0.0f : ((pt->v > 1.0f) ? 1.0f : pt->v);
                    editor->project->modified = true;
                }
                
                // End dragging
                if (editor->dragging_point_index == i && ImGui::IsMouseReleased(0)) {
                    editor->dragging_point_index = -1;
                    rev::curve::SortPoints(*curve);
                }
                
                // Delete point on right-click
                if (point_hovered && ImGui::IsMouseClicked(1) && curve->point_count > 2) {
                    // Remove point by shifting
                    for (int j = i; j < curve->point_count - 1; ++j) {
                        curve->points[j] = curve->points[j + 1];
                    }
                    curve->point_count--;
                    editor->project->modified = true;
                    break;
                }
                
                // Draw point
                ImU32 point_color = (editor->dragging_point_index == i) ? IM_COL32(255, 255, 100, 255) :
                                   (point_hovered ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 255, 255, 255));
                draw_list->AddCircleFilled(point_pos, point_radius, point_color);
                draw_list->AddCircle(point_pos, point_radius, IM_COL32(0, 0, 0, 255), 0, 1.5f);
            }
            
            // Point properties
            if (editor->dragging_point_index >= 0 && editor->dragging_point_index < curve->point_count) {
                ImGui::Separator();
                rev::curve::Point* pt = &curve->points[editor->dragging_point_index];
                ImGui::Text("Point %d: t=%.3f, v=%.3f", editor->dragging_point_index, pt->t, pt->v);
                
                const char* ease_modes[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut", "Smoothstep", "Hold"};
                int current_mode = (int)pt->mode;
                if (ImGui::Combo("Ease Mode", &current_mode, ease_modes, 6)) {
                    pt->mode = (rev::curve::EaseMode)current_mode;
                    editor->project->modified = true;
                }
            }
            
            // Instructions
            ImGui::Separator();
            ImGui::TextDisabled("Double-click: Add point | Drag: Move point | Right-click: Delete point");
        } else {
            ImGui::Text("No curve selected. Create or select a curve to edit.");
        }
        
        ImGui::End();
    }
}

void RenderShaderModal(EditorContext* editor) {
    if (!editor) return;
    
    // Open popup if requested
    if (editor->shader_modal_request_open) {
        ImGui::OpenPopup("Shader Parameters");
        editor->shader_modal_open = true;
        editor->shader_modal_request_open = false;
    }
    
    // ImGui shader modal
    if (ImGui::BeginPopupModal("Shader Parameters", &editor->shader_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ShaderCue* cue = &editor->editing_shader;
        
        // Preset selection
        ImGui::Text("Shader Preset:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", cue->shader_name);
        
        const char* current_preset_name = "Unknown";
        for (int i = 0; i < g_shader_preset_count; ++i) {
            if (g_shader_presets[i].id == cue->shader_scene_id) {
                current_preset_name = g_shader_presets[i].name;
                break;
            }
        }
        
        if (ImGui::BeginCombo("Select Shader", current_preset_name)) {
            for (int i = 0; i < g_shader_preset_count; ++i) {
                bool is_selected = (g_shader_presets[i].id == cue->shader_scene_id);
                if (ImGui::Selectable(g_shader_presets[i].name, is_selected)) {
                    LoadShaderPreset(cue, g_shader_presets[i].id);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Separator();
        
        // Color palette
        ImGui::Text("Color Palette:");
        ImGui::ColorEdit3("Low", &cue->palette_low.r);
        ImGui::ColorEdit3("Mid", &cue->palette_mid.r);
        ImGui::ColorEdit3("High", &cue->palette_high.r);
        
        if (ImGui::Button("Randomize Colors")) {
            RandomizeShaderColors(cue);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Colors")) {
            cue->palette_low = {0.1f, 0.3f, 0.8f};
            cue->palette_mid = {0.45f, 0.25f, 0.7f};
            cue->palette_high = {0.8f, 0.2f, 0.6f};
        }
        
        ImGui::Separator();
        
        // Animation parameters
        ImGui::Text("Animation:");
        ImGui::SliderFloat("Speed", &cue->speed, 0.1f, 5.0f);
        ImGui::SliderFloat("Intensity", &cue->intensity, 0.0f, 2.0f);
        ImGui::SliderFloat("Warp", &cue->warp, 0.0f, 1.0f);
        
        if (ImGui::Button("Randomize Values")) {
            RandomizeShaderValues(cue);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Values")) {
            cue->speed = 1.0f;
            cue->intensity = 1.0f;
            cue->warp = 0.5f;
        }
        
        ImGui::Separator();
        
        // Curve assignments
        ImGui::Text("Curve Assignments:");
        
        // Build curve dropdown items
        const int max_curves = 32;
        char curve_items[max_curves + 1][64];
        strcpy_s(curve_items[0], "None");
        int curve_count = 1;
        for (int i = 0; i < editor->project->curve_count && curve_count < max_curves; ++i) {
            sprintf_s(curve_items[curve_count], "Curve %d (%d pts)", i, editor->project->curves[i].point_count);
            curve_count++;
        }
        
        const char* curve_ptrs[max_curves + 1];
        for (int i = 0; i < curve_count; ++i) {
            curve_ptrs[i] = curve_items[i];
        }
        
        // Curve selection for each parameter
        int speed_idx = cue->curve_speed + 1;
        if (ImGui::Combo("Speed Curve", &speed_idx, curve_ptrs, curve_count)) {
            cue->curve_speed = speed_idx - 1;
        }
        
        int intensity_idx = cue->curve_intensity + 1;
        if (ImGui::Combo("Intensity Curve", &intensity_idx, curve_ptrs, curve_count)) {
            cue->curve_intensity = intensity_idx - 1;
        }
        
        int warp_idx = cue->curve_warp + 1;
        if (ImGui::Combo("Warp Curve", &warp_idx, curve_ptrs, curve_count)) {
            cue->curve_warp = warp_idx - 1;
        }
        
        int exposure_idx = cue->curve_exposure + 1;
        if (ImGui::Combo("Exposure Curve", &exposure_idx, curve_ptrs, curve_count)) {
            cue->curve_exposure = exposure_idx - 1;
        }
        
        int fade_idx = cue->curve_fade + 1;
        if (ImGui::Combo("Fade Curve", &fade_idx, curve_ptrs, curve_count)) {
            cue->curve_fade = fade_idx - 1;
        }
        
        ImGui::Separator();
        
        // Exposure & fade
        ImGui::Text("Exposure:");
        ImGui::SliderFloat("Base##exp", &cue->exposure_base, 0.0f, 2.0f);
        ImGui::SliderFloat("Ramp##exp", &cue->exposure_ramp, -0.5f, 0.5f);
        
        ImGui::Text("Fade:");
        ImGui::SliderFloat("Base##fade", &cue->fade_base, 0.0f, 1.0f);
        ImGui::SliderFloat("Ramp##fade", &cue->fade_ramp, -0.5f, 0.5f);
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing:");
        ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f);
        ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f);
        ImGui::InputFloat("Fade In", &cue->fade_in, 0.1f, 1.0f);
        ImGui::InputFloat("Fade Out", &cue->fade_out, 0.1f, 1.0f);
        
        ImGui::Separator();
        
        // Layer controls
        ImGui::Text("Layer:");
        const char* layer_roles[] = {"Background", "Midground", "Foreground", "Overlay"};
        ImGui::Combo("Role", &cue->layer_role, layer_roles, 4);
        ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f);
        
        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4);
        ImGui::InputInt("Order", &cue->layer_order);
        
        ImGui::Separator();
        
        // Apply/Cancel
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            // Apply changes back to the scene
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->shader_cue_count) {
                    scene->shader_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
            editor->shader_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            editor->shader_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    } else {
        // If popup was closed, reset the flag
        if (editor->shader_modal_open) {
            editor->shader_modal_open = false;
        }
    }
}

void RenderMusicModal(EditorContext* editor) {
    if (!editor) return;
    
    // Open popup if requested
    if (editor->music_modal_request_open) {
        ImGui::OpenPopup("Music Settings");
        editor->music_modal_open = true;
        editor->music_modal_request_open = false;
    }
    
    // Music modal
    if (ImGui::BeginPopupModal("Music Settings", &editor->music_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        MusicCue* cue = &editor->editing_music;
        
        ImGui::Text("XM Music File:");
        ImGui::InputText("##musicfile", cue->asset_key, sizeof(cue->asset_key));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
            // Win32 file dialog
            OPENFILENAMEA ofn = {};
            char filepath[260] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = (HWND)editor->window->hwnd;
            ofn.lpstrFile = filepath;
            ofn.nMaxFile = sizeof(filepath);
            ofn.lpstrFilter = "XM Modules\0*.xm\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrInitialDir = "assets";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            
            if (GetOpenFileNameA(&ofn)) {
                const char* filename = strrchr(filepath, '\\');
                if (!filename) filename = strrchr(filepath, '/');
                if (filename) filename++; else filename = filepath;

                strncpy_s(cue->asset_key, filename, _TRUNCATE);

                // Copy XM file into project assets folder (same pattern as image cues)
                if (editor->project->assets_path[0]) {
                    char dest_path[512] = {};
                    snprintf(dest_path, sizeof(dest_path), "%s\\%s",
                             editor->project->assets_path, filename);
                    if (!CopyFileA(filepath, dest_path, FALSE)) {
                        printf("[MUSIC] Warning: could not copy asset to %s (err=%lu)\n",
                               dest_path, GetLastError());
                    }
                    // Store workspace-relative path with forward slashes
                    size_t cwd_len = strlen(editor->startup_dir);
                    if (cwd_len > 0 &&
                        _strnicmp(dest_path, editor->startup_dir, cwd_len) == 0 &&
                        (dest_path[cwd_len] == '\\' || dest_path[cwd_len] == '/')) {
                        strncpy_s(cue->asset_path, dest_path + cwd_len + 1, _TRUNCATE);
                    } else {
                        strncpy_s(cue->asset_path, dest_path, _TRUNCATE);
                    }
                    for (char* p = cue->asset_path; *p; ++p) if (*p == '\\') *p = '/';
                } else {
                    printf("[MUSIC] Warning: project not saved yet, asset not copied.\n");
                    strncpy_s(cue->asset_path, filepath, _TRUNCATE);
                }
            }
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f);
        ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f);
        
        ImGui::Separator();
        
        // Apply/Cancel
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            // Apply changes
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 3) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->music_cue_count) {
                    scene->music_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
            editor->music_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            editor->music_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    } else {
        if (editor->music_modal_open) {
            editor->music_modal_open = false;
        }
    }
}

void RenderImageModal(EditorContext* editor) {
    if (!editor) return;
    
    // Open popup if requested
    if (editor->image_modal_request_open) {
        ImGui::OpenPopup("Image Settings");
        editor->image_modal_open = true;
        editor->image_modal_request_open = false;
    }
    
    // Image modal
    if (ImGui::BeginPopupModal("Image Settings", &editor->image_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImageCue* cue = &editor->editing_image;
        
        ImGui::Text("Image File:");
        ImGui::InputText("##imagefile", cue->asset_key, sizeof(cue->asset_key));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
            // Win32 file dialog
            OPENFILENAMEA ofn = {};
            char filepath[260] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = (HWND)editor->window->hwnd;
            ofn.lpstrFile = filepath;
            ofn.nMaxFile = sizeof(filepath);
            ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrInitialDir = "assets";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            
            if (GetOpenFileNameA(&ofn)) {
                // Extract just filename
                const char* filename = strrchr(filepath, '\\\\');
                if (!filename) filename = strrchr(filepath, '/');
                if (filename) filename++; else filename = filepath;

                strncpy_s(cue->asset_key, filename, _TRUNCATE);

                // Copy file into the project's assets folder so the preview
                // and packer can find it at assets_path\asset_key.
                if (editor->project->assets_path[0]) {
                    char dest_path[512] = {};
                    snprintf(dest_path, sizeof(dest_path), "%s\\%s",
                             editor->project->assets_path, filename);
                    if (!CopyFileA(filepath, dest_path, FALSE)) {
                        printf("[IMAGE] Warning: could not copy asset to %s (err=%lu)\n",
                               dest_path, GetLastError());
                    }
                } else {
                    printf("[IMAGE] Warning: project not saved yet, asset not copied.\n");
                }
            }
        }
        
        ImGui::Separator();
        
        // Position
        ImGui::Text("Position (0.0-1.0):");
        ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f);
        ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f);
        
        ImGui::Separator();
        
        // Transform
        ImGui::Text("Transform:");
        ImGui::SliderFloat("Scale", &cue->scale, 0.1f, 5.0f);
        ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f);
        
        ImGui::Separator();
        
        // Layer
        ImGui::Text("Layer Order (lower draws first):");
        ImGui::SliderInt("Layer", &cue->layer_order, -10, 10);
        
        ImGui::Separator();
        
        // Effect
        ImGui::Text("Effect:");
        const char* img_effects[] = {"None", "Fade In/Out"};
        ImGui::Combo("Type##img", &cue->effect_type, img_effects, 2);
        if (cue->effect_type > 0) {
            ImGui::InputFloat("Fade In Start##img",  &cue->fade_in_start,  0.1f, 1.0f);
            ImGui::InputFloat("Fade In End##img",    &cue->fade_in_end,    0.1f, 1.0f);
            ImGui::InputFloat("Fade Out Start##img", &cue->fade_out_start, 0.1f, 1.0f);
            ImGui::InputFloat("Fade Out End##img",   &cue->fade_out_end,   0.1f, 1.0f);
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f);
        ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f);
        
        ImGui::Separator();
        
        // Apply/Cancel
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            // Apply changes
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 1) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    scene->image_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
            editor->image_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            editor->image_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    } else {
        if (editor->image_modal_open) {
            editor->image_modal_open = false;
        }
    }
}

void RenderTextModal(EditorContext* editor) {
    if (!editor) return;
    
    // Open popup if requested
    if (editor->text_modal_request_open) {
        ImGui::OpenPopup("Text Settings");
        editor->text_modal_open = true;
        editor->text_modal_request_open = false;
    }
    
    // Text modal
    if (ImGui::BeginPopupModal("Text Settings", &editor->text_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        TextCue* cue = &editor->editing_text;
        
        ImGui::Text("Text Content:");
        ImGui::InputTextMultiline("##text", cue->text, sizeof(cue->text), ImVec2(400, 100));
        
        ImGui::Separator();
        
        // Font
        ImGui::Text("Font:");
        ImGui::InputText("Font Name", cue->font_name, sizeof(cue->font_name));
        ImGui::SliderFloat("Size", &cue->size, 8.0f, 128.0f);
        
        ImGui::Separator();
        
        // Color
        ImGui::Text("Color:");
        ImGui::ColorEdit3("##textcolor", &cue->color.r);
        
        ImGui::Separator();
        
        // Position
        ImGui::Text("Position (0.0-1.0):");
        ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f);
        ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f);
        
        ImGui::Separator();
        
        // Effect
        ImGui::Text("Effect:");
        const char* effects[] = {"None", "Fade In/Out", "Scroll"};
        ImGui::Combo("Type", &cue->effect_type, effects, 3);
        
        if (cue->effect_type > 0) {
            ImGui::InputFloat("Fade In Start",  &cue->fade_in_start,  0.1f, 1.0f);
            ImGui::InputFloat("Fade In End",    &cue->fade_in_end,    0.1f, 1.0f);
            ImGui::InputFloat("Fade Out Start", &cue->fade_out_start, 0.1f, 1.0f);
            ImGui::InputFloat("Fade Out End",   &cue->fade_out_end,   0.1f, 1.0f);
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f);
        ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f);
        
        ImGui::Separator();
        
        // Layer
        ImGui::Text("Layer Order (lower draws first):");
        ImGui::SliderInt("Layer", &cue->layer_order, -10, 10);
        
        ImGui::Separator();
        
        // Apply/Cancel
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            // Apply changes
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    scene->text_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
            editor->text_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            editor->text_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    } else {
        if (editor->text_modal_open) {
            editor->text_modal_open = false;
        }
    }
}

void RenderMeshModal(EditorContext* editor) {
    if (!editor) return;

    static const char* mesh_type_names[] = { "Cube", "Sphere", "Plane", "Torus", "glTF/GLB" };

    if (editor->mesh_modal_request_open) {
        ImGui::OpenPopup("Edit Mesh Cue");
        editor->mesh_modal_request_open = false;
        editor->mesh_modal_open = true;
    }

    ImGui::SetNextWindowSize(ImVec2(480, 500), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Edit Mesh Cue", &editor->mesh_modal_open)) {
        MeshCue* cue = &editor->editing_mesh;

        ImGui::InputText("Asset Key", cue->asset_key, sizeof(cue->asset_key));
        ImGui::Combo("Shape", &cue->mesh_type, mesh_type_names, 5);

        // glTF asset path (only shown for type 4)
        if (cue->mesh_type == 4) {
            ImGui::Text("glTF / GLB File:");
            ImGui::SetNextItemWidth(-80.0f);
            ImGui::InputText("##gltfpath", cue->asset_path, sizeof(cue->asset_path));
            ImGui::SameLine();
            if (ImGui::Button("Browse##gltf")) {
                OPENFILENAMEA ofn = {};
                char filepath[512] = {};
                ofn.lStructSize   = sizeof(ofn);
                ofn.hwndOwner     = (HWND)editor->window->hwnd;
                ofn.lpstrFile     = filepath;
                ofn.nMaxFile      = sizeof(filepath);
                ofn.lpstrFilter   = "glTF Files\0*.gltf;*.glb\0GLB Binary\0*.glb\0glTF JSON\0*.gltf\0All Files\0*.*\0";
                ofn.nFilterIndex  = 1;
                ofn.lpstrInitialDir = editor->project->assets_path[0]
                                        ? editor->project->assets_path
                                        : editor->startup_dir;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameA(&ofn)) {
                    // Extract filename
                    const char* filename = strrchr(filepath, '\\');
                    if (!filename) filename = strrchr(filepath, '/');
                    if (filename) filename++; else filename = filepath;

                    // Auto-fill asset_key from filename stem if still empty
                    if (cue->asset_key[0] == '\0') {
                        strncpy_s(cue->asset_key, filename, _TRUNCATE);
                        char* dot = strrchr(cue->asset_key, '.');
                        if (dot) *dot = '\0';
                    }

                    // Copy file into project assets folder
                    if (editor->project->assets_path[0]) {
                        char dest_path[512] = {};
                        snprintf(dest_path, sizeof(dest_path), "%s\\%s",
                                 editor->project->assets_path, filename);
                        if (!CopyFileA(filepath, dest_path, FALSE)) {
                            printf("[GLTF] Warning: could not copy asset to %s (err=%lu)\n",
                                   dest_path, GetLastError());
                        }
                        // Store workspace-relative path with forward slashes
                        size_t cwd_len = strlen(editor->startup_dir);
                        if (cwd_len > 0 &&
                            _strnicmp(dest_path, editor->startup_dir, cwd_len) == 0 &&
                            (dest_path[cwd_len] == '\\' || dest_path[cwd_len] == '/')) {
                            strncpy_s(cue->asset_path, dest_path + cwd_len + 1, _TRUNCATE);
                        } else {
                            strncpy_s(cue->asset_path, dest_path, _TRUNCATE);
                        }
                        for (char* p = cue->asset_path; *p; ++p) if (*p == '\\') *p = '/';
                    } else {
                        strncpy_s(cue->asset_path, filepath, _TRUNCATE);
                        printf("[GLTF] Warning: project not saved yet, asset not copied.\n");
                    }

                    // Extract material properties from the glTF and pre-fill the cue.
                    // This reads the Blender-exported PBR material: base color, metallic,
                    // roughness.  The user can override these in the sliders below.
                    rev::gltf::ImportResult* ir = rev::gltf::LoadMesh(filepath);
                    if (ir && ir->ok) {
                        const rev::gltf::Material& mat = ir->material;
                        // Only overwrite color if it's still the default white
                        bool color_is_default =
                            cue->color[0] == 1.0f && cue->color[1] == 1.0f &&
                            cue->color[2] == 1.0f && cue->color[3] == 1.0f;
                        if (color_is_default) {
                            cue->color[0] = mat.base_color[0];
                            cue->color[1] = mat.base_color[1];
                            cue->color[2] = mat.base_color[2];
                            cue->color[3] = mat.base_color[3];
                        }
                        cue->metallic  = mat.metallic;
                        cue->roughness = mat.roughness;
                        printf("[GLTF] Material: name=\"%s\" base=(%.2f,%.2f,%.2f) metallic=%.2f roughness=%.2f\n",
                               mat.name[0] ? mat.name : "(unnamed)",
                               mat.base_color[0], mat.base_color[1], mat.base_color[2],
                               mat.metallic, mat.roughness);
                    }
                    if (ir) rev::gltf::FreeImportResult(ir);
                }
            }
            if (cue->asset_path[0])
                ImGui::TextDisabled("%s", cue->asset_path);
            else
                ImGui::TextDisabled("No file selected");
            ImGui::TextDisabled("mesh_size / mesh_param not used for external meshes");
        } else {
            ImGui::DragFloat("Size",  &cue->mesh_size,  0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("Param (segs/minor-r)", &cue->mesh_param, 0.1f, 0.01f, 100.0f);
        }

        ImGui::Separator();
        ImGui::DragFloat3("Position", cue->pos,   0.01f);
        ImGui::DragFloat3("Rotation", cue->rot,   1.0f, -360.0f, 360.0f);
        ImGui::DragFloat3("Scale",    cue->scale, 0.01f, 0.001f, 100.0f);

        ImGui::Separator();
        ImGui::ColorEdit4("Color (Base Color)", cue->color);
        ImGui::SliderFloat("Metallic",  &cue->metallic,  0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &cue->roughness, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::DragFloat("Cue Start", &cue->cue_start, 0.01f, 0.0f, 9999.0f);
        ImGui::DragFloat("Cue End",   &cue->cue_end,   0.01f, 0.0f, 9999.0f);
        ImGui::DragInt  ("Layer Order", &cue->layer_order, 1, -100, 100);

        ImGui::Separator();
        const char* effect_names[] = { "None", "Fade In/Out" };
        ImGui::Combo("Effect", &cue->effect_type, effect_names, 2);
        if (cue->effect_type != 0) {
            ImGui::DragFloat("Fade In Start",  &cue->fade_in_start,  0.01f);
            ImGui::DragFloat("Fade In End",    &cue->fade_in_end,    0.01f);
            ImGui::DragFloat("Fade Out Start", &cue->fade_out_start, 0.01f);
            ImGui::DragFloat("Fade Out End",   &cue->fade_out_end,   0.01f);
        }

        ImGui::Separator();
        if (ImGui::Button("Apply")) {
            // Find this cue in the selected scene and update it
            if (editor->project && editor->selected_scene_index >= 0 &&
                editor->selected_scene_index < editor->project->scene_count) {
                SceneBlock* scene = &editor->project->scenes[editor->selected_scene_index];
                if (editor->selected_cue_index >= 0 &&
                    editor->selected_cue_index < scene->mesh_cue_count) {
                    scene->mesh_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
            ImGui::CloseCurrentPopup();
            editor->mesh_modal_open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            editor->mesh_modal_open = false;
        }

        ImGui::EndPopup();
    } else {
        if (editor->mesh_modal_open) {
            editor->mesh_modal_open = false;
        }
    }
}

bool ImportFromCues(EditorContext* editor, const char* cues_path) {
    if (!editor || !cues_path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;
    
    printf("[ImportFromCues] Reading %s\n", cues_path);
    
    // Clear existing project and create a default scene
    NewProject(editor);
    
    char line[1024];
    enum Section { NONE, SHADER_CUES, IMAGE_CUES, TEXT_CUES, MUSIC_CUES, CURVES, METADATA };
    Section current_section = NONE;
    
    float total_duration = 10.0f; // Default
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\n' || *start == '\r' || *start == '\0' || *start == '#') continue;
        
        // Section detection
        if (strstr(start, "[shader_cues]")) { current_section = SHADER_CUES; continue; }
        if (strstr(start, "[image_cues]")) { current_section = IMAGE_CUES; continue; }
        if (strstr(start, "[text_cues]")) { current_section = TEXT_CUES; continue; }
        if (strstr(start, "[music_cues]")) { current_section = MUSIC_CUES; continue; }
        if (strstr(start, "[curves]")) { current_section = CURVES; continue; }
        if (strstr(start, "[metadata]")) { current_section = METADATA; continue; }
        
        // Parse metadata
        if (current_section == METADATA) {
            if (sscanf_s(start, "total_duration=%f", &total_duration) == 1) {
                printf("[ImportFromCues] Total duration: %.2f\n", total_duration);
            }
            continue;
        }
        
        // Parse shader cues
        if (current_section == SHADER_CUES) {
            ShaderCue cue = {};
            int shader_id;
            float abs_start, abs_end;
            
            int parsed = sscanf_s(start, "%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%f|%d|%d",
                &shader_id,
                &cue.palette_low.r, &cue.palette_low.g, &cue.palette_low.b,
                &cue.palette_mid.r, &cue.palette_mid.g, &cue.palette_mid.b,
                &cue.palette_high.r, &cue.palette_high.g, &cue.palette_high.b,
                &cue.speed, &cue.intensity, &cue.warp,
                &cue.exposure_base, &cue.exposure_ramp,
                &cue.fade_base, &cue.fade_ramp,
                &abs_start, &abs_end, &cue.fade_in, &cue.fade_out,
                &cue.layer_role, &cue.opacity, &cue.blend_mode, &cue.layer_order
            );
            
            if (parsed >= 18) { // At least basic params
                cue.shader_scene_id = shader_id;
                cue.cue_start = abs_start;
                cue.cue_end = abs_end;
                
                // Set shader name based on ID
                const char* preset_name = "Unknown";
                for (int i = 0; i < 10; i++) {
                    if (g_shader_presets[i].id == shader_id) {
                        preset_name = g_shader_presets[i].name;
                        break;
                    }
                }
                strncpy_s(cue.shader_name, sizeof(cue.shader_name), preset_name, _TRUNCATE);
                
                // Create scene if needed (use total duration)
                if (editor->project->scene_count == 0) {
                    AddScene(editor, "Imported Scene", total_duration);
                }
                
                SceneBlock* scene = &editor->project->scenes[0];
                AddShaderCue(scene, cue);
                
                printf("[ImportFromCues] Imported shader cue: id=%d name='%s' %.2f-%.2f\n",
                       shader_id, cue.shader_name, abs_start, abs_end);
            }
            continue;
        }
        
        // Parse image cues: asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end
        if (current_section == IMAGE_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            ImageCue cue = {};
            size_t key_len = (size_t)(p1 - start);
            if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
            strncpy_s(cue.asset_key, start, key_len);
            char* p2 = strchr(p1 + 1, '|'); // skip asset_path field
            if (!p2) continue;
            float abs_start = 0.0f, abs_end = 0.0f;
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f",
                &cue.x, &cue.y, &cue.scale, &cue.opacity,
                &abs_start, &abs_end, &cue.layer_order,
                &cue.effect_type, &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end
            );
            if (parsed >= 7) {
                cue.cue_start = abs_start;
                cue.cue_end = abs_end;
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                SceneBlock* scene = &editor->project->scenes[0];
                AddImageCue(scene, cue);
                printf("[ImportFromCues] Imported image cue: %s\n", cue.asset_key);
            }
            continue;
        }

        // Parse text cues: text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order
        if (current_section == TEXT_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            TextCue cue = {};
            size_t text_len = (size_t)(p1 - start);
            if (text_len >= sizeof(cue.text)) text_len = sizeof(cue.text) - 1;
            strncpy_s(cue.text, start, text_len);
            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t font_len = (size_t)(p2 - (p1 + 1));
            if (font_len >= sizeof(cue.font_name)) font_len = sizeof(cue.font_name) - 1;
            strncpy_s(cue.font_name, p1 + 1, font_len);
            float size_f = 0.0f;
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d",
                &cue.x, &cue.y, &size_f,
                &cue.color.r, &cue.color.g, &cue.color.b,
                &cue.effect_type, &cue.cue_start, &cue.cue_end,
                &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.layer_order);
            if (parsed >= 9) {
                cue.size = size_f;
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                AddTextCue(&editor->project->scenes[0], cue);
                printf("[ImportFromCues] Imported text cue: %s\n", cue.text);
            }
            continue;
        }

        // Parse music cues: asset_key|asset_path|cue_start|cue_end
        if (current_section == MUSIC_CUES) {
            MusicCue cue = {};
            char* p1 = strchr(start, '|');
            if (p1) {
                size_t key_len = (size_t)(p1 - start);
                if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
                strncpy_s(cue.asset_key, start, key_len);
                char* p2 = strchr(p1 + 1, '|');
                if (p2) {
                    size_t path_len = (size_t)(p2 - (p1 + 1));
                    if (path_len >= sizeof(cue.asset_path)) path_len = sizeof(cue.asset_path) - 1;
                    strncpy_s(cue.asset_path, p1 + 1, path_len);
                    if (sscanf_s(p2 + 1, "%f|%f", &cue.cue_start, &cue.cue_end) >= 1) {
                        if (editor->project->scene_count == 0)
                            AddScene(editor, "Imported Scene", total_duration);
                        AddMusicCue(&editor->project->scenes[0], cue);
                        printf("[ImportFromCues] Imported music cue: %s path=%s\n",
                               cue.asset_key, cue.asset_path);
                    }
                }
            }
            continue;
        }
    }

    fclose(f);
    
    // Update project metadata
    if (editor->project) {
        editor->project->total_duration = total_duration;
    }
    
    printf("[ImportFromCues] Import complete!\n");
    return true;
}

bool ExportProject(EditorContext* editor, const char* output_path) {
    if (!editor || !output_path) return false;

    FILE* f = nullptr;
    fopen_s(&f, output_path, "w");
    if (!f) return false;

    // [shader_cues] section
    fprintf(f, "[shader_cues]\n");
    fprintf(f, "# shader_scene_id|palette_low_r|palette_low_g|palette_low_b|palette_mid_r|palette_mid_g|palette_mid_b|palette_high_r|palette_high_g|palette_high_b|speed|intensity|warp|exposure_base|exposure_ramp|fade_base|fade_ramp|cue_start|cue_end|fade_in|fade_out|layer_role|opacity|blend_mode|layer_order\n");
    
    // Collect all shader cues from all scenes
    int shader_cue_id = 0;
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        // Calculate scene start time
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->shader_cue_count; ++cue_idx) {
            ShaderCue* cue = &scene->shader_cues[cue_idx];
            
            // Convert scene-relative times to absolute times
            float abs_start = scene_start + cue->cue_start;
            float abs_end = (cue->cue_end < 0.0f) ? (scene_start + scene->duration) : (scene_start + cue->cue_end);
            
            fprintf(f, "%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%d|%d\n",
                cue->shader_scene_id,
                cue->palette_low.r, cue->palette_low.g, cue->palette_low.b,
                cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b,
                cue->palette_high.r, cue->palette_high.g, cue->palette_high.b,
                cue->speed, cue->intensity, cue->warp,
                cue->exposure_base, cue->exposure_ramp,
                cue->fade_base, cue->fade_ramp,
                abs_start, abs_end, cue->fade_in, cue->fade_out,
                cue->layer_role, cue->opacity, cue->blend_mode, cue->layer_order
            );
            
            shader_cue_id++;
        }
    }
    
    fprintf(f, "\n");
    
    // [image_cues] section
    fprintf(f, "[image_cues]\n");
    fprintf(f, "# asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end\n");
    
    // Compute workspace-root-relative prefix for asset paths once.
    // assets_path is absolute (e.g. E:\himym\intros\test\test_assets).
    // cwd is always the workspace root (e.g. E:\himym).
    // We need the forward-slash relative form: intros/test/test_assets
    char rel_assets_prefix[512] = {};
    {
        size_t cwd_len = strlen(editor->startup_dir);
        const char* ap = editor->project->assets_path;
        if (cwd_len > 0 && _strnicmp(ap, editor->startup_dir, cwd_len) == 0 &&
            (ap[cwd_len] == '\\' || ap[cwd_len] == '/')) {
            strncpy_s(rel_assets_prefix, ap + cwd_len + 1, sizeof(rel_assets_prefix) - 1);
        } else {
            // assets_path not under cwd — use just project_name_assets as fallback
            const char* proj_path = editor->project->project_path;
            const char* fn = strrchr(proj_path, '\\');
            if (!fn) fn = strrchr(proj_path, '/');
            fn = fn ? fn + 1 : proj_path;
            char pname[256] = {};
            strncpy_s(pname, fn, sizeof(pname) - 1);
            char* dot = strrchr(pname, '.');
            if (dot) *dot = '\0';
            snprintf(rel_assets_prefix, sizeof(rel_assets_prefix), "%s_assets", pname);
        }
        // normalise to forward slashes
        for (char* p = rel_assets_prefix; *p; ++p) if (*p == '\\') *p = '/';
    }

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->image_cue_count; ++cue_idx) {
            ImageCue* cue = &scene->image_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = (cue->cue_end < 0.0f) ? (scene_start + scene->duration) : (scene_start + cue->cue_end);
            
            // Construct workspace-root-relative path: rel_assets_prefix/asset_key
            char full_path[640];
            snprintf(full_path, sizeof(full_path), "%s/%s", rel_assets_prefix, cue->asset_key);
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f\n",
                cue->asset_key, full_path, cue->x, cue->y, cue->scale, cue->opacity,
                abs_start, abs_end, cue->layer_order,
                cue->effect_type, cue->fade_in_start, cue->fade_in_end, cue->fade_out_start, cue->fade_out_end
            );
        }
    }
    
    fprintf(f, "\n");
    
    // [text_cues] section
    fprintf(f, "[text_cues]\n");
    fprintf(f, "# text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order\n");
    
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->text_cue_count; ++cue_idx) {
            TextCue* cue = &scene->text_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            float abs_fade_in_start  = scene_start + cue->fade_in_start;
            float abs_fade_in_end    = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end   = scene_start + cue->fade_out_end;
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d\n",
                cue->text, cue->font_name, cue->x, cue->y, cue->size,
                cue->color.r, cue->color.g, cue->color.b,
                cue->effect_type, abs_start, abs_end,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->layer_order
            );
        }
    }
    
    fprintf(f, "\n");
    
    // [music_cues] section
    fprintf(f, "[music_cues]\n");
    fprintf(f, "# asset_key|asset_path|cue_start|cue_end\n");
    
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->music_cue_count; ++cue_idx) {
            MusicCue* cue = &scene->music_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            
            fprintf(f, "%s|%s|%.3f|%.3f\n",
                cue->asset_key, cue->asset_path, abs_start, abs_end
            );
        }
    }
    
    fprintf(f, "\n");

    // [mesh_cues] section
    fprintf(f, "[mesh_cues]\n");
    fprintf(f, "# asset_key|asset_path|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|metallic|roughness\n");

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;

        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }

        for (int cue_idx = 0; cue_idx < scene->mesh_cue_count; ++cue_idx) {
            MeshCue* cue = &scene->mesh_cues[cue_idx];
            float abs_start           = scene_start + cue->cue_start;
            float abs_end             = scene_start + cue->cue_end;
            float abs_fade_in_start   = scene_start + cue->fade_in_start;
            float abs_fade_in_end     = scene_start + cue->fade_in_end;
            float abs_fade_out_start  = scene_start + cue->fade_out_start;
            float abs_fade_out_end    = scene_start + cue->fade_out_end;

            fprintf(f, "%s|%s|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f\n",
                cue->asset_key, cue->asset_path, cue->mesh_type,
                cue->pos[0],   cue->pos[1],   cue->pos[2],
                cue->rot[0],   cue->rot[1],   cue->rot[2],
                cue->scale[0], cue->scale[1], cue->scale[2],
                cue->color[0], cue->color[1], cue->color[2], cue->color[3],
                cue->mesh_size, cue->mesh_param,
                abs_start, abs_end, cue->layer_order, cue->effect_type,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->metallic, cue->roughness
            );
        }
    }

    fprintf(f, "\n");
    
    // [curves] section (placeholder for now)
    fprintf(f, "[curves]\n");
    fprintf(f, "# target|param|point_count\n");
    
    // TODO: Export curves with proper targets
    for (int i = 0; i < editor->project->curve_count; ++i) {
        rev::curve::Curve* curve = &editor->project->curves[i];
        if (curve->point_count > 0) {
            fprintf(f, "curve_%d|value|%d\n", i, curve->point_count);
            
            for (int pt_idx = 0; pt_idx < curve->point_count; ++pt_idx) {
                rev::curve::Point* pt = &curve->points[pt_idx];
                
                const char* mode_str = "linear";
                switch (pt->mode) {
                    case rev::curve::EaseMode::Linear: mode_str = "linear"; break;
                    case rev::curve::EaseMode::EaseIn: mode_str = "ease_in"; break;
                    case rev::curve::EaseMode::EaseOut: mode_str = "ease_out"; break;
                    case rev::curve::EaseMode::EaseInOut: mode_str = "ease_in_out"; break;
                    case rev::curve::EaseMode::Smoothstep: mode_str = "smoothstep"; break;
                    case rev::curve::EaseMode::Hold: mode_str = "hold"; break;
                }
                
                fprintf(f, "%.3f|%.3f|%.3f|%.3f|%s\n",
                    pt->t, pt->v, pt->in_ease, pt->out_ease, mode_str
                );
            }
        }
    }
    
    fprintf(f, "\n");
    
    // [metadata] section
    fprintf(f, "[metadata]\n");
    fprintf(f, "total_duration=%.3f\n", editor->project->total_duration);
    fprintf(f, "scene_count=%d\n", editor->project->scene_count);
    
    fclose(f);
    return true;
}

bool BuildAndRun(EditorContext* editor) {
    if (!editor) return false;

    printf("\n=== Build and Run ===\n");

    // Step 1: compute project-relative cues path
    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    // Step 2: Export to {project_dir}/cues.txt
    printf("Step 1: Exporting to %s...\n", cues_path);
    if (!ExportProject(editor, cues_path)) {
        printf("ERROR: Export failed!\n");
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Export failed!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }
    printf("Export complete.\n");

    // Step 3: Build — use absolute build dir so CWD mutations don't matter
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Building intro...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 2: Building minimal_intro...\n");
    char build_cmd[768];
    snprintf(build_cmd, sizeof(build_cmd),
             "cmake --build \"%s\\build\" --config Release --target minimal_intro",
             editor->startup_dir);
    int build_result = system(build_cmd);
    if (build_result != 0) {
        printf("ERROR: Build failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Build complete.\n");

    // Step 4: Launch — absolute exe path + cues_path as argv[1]
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    printf("Step 3: Launching intro (%s)...\n", cues_path);
    char run_command[768];
    snprintf(run_command, sizeof(run_command),
             "start \"\" \"%s\\build\\bin\\Release\\minimal_intro.exe\" %s",
             editor->startup_dir, cues_path);
    int run_result = system(run_command);
    
    if (run_result == 0) {
        printf("Intro launched successfully!\n");
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Intro launched successfully!", _TRUNCATE);
        editor->build_status_timer = 3.0f;
    } else {
        printf("ERROR: Failed to launch intro (exit code %d)\n", run_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Failed to launch intro.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
    }
    
    return (run_result == 0);
}

bool PackBuildAndRun(EditorContext* editor) {
    if (!editor) return false;

    printf("\n=== Pack, Build and Run ===\n");

    // Step 1: compute project-relative cues path
    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    // Step 2: Export cues.txt to project directory
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Exporting project...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 1: Exporting to %s...\n", cues_path);
    if (!ExportProject(editor, cues_path)) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Export failed!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }
    printf("Export complete.\n");

    // Step 3: Pack assets into {startup_dir}\build\packed_assets.h
    // Pack cache lives next to the project so each project tracks its own checksums.
    char pack_cache_path[512] = {};
    snprintf(pack_cache_path, sizeof(pack_cache_path), "%s/pack_cache.txt",
             editor->project->workspace_path[0] ? editor->project->workspace_path
                                                 : editor->startup_dir);
    for (char* p = pack_cache_path; *p; ++p) if (*p == '\\') *p = '/';

    char packed_header_path[512] = {};
    char build_dir[512] = {};
    snprintf(build_dir, sizeof(build_dir), "%s\\build", editor->startup_dir);
    CreateDirectoryA(build_dir, NULL);
    snprintf(packed_header_path, sizeof(packed_header_path), "%s\\packed_assets.h", build_dir);

    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Packing assets...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 2: Packing assets (cache: %s)...\n", pack_cache_path);

    rev::pack::PackResult pack_result = rev::pack::PackAssets(
        cues_path,              // cues source (project-relative)
        packed_header_path,     // output header (absolute path to build dir)
        pack_cache_path,        // checksum cache next to project
        ""                      // workspace_root — paths in cues.txt are already relative to cwd
    );

    if (!pack_result.ok) {
        printf("ERROR: Packing failed: %s\n", pack_result.error);
        char msg[256];
        snprintf(msg, sizeof(msg), "Pack failed: %s", pack_result.error);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), msg, _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Pack complete: %d total, %d packed, %d skipped.\n",
           pack_result.total, pack_result.packed, pack_result.skipped);

    // Step 3: Build the packed target — absolute build dir
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Building packed intro...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 3: Building minimal_intro_packed...\n");
    char build_cmd[768];
    snprintf(build_cmd, sizeof(build_cmd),
             "cmake --build \"%s\\build\" --config Release --target minimal_intro_packed",
             editor->startup_dir);
    int build_result = system(build_cmd);
    if (build_result != 0) {
        printf("ERROR: Build failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Build complete.\n");

    // Step 4: Launch — absolute exe path + cues_path as argv[1]
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching packed intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    printf("Step 4: Launching packed intro (%s)...\n", cues_path);
    char run_command[768];
    snprintf(run_command, sizeof(run_command),
             "start \"\" \"%s\\build\\bin\\Release\\minimal_intro_packed.exe\" %s",
             editor->startup_dir, cues_path);
    int run_result = system(run_command);
    if (run_result == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Packed intro launched! (%d assets, %d skipped)",
                 pack_result.total, pack_result.skipped);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), msg, _TRUNCATE);
        editor->build_status_timer = 5.0f;
        printf("Packed intro launched successfully!\n");
    } else {
        printf("ERROR: Failed to launch (exit code %d)\n", run_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Failed to launch packed intro.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
    }

    return (run_result == 0);
}

// ===== Scene Management =====

int AddScene(EditorContext* editor, const char* name, float duration) {
    if (!editor || !name) return -1;
    
    // Resize array if needed
    if (editor->project->scene_count >= editor->project->scene_capacity) {
        int new_capacity = editor->project->scene_capacity == 0 ? 4 : editor->project->scene_capacity * 2;
        SceneBlock* new_scenes = new SceneBlock[new_capacity];
        
        // Copy existing scenes
        for (int i = 0; i < editor->project->scene_count; ++i) {
            new_scenes[i] = editor->project->scenes[i];
        }
        
        delete[] editor->project->scenes;
        editor->project->scenes = new_scenes;
        editor->project->scene_capacity = new_capacity;
    }
    
    // Initialize new scene
    int index = editor->project->scene_count++;
    SceneBlock* scene = &editor->project->scenes[index];
    
    strncpy_s(scene->name, sizeof(scene->name), name, _TRUNCATE);
    scene->duration = duration;
    
    scene->shader_cues = nullptr;
    scene->shader_cue_count = 0;
    scene->shader_cue_capacity = 0;
    
    scene->image_cues = nullptr;
    scene->image_cue_count = 0;
    scene->image_cue_capacity = 0;
    
    scene->text_cues = nullptr;
    scene->text_cue_count = 0;
    scene->text_cue_capacity = 0;
    
    scene->music_cues = nullptr;
    scene->music_cue_count = 0;
    scene->music_cue_capacity = 0;

    scene->mesh_cues = nullptr;
    scene->mesh_cue_count = 0;
    scene->mesh_cue_capacity = 0;

    // Update total duration
    editor->project->total_duration += duration;
    editor->project->modified = true;
    
    return index;
}

void DeleteScene(EditorContext* editor, int scene_index) {
    if (!editor || scene_index < 0 || scene_index >= editor->project->scene_count) return;
    
    SceneBlock* scene = &editor->project->scenes[scene_index];
    
    // Update total duration
    editor->project->total_duration -= scene->duration;
    
    // Free scene resources
    delete[] scene->shader_cues;
    delete[] scene->image_cues;
    delete[] scene->text_cues;
    delete[] scene->music_cues;
    delete[] scene->mesh_cues;
    
    // Shift remaining scenes
    for (int i = scene_index; i < editor->project->scene_count - 1; ++i) {
        editor->project->scenes[i] = editor->project->scenes[i + 1];
    }
    
    editor->project->scene_count--;
    editor->project->modified = true;
}

void MoveScene(EditorContext* editor, int from_index, int to_index) {
    if (!editor || from_index < 0 || from_index >= editor->project->scene_count) return;
    if (to_index < 0 || to_index >= editor->project->scene_count) return;
    if (from_index == to_index) return;
    
    SceneBlock temp = editor->project->scenes[from_index];
    
    if (from_index < to_index) {
        // Move forward
        for (int i = from_index; i < to_index; ++i) {
            editor->project->scenes[i] = editor->project->scenes[i + 1];
        }
    } else {
        // Move backward
        for (int i = from_index; i > to_index; --i) {
            editor->project->scenes[i] = editor->project->scenes[i - 1];
        }
    }
    
    editor->project->scenes[to_index] = temp;
    editor->project->modified = true;
}

SceneBlock* GetScene(EditorContext* editor, int scene_index) {
    if (!editor || scene_index < 0 || scene_index >= editor->project->scene_count) return nullptr;
    return &editor->project->scenes[scene_index];
}

// ===== Cue Management =====

int AddShaderCue(SceneBlock* scene, const ShaderCue& cue) {
    if (!scene) return -1;
    
    // Resize if needed
    if (scene->shader_cue_count >= scene->shader_cue_capacity) {
        int new_capacity = scene->shader_cue_capacity == 0 ? 4 : scene->shader_cue_capacity * 2;
        ShaderCue* new_cues = new ShaderCue[new_capacity];
        
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            new_cues[i] = scene->shader_cues[i];
        }
        
        delete[] scene->shader_cues;
        scene->shader_cues = new_cues;
        scene->shader_cue_capacity = new_capacity;
    }
    
    int index = scene->shader_cue_count++;
    scene->shader_cues[index] = cue;
    return index;
}

int AddImageCue(SceneBlock* scene, const ImageCue& cue) {
    if (!scene) return -1;
    
    if (scene->image_cue_count >= scene->image_cue_capacity) {
        int new_capacity = scene->image_cue_capacity == 0 ? 4 : scene->image_cue_capacity * 2;
        ImageCue* new_cues = new ImageCue[new_capacity];
        
        for (int i = 0; i < scene->image_cue_count; ++i) {
            new_cues[i] = scene->image_cues[i];
        }
        
        delete[] scene->image_cues;
        scene->image_cues = new_cues;
        scene->image_cue_capacity = new_capacity;
    }
    
    int index = scene->image_cue_count++;
    scene->image_cues[index] = cue;
    return index;
}

int AddTextCue(SceneBlock* scene, const TextCue& cue) {
    if (!scene) return -1;
    
    if (scene->text_cue_count >= scene->text_cue_capacity) {
        int new_capacity = scene->text_cue_capacity == 0 ? 4 : scene->text_cue_capacity * 2;
        TextCue* new_cues = new TextCue[new_capacity];
        
        for (int i = 0; i < scene->text_cue_count; ++i) {
            new_cues[i] = scene->text_cues[i];
        }
        
        delete[] scene->text_cues;
        scene->text_cues = new_cues;
        scene->text_cue_capacity = new_capacity;
    }
    
    int index = scene->text_cue_count++;
    scene->text_cues[index] = cue;
    return index;
}

int AddMusicCue(SceneBlock* scene, const MusicCue& cue) {
    if (!scene) return -1;
    
    if (scene->music_cue_count >= scene->music_cue_capacity) {
        int new_capacity = scene->music_cue_capacity == 0 ? 4 : scene->music_cue_capacity * 2;
        MusicCue* new_cues = new MusicCue[new_capacity];
        
        for (int i = 0; i < scene->music_cue_count; ++i) {
            new_cues[i] = scene->music_cues[i];
        }
        
        delete[] scene->music_cues;
        scene->music_cues = new_cues;
        scene->music_cue_capacity = new_capacity;
    }
    
    int index = scene->music_cue_count++;
    scene->music_cues[index] = cue;
    return index;
}

void DeleteShaderCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->shader_cue_count) return;
    
    for (int i = cue_index; i < scene->shader_cue_count - 1; ++i) {
        scene->shader_cues[i] = scene->shader_cues[i + 1];
    }
    scene->shader_cue_count--;
}

void DeleteImageCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->image_cue_count) return;
    
    for (int i = cue_index; i < scene->image_cue_count - 1; ++i) {
        scene->image_cues[i] = scene->image_cues[i + 1];
    }
    scene->image_cue_count--;
}

void DeleteTextCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->text_cue_count) return;
    
    for (int i = cue_index; i < scene->text_cue_count - 1; ++i) {
        scene->text_cues[i] = scene->text_cues[i + 1];
    }
    scene->text_cue_count--;
}

void DeleteMusicCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->music_cue_count) return;
    
    for (int i = cue_index; i < scene->music_cue_count - 1; ++i) {
        scene->music_cues[i] = scene->music_cues[i + 1];
    }
    scene->music_cue_count--;
}

int AddMeshCue(SceneBlock* scene, const MeshCue& cue) {
    if (!scene) return -1;

    if (scene->mesh_cue_count >= scene->mesh_cue_capacity) {
        int new_capacity = scene->mesh_cue_capacity == 0 ? 4 : scene->mesh_cue_capacity * 2;
        MeshCue* new_cues = new MeshCue[new_capacity];

        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            new_cues[i] = scene->mesh_cues[i];
        }

        delete[] scene->mesh_cues;
        scene->mesh_cues = new_cues;
        scene->mesh_cue_capacity = new_capacity;
    }

    int index = scene->mesh_cue_count++;
    scene->mesh_cues[index] = cue;
    return index;
}

void DeleteMeshCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->mesh_cue_count) return;

    for (int i = cue_index; i < scene->mesh_cue_count - 1; ++i) {
        scene->mesh_cues[i] = scene->mesh_cues[i + 1];
    }
    scene->mesh_cue_count--;
}

// ===== Shader Presets =====

void LoadShaderPreset(ShaderCue* cue, int preset_id) {
    if (!cue) return;
    
    cue->shader_scene_id = preset_id;
    
    // Find and set the preset name
    for (int i = 0; i < g_shader_preset_count; ++i) {
        if (g_shader_presets[i].id == preset_id) {
            strncpy_s(cue->shader_name, sizeof(cue->shader_name), g_shader_presets[i].name, _TRUNCATE);
            break;
        }
    }
    
    // Default values
    ResetShaderValues(cue);
}

void RandomizeShaderColors(ShaderCue* cue) {
    if (!cue) return;
    
    // Simple randomization (can be improved with color harmony later)
    cue->palette_low.r = (float)rand() / RAND_MAX;
    cue->palette_low.g = (float)rand() / RAND_MAX;
    cue->palette_low.b = (float)rand() / RAND_MAX;
    
    cue->palette_mid.r = (float)rand() / RAND_MAX;
    cue->palette_mid.g = (float)rand() / RAND_MAX;
    cue->palette_mid.b = (float)rand() / RAND_MAX;
    
    cue->palette_high.r = (float)rand() / RAND_MAX;
    cue->palette_high.g = (float)rand() / RAND_MAX;
    cue->palette_high.b = (float)rand() / RAND_MAX;
}

// ===== PREVIEW VIEWPORT =====

// Simple vertex shader for fullscreen quad
static const char* preview_vertex_shader = R"(
#version 330 core
out vec2 uv;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    uv = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

// Test fragment shader - Plasma effect
static const char* preview_fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float v = sin(p.x * 10.0 + t) + sin(p.y * 10.0 + t * 0.5) + sin((p.x + p.y) * 5.0 + t * 0.8);
    v = v / 3.0 * u_intensity;
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, smoothstep(-1.0, 0.0, v)), 
                   u_palette_high, smoothstep(0.0, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)";

// Sprite vertex shader for image/text rendering
static const char* sprite_vertex_shader = R"(
#version 330 core
out vec2 uv;
uniform vec2 u_position;  // -1 to 1
uniform vec2 u_size;      // width, height in normalized coords
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    uv = vec2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);  // Flip V coordinate
    gl_Position = vec4(u_position.x + x * u_size.x, u_position.y + y * u_size.y, 0.0, 1.0);
}
)";

// Sprite fragment shader - textured with opacity
static const char* sprite_fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform float u_opacity;
void main() {
    vec4 texColor = texture(u_texture, uv);
    fragColor = vec4(texColor.rgb, texColor.a * u_opacity);
}
)";

// Mesh (3D Phong) shaders
static const char* mesh_vertex_shader = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
out vec3 v_frag_pos;
out vec3 v_normal;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
void main() {
    vec4 world_pos = u_model * vec4(a_pos, 1.0);
    v_frag_pos = world_pos.xyz;
    v_normal   = mat3(transpose(inverse(u_model))) * a_normal;
    gl_Position = u_projection * u_view * world_pos;
}
)";

static const char* mesh_fragment_shader = R"(
#version 330 core
in vec3 v_frag_pos;
in vec3 v_normal;
out vec4 fragColor;
uniform vec3  u_light_pos;
uniform vec3  u_view_pos;
uniform vec4  u_color;
uniform float u_metallic;
uniform float u_roughness;
void main() {
    vec3  base     = u_color.rgb;
    vec3  norm     = normalize(v_normal);
    vec3  ldir     = normalize(u_light_pos - v_frag_pos);
    vec3  vdir     = normalize(u_view_pos  - v_frag_pos);
    vec3  hdir     = normalize(ldir + vdir);
    // Ambient
    float ambient  = 0.15;
    // Diffuse — metals have little diffuse
    float diff     = max(dot(norm, ldir), 0.0) * (1.0 - u_metallic * 0.9);
    // Specular: shininess driven by roughness; colour tinted for metals
    float shininess   = mix(2.0, 256.0, 1.0 - u_roughness);
    float spec_fac    = pow(max(dot(norm, hdir), 0.0), shininess);
    vec3  spec_col    = mix(vec3(0.04), base, u_metallic);
    vec3  spec        = spec_col * spec_fac * (1.0 - u_roughness * 0.85);
    vec3  result      = base * (ambient + diff) + spec;
    fragColor = vec4(result, u_color.a);
}
)";

// All 10 shader presets fragment shaders
static const char* g_preview_fragment_shaders[] = {
    // 0: Plasma Vibrant
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float v = sin(p.x * 10.0 + t) + sin(p.y * 10.0 + t * 0.5) + sin((p.x + p.y) * 5.0 + t * 0.8);
    v = v / 3.0 * u_intensity;
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, smoothstep(-1.0, 0.0, v)), 
                   u_palette_high, smoothstep(0.0, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 1: Tunnel Neon
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float r = length(p);
    float a = atan(p.y, p.x);
    float d = 1.0 / (r + 0.1);
    
    float tunnel = fract(d - t * 0.5);
    float rings = abs(sin(tunnel * 20.0));
    
    vec3 col = mix(u_palette_low, u_palette_high, rings);
    col = mix(col, u_palette_mid, smoothstep(0.3, 0.7, tunnel));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 2: Raymarcher SDF
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_high;
uniform float u_speed;

float sdSphere(vec3 p, float r) { return length(p) - r; }

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 ro = vec3(0, 0, -3);
    vec3 rd = normalize(vec3(p, 1.0));
    
    float dist = 0.0;
    for (int i = 0; i < 32; i++) {
        vec3 pos = ro + rd * dist;
        float d = sdSphere(pos - vec3(sin(t) * 0.5, cos(t * 0.7) * 0.5, 0), 0.5);
        if (d < 0.001) break;
        dist += d;
    }
    
    vec3 col = mix(u_palette_low, u_palette_high, smoothstep(2.0, 5.0, dist));
    fragColor = vec4(col, 1.0);
}
)",
    
    // 3: Fractal Mandelbrot
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0) * 2.0;
    float t = u_time * u_speed * 0.2;
    p += vec2(sin(t) * 0.3, cos(t * 0.7) * 0.3);
    
    vec2 c = p;
    vec2 z = vec2(0.0);
    float iter = 0.0;
    
    for (int i = 0; i < 64; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (length(z) > 4.0) break;
        iter += 1.0;
    }
    
    float v = iter / 64.0;
    vec3 col = mix(mix(u_palette_low, u_palette_mid, v), u_palette_high, smoothstep(0.7, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 4: Voronoi Cells
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

void main() {
    vec2 p = uv * 8.0 * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(x, y);
            vec2 h = hash2(ip + offset);
            vec2 pt = offset + sin(h * 6.28 + t) * 0.5 + 0.5;
            float d = length(pt - fp);
            minDist = min(minDist, d);
        }
    }
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, minDist), u_palette_high, smoothstep(0.5, 1.0, minDist));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 5: Wave Distortion
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_warp;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    p.x += sin(p.y * 5.0 + t) * u_warp;
    p.y += cos(p.x * 5.0 + t * 0.7) * u_warp;
    
    float d = length(p);
    float wave = sin(d * 10.0 - t * 2.0) * 0.5 + 0.5;
    
    vec3 col = mix(u_palette_low, u_palette_high, wave);
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 6: Particle System
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 32; i++) {
        float fi = float(i);
        float h = hash(vec2(fi, fi * 0.5));
        float angle = h * 6.28;
        float radius = fract(h * 7.13 + t * 0.3) * 2.0;
        
        vec2 pos = vec2(cos(angle), sin(angle)) * radius;
        float d = length(p - pos);
        float particle = smoothstep(0.05, 0.0, d);
        
        vec3 pcol = mix(u_palette_low, mix(u_palette_mid, u_palette_high, h), fract(radius));
        col += pcol * particle;
    }
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 7: Starfield
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 rd = normalize(vec3(p, 1.0));
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 64; i++) {
        float fi = float(i);
        vec3 star_pos = vec3(hash(vec3(fi, fi * 0.1, 0)) * 2.0 - 1.0,
                             hash(vec3(fi * 0.5, fi, 0)) * 2.0 - 1.0,
                             hash(vec3(fi, 0, fi * 0.7)) * 5.0 + 1.0);
        
        star_pos.z = fract(star_pos.z - t * 0.5) * 10.0;
        vec3 proj = star_pos / star_pos.z;
        
        float d = length(proj.xy - p);
        float star = smoothstep(0.02, 0.0, d) / star_pos.z;
        col += u_palette_high * star;
    }
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 8: Glow Orbs
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        vec2 orb_pos = vec2(sin(t * 0.5 + fi * 1.2) * 0.6, cos(t * 0.7 + fi * 0.8) * 0.6);
        float d = length(p - orb_pos);
        float glow = 0.02 / d;
        
        vec3 orb_col = mix(mix(u_palette_low, u_palette_mid, fi / 5.0), u_palette_high, smoothstep(0.3, 0.8, fi / 5.0));
        col += orb_col * glow;
    }
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 9: Matrix Rain
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = uv * vec2(40.0, 30.0);
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float t = u_time * u_speed;
    
    float h = hash(ip);
    float drop = fract(h * 7.13 - t * 0.5);
    
    float char_y = fract((ip.y + drop * 30.0) / 30.0);
    float char_brightness = smoothstep(0.0, 0.05, drop) * smoothstep(1.0, 0.8, drop);
    
    float char = step(0.3, hash(ip + floor(t * 10.0)));
    float glyph = char * char_brightness;
    
    vec3 col = u_palette_high * glyph;
    
    fragColor = vec4(col, 1.0);
}
)"
};

static void CompilePreviewShader(EditorContext* editor, int shader_id) {
    if (!editor) return;
    if (shader_id < 0 || shader_id >= 10) shader_id = 0;
    
    printf("[CompilePreviewShader] Compiling shader ID: %d\n", shader_id);
    
    // Destroy old shader if exists
    if (editor->preview_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->preview_shader);
        editor->preview_shader = nullptr;
        printf("[CompilePreviewShader] Destroyed old shader\n");
    }
    
    // Compile new shader with correct source
    editor->preview_shader = rev::shader::CompileFromSource(preview_vertex_shader, g_preview_fragment_shaders[shader_id]);
    editor->preview_current_shader_id = shader_id;
    
    if (editor->preview_shader) {
        printf("[CompilePreviewShader] SUCCESS: Shader %d compiled\n", shader_id);
    } else {
        printf("[CompilePreviewShader] FAILED: Shader %d compilation returned null\n", shader_id);
    }
}

void InitializePreview(EditorContext* editor, int width, int height) {
    if (!editor || editor->preview_initialized) return;
    
    // Load OpenGL functions
    typedef void (*PFNGLGENFRAMEBUFFERSPROC)(int n, unsigned int* framebuffers);
    typedef void (*PFNGLBINDFRAMEBUFFERPROC)(unsigned int target, unsigned int framebuffer);
    typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
    typedef void (*PFNGLGENRENDERBUFFERSPROC)(int n, unsigned int* renderbuffers);
    typedef void (*PFNGLBINDRENDERBUFFERPROC)(unsigned int target, unsigned int renderbuffer);
    typedef void (*PFNGLRENDERBUFFERSTORAGEPROC)(unsigned int target, unsigned int internalformat, int width, int height);
    typedef void (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(unsigned int target, unsigned int attachment, unsigned int renderbuffertarget, unsigned int renderbuffer);
    typedef unsigned int (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(unsigned int target);
    
    auto glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
    auto glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    auto glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    auto glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
    auto glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
    auto glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glRenderbufferStorage");
    auto glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
    auto glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
    
    if (!glGenFramebuffers || !glBindFramebuffer || !glFramebufferTexture2D || 
        !glGenRenderbuffers || !glBindRenderbuffer || !glRenderbufferStorage ||
        !glFramebufferRenderbuffer || !glCheckFramebufferStatus) {
        return; // OpenGL functions not available
    }
    
    editor->preview_width = width;
    editor->preview_height = height;
    
    // Create framebuffer
    glGenFramebuffers(1, &editor->preview_fbo);
    glBindFramebuffer(0x8D40, editor->preview_fbo); // GL_FRAMEBUFFER
    
    // Create color texture
    glGenTextures(1, &editor->preview_texture);
    glBindTexture(GL_TEXTURE_2D, editor->preview_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, editor->preview_texture, 0); // GL_COLOR_ATTACHMENT0
    
    // Create depth renderbuffer
    glGenRenderbuffers(1, &editor->preview_depth);
    glBindRenderbuffer(0x8D41, editor->preview_depth); // GL_RENDERBUFFER
    glRenderbufferStorage(0x8D41, 0x81A5, width, height); // GL_DEPTH_COMPONENT24
    glFramebufferRenderbuffer(0x8D40, 0x8D00, 0x8D41, editor->preview_depth); // GL_DEPTH_ATTACHMENT
    
    // Check framebuffer completeness
    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) { // GL_FRAMEBUFFER_COMPLETE
        CleanupPreview(editor);
        return;
    }
    
    // Unbind framebuffer
    glBindFramebuffer(0x8D40, 0);
    
    // Don't compile any shader yet - wait for first render with actual cue
    // (preview_current_shader_id is -1, so first cue will trigger compile)
    
    // Compile sprite shader for image/text
    editor->sprite_shader = rev::shader::CompileFromSource(sprite_vertex_shader, sprite_fragment_shader);
    if (!editor->sprite_shader) {
        CleanupPreview(editor);
        return;
    }

    editor->mesh_shader = rev::shader::CompileFromSource(mesh_vertex_shader, mesh_fragment_shader);
    // mesh_shader failure is non-fatal — mesh cues just won't render

    // Create a dummy VAO so gl_VertexID-based fullscreen-quad draws are valid in
    // OpenGL 3.3 core profile (draw calls with VAO 0 are undefined behaviour).
    typedef void (*PFNGLGENVERTEXARRAYSPROC)(int n, unsigned int* arrays);
    auto glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    if (glGenVertexArrays) {
        glGenVertexArrays(1, &editor->preview_vao);
    }

    editor->preview_initialized = true;
}

void CleanupPreview(EditorContext* editor) {
    if (!editor || !editor->preview_initialized) return;
    
    typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(int n, const unsigned int* framebuffers);
    typedef void (*PFNGLDELETERENDERBUFFERSPROC)(int n, const unsigned int* renderbuffers);
    
    auto glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
    auto glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");
    
    // Destroy shader programs
    if (editor->preview_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->preview_shader);
        editor->preview_shader = nullptr;
    }
    if (editor->sprite_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->sprite_shader);
        editor->sprite_shader = nullptr;
    }
    if (editor->mesh_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->mesh_shader);
        editor->mesh_shader = nullptr;
    }
    editor->preview_current_shader_id = -1;
    
    if (editor->preview_texture) {
        glDeleteTextures(1, &editor->preview_texture);
        editor->preview_texture = 0;
    }
    
    if (editor->preview_depth && glDeleteRenderbuffers) {
        glDeleteRenderbuffers(1, &editor->preview_depth);
        editor->preview_depth = 0;
    }
    
    if (editor->preview_fbo && glDeleteFramebuffers) {
        glDeleteFramebuffers(1, &editor->preview_fbo);
        editor->preview_fbo = 0;
    }

    if (editor->preview_vao) {
        typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(int n, const unsigned int* arrays);
        auto glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
        if (glDeleteVertexArrays) glDeleteVertexArrays(1, &editor->preview_vao);
        editor->preview_vao = 0;
    }

    editor->preview_initialized = false;
}

void ResizePreview(EditorContext* editor, int width, int height) {
    if (!editor) return;
    
    if (editor->preview_width != width || editor->preview_height != height) {
        CleanupPreview(editor);
        InitializePreview(editor, width, height);
    }
}

void RenderPreviewFrame(EditorContext* editor) {
    if (!editor || !editor->preview_initialized) return;
    
    typedef void (*PFNGLBINDFRAMEBUFFERPROC)(unsigned int target, unsigned int framebuffer);
    auto glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    
    if (!glBindFramebuffer) return;
    
    // Bind preview framebuffer
    glBindFramebuffer(0x8D40, editor->preview_fbo); // GL_FRAMEBUFFER

    // Bind the dummy VAO — required in OpenGL 3.3 core profile for any glDrawArrays
    // call, including gl_VertexID-based fullscreen quads.  Without this, draw calls
    // silently fail (e.g. background shader shows as black).
    if (editor->preview_vao) {
        typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
        auto glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
        if (glBindVertexArray) glBindVertexArray(editor->preview_vao);
    }

    // Set viewport
    glViewport(0, 0, editor->preview_width, editor->preview_height);
    
    // Clear
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Only render shader if we have a project with active shader cues
    if (editor->project) {
        // Find active shader cue in any scene
        int active_shader_id = -1;  // -1 means no active cue
        float speed = 1.0f;
        float intensity = 1.0f;
        float warp = 0.5f;
        float palette_low[3] = {0.2f, 0.0f, 0.4f};
        float palette_mid[3] = {0.8f, 0.2f, 0.6f};
        float palette_high[3] = {1.0f, 0.8f, 0.2f};
        bool found_active_cue = false;
        
        for (int s = 0; s < editor->project->scene_count; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            for (int i = 0; i < scene->shader_cue_count; i++) {
                ShaderCue* cue = &scene->shader_cues[i];
                // Handle cue_end = -1 (means until end of scene)
                float actual_end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                
                // Debug: log cue check
                static int debug_frame_count = 0;
                if (debug_frame_count % 60 == 0) { // Log every 60 frames to avoid spam
                    printf("[PREVIEW] Checking cue: id=%d time=%.2f range=[%.2f, %.2f] (actual_end=%.2f)\n",
                           cue->shader_scene_id, editor->current_time, cue->cue_start, cue->cue_end, actual_end);
                }
                debug_frame_count++;
                
                if (editor->current_time >= cue->cue_start && editor->current_time <= actual_end) {
                    active_shader_id = cue->shader_scene_id;
                    speed = cue->speed;
                    intensity = cue->intensity;
                    warp = cue->warp;
                    palette_low[0] = cue->palette_low.r;
                    palette_low[1] = cue->palette_low.g;
                    palette_low[2] = cue->palette_low.b;
                    palette_mid[0] = cue->palette_mid.r;
                    palette_mid[1] = cue->palette_mid.g;
                    palette_mid[2] = cue->palette_mid.b;
                    palette_high[0] = cue->palette_high.r;
                    palette_high[1] = cue->palette_high.g;
                    palette_high[2] = cue->palette_high.b;
                    found_active_cue = true;
                    goto found_shader;
                }
            }
        }
        found_shader:
        
        // Only render if we found an active shader cue
        if (found_active_cue && active_shader_id >= 0) {
            // Debug output
            static int last_logged_shader = -2;
            if (active_shader_id != last_logged_shader) {
                printf("[PREVIEW] Active shader ID: %d | Current compiled: %d\n", active_shader_id, editor->preview_current_shader_id);
                last_logged_shader = active_shader_id;
            }
            
            // Check if shader has changed and recompile if needed
            if (editor->preview_current_shader_id != active_shader_id) {
                printf("[PREVIEW] Recompiling shader from %d to %d\n", editor->preview_current_shader_id, active_shader_id);
                CompilePreviewShader(editor, active_shader_id);
                if (editor->preview_shader) {
                    printf("[PREVIEW] Shader %d compiled successfully!\n", active_shader_id);
                } else {
                    printf("[PREVIEW] ERROR: Shader %d compilation failed!\n", active_shader_id);
                }
            }
            
            // Use the current shader program
            auto* prog = (rev::shader::Program*)editor->preview_shader;
            if (prog) {
                rev::shader::Use(prog);
                
                rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_time"), editor->current_time);
                rev::shader::SetVec2(prog, rev::shader::GetUniformLocation(prog, "u_resolution"), 
                                    (float)editor->preview_width, (float)editor->preview_height);
                rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_palette_low"), 
                                    palette_low[0], palette_low[1], palette_low[2]);
                rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_palette_mid"), 
                                    palette_mid[0], palette_mid[1], palette_mid[2]);
                rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_palette_high"), 
                                    palette_high[0], palette_high[1], palette_high[2]);
                rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_speed"), speed);
                rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_intensity"), intensity);
                rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_warp"), warp);
                
                // Draw fullscreen quad
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }
    }
    
    // Render image/text cues as sprites (sorted by layer order)
    if (editor->sprite_shader && editor->project) {
        auto* sprite_prog = (rev::shader::Program*)editor->sprite_shader;
        rev::shader::Use(sprite_prog);
        
        // Enable blending for sprites
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Collect all active image cues with their layer order
        struct LayeredImageCue {
            ImageCue* cue;
            int layer_order;
        };
        LayeredImageCue layered_images[256]; // Max 256 images
        int image_count = 0;
        
        for (int s = 0; s < editor->project->scene_count; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            for (int i = 0; i < scene->image_cue_count && image_count < 256; i++) {
                ImageCue* cue = &scene->image_cues[i];
                // Handle cue_end = -1 (means until end of scene)
                float actual_end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                
                static int debug_frame = 0;
                if (debug_frame++ % 120 == 0) {
                    printf("[IMAGE] Checking cue: %s time=%.2f range=[%.2f, %.2f] (actual_end=%.2f) scene_count=%d\n",
                           cue->asset_key, editor->current_time, cue->cue_start, cue->cue_end, actual_end, scene->image_cue_count);
                }
                
                if (editor->current_time >= cue->cue_start && editor->current_time <= actual_end) {
                    layered_images[image_count].cue = cue;
                    layered_images[image_count].layer_order = cue->layer_order;
                    image_count++;
                    
                    static bool logged_active = false;
                    if (!logged_active) {
                        printf("[IMAGE] Found active image cue: %s\n", cue->asset_key);
                        logged_active = true;
                    }
                }
            }
        }
        
        // Simple bubble sort by layer_order (lower first)
        for (int i = 0; i < image_count - 1; i++) {
            for (int j = 0; j < image_count - i - 1; j++) {
                if (layered_images[j].layer_order > layered_images[j + 1].layer_order) {
                    LayeredImageCue temp = layered_images[j];
                    layered_images[j] = layered_images[j + 1];
                    layered_images[j + 1] = temp;
                }
            }
        }
        
        // Render active image cues in layer order
        for (int idx = 0; idx < image_count; idx++) {
            ImageCue* cue = layered_images[idx].cue;
                    // Construct full path - use project-specific assets folder
                    char full_path[512];
                    snprintf(full_path, sizeof(full_path), "%s\\%s", 
                            editor->project->assets_path, cue->asset_key);
                    
                    // Load texture (temporary - should be cached)
                    static bool logged_path = false;
                    if (!logged_path) {
                        printf("[IMAGE] Attempting to load: %s\n", full_path);
                        logged_path = true;
                    }
                    
                    rev::runtime::ImageTexture rt_img{};
                    bool img_ok = rev::runtime::LoadImageTexture(full_path, &rt_img);
                    unsigned int tex = img_ok ? rt_img.texture_id : 0u;
                    int tex_width = rt_img.width, tex_height = rt_img.height;
                    
                    static bool logged_result = false;
                    if (!logged_result) {
                        if (tex) {
                            printf("[IMAGE] LoadImageTexture SUCCESS: tex=%u\n", tex);
                        } else {
                            printf("[IMAGE] LoadImageTexture FAILED for: %s\n", full_path);
                        }
                        logged_result = true;
                    }
                    
                        if (img_ok) {
                        
                        // Calculate normalized size (screen space -1 to 1)
                        float norm_w = (tex_width * cue->scale) / editor->preview_width * 2.0f;
                        float norm_h = (tex_height * cue->scale) / editor->preview_height * 2.0f;
                        
                        // Convert 0-1 coords to -1 to 1
                        float pos_x = (cue->x * 2.0f) - 1.0f;
                        float pos_y = -((cue->y * 2.0f) - 1.0f);  // Flip Y
                        
                        // Set uniforms
                        glBindTexture(GL_TEXTURE_2D, tex);
                        rev::shader::SetInt(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_texture"), 0);
                        rev::shader::SetVec2(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_position"), 
                                           pos_x, pos_y);
                        rev::shader::SetVec2(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_size"), 
                                           norm_w, norm_h);
                        rev::shader::SetFloat(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_opacity"), 
                                            cue->opacity * rev::runtime::ComputeEffectOpacity(cue->effect_type, cue->fade_in_start, cue->fade_in_end, cue->fade_out_start, cue->fade_out_end, editor->current_time));
                        
                        // Draw sprite
                        glDrawArrays(GL_TRIANGLES, 0, 3);
                        
                        // Cleanup texture (temporary - should cache)
                        glDeleteTextures(1, &tex);
                    }
        }
        
        glDisable(GL_BLEND);
    }

    // Render text cues
    if (editor->sprite_shader && editor->project) {
        auto* sprite_prog = (rev::shader::Program*)editor->sprite_shader;
        rev::shader::Use(sprite_prog);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (int s = 0; s < editor->project->scene_count; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            for (int i = 0; i < scene->text_cue_count; i++) {
                TextCue* cue = &scene->text_cues[i];
                float actual_end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                if (editor->current_time < cue->cue_start || editor->current_time > actual_end) continue;
                if (cue->text[0] == '\0') continue;

                int tw = 0, th = 0;
                rev::runtime::TextTexture rt_txt{};
                bool txt_ok = rev::runtime::RenderTextToTexture(
                    cue->text, cue->font_name, cue->size,
                    cue->color.r, cue->color.g, cue->color.b, &rt_txt);
                if (!txt_ok) continue;
                unsigned int tex = rt_txt.texture_id;
                tw = rt_txt.width; th = rt_txt.height;

                float norm_w = (float)tw / (float)editor->preview_width  * 2.0f;
                float norm_h = (float)th / (float)editor->preview_height * 2.0f;
                float pos_x  =  (cue->x * 2.0f) - 1.0f;
                float pos_y  = -((cue->y * 2.0f) - 1.0f); // flip Y

                glBindTexture(GL_TEXTURE_2D, tex);
                rev::shader::SetInt(sprite_prog,   rev::shader::GetUniformLocation(sprite_prog, "u_texture"),  0);
                rev::shader::SetVec2(sprite_prog,  rev::shader::GetUniformLocation(sprite_prog, "u_position"), pos_x, pos_y);
                rev::shader::SetVec2(sprite_prog,  rev::shader::GetUniformLocation(sprite_prog, "u_size"),     norm_w, norm_h);
                rev::shader::SetFloat(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_opacity"),
                    rev::runtime::ComputeEffectOpacity(cue->effect_type, cue->fade_in_start, cue->fade_in_end, cue->fade_out_start, cue->fade_out_end, editor->current_time));
                glDrawArrays(GL_TRIANGLES, 0, 3);

                glDeleteTextures(1, &tex);
            }
        }

        glDisable(GL_BLEND);
    }

    // Render mesh cues (3D Phong)
    if (editor->mesh_shader && editor->project) {
        typedef void (*PFNGLENABLEPROC)(unsigned int cap);
        typedef void (*PFNGLDISABLEPROC)(unsigned int cap);
        typedef void (*PFNGLDEPTHFUNCPROC)(unsigned int func);
        auto glDepthFunc_fn = (PFNGLDEPTHFUNCPROC)wglGetProcAddress("glDepthFunc");

        glEnable(0x0B71);           // GL_DEPTH_TEST
        if (glDepthFunc_fn) glDepthFunc_fn(0x0201);  // GL_LESS

        auto* mesh_prog = (rev::shader::Program*)editor->mesh_shader;
        rev::shader::Use(mesh_prog);

        float aspect = (editor->preview_height > 0)
            ? (float)editor->preview_width / (float)editor->preview_height : 1.0f;

        // Camera
        float eye[3]    = {0.0f, 0.0f, 5.0f};
        float center[3] = {0.0f, 0.0f, 0.0f};
        float up[3]     = {0.0f, 1.0f, 0.0f};

        float view_mat[16], proj_mat[16];
        rev::runtime::Mat4Perspective(proj_mat, 3.14159265f * 0.25f, aspect, 0.1f, 100.0f);
        rev::runtime::Mat4LookAt(view_mat, eye, center, up);

        auto glUniformMatrix4fv = (void(*)(int,int,unsigned char,const float*))wglGetProcAddress("glUniformMatrix4fv");

        int loc_model = rev::shader::GetUniformLocation(mesh_prog, "u_model");
        int loc_view  = rev::shader::GetUniformLocation(mesh_prog, "u_view");
        int loc_proj  = rev::shader::GetUniformLocation(mesh_prog, "u_projection");
        int loc_light = rev::shader::GetUniformLocation(mesh_prog, "u_light_pos");
        int loc_vpos  = rev::shader::GetUniformLocation(mesh_prog, "u_view_pos");
        int loc_color = rev::shader::GetUniformLocation(mesh_prog, "u_color");
        int loc_metal = rev::shader::GetUniformLocation(mesh_prog, "u_metallic");
        int loc_rough = rev::shader::GetUniformLocation(mesh_prog, "u_roughness");

        if (glUniformMatrix4fv) {
            glUniformMatrix4fv(loc_view, 1, 0, view_mat);
            glUniformMatrix4fv(loc_proj, 1, 0, proj_mat);
        }

        float light_pos[3] = {3.0f, 5.0f, 4.0f};
        rev::shader::SetVec3(mesh_prog, loc_light, light_pos[0], light_pos[1], light_pos[2]);
        rev::shader::SetVec3(mesh_prog, loc_vpos, eye[0], eye[1], eye[2]);

        for (int s = 0; s < editor->project->scene_count; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            for (int i = 0; i < scene->mesh_cue_count; i++) {
                MeshCue* cue = &scene->mesh_cues[i];
                float actual_end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                if (editor->current_time < cue->cue_start || editor->current_time > actual_end) continue;

                float opacity = rev::runtime::ComputeEffectOpacity(
                    cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                    cue->fade_out_start, cue->fade_out_end, editor->current_time);

                // Build model matrix
                float model[16];
                rev::runtime::Mat4Model(model, cue->pos, cue->rot, cue->scale);
                if (glUniformMatrix4fv) glUniformMatrix4fv(loc_model, 1, 0, model);

                // Set color with opacity
                typedef void (*PFNGLUNIFORM4FVPROC)(int, int, const float*);
                auto glUniform4fv_fn = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
                if (glUniform4fv_fn) {
                    float col[4] = { cue->color[0], cue->color[1], cue->color[2], cue->color[3] * opacity };
                    glUniform4fv_fn(loc_color, 1, col);
                }
                rev::shader::SetFloat(mesh_prog, loc_metal, cue->metallic);
                rev::shader::SetFloat(mesh_prog, loc_rough, cue->roughness);

                // Create procedural mesh based on type
                float size  = cue->mesh_size  > 0.0f ? cue->mesh_size  : 1.0f;
                float param = cue->mesh_param > 0.0f ? cue->mesh_param : 16.0f;
                rev::mesh::Mesh* mesh = nullptr;
                switch (cue->mesh_type) {
                    case 0: mesh = rev::mesh::CreateCube(size);                         break;
                    case 1: mesh = rev::mesh::CreateSphere(size, (int)param);           break;
                    case 2: mesh = rev::mesh::CreatePlane(size, param > 0.0f ? param : size); break;
                    case 3: mesh = rev::mesh::CreateTorus(size, param > 0.0f ? param : 0.3f, 32, 16); break;
                    case 4: {
                        // External glTF/GLB
                        if (cue->asset_path[0]) {
                            rev::gltf::ImportResult* ir = rev::gltf::LoadMesh(cue->asset_path);
                            if (ir && ir->ok) {
                                mesh = ir->mesh;
                                ir->mesh = nullptr;
                            }
                            if (ir) rev::gltf::FreeImportResult(ir);
                        }
                        if (!mesh) mesh = rev::mesh::CreateCube(1.0f); // fallback if path empty or failed
                        break;
                    }
                    default: mesh = rev::mesh::CreateCube(1.0f); break;
                }
                if (!mesh) continue;

                rev::mesh::UploadToGPU(mesh);
                rev::mesh::Render(mesh, -1);
                rev::mesh::DestroyMesh(mesh);
            }
        }

        glDisable(0x0B71); // GL_DEPTH_TEST
    }

    // Unbind framebuffer (restore default)
    glBindFramebuffer(0x8D40, 0);
}

void UpdatePlayback(EditorContext* editor, float delta_time) {
    if (!editor || !editor->playing) return;
    
    editor->current_time += delta_time;
    
    // Clamp to project duration (use 10s default if duration is 0)
    if (editor->project) {
        float max_duration = editor->project->total_duration;
        if (max_duration <= 0.0f) max_duration = 10.0f; // Default playback duration
        
        if (editor->current_time >= max_duration) {
            editor->current_time = max_duration;
            editor->playing = false; // Stop at end
        }
    }
}

void RenderPreviewPanel(EditorContext* editor) {
    if (!editor || !editor->show_preview) return;
    
    ImGui::Begin("Preview", &editor->show_preview);
    
    // Initialize preview on first show
    if (!editor->preview_initialized) {
        InitializePreview(editor, 1920, 1080);
    }
    
    // Check if project is loaded
    if (!editor->project) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "No project loaded");
        ImGui::Text("Create or open a project to use the preview.");
        ImGui::End();
        return;
    }
    
    // Playback controls
    if (ImGui::Button(editor->playing ? "Pause" : "Play")) {
        editor->playing = !editor->playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        editor->playing = false;
        editor->current_time = 0.0f;
    }
    ImGui::SameLine();
    
    // Show duration info
    float display_duration = editor->project->total_duration;
    if (display_duration <= 0.0f) {
        ImGui::Text("Time: %.2fs / %.2fs (no scenes, using default)", editor->current_time, 10.0f);
    } else {
        ImGui::Text("Time: %.2fs / %.2fs", editor->current_time, display_duration);
    }
    
    // Time slider
    float max_time = editor->project->total_duration;
    if (max_time <= 0.0f) max_time = 10.0f; // Default if no duration
    
    if (ImGui::SliderFloat("##timeline_scrub", &editor->current_time, 0.0f, max_time, "%.2fs")) {
        // Pause when manually scrubbing
        if (editor->playing) {
            editor->playing = false;
        }
    }
    
    ImGui::Separator();
    
    // Display preview texture
    if (editor->preview_texture) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        
        // Calculate size maintaining 16:9 aspect ratio
        float aspect = 16.0f / 9.0f;
        float w = avail.x;
        float h = w / aspect;
        
        if (h > avail.y) {
            h = avail.y;
            w = h * aspect;
        }
        
        // Center the preview
        float offset_x = (avail.x - w) * 0.5f;
        if (offset_x > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
        }
        
        ImGui::Image((ImTextureID)(intptr_t)editor->preview_texture, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Preview not initialized");
    }
    
    ImGui::End();
}


void RandomizeShaderValues(ShaderCue* cue) {
    if (!cue) return;
    
    cue->speed = 0.5f + (float)rand() / RAND_MAX * 1.5f;      // 0.5-2.0
    cue->intensity = 0.5f + (float)rand() / RAND_MAX * 1.0f;  // 0.5-1.5
    cue->warp = (float)rand() / RAND_MAX;                     // 0.0-1.0
}

void ResetShaderValues(ShaderCue* cue) {
    if (!cue) return;
    
    // Default palette
    cue->palette_low = {0.1f, 0.3f, 0.8f};
    cue->palette_mid = {0.45f, 0.25f, 0.7f};
    cue->palette_high = {0.8f, 0.2f, 0.6f};
    
    // Default parameters
    cue->speed = 1.0f;
    cue->intensity = 1.0f;
    cue->warp = 0.5f;
    cue->exposure_base = 0.76f;
    cue->exposure_ramp = 0.02f;
    cue->fade_base = 0.04f;
    cue->fade_ramp = -0.04f;
    
    // Default timing
    cue->cue_start = 0.0f;
    cue->cue_end = -1.0f;  // Implicit scene end
    cue->fade_in = 0.5f;
    cue->fade_out = 0.5f;
    
    // Default layer
    cue->layer_role = 0;  // Background
    cue->opacity = 1.0f;
    cue->blend_mode = 0;  // Alpha
    cue->layer_order = 0;
    
    // No curves assigned
    cue->curve_speed = -1;
    cue->curve_intensity = -1;
    cue->curve_warp = -1;
    cue->curve_exposure = -1;
    cue->curve_fade = -1;
}

}  // namespace editor
}  // namespace rev
