#include "editor_internal.h"
#include <cstring>
#include <cstdio>
#include <windows.h>
#include "imgui.h"

namespace rev {
namespace editor {

namespace {

void UpdateCurveRefAfterDelete(int* ref, int deleted_curve)
{
    if (!ref) return;
    if (*ref == deleted_curve) {
        *ref = -1;
    } else if (*ref > deleted_curve) {
        --(*ref);
    }
}

void ReindexCurveReferencesAfterDelete(ProjectData* project, int deleted_curve)
{
    if (!project || deleted_curve < 0) return;

    for (int si = 0; si < project->scene_count; ++si) {
        SceneBlock* scene = &project->scenes[si];

        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_speed, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_intensity, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_warp, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_exposure, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_fade, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_low_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_low_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_low_b, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_mid_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_mid_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_mid_b, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_high_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_high_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_palette_high_b, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_opacity, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_exposure_ramp, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_fade_ramp, deleted_curve);
        }

        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_scale, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_opacity, deleted_curve);
        }

        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_scale, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_opacity, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_frame, deleted_curve);
        }

        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_size, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_b, deleted_curve);
        }

        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_pos_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_pos_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_pos_z, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_rot_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_rot_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_rot_z, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_scale_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_scale_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_scale_z, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_b, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_a, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_mesh_size, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_metallic, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_roughness, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_fov, deleted_curve);
        }
    }
}

void RegisterCurveUsage(int field,
                        int curve_index,
                        const char* field_label,
                        const char* owner_label,
                        int* usage_count,
                        char* first_usage,
                        size_t first_usage_size)
{
    if (field != curve_index || !usage_count || !first_usage || first_usage_size == 0) {
        return;
    }

    ++(*usage_count);
    if (first_usage[0] == '\0') {
        snprintf(first_usage, first_usage_size, "%s @ %s", field_label, owner_label ? owner_label : "cue");
    }
}

