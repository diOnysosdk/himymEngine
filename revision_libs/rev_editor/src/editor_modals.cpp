#include "editor_internal.h"
#include "rev_shader.h"
#include "rev_mesh.h"
#include "rev_gltf.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <windows.h>
#include "imgui.h"

namespace rev {
namespace editor {
void RenderCurveEditorModal(EditorContext* editor) {
    if (!editor) return;

    // Handle open request
    if (editor->curve_editor_modal_request_open) {
        ImGui::OpenPopup("Edit Curve");
        editor->curve_editor_modal_request_open = false;
        editor->curve_editor_modal_open = true;
        editor->dragging_point_index = -1;
        editor->selected_point_index = -1;
    }

    ImGui::SetNextWindowSize(ImVec2(700, 550), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Edit Curve", &editor->curve_editor_modal_open, ImGuiWindowFlags_NoScrollbar)) {
        
        // Validate curve index
        if (editor->editing_curve_index < 0 || editor->editing_curve_index >= editor->project->curve_count) {
            ImGui::Text("Error: Invalid curve index");
            if (ImGui::Button("Close")) {
                editor->curve_editor_modal_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        rev::curve::Curve* curve = &editor->project->curves[editor->editing_curve_index];
        
        // Header with curve name
        ImGui::Text("Editing: %s", editor->editing_curve_label);
        ImGui::SameLine();
        ImGui::TextDisabled("(Curve #%d, %d points)", editor->editing_curve_index, curve->point_count);
        
        ImGui::Separator();
        
        // Curve wrap mode
        const char* wrap_modes[] = {"Clamp", "Loop", "PingPong", "Mirror"};
        int current_wrap_mode = (int)curve->wrap_mode;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Wrap Mode", &current_wrap_mode, wrap_modes, 4)) {
            curve->wrap_mode = (rev::curve::WrapMode)current_wrap_mode;
            editor->project->modified = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How the curve behaves outside 0-1 time range:\n"
                            "Clamp: Hold first/last value\n"
                            "Loop: Repeat (0-1, 0-1, ...)\n"
                            "PingPong: Bounce (0-1, 1-0, 0-1, ...)\n"
                            "Mirror: Mirror the curve");
        }
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        float duration = curve->duration;
        if (ImGui::InputFloat("Duration (s)", &duration, 0.1f, 1.0f, "%.2f")) {
            if (duration < 0.01f) duration = 0.01f;  // Minimum duration
            if (duration > 3600.0f) duration = 3600.0f;  // Maximum 1 hour
            curve->duration = duration;
            editor->project->modified = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How long the curve takes to complete (in seconds).\n"
                            "With wrap modes, the curve will loop/pingpong over this duration.");
        }
        
        ImGui::Separator();
        
        // Curve canvas
        ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, 350.0f);
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Background
        draw_list->AddRectFilled(canvas_pos, 
                                 ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                                 IM_COL32(40, 40, 40, 255));
        
        // Grid
        if (editor->show_curve_grid) {
            const int grid_lines = 10;
            for (int i = 0; i <= grid_lines; ++i) {
                float x = canvas_pos.x + (canvas_size.x / grid_lines) * i;
                float y = canvas_pos.y + (canvas_size.y / grid_lines) * i;
                draw_list->AddLine(ImVec2(x, canvas_pos.y), 
                                  ImVec2(x, canvas_pos.y + canvas_size.y),
                                  IM_COL32(60, 60, 60, 255));
                draw_list->AddLine(ImVec2(canvas_pos.x, y), 
                                  ImVec2(canvas_pos.x + canvas_size.x, y),
                                  IM_COL32(60, 60, 60, 255));
            }
        }
        
        // Border
        draw_list->AddRect(canvas_pos, 
                          ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                          IM_COL32(100, 100, 100, 255));
        
        // Draw curve line
        if (curve->point_count > 1) {
            const int segments = 100;
            for (int i = 0; i < segments; ++i) {
                float t0 = (float)i / segments;
                float t1 = (float)(i + 1) / segments;
                float v0 = rev::curve::Evaluate(*curve, t0);
                float v1 = rev::curve::Evaluate(*curve, t1);
                
                // Clamp values for display
                v0 = (v0 < 0.0f) ? 0.0f : ((v0 > 1.0f) ? 1.0f : v0);
                v1 = (v1 < 0.0f) ? 0.0f : ((v1 > 1.0f) ? 1.0f : v1);
                
                ImVec2 p0 = ImVec2(canvas_pos.x + t0 * canvas_size.x,
                                  canvas_pos.y + canvas_size.y - v0 * canvas_size.y);
                ImVec2 p1 = ImVec2(canvas_pos.x + t1 * canvas_size.x,
                                  canvas_pos.y + canvas_size.y - v1 * canvas_size.y);
                
                draw_list->AddLine(p0, p1, IM_COL32(100, 200, 255, 255), 2.0f);
            }
        }
        
        // Interaction area
        ImGui::SetCursorScreenPos(canvas_pos);
        ImGui::InvisibleButton("canvas", canvas_size);
        bool is_hovered = ImGui::IsItemHovered();
        ImVec2 mouse_pos = ImGui::GetMousePos();
        
        // First, check if we're clicking on a point (prioritize point interaction)
        bool clicked_on_point = false;
        
