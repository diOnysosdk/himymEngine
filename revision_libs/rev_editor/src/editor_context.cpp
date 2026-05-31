#include "rev_editor.h"
#include <cstring>
#include <cstdio>
#include <windows.h>

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
    editor->shader_modal_open = false;
    editor->shader_modal_request_open = false;
    editor->music_modal_open = false;
    editor->music_modal_request_open = false;
    editor->image_modal_open = false;
    editor->image_modal_request_open = false;
    editor->text_modal_open = false;
    editor->text_modal_request_open = false;
    editor->selected_curve_index = -1;
    editor->dragging_point_index = -1;
    editor->show_curve_grid = true;
    editor->build_status_message[0] = '\0';
    editor->build_status_timer = 0.0f;
    
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
        }
        delete[] editor->project->scenes;
        
        // Clean up curves
        for (int i = 0; i < editor->project->curve_count; ++i) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
        }
        delete[] editor->project->curves;
        
        delete editor->project;
    }
    
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
    bool in_curves = false;
    bool in_curve_points = false;
    
    ShaderCue current_shader_cue = {};
    ImageCue current_image_cue = {};
    TextCue current_text_cue = {};
    MusicCue current_music_cue = {};
    rev::curve::Curve* current_curve = nullptr;
    
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
                AddShaderCue(current_scene, current_shader_cue);
                memset(&current_shader_cue, 0, sizeof(current_shader_cue));
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
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f\n", cue->cue_end);
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
            fprintf(f, "          \"effect_start\": %.3f,\n", cue->effect_start);
            fprintf(f, "          \"effect_end\": %.3f\n", cue->effect_end);
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
    editor->project->modified = false;
    
    return true;
}

void BeginFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame start
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
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
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        
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
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                
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
                ofn.Flags = OFN_OVERWRITEPROMPT;
                
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
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &editor->show_demo);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Export Project")) { 
                ExportProject(editor, "assets/cues.txt"); 
            }
            if (ImGui::MenuItem("Build and Run", "F5")) { 
                BuildAndRun(editor); 
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
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            
            if (GetOpenFileNameA(&ofn)) {
                // Extract just filename
                const char* filename = strrchr(filepath, '\\');
                if (!filename) filename = strrchr(filepath, '/');
                if (filename) filename++; else filename = filepath;
                
                strncpy_s(cue->asset_key, filename, _TRUNCATE);
                strncpy_s(cue->asset_path, filepath, _TRUNCATE);
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
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            
            if (GetOpenFileNameA(&ofn)) {
                // Extract just filename
                const char* filename = strrchr(filepath, '\\\\');
                if (!filename) filename = strrchr(filepath, '/');
                if (filename) filename++; else filename = filepath;
                
                strncpy_s(cue->asset_key, filename, _TRUNCATE);
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
            ImGui::InputFloat("Effect Start", &cue->effect_start, 0.1f, 1.0f);
            ImGui::InputFloat("Effect End", &cue->effect_end, 0.1f, 1.0f);
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
    fprintf(f, "# asset_key|x|y|scale|opacity|cue_start|cue_end\n");
    
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->image_cue_count; ++cue_idx) {
            ImageCue* cue = &scene->image_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            
            fprintf(f, "%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f\n",
                cue->asset_key, cue->x, cue->y, cue->scale, cue->opacity,
                abs_start, abs_end
            );
        }
    }
    
    fprintf(f, "\n");
    
    // [text_cues] section
    fprintf(f, "[text_cues]\n");
    fprintf(f, "# text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|effect_start|effect_end\n");
    
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
            float abs_effect_start = scene_start + cue->effect_start;
            float abs_effect_end = scene_start + cue->effect_end;
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%.3f|%.3f|%.3f\n",
                cue->text, cue->font_name, cue->x, cue->y, cue->size,
                cue->color.r, cue->color.g, cue->color.b,
                cue->effect_type, abs_start, abs_end, abs_effect_start, abs_effect_end
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
    
    // Show status
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Exporting project...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    
    // Step 1: Export to cues.txt
    printf("Step 1: Exporting to assets/cues.txt...\n");
    if (!ExportProject(editor, "assets/cues.txt")) {
        printf("ERROR: Export failed!\n");
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Export failed!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }
    printf("Export complete.\n");
    
    // Step 2: Build the project using CMake
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Building intro...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    
    printf("Step 2: Building minimal_intro...\n");
    const char* build_command = "cmake --build build --config Release --target minimal_intro";
    int build_result = system(build_command);
    
    if (build_result != 0) {
        printf("ERROR: Build failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Build complete.\n");
    
    // Step 3: Launch the executable
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    
    printf("Step 3: Launching intro...\n");
    const char* run_command = "start build\\bin\\Release\\minimal_intro.exe";
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