void BuildCurveDisplayLabel(EditorContext* editor, int curve_index, char* out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';

    if (!editor || !editor->project || curve_index < 0 || curve_index >= editor->project->curve_count) {
        snprintf(out, out_size, "Curve %d", curve_index);
        return;
    }

    int usage_count = 0;
    char first_usage[192] = {};

    for (int si = 0; si < editor->project->scene_count; ++si) {
        SceneBlock* scene = &editor->project->scenes[si];
        const char* scene_name = (scene->name[0] != '\0') ? scene->name : "Scene";

        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/%s", scene_name, cue->shader_name[0] ? cue->shader_name : "shader");
            RegisterCurveUsage(cue->curve_speed, curve_index, "Shader Speed", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_intensity, curve_index, "Shader Intensity", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_warp, curve_index, "Shader Warp", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_exposure, curve_index, "Shader Exposure", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_fade, curve_index, "Shader Fade", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_low_r, curve_index, "Shader Low R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_low_g, curve_index, "Shader Low G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_low_b, curve_index, "Shader Low B", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_mid_r, curve_index, "Shader Mid R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_mid_g, curve_index, "Shader Mid G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_mid_b, curve_index, "Shader Mid B", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_high_r, curve_index, "Shader High R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_high_g, curve_index, "Shader High G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_palette_high_b, curve_index, "Shader High B", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_opacity, curve_index, "Shader Opacity", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_exposure_ramp, curve_index, "Shader Exposure Ramp", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_fade_ramp, curve_index, "Shader Fade Ramp", owner, &usage_count, first_usage, sizeof(first_usage));
        }

        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/%s", scene_name, cue->asset_key[0] ? cue->asset_key : "image");
            RegisterCurveUsage(cue->curve_x, curve_index, "Image X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_y, curve_index, "Image Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_scale, curve_index, "Image Scale", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_opacity, curve_index, "Image Opacity", owner, &usage_count, first_usage, sizeof(first_usage));
        }

        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/%s", scene_name, cue->sprite_name[0] ? cue->sprite_name : "anim_sprite");
            RegisterCurveUsage(cue->curve_x, curve_index, "AnimSprite X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_y, curve_index, "AnimSprite Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_scale, curve_index, "AnimSprite Scale", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_opacity, curve_index, "AnimSprite Opacity", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_frame, curve_index, "AnimSprite Frame", owner, &usage_count, first_usage, sizeof(first_usage));
        }

        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            char snippet[48] = {};
            if (cue->text[0] != '\0') {
                strncpy_s(snippet, sizeof(snippet), cue->text, _TRUNCATE);
            } else {
                strncpy_s(snippet, sizeof(snippet), "text", _TRUNCATE);
            }
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/%s", scene_name, snippet);
            RegisterCurveUsage(cue->curve_x, curve_index, "Text X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_y, curve_index, "Text Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_size, curve_index, "Text Size", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_r, curve_index, "Text Color R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_g, curve_index, "Text Color G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_b, curve_index, "Text Color B", owner, &usage_count, first_usage, sizeof(first_usage));
        }

        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/%s", scene_name, cue->asset_key[0] ? cue->asset_key : "mesh");
            RegisterCurveUsage(cue->curve_pos_x, curve_index, "Mesh Pos X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_pos_y, curve_index, "Mesh Pos Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_pos_z, curve_index, "Mesh Pos Z", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_rot_x, curve_index, "Mesh Rot X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_rot_y, curve_index, "Mesh Rot Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_rot_z, curve_index, "Mesh Rot Z", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_scale_x, curve_index, "Mesh Scale X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_scale_y, curve_index, "Mesh Scale Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_scale_z, curve_index, "Mesh Scale Z", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_r, curve_index, "Mesh Color R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_g, curve_index, "Mesh Color G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_b, curve_index, "Mesh Color B", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_color_a, curve_index, "Mesh Color A", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_mesh_size, curve_index, "Mesh Size", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_metallic, curve_index, "Mesh Metallic", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_roughness, curve_index, "Mesh Roughness", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_fov, curve_index, "Mesh Camera FOV", owner, &usage_count, first_usage, sizeof(first_usage));
        }
    }

    int points = editor->project->curves[curve_index].point_count;
    if (usage_count <= 0) {
        snprintf(out, out_size, "Curve %d - Unused (%d pts)", curve_index, points);
    } else if (usage_count == 1) {
        snprintf(out, out_size, "Curve %d - %s (%d pts)", curve_index, first_usage, points);
    } else {
        snprintf(out, out_size, "Curve %d - %s (+%d) (%d pts)", curve_index, first_usage, usage_count - 1, points);
    }
}

