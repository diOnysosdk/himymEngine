#include "editor_internal.h"
#include <cstring>
#include <cstdio>
#include <windows.h>
#include "imgui.h"

namespace rev {
namespace editor {

void RenderTimeline(EditorContext* editor) {
    if (!editor) return;
    
    if (ImGui::Begin("Timeline", &editor->show_timeline)) {
        // Top controls
        ImGui::Text("Total Duration: %.2fs | Scenes: %d", 
                    editor->project->total_duration, 
                    editor->project->scene_count);

        bool loop_intro = editor->project->loop_intro;
        if (ImGui::Checkbox("Loop Intro", &loop_intro)) {
            editor->project->loop_intro = loop_intro;
            editor->project->modified = true;
        }
        ImGui::SameLine();
        bool loop_music = editor->project->loop_music;
        if (ImGui::Checkbox("Loop Music", &loop_music)) {
            editor->project->loop_music = loop_music;
            editor->project->modified = true;
        }
        
        ImGui::SliderFloat("Zoom", &editor->timeline_zoom, 0.1f, 10.0f);
        ImGui::SameLine();
        
        if (ImGui::Button("+ Scene")) {
            AddScene(editor, "New Scene", 10.0f);
        }
        
        ImGui::Separator();

        int pending_move_from = -1;
        int pending_move_to = -1;
        
        // Scene list
        for (int i = 0; i < editor->project->scene_count; ++i) {
            SceneBlock* scene = &editor->project->scenes[i];
            
            ImGui::PushID(i);
            
            bool selected = (editor->selected_scene_index == i);
            if (ImGui::Selectable(scene->name, selected, 0, ImVec2(0, 0))) {
                editor->selected_scene_index = i;
                editor->selected_cue_index = -1;
                
                // Jump to the start of the selected scene
                float scene_start_time = 0.0f;
                for (int j = 0; j < i; j++) {
                    scene_start_time += editor->project->scenes[j].duration;
                }
                editor->current_time = scene_start_time;
                editor->playing = false; // Pause playback when switching scenes
            }

            // Drag scene row to reorder timeline scene order.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                int drag_scene_index = i;
                ImGui::SetDragDropPayload("TIMELINE_SCENE_INDEX", &drag_scene_index, sizeof(int));
                ImGui::Text("Move Scene: %s", scene->name);
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TIMELINE_SCENE_INDEX")) {
                    if (payload->DataSize == sizeof(int)) {
                        int from_index = *(const int*)payload->Data;
                        if (from_index >= 0 && from_index < editor->project->scene_count && from_index != i) {
                            pending_move_from = from_index;
                            pending_move_to = i;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            
            ImGui::SameLine();
            ImGui::Text("%.2fs", scene->duration);

            ImGui::SameLine();
            bool can_move_up = (i > 0);
            if (!can_move_up) ImGui::BeginDisabled();
            if (ImGui::SmallButton("Up")) {
                pending_move_from = i;
                pending_move_to = i - 1;
            }
            if (!can_move_up) ImGui::EndDisabled();

            ImGui::SameLine();
            bool can_move_down = (i < editor->project->scene_count - 1);
            if (!can_move_down) ImGui::BeginDisabled();
            if (ImGui::SmallButton("Down")) {
                pending_move_from = i;
                pending_move_to = i + 1;
            }
            if (!can_move_down) ImGui::EndDisabled();
            
            // Show cue counts
            if (scene->shader_cue_count > 0 || scene->image_cue_count > 0 || 
                scene->text_cue_count > 0 || scene->music_cue_count > 0 ||
                scene->mesh_cue_count > 0) {
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
                if (scene->mesh_cue_count > 0) {
                    ImGui::Text("  Meshes: %d", scene->mesh_cue_count);
                }
                ImGui::Unindent();
            }
            
            ImGui::PopID();
        }

        if (pending_move_from >= 0 && pending_move_to >= 0) {
            MoveScene(editor, pending_move_from, pending_move_to);

            // Keep selected scene consistent after reorder.
            int sel = editor->selected_scene_index;
            if (sel == pending_move_from) {
                editor->selected_scene_index = pending_move_to;
            } else if (pending_move_from < pending_move_to) {
                if (sel > pending_move_from && sel <= pending_move_to) {
                    editor->selected_scene_index = sel - 1;
                }
            } else {
                if (sel >= pending_move_to && sel < pending_move_from) {
                    editor->selected_scene_index = sel + 1;
                }
            }
        }
        
        ImGui::Separator();
        
        // Bottom controls
        if (editor->selected_scene_index >= 0) {
            bool can_move_selected_up = (editor->selected_scene_index > 0);
            if (!can_move_selected_up) ImGui::BeginDisabled();
            if (ImGui::Button("Move Selected Up")) {
                int from = editor->selected_scene_index;
                int to = from - 1;
                MoveScene(editor, from, to);
                editor->selected_scene_index = to;
            }
            if (!can_move_selected_up) ImGui::EndDisabled();

            ImGui::SameLine();
            bool can_move_selected_down = (editor->selected_scene_index < editor->project->scene_count - 1);
            if (!can_move_selected_down) ImGui::BeginDisabled();
            if (ImGui::Button("Move Selected Down")) {
                int from = editor->selected_scene_index;
                int to = from + 1;
                MoveScene(editor, from, to);
                editor->selected_scene_index = to;
            }
            if (!can_move_selected_down) ImGui::EndDisabled();

            ImGui::SameLine();
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
                LoadShaderPreset(&cue, 0);  // Default to first preset (Horizontal Gradient Bands)
                int new_index = AddShaderCue(scene, cue);
                // Immediately open modal for the new shader
                editor->editing_shader = scene->shader_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 0;
                editor->shader_modal_request_open = true;
                editor->project->modified = true;
            }
            
            // Disable image/mesh cue buttons if project not saved (no assets folder yet)
            bool project_saved = editor->project->workspace_path[0] != '\0';
            if (!project_saved) {
                ImGui::BeginDisabled();
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
                cue.blend_mode = 0;
                cue.curve_x = -1;
                cue.curve_y = -1;
                cue.curve_scale = -1;
                cue.curve_opacity = -1;
                int new_index = AddImageCue(scene, cue);
                editor->editing_image = scene->image_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 1;  // image
                editor->image_modal_request_open = true;
                editor->project->modified = true;
            }
            if (!project_saved && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Save the project first to set up the assets folder");
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
                cue.blend_mode = 0;
                cue.curve_x = -1;
                cue.curve_y = -1;
                cue.curve_size = -1;
                cue.curve_color_r = -1;
                cue.curve_color_g = -1;
                cue.curve_color_b = -1;
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
                cue.curve_pos_x = -1; cue.curve_pos_y = -1; cue.curve_pos_z = -1;
                cue.curve_rot_x = -1; cue.curve_rot_y = -1; cue.curve_rot_z = -1;
                cue.curve_scale_x = -1; cue.curve_scale_y = -1; cue.curve_scale_z = -1;
                cue.curve_color_r = -1; cue.curve_color_g = -1; cue.curve_color_b = -1; cue.curve_color_a = -1;
                cue.curve_mesh_size = -1; cue.curve_metallic = -1; cue.curve_roughness = -1;
                snprintf(cue.asset_key, sizeof(cue.asset_key), "mesh_%d", scene->mesh_cue_count);
                int new_index = AddMeshCue(scene, cue);
                editor->editing_mesh = scene->mesh_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = 4;
                editor->mesh_modal_request_open = true;
                editor->project->modified = true;
            }
            if (!project_saved && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Save the project first to set up the assets folder");
            }
            
            if (!project_saved) {
                ImGui::EndDisabled();
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

} // namespace editor
} // namespace rev
