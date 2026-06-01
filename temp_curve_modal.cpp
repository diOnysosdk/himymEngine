void RenderCurveEditorModal(EditorContext* editor) {
    if (!editor) return;

    // Handle open request
    if (editor->curve_editor_modal_request_open) {
        ImGui::OpenPopup("Edit Curve");
        editor->curve_editor_modal_request_open = false;
        editor->curve_editor_modal_open = true;
        editor->dragging_point_index = -1;
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
        
        // Header with curve name and delete button
        ImGui::Text("Editing: %s", editor->editing_curve_label);
        ImGui::SameLine();
        ImGui::TextDisabled("(Curve #%d, %d points)", editor->editing_curve_index, curve->point_count);
        
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
        
        // Add point on double-click
        if (is_hovered && ImGui::IsMouseDoubleClicked(0)) {
            float t = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
            float v = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
            t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
            // Allow values outside 0-1 range for flexibility
            rev::curve::AddPoint(*curve, t, v, rev::curve::EaseMode::Linear);
            rev::curve::SortPoints(*curve);
            editor->project->modified = true;
        }
        
        // Draw and interact with control points
        for (int i = 0; i < curve->point_count; ++i) {
            rev::curve::Point* pt = &curve->points[i];
            
            // Clamp display position but allow actual value to be outside 0-1
            float display_v = (pt->v < 0.0f) ? 0.0f : ((pt->v > 1.0f) ? 1.0f : pt->v);
            ImVec2 point_pos = ImVec2(canvas_pos.x + pt->t * canvas_size.x,
                                     canvas_pos.y + canvas_size.y - display_v * canvas_size.y);
            
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
                // Allow dragging outside 0-1 for values
                editor->project->modified = true;
            }
            
            // End dragging
            if (editor->dragging_point_index == i && ImGui::IsMouseReleased(0)) {
                editor->dragging_point_index = -1;
                rev::curve::SortPoints(*curve);
            }
            
            // Delete point on right-click
            if (point_hovered && ImGui::IsMouseClicked(1) && curve->point_count > 2) {
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
        
        ImGui::Separator();
        
        // Point properties
        if (editor->dragging_point_index >= 0 && editor->dragging_point_index < curve->point_count) {
            rev::curve::Point* pt = &curve->points[editor->dragging_point_index];
            ImGui::Text("Selected Point %d:", editor->dragging_point_index);
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
        ImGui::TextDisabled("Double-click: Add point | Drag: Move | Right-click: Delete");
        
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
                // Find which cue field is using this curve and reset it
                // This requires searching through all cues - we'll set it to -1
                // The modal knows which field via editing_curve_field
                
                // Reset the curve assignment in the appropriate cue
                // (The calling code will handle this when detecting the curve was deleted)
                
                // Destroy the curve
                rev::curve::DestroyCurve(editor->project->curves[editor->editing_curve_index]);
                
                // Shift remaining curves down
                for (int i = editor->editing_curve_index; i < editor->project->curve_count - 1; ++i) {
                    editor->project->curves[i] = editor->project->curves[i + 1];
                }
                editor->project->curve_count--;
                
                // Update all curve indices in all cues that were above this one
                // (decrease by 1 if they're greater than the deleted index)
                for (int s = 0; s < editor->project->scene_count; ++s) {
                    SceneBlock* scene = &editor->project->scenes[s];
                    
                    // Update image cues
                    for (int i = 0; i < scene->image_cue_count; ++i) {
                        ImageCue* cue = &scene->image_cues[i];
                        if (cue->curve_x > editor->editing_curve_index) cue->curve_x--;
                        if (cue->curve_y > editor->editing_curve_index) cue->curve_y--;
                        if (cue->curve_scale > editor->editing_curve_index) cue->curve_scale--;
                        if (cue->curve_opacity > editor->editing_curve_index) cue->curve_opacity--;
                        // Reset if it was the deleted curve
                        if (cue->curve_x == editor->editing_curve_index) cue->curve_x = -1;
                        if (cue->curve_y == editor->editing_curve_index) cue->curve_y = -1;
                        if (cue->curve_scale == editor->editing_curve_index) cue->curve_scale = -1;
                        if (cue->curve_opacity == editor->editing_curve_index) cue->curve_opacity = -1;
                    }
                    
                    // Update text cues
                    for (int i = 0; i < scene->text_cue_count; ++i) {
                        TextCue* cue = &scene->text_cues[i];
                        if (cue->curve_x > editor->editing_curve_index) cue->curve_x--;
                        if (cue->curve_y > editor->editing_curve_index) cue->curve_y--;
                        if (cue->curve_size > editor->editing_curve_index) cue->curve_size--;
                        if (cue->curve_color_r > editor->editing_curve_index) cue->curve_color_r--;
                        if (cue->curve_color_g > editor->editing_curve_index) cue->curve_color_g--;
                        if (cue->curve_color_b > editor->editing_curve_index) cue->curve_color_b--;
                        if (cue->curve_x == editor->editing_curve_index) cue->curve_x = -1;
                        if (cue->curve_y == editor->editing_curve_index) cue->curve_y = -1;
                        if (cue->curve_size == editor->editing_curve_index) cue->curve_size = -1;
                        if (cue->curve_color_r == editor->editing_curve_index) cue->curve_color_r = -1;
                        if (cue->curve_color_g == editor->editing_curve_index) cue->curve_color_g = -1;
                        if (cue->curve_color_b == editor->editing_curve_index) cue->curve_color_b = -1;
                    }
                    
                    // Update mesh cues
                    for (int i = 0; i < scene->mesh_cue_count; ++i) {
                        MeshCue* cue = &scene->mesh_cues[i];
                        if (cue->curve_pos_x > editor->editing_curve_index) cue->curve_pos_x--;
                        if (cue->curve_pos_y > editor->editing_curve_index) cue->curve_pos_y--;
                        if (cue->curve_pos_z > editor->editing_curve_index) cue->curve_pos_z--;
                        if (cue->curve_rot_x > editor->editing_curve_index) cue->curve_rot_x--;
                        if (cue->curve_rot_y > editor->editing_curve_index) cue->curve_rot_y--;
                        if (cue->curve_rot_z > editor->editing_curve_index) cue->curve_rot_z--;
                        if (cue->curve_scale_x > editor->editing_curve_index) cue->curve_scale_x--;
                        if (cue->curve_scale_y > editor->editing_curve_index) cue->curve_scale_y--;
                        if (cue->curve_scale_z > editor->editing_curve_index) cue->curve_scale_z--;
                        if (cue->curve_color_r > editor->editing_curve_index) cue->curve_color_r--;
                        if (cue->curve_color_g > editor->editing_curve_index) cue->curve_color_g--;
                        if (cue->curve_color_b > editor->editing_curve_index) cue->curve_color_b--;
                        if (cue->curve_color_a > editor->editing_curve_index) cue->curve_color_a--;
                        if (cue->curve_mesh_size > editor->editing_curve_index) cue->curve_mesh_size--;
                        if (cue->curve_metallic > editor->editing_curve_index) cue->curve_metallic--;
                        if (cue->curve_roughness > editor->editing_curve_index) cue->curve_roughness--;
                        if (cue->curve_pos_x == editor->editing_curve_index) cue->curve_pos_x = -1;
                        if (cue->curve_pos_y == editor->editing_curve_index) cue->curve_pos_y = -1;
                        if (cue->curve_pos_z == editor->editing_curve_index) cue->curve_pos_z = -1;
                        if (cue->curve_rot_x == editor->editing_curve_index) cue->curve_rot_x = -1;
                        if (cue->curve_rot_y == editor->editing_curve_index) cue->curve_rot_y = -1;
                        if (cue->curve_rot_z == editor->editing_curve_index) cue->curve_rot_z = -1;
                        if (cue->curve_scale_x == editor->editing_curve_index) cue->curve_scale_x = -1;
                        if (cue->curve_scale_y == editor->editing_curve_index) cue->curve_scale_y = -1;
                        if (cue->curve_scale_z == editor->editing_curve_index) cue->curve_scale_z = -1;
                        if (cue->curve_color_r == editor->editing_curve_index) cue->curve_color_r = -1;
                        if (cue->curve_color_g == editor->editing_curve_index) cue->curve_color_g = -1;
                        if (cue->curve_color_b == editor->editing_curve_index) cue->curve_color_b = -1;
                        if (cue->curve_color_a == editor->editing_curve_index) cue->curve_color_a = -1;
                        if (cue->curve_mesh_size == editor->editing_curve_index) cue->curve_mesh_size = -1;
                        if (cue->curve_metallic == editor->editing_curve_index) cue->curve_metallic = -1;
                        if (cue->curve_roughness == editor->editing_curve_index) cue->curve_roughness = -1;
                    }
                }
                
                editor->project->modified = true;
                editor->curve_editor_modal_open = false;
                ImGui::CloseCurrentPopup();
                ImGui::CloseCurrentPopup(); // Close the confirm popup as well
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