        // Draw and interact with control points
        for (int i = 0; i < curve->point_count; ++i) {
            rev::curve::Point* pt = &curve->points[i];
            
            float display_v = (pt->v < 0.0f) ? 0.0f : ((pt->v > 1.0f) ? 1.0f : pt->v);
            ImVec2 point_pos = ImVec2(canvas_pos.x + pt->t * canvas_size.x,
                                     canvas_pos.y + canvas_size.y - display_v * canvas_size.y);
            
            float point_radius = 6.0f;
            bool point_hovered = (mouse_pos.x - point_pos.x) * (mouse_pos.x - point_pos.x) +
                                (mouse_pos.y - point_pos.y) * (mouse_pos.y - point_pos.y) < point_radius * point_radius;
            
            if (point_hovered && ImGui::IsMouseClicked(0)) {
                editor->dragging_point_index = i;
                editor->selected_point_index = i;
                clicked_on_point = true;
            }
            
            // Double-click to open point properties modal
            if (point_hovered && ImGui::IsMouseDoubleClicked(0)) {
                editor->selected_point_index = i;
                editor->point_properties_modal_open = true;
                ImGui::OpenPopup("Point Properties");
            }
            
            if (editor->dragging_point_index == i && ImGui::IsMouseDragging(0)) {
                bool is_first = (i == 0);
                bool is_last = (i == curve->point_count - 1);
                
                if (is_first || is_last) {
                    // Endpoints: only allow vertical (value) movement
                    pt->v = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
                    // Keep time locked
                    pt->t = is_first ? 0.0f : 1.0f;
                } else {
                    // Middle points: allow both time and value movement
                    pt->t = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
                    pt->v = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
                    pt->t = (pt->t < 0.0f) ? 0.0f : ((pt->t > 1.0f) ? 1.0f : pt->t);
                }
                // Don't clamp v - allow any value for rotations, etc.
                editor->project->modified = true;
            }
            
            if (editor->dragging_point_index == i && ImGui::IsMouseReleased(0)) {
                editor->dragging_point_index = -1;
                rev::curve::SortPoints(*curve);
            }
            
            if (point_hovered && ImGui::IsMouseClicked(1) && curve->point_count > 2) {
                // Delete point
                for (int j = i; j < curve->point_count - 1; ++j) {
                    curve->points[j] = curve->points[j + 1];
                }
                curve->point_count--;
                editor->project->modified = true;
                // Adjust selected index if needed
                if (editor->selected_point_index == i) {
                    editor->selected_point_index = -1;
                } else if (editor->selected_point_index > i) {
                    editor->selected_point_index--;
                }
                break;
            }
            
            // Visual feedback for selection
            ImU32 point_color;
            if (editor->selected_point_index == i) {
                point_color = IM_COL32(100, 255, 100, 255);  // Green for selected
            } else if (editor->dragging_point_index == i) {
                point_color = IM_COL32(255, 255, 100, 255);  // Yellow for dragging
            } else if (point_hovered) {
                point_color = IM_COL32(255, 200, 100, 255);  // Orange for hovered
            } else {
                point_color = IM_COL32(255, 255, 255, 255);  // White for normal
            }
            draw_list->AddCircleFilled(point_pos, point_radius, point_color);
            draw_list->AddCircle(point_pos, point_radius, IM_COL32(0, 0, 0, 255), 0, 1.5f);
        }
        
        // Click on canvas background deselects point (only if we didn't click on a point)
        if (is_hovered && ImGui::IsMouseClicked(0) && !clicked_on_point) {
            editor->selected_point_index = -1;
        }
        
        // Add point on double-click (only if not clicking on existing point)
        if (is_hovered && ImGui::IsMouseDoubleClicked(0) && !clicked_on_point) {
            float t = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
            float v = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
            t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
            rev::curve::AddPoint(*curve, t, v, rev::curve::EaseMode::Linear);
            rev::curve::SortPoints(*curve);
            editor->project->modified = true;
            editor->selected_point_index = curve->point_count - 1;
        }
        
        ImGui::Separator();
        