static int AllocateCurveSlotForPanel(EditorContext* editor, float start_v, float end_v)
{
    if (!editor || !editor->project) return -1;

    if (editor->project->curve_count < rev::runtime::kMaxCurves) {
        int idx = editor->project->curve_count;
        editor->project->curves[idx] = rev::curve::CreateCurve();
        rev::curve::AddPoint(editor->project->curves[idx], 0.0f, start_v);
        rev::curve::AddPoint(editor->project->curves[idx], 1.0f, end_v);
        editor->project->curve_count++;
        return idx;
    }

    bool used[rev::runtime::kMaxCurves] = {};

    // Build usage map by scanning all cue references.
    for (int si = 0; si < editor->project->scene_count; ++si) {
        SceneBlock* scene = &editor->project->scenes[si];
        auto mark = [&](int idx) { if (idx >= 0 && idx < rev::runtime::kMaxCurves) used[idx] = true; };

        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            mark(cue->curve_speed); mark(cue->curve_intensity); mark(cue->curve_warp);
            mark(cue->curve_exposure); mark(cue->curve_fade); mark(cue->curve_palette_low_r);
            mark(cue->curve_palette_low_g); mark(cue->curve_palette_low_b); mark(cue->curve_palette_mid_r);
            mark(cue->curve_palette_mid_g); mark(cue->curve_palette_mid_b); mark(cue->curve_palette_high_r);
            mark(cue->curve_palette_high_g); mark(cue->curve_palette_high_b); mark(cue->curve_opacity);
            mark(cue->curve_exposure_ramp); mark(cue->curve_fade_ramp);
        }
        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_scale); mark(cue->curve_opacity);
        }
        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_scale); mark(cue->curve_opacity); mark(cue->curve_frame);
        }
        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_size);
            mark(cue->curve_color_r); mark(cue->curve_color_g); mark(cue->curve_color_b);
        }
        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_speed); mark(cue->curve_size);
            mark(cue->curve_opacity); mark(cue->curve_color_r); mark(cue->curve_color_g); mark(cue->curve_color_b);
            mark(cue->curve_wave_amp); mark(cue->curve_wave_freq); mark(cue->curve_jitter_amp); mark(cue->curve_jitter_freq);
        }
        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            mark(cue->curve_pos_x); mark(cue->curve_pos_y); mark(cue->curve_pos_z);
            mark(cue->curve_rot_x); mark(cue->curve_rot_y); mark(cue->curve_rot_z);
            mark(cue->curve_scale_x); mark(cue->curve_scale_y); mark(cue->curve_scale_z);
            mark(cue->curve_color_r); mark(cue->curve_color_g); mark(cue->curve_color_b); mark(cue->curve_color_a);
            mark(cue->curve_mesh_size); mark(cue->curve_metallic); mark(cue->curve_roughness); mark(cue->curve_fov);
        }
    }

    for (int i = 0; i < editor->project->curve_count; ++i) {
        if (!used[i]) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
            editor->project->curves[i] = rev::curve::CreateCurve();
            rev::curve::AddPoint(editor->project->curves[i], 0.0f, start_v);
            rev::curve::AddPoint(editor->project->curves[i], 1.0f, end_v);
            return i;
        }
    }

    return -1;
}

} // namespace

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

        bool music_persist = editor->project->music_persist_across_scenes;
        if (ImGui::Checkbox("Carry Music Across Scenes", &music_persist)) {
            editor->project->music_persist_across_scenes = music_persist;
            editor->project->modified = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Keep current track across scene changes unless a different music cue becomes active.");
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
            if (scene->shader_cue_count > 0 || scene->image_cue_count > 0 || scene->animated_sprite_cue_count > 0 ||
                scene->text_cue_count > 0 || scene->scroll_text_cue_count > 0 || scene->music_cue_count > 0 ||
                scene->mesh_cue_count > 0) {
                ImGui::Indent();
                if (scene->shader_cue_count > 0) {
                    ImGui::Text("  Shaders: %d", scene->shader_cue_count);
                }
                if (scene->image_cue_count > 0) {
                    ImGui::Text("  Images: %d", scene->image_cue_count);
                }
                if (scene->animated_sprite_cue_count > 0) {
                    ImGui::Text("  Animated Sprites: %d", scene->animated_sprite_cue_count);
                }
                if (scene->text_cue_count > 0) {
                    ImGui::Text("  Text: %d", scene->text_cue_count);
                }
                if (scene->scroll_text_cue_count > 0) {
                    ImGui::Text("  Scroll Text: %d", scene->scroll_text_cue_count);
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
                        editor->selected_cue_type = CueTypeShader;
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
                editor->selected_cue_type = CueTypeShader;
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
                editor->selected_cue_type = CueTypeImage;
                editor->image_modal_request_open = true;
                editor->project->modified = true;
            }

            if (ImGui::Button("+ Animated Sprite Cue")) {
                AnimatedSpriteCue cue = {};
                snprintf(cue.sprite_name, sizeof(cue.sprite_name), "sprite_%d", scene->animated_sprite_cue_count);
                cue.x = 0.5f;
                cue.y = 0.5f;
                cue.scale = 1.0f;
                cue.opacity = 1.0f;
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                cue.layer_order = 0;
                cue.blend_mode = 0;
                cue.fps = 12.0f;
                cue.playback_mode = 0;
                cue.start_frame = 0;
                cue.curve_x = -1;
                cue.curve_y = -1;
                cue.curve_scale = -1;
                cue.curve_opacity = -1;
                cue.curve_frame = -1;
                int new_index = AddAnimatedSpriteCue(scene, cue);
                editor->editing_animated_sprite = scene->animated_sprite_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypeAnimatedSprite;
                editor->animated_sprite_modal_request_open = true;
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
                editor->selected_cue_type = CueTypeText;
                editor->text_modal_request_open = true;
                editor->project->modified = true;
            }

            if (ImGui::Button("+ Scroll Text Cue")) {
                ScrollTextCue cue = {};
                strncpy_s(cue.text, sizeof(cue.text), "SCROLL TEXT", _TRUNCATE);
                strncpy_s(cue.font_name, sizeof(cue.font_name), "Arial", _TRUNCATE);
                cue.x = 0.5f;
                cue.y = 0.9f;
                cue.size = 40.0f;
                cue.color = {1.0f, 1.0f, 1.0f};
                cue.opacity = 1.0f;
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                cue.layer_order = 0;
                cue.blend_mode = 0;
                cue.style_id = 0;
                cue.direction = 0;
                cue.loop_mode = 0;
                cue.speed = 0.25f;
                cue.wrap_gap = 0.2f;
                cue.spacing = 1.0f;
                cue.slant_deg = 0.0f;
                cue.wave_amp = 0.0f;
                cue.wave_freq = 1.0f;
                cue.jitter_amp = 0.0f;
                cue.jitter_freq = 1.0f;
                cue.glow = 0.0f;
                cue.shadow = 0.0f;
                cue.outline = 0.0f;
                cue.chroma_shift = 0.0f;
                cue.distortion = 0.0f;
                cue.bake_mode = 0;
                cue.baked_asset_key[0] = '\0';
                cue.baked_asset_path[0] = '\0';
                cue.curve_x = -1;
                cue.curve_y = -1;
                cue.curve_speed = -1;
                cue.curve_size = -1;
                cue.curve_opacity = -1;
                cue.curve_color_r = -1;
                cue.curve_color_g = -1;
                cue.curve_color_b = -1;
                cue.curve_wave_amp = -1;
                cue.curve_wave_freq = -1;
                cue.curve_jitter_amp = -1;
                cue.curve_jitter_freq = -1;
                int new_index = AddScrollTextCue(scene, cue);
                editor->editing_scroll_text = scene->scroll_text_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypeScrollText;
                editor->scroll_text_modal_request_open = true;
                editor->project->modified = true;
            }
            
            if (ImGui::Button("+ Music Cue")) {
                MusicCue cue = {};
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                int new_index = AddMusicCue(scene, cue);
                editor->editing_music = scene->music_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypeMusic;
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
                cue.emissive_color[0] = 1.0f;
                cue.emissive_color[1] = 1.0f;
                cue.emissive_color[2] = 1.0f;
                cue.metallic   = 0.0f;
                cue.roughness  = 0.5f;
                cue.emissive_strength = 0.0f;
                cue.fov_deg    = 45.0f;
                cue.cull_mode  = 0;
                cue.use_imported_light = 0;
                cue.use_imported_camera = 0;
                cue.cue_start  = 0.0f;
                cue.cue_end    = scene->duration;
                cue.curve_pos_x = -1; cue.curve_pos_y = -1; cue.curve_pos_z = -1;
                cue.curve_rot_x = -1; cue.curve_rot_y = -1; cue.curve_rot_z = -1;
                cue.curve_scale_x = -1; cue.curve_scale_y = -1; cue.curve_scale_z = -1;
                cue.curve_color_r = -1; cue.curve_color_g = -1; cue.curve_color_b = -1; cue.curve_color_a = -1;
                cue.curve_mesh_size = -1; cue.curve_metallic = -1; cue.curve_roughness = -1; cue.curve_fov = -1;
                snprintf(cue.asset_key, sizeof(cue.asset_key), "mesh_%d", scene->mesh_cue_count);
                int new_index = AddMeshCue(scene, cue);
                editor->editing_mesh = scene->mesh_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypeMesh;
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
                        editor->selected_cue_type = CueTypeImage;
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

            if (scene->animated_sprite_cue_count > 0) {
                ImGui::Text("Animated Sprite Cues:");
                for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
                    ImGui::PushID(2500 + i);
                    const char* display_name = scene->animated_sprite_cues[i].sprite_name[0] != '\0'
                        ? scene->animated_sprite_cues[i].sprite_name
                        : "(animated sprite)";
                    if (ImGui::Button(display_name)) {
                        editor->editing_animated_sprite = scene->animated_sprite_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = CueTypeAnimatedSprite;
                        editor->animated_sprite_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteAnimatedSpriteCue(scene, i);
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
                        editor->selected_cue_type = CueTypeText;
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
            
            // Display existing scroll text cues
            if (scene->scroll_text_cue_count > 0) {
                ImGui::Text("Scroll Text Cues:");
                for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
                    ImGui::PushID(3500 + i);
                    const char* display_text = scene->scroll_text_cues[i].text[0] != '\0'
                        ? scene->scroll_text_cues[i].text
                        : "(no scroll text)";
                    if (ImGui::Button(display_text)) {
                        editor->editing_scroll_text = scene->scroll_text_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = CueTypeScrollText;
                        editor->scroll_text_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteScrollTextCue(scene, i);
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
                        editor->selected_cue_type = CueTypeMusic;
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
                        editor->selected_cue_type = CueTypeMesh;
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
            int new_index = AllocateCurveSlotForPanel(editor, 0.0f, 1.0f);
            if (new_index >= 0) {
                editor->selected_curve_index = new_index;
                editor->project->modified = true;
            } else {
                snprintf(editor->build_status_message, sizeof(editor->build_status_message), "No free curve slots (32/32 in use)");
                editor->build_status_timer = 3.0f;
            }
        }
        
        ImGui::SameLine();
        if (editor->selected_curve_index >= 0 && ImGui::Button("Delete Curve")) {
            if (editor->selected_curve_index < editor->project->curve_count) {
                const int deleted_curve = editor->selected_curve_index;
                rev::curve::DestroyCurve(editor->project->curves[editor->selected_curve_index]);
                // Shift remaining curves
                for (int i = editor->selected_curve_index; i < editor->project->curve_count - 1; ++i) {
                    editor->project->curves[i] = editor->project->curves[i + 1];
                }
                editor->project->curve_count--;
                ReindexCurveReferencesAfterDelete(editor->project, deleted_curve);
                editor->selected_curve_index = -1;
                editor->project->modified = true;
            }
        }
        
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("Delete Unused")) {
            // Build usage map
            bool used[rev::runtime::kMaxCurves];
            BuildCurveUsageMap(editor->project, used);
            
            // Delete unused curves (iterate backwards to avoid index shifting issues)
            for (int i = editor->project->curve_count - 1; i >= 0; --i) {
                if (!used[i]) {
                    rev::curve::DestroyCurve(editor->project->curves[i]);
                    // Shift remaining curves
                    for (int j = i; j < editor->project->curve_count - 1; ++j) {
                        editor->project->curves[j] = editor->project->curves[j + 1];
                    }
                    // Update all curve references that were affected
                    ReindexCurveReferencesAfterDelete(editor->project, i);
                    editor->project->curve_count--;
                }
            }
            editor->selected_curve_index = -1;
            editor->project->modified = true;
            snprintf(editor->build_status_message, sizeof(editor->build_status_message), "Deleted unused curves");
            editor->build_status_timer = 2.0f;
        }
        ImGui::PopStyleColor(2);
        
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &editor->show_curve_grid);
        
        // Curve list
        if (editor->project->curve_count > 0) {
            ImGui::Text("Select Curve:");
            for (int i = 0; i < editor->project->curve_count; ++i) {
                ImGui::PushID(i);
                char label[256] = {};
                BuildCurveDisplayLabel(editor, i, label, sizeof(label));
                if (ImGui::Selectable(label, editor->selected_curve_index == i)) {
                    editor->selected_curve_index = i;
                }
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        // Assign an existing curve to another parameter on the currently selected cue.
        if (editor->selected_curve_index >= 0 && editor->selected_curve_index < editor->project->curve_count &&
            editor->selected_scene_index >= 0 && editor->selected_scene_index < editor->project->scene_count &&
            editor->selected_cue_index >= 0) {
            SceneBlock* scene = &editor->project->scenes[editor->selected_scene_index];
            int* target_fields[24] = {};
            const char* target_names[24] = {};
            int target_count = 0;

            auto add_target = [&](const char* name, int* field) {
                if (!name || !field || target_count >= 24) return;
                target_names[target_count] = name;
                target_fields[target_count] = field;
                ++target_count;
            };

            if (editor->selected_cue_type == CueTypeShader && editor->selected_cue_index < scene->shader_cue_count) {
                ShaderCue* cue = &scene->shader_cues[editor->selected_cue_index];
                add_target("Shader Speed", &cue->curve_speed);
                add_target("Shader Intensity", &cue->curve_intensity);
                add_target("Shader Warp", &cue->curve_warp);
                add_target("Shader Exposure", &cue->curve_exposure);
                add_target("Shader Fade", &cue->curve_fade);
                add_target("Shader Low R", &cue->curve_palette_low_r);
                add_target("Shader Low G", &cue->curve_palette_low_g);
                add_target("Shader Low B", &cue->curve_palette_low_b);
                add_target("Shader Mid R", &cue->curve_palette_mid_r);
                add_target("Shader Mid G", &cue->curve_palette_mid_g);
                add_target("Shader Mid B", &cue->curve_palette_mid_b);
                add_target("Shader High R", &cue->curve_palette_high_r);
                add_target("Shader High G", &cue->curve_palette_high_g);
                add_target("Shader High B", &cue->curve_palette_high_b);
                add_target("Shader Opacity", &cue->curve_opacity);
                add_target("Shader Exposure Ramp", &cue->curve_exposure_ramp);
                add_target("Shader Fade Ramp", &cue->curve_fade_ramp);
            } else if (editor->selected_cue_type == CueTypeImage && editor->selected_cue_index < scene->image_cue_count) {
                ImageCue* cue = &scene->image_cues[editor->selected_cue_index];
                add_target("Image X", &cue->curve_x);
                add_target("Image Y", &cue->curve_y);
                add_target("Image Scale", &cue->curve_scale);
                add_target("Image Opacity", &cue->curve_opacity);
            } else if (editor->selected_cue_type == CueTypeAnimatedSprite && editor->selected_cue_index < scene->animated_sprite_cue_count) {
                AnimatedSpriteCue* cue = &scene->animated_sprite_cues[editor->selected_cue_index];
                add_target("AnimSprite X", &cue->curve_x);
                add_target("AnimSprite Y", &cue->curve_y);
                add_target("AnimSprite Scale", &cue->curve_scale);
                add_target("AnimSprite Opacity", &cue->curve_opacity);
                add_target("AnimSprite Frame", &cue->curve_frame);
            } else if (editor->selected_cue_type == CueTypeText && editor->selected_cue_index < scene->text_cue_count) {
                TextCue* cue = &scene->text_cues[editor->selected_cue_index];
                add_target("Text X", &cue->curve_x);
                add_target("Text Y", &cue->curve_y);
                add_target("Text Size", &cue->curve_size);
                add_target("Text Color R", &cue->curve_color_r);
                add_target("Text Color G", &cue->curve_color_g);
                add_target("Text Color B", &cue->curve_color_b);
            } else if (editor->selected_cue_type == CueTypeScrollText && editor->selected_cue_index < scene->scroll_text_cue_count) {
                ScrollTextCue* cue = &scene->scroll_text_cues[editor->selected_cue_index];
                add_target("Scroll X", &cue->curve_x);
                add_target("Scroll Y", &cue->curve_y);
                add_target("Scroll Speed", &cue->curve_speed);
                add_target("Scroll Size", &cue->curve_size);
                add_target("Scroll Opacity", &cue->curve_opacity);
                add_target("Scroll Color R", &cue->curve_color_r);
                add_target("Scroll Color G", &cue->curve_color_g);
                add_target("Scroll Color B", &cue->curve_color_b);
                add_target("Scroll Wave Amp", &cue->curve_wave_amp);
                add_target("Scroll Wave Freq", &cue->curve_wave_freq);
                add_target("Scroll Jitter Amp", &cue->curve_jitter_amp);
                add_target("Scroll Jitter Freq", &cue->curve_jitter_freq);
            } else if (editor->selected_cue_type == CueTypeMesh && editor->selected_cue_index < scene->mesh_cue_count) {
                MeshCue* cue = &scene->mesh_cues[editor->selected_cue_index];
                add_target("Mesh Pos X", &cue->curve_pos_x);
                add_target("Mesh Pos Y", &cue->curve_pos_y);
                add_target("Mesh Pos Z", &cue->curve_pos_z);
                add_target("Mesh Rot X", &cue->curve_rot_x);
                add_target("Mesh Rot Y", &cue->curve_rot_y);
                add_target("Mesh Rot Z", &cue->curve_rot_z);
                add_target("Mesh Scale X", &cue->curve_scale_x);
                add_target("Mesh Scale Y", &cue->curve_scale_y);
                add_target("Mesh Scale Z", &cue->curve_scale_z);
                add_target("Mesh Color R", &cue->curve_color_r);
                add_target("Mesh Color G", &cue->curve_color_g);
                add_target("Mesh Color B", &cue->curve_color_b);
                add_target("Mesh Color A", &cue->curve_color_a);
                add_target("Mesh Size", &cue->curve_mesh_size);
                add_target("Mesh Metallic", &cue->curve_metallic);
                add_target("Mesh Roughness", &cue->curve_roughness);
                add_target("Mesh Camera FOV", &cue->curve_fov);
            }

            if (target_count > 0) {
                static int selected_target = 0;
                if (selected_target >= target_count) selected_target = 0;

                ImGui::Text("Reuse Existing Curve on Current Cue:");
                ImGui::SetNextItemWidth(280.0f);
                ImGui::Combo("Parameter", &selected_target, target_names, target_count);

                if (ImGui::Button("Assign Selected Curve")) {
                    *target_fields[selected_target] = editor->selected_curve_index;

                    // Keep modal editing copies in sync when a cue is open.
                    if (editor->selected_cue_type == CueTypeShader && editor->selected_cue_index < scene->shader_cue_count) {
                        editor->editing_shader = scene->shader_cues[editor->selected_cue_index];
                    } else if (editor->selected_cue_type == CueTypeImage && editor->selected_cue_index < scene->image_cue_count) {
                        editor->editing_image = scene->image_cues[editor->selected_cue_index];
                    } else if (editor->selected_cue_type == CueTypeAnimatedSprite && editor->selected_cue_index < scene->animated_sprite_cue_count) {
                        editor->editing_animated_sprite = scene->animated_sprite_cues[editor->selected_cue_index];
                    } else if (editor->selected_cue_type == CueTypeText && editor->selected_cue_index < scene->text_cue_count) {
                        editor->editing_text = scene->text_cues[editor->selected_cue_index];
                    } else if (editor->selected_cue_type == CueTypeScrollText && editor->selected_cue_index < scene->scroll_text_cue_count) {
                        editor->editing_scroll_text = scene->scroll_text_cues[editor->selected_cue_index];
                    } else if (editor->selected_cue_type == CueTypeMesh && editor->selected_cue_index < scene->mesh_cue_count) {
                        editor->editing_mesh = scene->mesh_cues[editor->selected_cue_index];
                    }

                    editor->project->modified = true;
                }

                ImGui::SameLine();
                if (ImGui::Button("Open Selected Curve")) {
                    editor->editing_curve_index = editor->selected_curve_index;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", target_names[selected_target]);
                    editor->curve_editor_modal_request_open = true;
                }
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