        // Point properties
        if (editor->selected_point_index >= 0 && editor->selected_point_index < curve->point_count) {
            rev::curve::Point* pt = &curve->points[editor->selected_point_index];
            ImGui::Text("Selected Point %d:", editor->selected_point_index);
            ImGui::Text("Time: %.3f  |  Value: %.3f", pt->t, pt->v);
            
            const char* ease_modes[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut", "Smoothstep", "Hold"};
            int current_mode = (int)pt->mode;
            ImGui::SetNextItemWidth(200);
            if (ImGui::Combo("Ease Mode", &current_mode, ease_modes, 6)) {
                pt->mode = (rev::curve::EaseMode)current_mode;
                editor->project->modified = true;
            }
        } else {
            ImGui::TextDisabled("Click a point to edit its properties");
        }
        
        ImGui::Separator();
        
        // Controls
        ImGui::Checkbox("Show Grid", &editor->show_curve_grid);
        ImGui::SameLine();
        ImGui::TextDisabled("  |  ");
        ImGui::SameLine();
        ImGui::TextDisabled("Click: Select | Double-click point: Edit | Double-click canvas: Add | Drag: Move | Right-click: Delete");
        
        // Point Properties Modal
        if (ImGui::BeginPopupModal("Point Properties", &editor->point_properties_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (editor->selected_point_index >= 0 && editor->selected_point_index < curve->point_count) {
                rev::curve::Point* pt = &curve->points[editor->selected_point_index];
                
                ImGui::Text("Edit Point %d", editor->selected_point_index);
                ImGui::Separator();
                
                bool is_first_point = (editor->selected_point_index == 0);
                bool is_last_point = (editor->selected_point_index == curve->point_count - 1);
                
                // Time input
                ImGui::Text("Time:");
                ImGui::SameLine();
                float time_val = pt->t;
                ImGui::SetNextItemWidth(150);
                
                if (is_first_point || is_last_point) {
                    // Lock endpoints at 0 and 1
                    ImGui::BeginDisabled();
                    ImGui::DragFloat("##time", &time_val, 0.001f, 0.0f, 1.0f, "%.3f");
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s point is locked at t=%.1f\nUse Duration to control playback speed", 
                                        is_first_point ? "Start" : "End", 
                                        is_first_point ? 0.0f : 1.0f);
                    }
                } else {
                    if (ImGui::DragFloat("##time", &time_val, 0.001f, 0.0f, 1.0f, "%.3f")) {
                        pt->t = time_val;
                        editor->project->modified = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Position along the curve timeline (0 = start, 1 = end)\nClick and drag, or click to type");
                    }
                }
                
                // Value input - NOT clamped - allow any value
                ImGui::Text("Value:");
                ImGui::SameLine();
                float value_val = pt->v;
                ImGui::SetNextItemWidth(150);
                if (ImGui::DragFloat("##value", &value_val, 0.1f, -FLT_MAX, FLT_MAX, "%.3f")) {
                    pt->v = value_val;
                    editor->project->modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Output value at this point\n(can be any number for rotations, positions, etc.)\nClick and drag, or click to type");
                }
                
                // Ease Mode dropdown
                const char* ease_modes[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut", "Smoothstep", "Hold"};
                int current_mode = (int)pt->mode;
                ImGui::SetNextItemWidth(200);
                if (ImGui::Combo("Ease Mode", &current_mode, ease_modes, 6)) {
                    pt->mode = (rev::curve::EaseMode)current_mode;
                    editor->project->modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Interpolation method between this point and the next");
                }
                
                ImGui::Separator();
                
                if (ImGui::Button("Done", ImVec2(200, 0))) {
                    // Re-sort points in case time was changed
                    rev::curve::SortPoints(*curve);
                    editor->point_properties_modal_open = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                ImGui::Text("Invalid point selection");
                if (ImGui::Button("Close", ImVec2(120, 0))) {
                    editor->point_properties_modal_open = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            
            ImGui::EndPopup();
        }
        
        ImGui::Separator();
        
        // Action buttons
        if (ImGui::Button("Done", ImVec2(120, 0))) {
            editor->curve_editor_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        
        // Delete curve button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        
        if (ImGui::Button("Delete Curve", ImVec2(120, 0))) {
            ImGui::OpenPopup("Confirm Delete");
        }
        
        ImGui::PopStyleColor(3);
        
        // Confirmation popup for delete
        if (ImGui::BeginPopupModal("Confirm Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete this curve?");
            ImGui::Text("This will remove the animation from %s.", editor->editing_curve_label);
            ImGui::Separator();
            
            if (ImGui::Button("Yes, Delete", ImVec2(120, 0))) {
                // Delete the curve by resetting the field to -1
                int curve_index = editor->editing_curve_index;
                int cue_type = editor->editing_curve_cue_type;
                
                if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                    SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                    if (scene) {
                        // Reset curve field to -1 based on cue type
                        if (cue_type == 0 && editor->selected_cue_index < scene->shader_cue_count) {
                            // Shader cue
                            ShaderCue* cue = &scene->shader_cues[editor->selected_cue_index];
                            if (cue->curve_speed == curve_index) cue->curve_speed = -1;
                            if (cue->curve_intensity == curve_index) cue->curve_intensity = -1;
                            if (cue->curve_warp == curve_index) cue->curve_warp = -1;
                            if (cue->curve_exposure == curve_index) cue->curve_exposure = -1;
                            if (cue->curve_fade == curve_index) cue->curve_fade = -1;
                            if (cue->curve_palette_low_r == curve_index) cue->curve_palette_low_r = -1;
                            if (cue->curve_palette_low_g == curve_index) cue->curve_palette_low_g = -1;
                            if (cue->curve_palette_low_b == curve_index) cue->curve_palette_low_b = -1;
                            if (cue->curve_palette_mid_r == curve_index) cue->curve_palette_mid_r = -1;
                            if (cue->curve_palette_mid_g == curve_index) cue->curve_palette_mid_g = -1;
                            if (cue->curve_palette_mid_b == curve_index) cue->curve_palette_mid_b = -1;
                            if (cue->curve_palette_high_r == curve_index) cue->curve_palette_high_r = -1;
                            if (cue->curve_palette_high_g == curve_index) cue->curve_palette_high_g = -1;
                            if (cue->curve_palette_high_b == curve_index) cue->curve_palette_high_b = -1;
                            if (cue->curve_opacity == curve_index) cue->curve_opacity = -1;
                            if (cue->curve_exposure_ramp == curve_index) cue->curve_exposure_ramp = -1;
                            if (cue->curve_fade_ramp == curve_index) cue->curve_fade_ramp = -1;
                            editor->editing_shader = *cue; // Update editing copy
                        } else if (cue_type == 1 && editor->selected_cue_index < scene->image_cue_count) {
                            // Image cue
                            ImageCue* cue = &scene->image_cues[editor->selected_cue_index];
                            if (cue->curve_x == curve_index) cue->curve_x = -1;
                            if (cue->curve_y == curve_index) cue->curve_y = -1;
                            if (cue->curve_scale == curve_index) cue->curve_scale = -1;
                            if (cue->curve_opacity == curve_index) cue->curve_opacity = -1;
                            editor->editing_image = *cue; // Update editing copy
                        } else if (cue_type == 2 && editor->selected_cue_index < scene->text_cue_count) {
                            // Text cue
                            TextCue* cue = &scene->text_cues[editor->selected_cue_index];
                            if (cue->curve_size == curve_index) cue->curve_size = -1;
                            if (cue->curve_color_r == curve_index) cue->curve_color_r = -1;
                            if (cue->curve_color_g == curve_index) cue->curve_color_g = -1;
                            if (cue->curve_color_b == curve_index) cue->curve_color_b = -1;
                            if (cue->curve_x == curve_index) cue->curve_x = -1;
                            if (cue->curve_y == curve_index) cue->curve_y = -1;
                            editor->editing_text = *cue; // Update editing copy
                        } else if (cue_type == 3 && editor->selected_cue_index < scene->mesh_cue_count) {
                            // Mesh cue
                            MeshCue* cue = &scene->mesh_cues[editor->selected_cue_index];
                            if (cue->curve_mesh_size == curve_index) cue->curve_mesh_size = -1;
                            if (cue->curve_pos_x == curve_index) cue->curve_pos_x = -1;
                            if (cue->curve_pos_y == curve_index) cue->curve_pos_y = -1;
                            if (cue->curve_pos_z == curve_index) cue->curve_pos_z = -1;
                            if (cue->curve_rot_x == curve_index) cue->curve_rot_x = -1;
                            if (cue->curve_rot_y == curve_index) cue->curve_rot_y = -1;
                            if (cue->curve_rot_z == curve_index) cue->curve_rot_z = -1;
                            if (cue->curve_scale_x == curve_index) cue->curve_scale_x = -1;
                            if (cue->curve_scale_y == curve_index) cue->curve_scale_y = -1;
                            if (cue->curve_scale_z == curve_index) cue->curve_scale_z = -1;
                            if (cue->curve_color_r == curve_index) cue->curve_color_r = -1;
                            if (cue->curve_color_g == curve_index) cue->curve_color_g = -1;
                            if (cue->curve_color_b == curve_index) cue->curve_color_b = -1;
                            if (cue->curve_color_a == curve_index) cue->curve_color_a = -1;
                            if (cue->curve_metallic == curve_index) cue->curve_metallic = -1;
                            if (cue->curve_roughness == curve_index) cue->curve_roughness = -1;
                            editor->editing_mesh = *cue; // Update editing copy
                        }
                        editor->project->modified = true;
                    }
                }
                
                editor->curve_editor_modal_open = false;
                ImGui::CloseCurrentPopup();
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        ImGui::EndPopup();
    } else {
        if (editor->curve_editor_modal_open) {
            editor->curve_editor_modal_open = false;
        }
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
    
    // ImGui shader modal (NULL = no close button, must use Close)
    if (ImGui::BeginPopupModal("Shader Parameters", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ShaderCue* cue = &editor->editing_shader;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->shader_cue_count) {
                    scene->shader_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };
        
        // Helper lambda for curve buttons
        auto OpenShaderCurve = [&](int& curve_field, const char* label, float current_value) {
            if (curve_field < 0 && editor->project->curve_count < 32) {
                // Create new curve
                rev::curve::Curve& curve = editor->project->curves[editor->project->curve_count];
                curve = rev::curve::CreateCurve(16);
                rev::curve::AddPoint(curve, 0.0f, current_value);
                rev::curve::AddPoint(curve, 1.0f, current_value);
                curve_field = editor->project->curve_count;
                editor->project->curve_count++;
                editor->project->modified = true;
                AutoSave(); // Save curve assignment immediately
            }
            
            // Open curve editor modal (validate index first)
            if (curve_field >= 0 && curve_field < editor->project->curve_count) {
                AutoSave(); // Save before opening curve editor
                editor->editing_curve_index = curve_field;
                editor->editing_curve_cue_type = 0; // Shader
                snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", label);
                editor->curve_editor_modal_request_open = true;
            }
        };
        
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
                    AutoSave();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Separator();
        
        // Color palette with individual R, G, B sliders and curve buttons
        ImGui::Text("Color Palette:");
        
        // Palette Low
        ImGui::Text("Low:");
        if (ImGui::SliderFloat("R##low", &cue->palette_low.r, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_low_r >= 0 ? "[C]##low_r" : "+##low_r")) {
            OpenShaderCurve(cue->curve_palette_low_r, "Shader Palette Low R", cue->palette_low.r);
        }
        
        if (ImGui::SliderFloat("G##low", &cue->palette_low.g, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_low_g >= 0 ? "[C]##low_g" : "+##low_g")) {
            OpenShaderCurve(cue->curve_palette_low_g, "Shader Palette Low G", cue->palette_low.g);
        }
        
        if (ImGui::SliderFloat("B##low", &cue->palette_low.b, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_low_b >= 0 ? "[C]##low_b" : "+##low_b")) {
            OpenShaderCurve(cue->curve_palette_low_b, "Shader Palette Low B", cue->palette_low.b);
        }
        
        // Palette Mid
        ImGui::Text("Mid:");
        if (ImGui::SliderFloat("R##mid", &cue->palette_mid.r, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_mid_r >= 0 ? "[C]##mid_r" : "+##mid_r")) {
            OpenShaderCurve(cue->curve_palette_mid_r, "Shader Palette Mid R", cue->palette_mid.r);
        }
        
        if (ImGui::SliderFloat("G##mid", &cue->palette_mid.g, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_mid_g >= 0 ? "[C]##mid_g" : "+##mid_g")) {
            OpenShaderCurve(cue->curve_palette_mid_g, "Shader Palette Mid G", cue->palette_mid.g);
        }
        
        if (ImGui::SliderFloat("B##mid", &cue->palette_mid.b, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_mid_b >= 0 ? "[C]##mid_b" : "+##mid_b")) {
            OpenShaderCurve(cue->curve_palette_mid_b, "Shader Palette Mid B", cue->palette_mid.b);
        }
        
        // Palette High
        ImGui::Text("High:");
        if (ImGui::SliderFloat("R##high", &cue->palette_high.r, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_high_r >= 0 ? "[C]##high_r" : "+##high_r")) {
            OpenShaderCurve(cue->curve_palette_high_r, "Shader Palette High R", cue->palette_high.r);
        }
        
        if (ImGui::SliderFloat("G##high", &cue->palette_high.g, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_high_g >= 0 ? "[C]##high_g" : "+##high_g")) {
            OpenShaderCurve(cue->curve_palette_high_g, "Shader Palette High G", cue->palette_high.g);
        }
        
        if (ImGui::SliderFloat("B##high", &cue->palette_high.b, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_high_b >= 0 ? "[C]##high_b" : "+##high_b")) {
            OpenShaderCurve(cue->curve_palette_high_b, "Shader Palette High B", cue->palette_high.b);
        }
        
        if (ImGui::Button("Randomize Colors")) {
            RandomizeShaderColors(cue);
            AutoSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Colors")) {
            cue->palette_low = {0.0f, 0.0f, 0.0f};
            cue->palette_mid = {0.0f, 0.0f, 0.0f};
            cue->palette_high = {0.0f, 0.0f, 0.0f};
            AutoSave();
        }
        
        ImGui::Separator();
        
        // Animation parameters with curve buttons
        ImGui::Text("Animation:");
        
        if (ImGui::SliderFloat("Speed", &cue->speed, 0.1f, 5.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_speed >= 0 ? "[C]##speed" : "+##speed")) {
            OpenShaderCurve(cue->curve_speed, "Shader Speed", cue->speed);
        }
        
        if (ImGui::SliderFloat("Intensity", &cue->intensity, 0.0f, 2.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_intensity >= 0 ? "[C]##intensity" : "+##intensity")) {
            OpenShaderCurve(cue->curve_intensity, "Shader Intensity", cue->intensity);
        }
        
        if (ImGui::SliderFloat("Warp", &cue->warp, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_warp >= 0 ? "[C]##warp" : "+##warp")) {
            OpenShaderCurve(cue->curve_warp, "Shader Warp", cue->warp);
        }
        
        if (ImGui::Button("Randomize Values")) {
            RandomizeShaderValues(cue);
            AutoSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Values")) {
            cue->speed = 1.0f;
            cue->intensity = 1.0f;
            cue->warp = 0.5f;
            AutoSave();
        }
        
        ImGui::Separator();
        
        // Exposure & fade with curve buttons
        ImGui::Text("Exposure:");
        if (ImGui::SliderFloat("Base##exp", &cue->exposure_base, 0.0f, 2.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_exposure >= 0 ? "[C]##exp_base" : "+##exp_base")) {
            OpenShaderCurve(cue->curve_exposure, "Shader Exposure Base", cue->exposure_base);
        }
        
        if (ImGui::SliderFloat("Ramp##exp", &cue->exposure_ramp, -0.5f, 0.5f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_exposure_ramp >= 0 ? "[C]##exp_ramp" : "+##exp_ramp")) {
            OpenShaderCurve(cue->curve_exposure_ramp, "Shader Exposure Ramp", cue->exposure_ramp);
        }
        
        ImGui::Text("Fade:");
        if (ImGui::SliderFloat("Base##fade", &cue->fade_base, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_fade >= 0 ? "[C]##fade_base" : "+##fade_base")) {
            OpenShaderCurve(cue->curve_fade, "Shader Fade Base", cue->fade_base);
        }
        
        if (ImGui::SliderFloat("Ramp##fade", &cue->fade_ramp, -0.5f, 0.5f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_fade_ramp >= 0 ? "[C]##fade_ramp" : "+##fade_ramp")) {
            OpenShaderCurve(cue->curve_fade_ramp, "Shader Fade Ramp", cue->fade_ramp);
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing:");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade In", &cue->fade_in, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade Out", &cue->fade_out, 0.1f, 1.0f)) AutoSave();
        
        ImGui::Separator();
        
        // Layer controls
        ImGui::Text("Layer:");
        const char* layer_roles[] = {"Background", "Midground", "Foreground", "Overlay"};
        if (ImGui::Combo("Role", &cue->layer_role, layer_roles, 4)) AutoSave();
        
        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_opacity >= 0 ? "[C]##opacity" : "+##opacity")) {
            OpenShaderCurve(cue->curve_opacity, "Shader Opacity", cue->opacity);
        }
        
        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();
        if (ImGui::InputInt("Order", &cue->layer_order)) AutoSave();
        
        ImGui::Separator();
        
        // Close button (changes auto-saved continuously)
        if (ImGui::Button("Close", ImVec2(240, 0))) {
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
    
    // Music modal (NULL = no close button)
    if (ImGui::BeginPopupModal("Music Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        MusicCue* cue = &editor->editing_music;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 3) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->music_cue_count) {
                    scene->music_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };
        
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
                    AutoSave();
                } else {
                    printf("[MUSIC] Warning: project not saved yet, asset not copied.\n");
                    strncpy_s(cue->asset_path, filepath, _TRUNCATE);
                }
            }
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();
        
        ImGui::Separator();
        
        // Close button (changes auto-saved continuously)
        if (ImGui::Button("Close", ImVec2(240, 0))) {
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
    
    // Image modal (NULL = no close button)
    if (ImGui::BeginPopupModal("Image Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImageCue* cue = &editor->editing_image;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 1) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    scene->image_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };
        
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
                    AutoSave();
                } else {
                    printf("[IMAGE] Warning: project not saved yet, asset not copied.\n");
                }
            }
        }
        
        ImGui::Separator();
        
        // Position
        ImGui::Text("Position (0.0-1.0):");
        if (ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_x")) {
            // Get the actual scene cue to modify it directly
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 1) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    // Create curve if it doesn't exist
                    if (actual_cue->curve_x < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_x = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_x];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->x);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->x);
                        editor->project->modified = true;
                    }
                    
                    // Sync back to editing copy
                    cue->curve_x = actual_cue->curve_x;
                    
                    // Open curve editor modal
                    if (actual_cue->curve_x >= 0 && actual_cue->curve_x < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_x;
                        editor->editing_curve_cue_type = 1; // Image
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image X Position");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_y")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 1) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_y < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_y = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_y];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->y);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->y);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_y = actual_cue->curve_y;
                    
                    if (actual_cue->curve_y >= 0 && actual_cue->curve_y < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_y;
                        editor->editing_curve_cue_type = 1;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image Y Position");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        
        // Transform
        ImGui::Text("Transform:");
        if (ImGui::SliderFloat("Scale", &cue->scale, 0.1f, 5.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_scale >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_scale);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_scale")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 1) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_scale < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_scale = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_scale];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->scale);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->scale);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_scale = actual_cue->curve_scale;
                    
                    if (actual_cue->curve_scale >= 0 && actual_cue->curve_scale < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_scale;
                        editor->editing_curve_cue_type = 1;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image Scale");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_opacity >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_opacity);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_opacity")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 1) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_opacity < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_opacity = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_opacity];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->opacity);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->opacity);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_opacity = actual_cue->curve_opacity;
                    
                    if (actual_cue->curve_opacity >= 0 && actual_cue->curve_opacity < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_opacity;
                        editor->editing_curve_cue_type = 1;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image Opacity");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        
        // Layer
        ImGui::Text("Layer Order (lower draws first):");
        if (ImGui::SliderInt("Layer", &cue->layer_order, -10, 10)) AutoSave();
        
        ImGui::Separator();
        
        // Effect
        ImGui::Text("Effect:");
        const char* img_effects[] = {"None", "Fade In/Out"};
        if (ImGui::Combo("Type##img", &cue->effect_type, img_effects, 2)) AutoSave();
        if (cue->effect_type > 0) {
            if (ImGui::InputFloat("Fade In Start##img",  &cue->fade_in_start,  0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade In End##img",    &cue->fade_in_end,    0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out Start##img", &cue->fade_out_start, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out End##img",   &cue->fade_out_end,   0.1f, 1.0f)) AutoSave();
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();
        
        ImGui::Separator();
        
        // Close button (changes auto-saved continuously)
        if (ImGui::Button("Close", ImVec2(240, 0))) {
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
    
    // Text modal (NULL = no close button)
    if (ImGui::BeginPopupModal("Text Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        TextCue* cue = &editor->editing_text;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    scene->text_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };
        
        ImGui::Text("Text Content:");
        if (ImGui::InputTextMultiline("##text", cue->text, sizeof(cue->text), ImVec2(400, 100))) AutoSave();
        
        ImGui::Separator();
        
        // Font
        ImGui::Text("Font:");
        if (ImGui::InputText("Font Name", cue->font_name, sizeof(cue->font_name))) AutoSave();
        if (ImGui::SliderFloat("Size", &cue->size, 8.0f, 128.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_size >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_size);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_size")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_size < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_size = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_size];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->size);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->size);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_size = actual_cue->curve_size;
                    
                    if (actual_cue->curve_size >= 0 && actual_cue->curve_size < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_size;
                        editor->editing_curve_cue_type = 2;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Size");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        
        // Color
        ImGui::Text("Color:");
        if (ImGui::ColorEdit3("##textcolor", &cue->color.r)) AutoSave();
        
        // Individual curve buttons for RGB
        ImGui::Text("Color Curves:");
        
        ImGui::Text("R:");
        ImGui::SameLine();
        if (cue->curve_color_r >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_r);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_r")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_color_r < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_r = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_r];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color.r);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color.r);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_color_r = actual_cue->curve_color_r;
                    
                    if (actual_cue->curve_color_r >= 0 && actual_cue->curve_color_r < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_r;
                        editor->editing_curve_cue_type = 2;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Color R");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit R curve");
        
        ImGui::SameLine();
        ImGui::Text("G:");
        ImGui::SameLine();
        if (cue->curve_color_g >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_g);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_g")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_color_g < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_g = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_g];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color.g);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color.g);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_color_g = actual_cue->curve_color_g;
                    
                    if (actual_cue->curve_color_g >= 0 && actual_cue->curve_color_g < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_g;
                        editor->editing_curve_cue_type = 2;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Color G");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit G curve");
        
        ImGui::SameLine();
        ImGui::Text("B:");
        ImGui::SameLine();
        if (cue->curve_color_b >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_b);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_b")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_color_b < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_b = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_b];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color.b);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color.b);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_color_b = actual_cue->curve_color_b;
                    
                    if (actual_cue->curve_color_b >= 0 && actual_cue->curve_color_b < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_b;
                        editor->editing_curve_cue_type = 2;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Color B");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit B curve");
        
        ImGui::Separator();
        
        // Position
        ImGui::Text("Position (0.0-1.0):");
        if (ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_x")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_x < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_x = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_x];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->x);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->x);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_x = actual_cue->curve_x;
                    
                    if (actual_cue->curve_x >= 0 && actual_cue->curve_x < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_x;
                        editor->editing_curve_cue_type = 2;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text X Position");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_y")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == 2) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_y < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_y = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_y];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->y);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->y);
                        editor->project->modified = true;
                    }
                    
                    cue->curve_y = actual_cue->curve_y;
                    
                    if (actual_cue->curve_y >= 0 && actual_cue->curve_y < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_y;
                        editor->editing_curve_cue_type = 2;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Y Position");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        
        // Effect
        ImGui::Text("Effect:");
        const char* effects[] = {"None", "Fade In/Out", "Scroll"};
        if (ImGui::Combo("Type", &cue->effect_type, effects, 3)) AutoSave();
        
        if (cue->effect_type > 0) {
            if (ImGui::InputFloat("Fade In Start",  &cue->fade_in_start,  0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade In End",    &cue->fade_in_end,    0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out Start", &cue->fade_out_start, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out End",   &cue->fade_out_end,   0.1f, 1.0f)) AutoSave();
        }
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();
        
        ImGui::Separator();
        
        // Layer
        ImGui::Text("Layer Order (lower draws first):");
        if (ImGui::SliderInt("Layer", &cue->layer_order, -10, 10)) AutoSave();
        
        ImGui::Separator();
        
        // Close button (changes auto-saved continuously)
        if (ImGui::Button("Close", ImVec2(240, 0))) {
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
        
        // Pre-load glTF mesh into cache if not already there
        MeshCue* cue = &editor->editing_mesh;
        if (cue->mesh_type == 4 && cue->asset_path[0]) {
            bool already_cached = false;
            for (int c = 0; c < editor->mesh_cache_count; ++c) {
                if (strcmp(editor->mesh_cache[c].path, cue->asset_path) == 0) {
                    already_cached = true;
                    break;
                }
            }
            
            if (!already_cached && editor->project && editor->project->assets_path[0]) {
                printf("[MeshModal] Pre-loading glTF mesh: %s\n", cue->asset_path);
                rev::gltf::ImportResult* ir = rev::gltf::LoadMesh(cue->asset_path, editor->project->assets_path);
                if (ir && ir->ok) {
                    rev::mesh::Mesh* mesh = ir->mesh;
                    
                    // Load texture if present
                    if (mesh && ir->material.base_color_texture[0]) {
                        rev::runtime::ImageTexture tex = {};
                        if (rev::runtime::LoadImageTexture(ir->material.base_color_texture, &tex)) {
                            mesh->base_color_texture = tex.texture_id;
                        }
                    }
                    
                    // Transfer animations
                    if (mesh && ir->animation_count > 0) {
                        mesh->animation_data = ir->animations;
                        mesh->animation_count = ir->animation_count;
                        mesh->current_animation = 0;
                        mesh->animation_time = 0.0f;
                        mesh->animation_speed = 1.0f;
                        mesh->animation_loop = true;
                        ir->animations = nullptr;
                    }
                    
                    if (mesh) {
                        rev::mesh::UploadToGPU(mesh);
                        
                        // Add to cache
                        if (editor->mesh_cache_count < EditorContext::kMeshCacheSize) {
                            auto& entry = editor->mesh_cache[editor->mesh_cache_count++];
                            strncpy_s(entry.path, cue->asset_path, _TRUNCATE);
                            entry.mesh = mesh;
                            entry.last_write_time = GetFileModificationTime(cue->asset_path);
                            printf("[MeshModal] Cached mesh with %d animations\n", mesh->animation_count);
                        }
                    }
                }
                if (ir) rev::gltf::FreeImportResult(ir);
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(480, 500), ImGuiCond_FirstUseEver);
    // Edit Mesh Cue modal (NULL = no close button)
    if (ImGui::BeginPopupModal("Edit Mesh Cue", NULL)) {
        MeshCue* cue = &editor->editing_mesh;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->project && editor->selected_scene_index >= 0 &&
                editor->selected_scene_index < editor->project->scene_count) {
                SceneBlock* scene = &editor->project->scenes[editor->selected_scene_index];
                if (editor->selected_cue_index >= 0 &&
                    editor->selected_cue_index < scene->mesh_cue_count) {
                    scene->mesh_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };

        if (ImGui::InputText("Asset Key", cue->asset_key, sizeof(cue->asset_key))) AutoSave();
        if (ImGui::Combo("Shape", &cue->mesh_type, mesh_type_names, 5)) AutoSave();

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
                        AutoSave();
                    } else {
                        strncpy_s(cue->asset_path, filepath, _TRUNCATE);
                        printf("[GLTF] Warning: project not saved yet, asset not copied.\n");
                    }

                    // Extract material properties from the glTF and pre-fill the cue.
                    // This reads the Blender-exported PBR material: base color, metallic,
                    // roughness.  The user can override these in the sliders below.
                    // Extract textures to assets folder so they can be loaded later
                    rev::gltf::ImportResult* ir = rev::gltf::LoadMesh(filepath, editor->project->assets_path);
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
                        if (mat.base_color_texture[0]) {
                            printf("[GLTF] Base color texture: %s\n", mat.base_color_texture);
                        } else {
                            printf("[GLTF] No base color texture extracted\n");
                        }
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
            if (ImGui::DragFloat("Size",  &cue->mesh_size,  0.01f, 0.01f, 100.0f)) AutoSave();
            ImGui::SameLine();
            if (cue->curve_mesh_size >= 0) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_mesh_size);
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("+##curve_mesh_size")) {
                if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                    SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                    if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                        MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                        if (actual_cue->curve_mesh_size < 0 && editor->project->curve_count < 32) {
                            actual_cue->curve_mesh_size = editor->project->curve_count++;
                            auto& curve = editor->project->curves[actual_cue->curve_mesh_size];
                            curve = rev::curve::CreateCurve(16);
                            rev::curve::AddPoint(curve, 0.0f, actual_cue->mesh_size);
                            rev::curve::AddPoint(curve, 1.0f, actual_cue->mesh_size);
                            editor->project->modified = true;
                        }
                        cue->curve_mesh_size = actual_cue->curve_mesh_size;
                        if (actual_cue->curve_mesh_size >= 0 && actual_cue->curve_mesh_size < editor->project->curve_count) {
                            editor->editing_curve_index = actual_cue->curve_mesh_size;
                            editor->editing_curve_cue_type = 3;
                            snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Size");
                            editor->curve_editor_modal_request_open = true;
                        }
                    }
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
            if (ImGui::DragFloat("Param (segs/minor-r)", &cue->mesh_param, 0.1f, 0.01f, 100.0f)) AutoSave();
        }

        ImGui::Separator();
        
        // Position with individual curve buttons
        ImGui::Text("Position:");
        if (ImGui::DragFloat("Pos X", &cue->pos[0], 0.01f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_pos_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_pos_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_pos_x")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_pos_x < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_pos_x = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_pos_x];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->pos[0]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->pos[0]);
                        editor->project->modified = true;
                    }
                    cue->curve_pos_x = actual_cue->curve_pos_x;
                    if (actual_cue->curve_pos_x >= 0 && actual_cue->curve_pos_x < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_pos_x;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Pos X");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::DragFloat("Pos Y", &cue->pos[1], 0.01f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_pos_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_pos_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_pos_y")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_pos_y < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_pos_y = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_pos_y];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->pos[1]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->pos[1]);
                        editor->project->modified = true;
                    }
                    cue->curve_pos_y = actual_cue->curve_pos_y;
                    if (actual_cue->curve_pos_y >= 0 && actual_cue->curve_pos_y < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_pos_y;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Pos Y");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::DragFloat("Pos Z", &cue->pos[2], 0.01f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_pos_z >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_pos_z);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_pos_z")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_pos_z < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_pos_z = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_pos_z];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->pos[2]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->pos[2]);
                        editor->project->modified = true;
                    }
                    cue->curve_pos_z = actual_cue->curve_pos_z;
                    if (actual_cue->curve_pos_z >= 0 && actual_cue->curve_pos_z < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_pos_z;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Pos Z");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        ImGui::Text("Rotation:");
        if (ImGui::DragFloat("Rot X", &cue->rot[0], 1.0f, -360.0f, 360.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_rot_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_rot_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_rot_x")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_rot_x < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_rot_x = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_rot_x];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->rot[0]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->rot[0]);
                        editor->project->modified = true;
                    }
                    cue->curve_rot_x = actual_cue->curve_rot_x;
                    if (actual_cue->curve_rot_x >= 0 && actual_cue->curve_rot_x < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_rot_x;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Rot X");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::DragFloat("Rot Y", &cue->rot[1], 1.0f, -360.0f, 360.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_rot_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_rot_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_rot_y")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_rot_y < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_rot_y = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_rot_y];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->rot[1]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->rot[1]);
                        editor->project->modified = true;
                    }
                    cue->curve_rot_y = actual_cue->curve_rot_y;
                    if (actual_cue->curve_rot_y >= 0 && actual_cue->curve_rot_y < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_rot_y;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Rot Y");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::DragFloat("Rot Z", &cue->rot[2], 1.0f, -360.0f, 360.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_rot_z >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_rot_z);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_rot_z")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_rot_z < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_rot_z = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_rot_z];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->rot[2]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->rot[2]);
                        editor->project->modified = true;
                    }
                    cue->curve_rot_z = actual_cue->curve_rot_z;
                    if (actual_cue->curve_rot_z >= 0 && actual_cue->curve_rot_z < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_rot_z;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Rot Z");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        ImGui::Text("Scale:");
        if (ImGui::DragFloat("Scale X", &cue->scale[0], 0.01f, 0.001f, 100.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_scale_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_scale_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_scale_x")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_scale_x < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_scale_x = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_scale_x];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->scale[0]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->scale[0]);
                        editor->project->modified = true;
                    }
                    cue->curve_scale_x = actual_cue->curve_scale_x;
                    if (actual_cue->curve_scale_x >= 0 && actual_cue->curve_scale_x < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_scale_x;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Scale X");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::DragFloat("Scale Y", &cue->scale[1], 0.01f, 0.001f, 100.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_scale_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_scale_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_scale_y")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_scale_y < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_scale_y = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_scale_y];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->scale[1]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->scale[1]);
                        editor->project->modified = true;
                    }
                    cue->curve_scale_y = actual_cue->curve_scale_y;
                    if (actual_cue->curve_scale_y >= 0 && actual_cue->curve_scale_y < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_scale_y;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Scale Y");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::DragFloat("Scale Z", &cue->scale[2], 0.01f, 0.001f, 100.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_scale_z >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_scale_z);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_scale_z")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_scale_z < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_scale_z = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_scale_z];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->scale[2]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->scale[2]);
                        editor->project->modified = true;
                    }
                    cue->curve_scale_z = actual_cue->curve_scale_z;
                    if (actual_cue->curve_scale_z >= 0 && actual_cue->curve_scale_z < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_scale_z;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Scale Z");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        ImGui::Separator();
        if (ImGui::ColorEdit4("Color (Base Color)", cue->color)) AutoSave();
        
        // Individual curve buttons for RGBA
        ImGui::Text("Color Curves:");
        
        // R/G/B/A on two lines
        ImGui::Text("R:");
        ImGui::SameLine();
        if (cue->curve_color_r >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_r);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_r")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_color_r < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_r = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_r];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color[0]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color[0]);
                        editor->project->modified = true;
                    }
                    cue->curve_color_r = actual_cue->curve_color_r;
                    if (actual_cue->curve_color_r >= 0 && actual_cue->curve_color_r < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_r;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Color R");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit R curve");
        
        ImGui::SameLine();
        ImGui::Text("G:");
        ImGui::SameLine();
        if (cue->curve_color_g >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_g);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_g")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_color_g < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_g = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_g];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color[1]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color[1]);
                        editor->project->modified = true;
                    }
                    cue->curve_color_g = actual_cue->curve_color_g;
                    if (actual_cue->curve_color_g >= 0 && actual_cue->curve_color_g < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_g;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Color G");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit G curve");
        
        ImGui::SameLine();
        ImGui::Text("B:");
        ImGui::SameLine();
        if (cue->curve_color_b >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_b);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_b")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_color_b < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_b = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_b];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color[2]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color[2]);
                        editor->project->modified = true;
                    }
                    cue->curve_color_b = actual_cue->curve_color_b;
                    if (actual_cue->curve_color_b >= 0 && actual_cue->curve_color_b < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_b;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Color B");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit B curve");
        
        ImGui::SameLine();
        ImGui::Text("A:");
        ImGui::SameLine();
        if (cue->curve_color_a >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_color_a);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_a")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_color_a < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_color_a = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_color_a];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->color[3]);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->color[3]);
                        editor->project->modified = true;
                    }
                    cue->curve_color_a = actual_cue->curve_color_a;
                    if (actual_cue->curve_color_a >= 0 && actual_cue->curve_color_a < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_color_a;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Color A");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit A curve");
        
        if (ImGui::SliderFloat("Metallic",  &cue->metallic,  0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_metallic >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_metallic);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_metallic")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_metallic < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_metallic = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_metallic];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->metallic);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->metallic);
                        editor->project->modified = true;
                    }
                    cue->curve_metallic = actual_cue->curve_metallic;
                    if (actual_cue->curve_metallic >= 0 && actual_cue->curve_metallic < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_metallic;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Metallic");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Roughness", &cue->roughness, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_roughness >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_roughness);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_roughness")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_roughness < 0 && editor->project->curve_count < 32) {
                        actual_cue->curve_roughness = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_roughness];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->roughness);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->roughness);
                        editor->project->modified = true;
                    }
                    cue->curve_roughness = actual_cue->curve_roughness;
                    if (actual_cue->curve_roughness >= 0 && actual_cue->curve_roughness < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_roughness;
                        editor->editing_curve_cue_type = 3;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Roughness");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");

        ImGui::Separator();
        if (ImGui::DragFloat("Cue Start", &cue->cue_start, 0.01f, 0.0f, 9999.0f)) AutoSave();
        if (ImGui::DragFloat("Cue End",   &cue->cue_end,   0.01f, 0.0f, 9999.0f)) AutoSave();
        if (ImGui::DragInt  ("Layer Order", &cue->layer_order, 1, -100, 100)) AutoSave();

        ImGui::Separator();
        const char* effect_names[] = { "None", "Fade In/Out" };
        if (ImGui::Combo("Effect", &cue->effect_type, effect_names, 2)) AutoSave();
        if (cue->effect_type != 0) {
            if (ImGui::DragFloat("Fade In Start",  &cue->fade_in_start,  0.01f)) AutoSave();
            if (ImGui::DragFloat("Fade In End",    &cue->fade_in_end,    0.01f)) AutoSave();
            if (ImGui::DragFloat("Fade Out Start", &cue->fade_out_start, 0.01f)) AutoSave();
            if (ImGui::DragFloat("Fade Out End",   &cue->fade_out_end,   0.01f)) AutoSave();
        }

        // Animation controls (only for glTF meshes)
        if (cue->mesh_type == 4 && cue->asset_path[0]) {
            ImGui::Separator();
            ImGui::Text("Animation Controls:");
            
            // Find cached mesh for this asset
            rev::mesh::Mesh* cached_mesh = nullptr;
            int cache_index = -1;
            for (int c = 0; c < editor->mesh_cache_count; ++c) {
                if (strcmp(editor->mesh_cache[c].path, cue->asset_path) == 0) {
                    cached_mesh = (rev::mesh::Mesh*)editor->mesh_cache[c].mesh;
                    cache_index = c;
                    break;
                }
            }
            
            if (cached_mesh && cached_mesh->animation_count > 0) {
                rev::gltf::Animation* anims = (rev::gltf::Animation*)cached_mesh->animation_data;
                
                ImGui::TextDisabled("Found: %d animation(s) in cache slot %d", cached_mesh->animation_count, cache_index);
                
                // Animation selection dropdown
                if (cached_mesh->animation_count > 1) {
                    char** anim_names = new char*[cached_mesh->animation_count];
                    for (int i = 0; i < cached_mesh->animation_count; ++i) {
                        anim_names[i] = anims[i].name[0] ? anims[i].name : "(unnamed)";
                    }
                    if (ImGui::Combo("Animation", &cached_mesh->current_animation, 
                                 (const char**)anim_names, cached_mesh->animation_count)) {
                        cached_mesh->animation_time = 0.0f;  // Reset time when changing animation
                        printf("[AnimControl] Switched to animation %d\n", cached_mesh->current_animation);
                    }
                    delete[] anim_names;
                } else {
                    ImGui::Text("Animation: %s", anims[0].name[0] ? anims[0].name : "(unnamed)");
                }
                
                // Current animation info
                if (cached_mesh->current_animation >= 0 && 
                    cached_mesh->current_animation < cached_mesh->animation_count) {
                    float anim_duration = anims[cached_mesh->current_animation].duration;
                    
                    // Pause playback while scrubbing
                    bool was_playing = editor->playing;
                    
                    // Time scrubber
                    float old_time = cached_mesh->animation_time;
                    if (ImGui::SliderFloat("Time", &cached_mesh->animation_time, 0.0f, anim_duration, "%.2fs")) {
                        if (old_time != cached_mesh->animation_time) {
                            printf("[AnimControl] Scrubbed to %.2fs\n", cached_mesh->animation_time);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset##anim")) {
                        cached_mesh->animation_time = 0.0f;
                        printf("[AnimControl] Reset to 0.0s\n");
                    }
                    
                    // Speed and loop controls
                    if (ImGui::SliderFloat("Speed", &cached_mesh->animation_speed, 0.0f, 3.0f)) {
                        printf("[AnimControl] Speed changed to %.2fx\n", cached_mesh->animation_speed);
                    }
                    if (ImGui::Checkbox("Loop", &cached_mesh->animation_loop)) {
                        printf("[AnimControl] Loop %s\n", cached_mesh->animation_loop ? "enabled" : "disabled");
                    }
                    
                    ImGui::TextDisabled("Duration: %.2fs", anim_duration);
                    ImGui::TextDisabled("Current Time: %.2fs", cached_mesh->animation_time);
                } else {
                    ImGui::TextDisabled("No animation selected");
                }
            } else if (cached_mesh) {
                ImGui::TextDisabled("No animations in this mesh");
            } else {
                ImGui::TextDisabled("Mesh not loaded yet - cache has %d entries", editor->mesh_cache_count);
                if (editor->mesh_cache_count > 0) {
                    ImGui::TextDisabled("Looking for: '%s'", cue->asset_path);
                    for (int c = 0; c < editor->mesh_cache_count; ++c) {
                        ImGui::TextDisabled("  [%d]: '%s'", c, editor->mesh_cache[c].path);
                    }
                }
            }
        }
        
        ImGui::Separator();
        // Close button (changes auto-saved continuously)
        if (ImGui::Button("Close", ImVec2(240, 0))) {
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


} // namespace editor
} // namespace rev

