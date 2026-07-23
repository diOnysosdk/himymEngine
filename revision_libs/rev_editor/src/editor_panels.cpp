#include "editor_internal.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <windows.h>
#include "imgui.h"

namespace rev {
namespace editor {

namespace {

static int CreateCurveFromTriggerTrack(EditorContext* editor, int track_index)
{
    if (!editor || !editor->project || track_index < 0 ||
        track_index >= editor->project->trigger_track_count ||
        editor->project->curve_count >= rev::runtime::kMaxCurves) return -1;

    const TriggerTrack& track = editor->project->trigger_tracks[track_index];
    if (track.event_count <= 0 || track.timing.bpm <= 0.0f) return -1;

    float duration = 1.0f;
    for (int i = 0; i < track.event_count; ++i) {
        float event_time = rev::runtime::GetTriggerTimeSeconds(&track.timing, track.events[i].beat);
        duration = fmaxf(duration, event_time);
    }

    int curve_index = editor->project->curve_count++;
    rev::curve::Curve& curve = editor->project->curves[curve_index];
    curve = rev::curve::CreateCurve(track.event_count + 2);
    curve.duration = duration;
    curve.wrap_mode = rev::curve::WrapMode::Clamp;
    rev::curve::AddPoint(curve, 0.0f, 0.0f);

    float last_t = 0.0f;
    for (int i = 0; i < track.event_count; ++i) {
        float event_time = fmaxf(0.0f,
            rev::runtime::GetTriggerTimeSeconds(&track.timing, track.events[i].beat));
        float event_t = event_time / duration;
        if (event_t <= last_t) continue;
        rev::curve::AddPoint(curve, event_t, 0.0f, rev::curve::EaseMode::Linear);
        last_t = event_t;
    }
    rev::curve::SortPoints(curve);
    editor->project->modified = true;
    return curve_index;
}

static bool AppendTriggerTrackToCurve(EditorContext* editor, int track_index, int curve_index)
{
    if (!editor || !editor->project || track_index < 0 ||
        track_index >= editor->project->trigger_track_count || curve_index < 0 ||
        curve_index >= editor->project->curve_count) return false;

    const TriggerTrack& track = editor->project->trigger_tracks[track_index];
    rev::curve::Curve& curve = editor->project->curves[curve_index];
    if (track.event_count <= 0 || track.timing.bpm <= 0.0f || curve.point_count <= 0) return false;

    float append_duration = 1.0f;
    for (int i = 0; i < track.event_count; ++i) {
        float event_time = rev::runtime::GetTriggerTimeSeconds(&track.timing, track.events[i].beat);
        append_duration = fmaxf(append_duration, event_time);
    }

    const float old_duration = curve.duration > 0.01f ? curve.duration : 1.0f;
    const float new_duration = old_duration + append_duration;
    for (int i = 0; i < curve.point_count; ++i) {
        float absolute_time = curve.points[i].t * old_duration;
        curve.points[i].t = absolute_time / new_duration;
    }

    float last_t = curve.point_count > 0 ? curve.points[curve.point_count - 1].t : 0.0f;
    for (int i = 0; i < track.event_count; ++i) {
        float event_time = fmaxf(0.0f,
            rev::runtime::GetTriggerTimeSeconds(&track.timing, track.events[i].beat));
        float event_t = (old_duration + event_time) / new_duration;
        if (event_t <= last_t) continue;
        rev::curve::AddPoint(curve, event_t, 0.0f, rev::curve::EaseMode::Linear);
        last_t = event_t;
    }
    curve.duration = new_duration;
    rev::curve::SortPoints(curve);
    editor->project->modified = true;
    return true;
}

static bool MergeTriggerTrackIntoCurve(EditorContext* editor, int track_index, int curve_index)
{
    if (!editor || !editor->project || track_index < 0 ||
        track_index >= editor->project->trigger_track_count || curve_index < 0 ||
        curve_index >= editor->project->curve_count) return false;

    const TriggerTrack& track = editor->project->trigger_tracks[track_index];
    rev::curve::Curve& curve = editor->project->curves[curve_index];
    if (track.event_count <= 0 || track.timing.bpm <= 0.0f ||
        curve.point_count <= 0 || curve.duration <= 0.01f) return false;

    for (int i = 0; i < track.event_count; ++i) {
        float event_time = fmaxf(0.0f,
            rev::runtime::GetTriggerTimeSeconds(&track.timing, track.events[i].beat));
        float event_t = fminf(1.0f, event_time / curve.duration);
        rev::curve::AddPoint(curve, event_t, 0.0f, rev::curve::EaseMode::Linear);
    }
    rev::curve::SortPoints(curve);
    editor->project->modified = true;
    return true;
}

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
            UpdateCurveRefAfterDelete(&cue->curve_rotation, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_opacity, deleted_curve);
            for (int e = 0; e < cue->post_effect_count; ++e) {
                LayerPostEffect* effect = &cue->post_effects[e];
                UpdateCurveRefAfterDelete(&effect->curve_intensity, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_threshold, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_radius, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_r, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_g, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_b, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_a, deleted_curve);
            }
        }

        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_scale, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_rotation, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_opacity, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_frame, deleted_curve);
            for (int e = 0; e < cue->post_effect_count; ++e) {
                LayerPostEffect* effect = &cue->post_effects[e];
                UpdateCurveRefAfterDelete(&effect->curve_intensity, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_threshold, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_radius, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_r, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_g, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_b, deleted_curve);
                UpdateCurveRefAfterDelete(&effect->curve_color_a, deleted_curve);
            }
        }

        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_size, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_rotation, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_b, deleted_curve);
        }

        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            UpdateCurveRefAfterDelete(&cue->curve_x, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_y, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_speed, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_size, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_rotation, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_opacity, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_r, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_g, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_color_b, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_wave_amp, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_wave_freq, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_wave_length, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_jitter_amp, deleted_curve);
            UpdateCurveRefAfterDelete(&cue->curve_jitter_freq, deleted_curve);
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

        for (int i = 0; i < scene->post_effect_count; ++i) {
            PostEffect* effect = &scene->post_effects[i];
            UpdateCurveRefAfterDelete(&effect->curve_intensity, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_threshold, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_radius, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_r, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_g, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_b, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_a, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_amount, deleted_curve);
        }
        for (int i = 0; i < scene->scene_layer_post_effect_count; ++i) {
            LayerPostEffect* effect = &scene->scene_layer_post_effects[i];
            UpdateCurveRefAfterDelete(&effect->curve_intensity, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_threshold, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_radius, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_r, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_g, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_b, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_color_a, deleted_curve);
            UpdateCurveRefAfterDelete(&effect->curve_amount, deleted_curve);
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

void BuildCurveDisplayLabelInternal(EditorContext* editor, int curve_index, char* out, size_t out_size)
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
            RegisterCurveUsage(cue->curve_rotation, curve_index, "Image Rotation", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_opacity, curve_index, "Image Opacity", owner, &usage_count, first_usage, sizeof(first_usage));
            for (int e = 0; e < cue->post_effect_count; ++e) {
                LayerPostEffect* effect = &cue->post_effects[e];
                RegisterCurveUsage(effect->curve_intensity, curve_index, "Layer Effect Intensity", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_threshold, curve_index, "Layer Effect Threshold", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_radius, curve_index, "Layer Effect Radius", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_r, curve_index, "Layer Effect Color R", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_g, curve_index, "Layer Effect Color G", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_b, curve_index, "Layer Effect Color B", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_a, curve_index, "Layer Effect Color A", owner, &usage_count, first_usage, sizeof(first_usage));
            }
        }

        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/%s", scene_name, cue->sprite_name[0] ? cue->sprite_name : "anim_sprite");
            RegisterCurveUsage(cue->curve_x, curve_index, "AnimSprite X", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_y, curve_index, "AnimSprite Y", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_scale, curve_index, "AnimSprite Scale", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_rotation, curve_index, "AnimSprite Rotation", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_opacity, curve_index, "AnimSprite Opacity", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(cue->curve_frame, curve_index, "AnimSprite Frame", owner, &usage_count, first_usage, sizeof(first_usage));
            for (int e = 0; e < cue->post_effect_count; ++e) {
                LayerPostEffect* effect = &cue->post_effects[e];
                RegisterCurveUsage(effect->curve_intensity, curve_index, "Layer Effect Intensity", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_threshold, curve_index, "Layer Effect Threshold", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_radius, curve_index, "Layer Effect Radius", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_r, curve_index, "Layer Effect Color R", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_g, curve_index, "Layer Effect Color G", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_b, curve_index, "Layer Effect Color B", owner, &usage_count, first_usage, sizeof(first_usage));
                RegisterCurveUsage(effect->curve_color_a, curve_index, "Layer Effect Color A", owner, &usage_count, first_usage, sizeof(first_usage));
            }
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

        for (int i = 0; i < scene->post_effect_count; ++i) {
            PostEffect* effect = &scene->post_effects[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/Post Effect %d", scene_name, i + 1);
            RegisterCurveUsage(effect->curve_intensity, curve_index, "Post Intensity", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_threshold, curve_index, "Post Threshold", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_radius, curve_index, "Post Radius", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_r, curve_index, "Post Color R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_g, curve_index, "Post Color G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_b, curve_index, "Post Color B", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_a, curve_index, "Post Color A", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_amount, curve_index, "Post Amount", owner, &usage_count, first_usage, sizeof(first_usage));
        }
        for (int i = 0; i < scene->scene_layer_post_effect_count; ++i) {
            LayerPostEffect* effect = &scene->scene_layer_post_effects[i];
            char owner[128] = {};
            snprintf(owner, sizeof(owner), "%s/Scene Layer Effect %d", scene_name, i + 1);
            RegisterCurveUsage(effect->curve_intensity, curve_index, "Scene Layer Intensity", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_threshold, curve_index, "Scene Layer Threshold", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_radius, curve_index, "Scene Layer Radius", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_r, curve_index, "Scene Layer Color R", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_g, curve_index, "Scene Layer Color G", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_b, curve_index, "Scene Layer Color B", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_color_a, curve_index, "Scene Layer Color A", owner, &usage_count, first_usage, sizeof(first_usage));
            RegisterCurveUsage(effect->curve_amount, curve_index, "Scene Layer Amount", owner, &usage_count, first_usage, sizeof(first_usage));
        }
    }

    int points = editor->project->curves[curve_index].point_count;
    if (editor->project->curve_names[curve_index][0] != '\0') {
        if (usage_count > 1) {
            snprintf(out, out_size, "%s (+%d uses, %d pts)", editor->project->curve_names[curve_index], usage_count - 1, points);
        } else {
            snprintf(out, out_size, "%s (%d pts)", editor->project->curve_names[curve_index], points);
        }
        return;
    }
    if (usage_count <= 0) {
        snprintf(out, out_size, "Unassigned timing curve %d (%d pts)", curve_index, points);
    } else if (usage_count == 1) {
        snprintf(out, out_size, "%s (%d pts)", first_usage, points);
    } else {
        snprintf(out, out_size, "%s (+%d uses, %d pts)", first_usage, usage_count - 1, points);
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
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_scale); mark(cue->curve_rotation); mark(cue->curve_opacity);
            for (int e = 0; e < cue->post_effect_count; ++e) {
                LayerPostEffect* effect = &cue->post_effects[e];
                mark(effect->curve_intensity); mark(effect->curve_threshold); mark(effect->curve_radius);
                mark(effect->curve_color_r); mark(effect->curve_color_g); mark(effect->curve_color_b); mark(effect->curve_color_a);
            }
        }
        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_scale); mark(cue->curve_rotation); mark(cue->curve_opacity); mark(cue->curve_frame);
            for (int e = 0; e < cue->post_effect_count; ++e) {
                LayerPostEffect* effect = &cue->post_effects[e];
                mark(effect->curve_intensity); mark(effect->curve_threshold); mark(effect->curve_radius);
                mark(effect->curve_color_r); mark(effect->curve_color_g); mark(effect->curve_color_b); mark(effect->curve_color_a);
            }
        }
        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_size); mark(cue->curve_rotation);
            mark(cue->curve_color_r); mark(cue->curve_color_g); mark(cue->curve_color_b);
        }
        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            mark(cue->curve_x); mark(cue->curve_y); mark(cue->curve_speed); mark(cue->curve_size); mark(cue->curve_rotation);
            mark(cue->curve_opacity); mark(cue->curve_color_r); mark(cue->curve_color_g); mark(cue->curve_color_b);
            mark(cue->curve_wave_amp); mark(cue->curve_wave_freq); mark(cue->curve_wave_length); mark(cue->curve_jitter_amp); mark(cue->curve_jitter_freq);
        }
        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            mark(cue->curve_pos_x); mark(cue->curve_pos_y); mark(cue->curve_pos_z);
            mark(cue->curve_rot_x); mark(cue->curve_rot_y); mark(cue->curve_rot_z);
            mark(cue->curve_scale_x); mark(cue->curve_scale_y); mark(cue->curve_scale_z);
            mark(cue->curve_color_r); mark(cue->curve_color_g); mark(cue->curve_color_b); mark(cue->curve_color_a);
            mark(cue->curve_mesh_size); mark(cue->curve_metallic); mark(cue->curve_roughness); mark(cue->curve_fov);
        }
        for (int i = 0; i < scene->scene_layer_post_effect_count; ++i) {
            LayerPostEffect* effect = &scene->scene_layer_post_effects[i];
            mark(effect->curve_intensity); mark(effect->curve_threshold); mark(effect->curve_radius);
            mark(effect->curve_color_r); mark(effect->curve_color_g); mark(effect->curve_color_b);
            mark(effect->curve_color_a); mark(effect->curve_amount);
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

int CreateTriggerTimingCurve(EditorContext* editor, int track_index)
{
    return CreateCurveFromTriggerTrack(editor, track_index);
}

void RenderTimeline(EditorContext* editor) {
    if (!editor || !editor->project) return;

    if (editor->selected_scene_index < -1 ||
        editor->selected_scene_index >= editor->project->scene_count) {
        editor->selected_scene_index = -1;
        editor->selected_cue_index = -1;
    }
    
    bool timeline_visible = ImGui::Begin("Timeline", &editor->show_timeline);
    if (timeline_visible) {
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
        
        static float curve_view_zoom = 1.0f;
        static float curve_view_center_t = 0.5f;
        static float curve_view_center_v = 0.5f;
        static int curve_view_index = -1;
        if (curve_view_index != editor->selected_curve_index) {
            curve_view_zoom = 1.0f;
            curve_view_center_t = 0.5f;
            curve_view_center_v = 0.5f;
            curve_view_index = editor->selected_curve_index;
        }
        ImGui::Text("View");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##curve_zoom_out")) curve_view_zoom = fmaxf(1.0f, curve_view_zoom / 1.5f);
        ImGui::SameLine();
        ImGui::Text("%.1fx", curve_view_zoom);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##curve_zoom_in")) curve_view_zoom = fminf(32.0f, curve_view_zoom * 1.5f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shift+mouse wheel zooms. Middle mouse pans the zoomed view.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##curve_zoom_reset")) {
            curve_view_zoom = 1.0f;
            curve_view_center_t = 0.5f;
            curve_view_center_v = 0.5f;
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
                editor->selected_cue_type = CueTypeShader;
                editor->selected_curve_index = -1;
                editor->editing_curve_index = -1;
                editor->editing_curve_field = -1;
                editor->editing_curve_cue_type = CueTypeShader;
                editor->shader_modal_request_open = false;
                editor->music_modal_request_open = false;
                editor->image_modal_request_open = false;
                editor->animated_sprite_modal_request_open = false;
                editor->pixel_modal_request_open = false;
                editor->pixel_emitter_modal_request_open = false;
                editor->text_modal_request_open = false;
                editor->scroll_text_modal_request_open = false;
                editor->mesh_modal_request_open = false;
                editor->curve_editor_modal_request_open = false;
                editor->shader_modal_open = false;
                editor->music_modal_open = false;
                editor->image_modal_open = false;
                editor->animated_sprite_modal_open = false;
                editor->pixel_modal_open = false;
                editor->pixel_emitter_modal_open = false;
                editor->text_modal_open = false;
                editor->scroll_text_modal_open = false;
                editor->mesh_modal_open = false;
                editor->curve_editor_modal_open = false;
                editor->point_properties_modal_open = false;
                
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
        if (editor->selected_scene_index >= 0 &&
            editor->selected_scene_index < editor->project->scene_count) {
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
        
    }
    ImGui::End();
}

void RenderProperties(EditorContext* editor) {
    if (!editor || !editor->project) return;
    
    bool properties_visible = ImGui::Begin("Properties", &editor->show_properties);
    if (properties_visible) {
        ImGui::Text("Runtime");
        ImGui::Separator();
        bool runtime_fullscreen = editor->project->runtime_fullscreen;
        if (ImGui::Checkbox("Fullscreen", &runtime_fullscreen)) {
            editor->project->runtime_fullscreen = runtime_fullscreen;
            editor->project->modified = true;
        }
        char runtime_title[128] = {};
        strncpy_s(runtime_title, sizeof(runtime_title), editor->project->runtime_title, _TRUNCATE);
        if (ImGui::InputText("Title", runtime_title, sizeof(runtime_title))) {
            strncpy_s(editor->project->runtime_title, sizeof(editor->project->runtime_title), runtime_title, _TRUNCATE);
            editor->project->modified = true;
        }
        ImGui::Separator();

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

            static const char* post_effect_names[] = {
                "HDR Rendering", "ACES Tone Mapping", "Bloom", "Vignette",
                "Color Grading", "Film Grain", "Blue Noise Dithering",
                "Exponential Fog", "FXAA", "Chromatic Aberration",
                "Camera Shake", "Beat Flash", "Fade In / Fade Out",
                "CRT Warp", "Scanlines", "Lens Distortion", "Palette Cycling",
                "Heat Distortion", "Glitch", "Bloom Pulsing", "Feedback Buffer",
                "Infinite Zoom", "Recursive Feedback"
            };

            ImGui::Separator();
            char post_effect_header[96] = {};
            snprintf(post_effect_header, sizeof(post_effect_header), "Post Effects (%d)", scene->post_effect_count);
            if (ImGui::CollapsingHeader(post_effect_header, ImGuiTreeNodeFlags_DefaultOpen)) {
            static int new_post_effect_type = PostEffectBloom;
            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("Effect", &new_post_effect_type, post_effect_names, PostEffectCount);
            if (ImGui::Button("+ Add Post Effect")) {
                PostEffect effect = {};
                effect.type = new_post_effect_type;
                effect.enabled = true;
                effect.order = scene->post_effect_count;
                effect.intensity = 1.0f;
                effect.threshold = 1.0f;
                effect.radius = 1.0f;
                effect.color[0] = effect.color[1] = effect.color[2] = effect.color[3] = 1.0f;
                effect.end_time = -1.0f;
                effect.curve_intensity = effect.curve_threshold = effect.curve_radius = -1;
                effect.curve_color_r = effect.curve_color_g = effect.curve_color_b = effect.curve_color_a = -1;
                effect.curve_amount = -1;
                effect.trigger_track = -1;
                effect.trigger_pulse_beats = 0.5f;
                AddPostEffect(scene, effect);
                editor->project->modified = true;
            }

            for (int i = 0; i < scene->post_effect_count; ++i) {
                PostEffect* effect = &scene->post_effects[i];
                ImGui::PushID(i);
                ImGui::Separator();
                bool enabled = effect->enabled;
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    effect->enabled = enabled;
                    editor->project->modified = true;
                }
                ImGui::SameLine();
                const int effect_type = (effect->type >= 0 && effect->type < PostEffectCount) ? effect->type : 0;
                ImGui::Text("%d. %s", i + 1, post_effect_names[effect_type]);
                ImGui::SameLine();
                if (ImGui::SmallButton("Up") && i > 0) {
                    PostEffect temp = scene->post_effects[i - 1];
                    scene->post_effects[i - 1] = *effect;
                    *effect = temp;
                    editor->project->modified = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && i + 1 < scene->post_effect_count) {
                    PostEffect temp = scene->post_effects[i + 1];
                    scene->post_effects[i + 1] = *effect;
                    *effect = temp;
                    editor->project->modified = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) {
                    PostEffect clone = *effect;
                    AddPostEffect(scene, clone);
                    editor->post_frame_rendered = false;
                    editor->project->modified = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    DeletePostEffect(scene, i);
                    editor->post_frame_rendered = false;
                    editor->project->modified = true;
                    ImGui::PopID();
                    break;
                }
                if (ImGui::SliderFloat("Intensity", &effect->intensity, 0.0f, 2.0f)) {
                    editor->project->modified = true;
                }
                if (ImGui::SliderFloat("Threshold", &effect->threshold, 0.0f, 4.0f)) {
                    editor->project->modified = true;
                }
                if (ImGui::SliderFloat("Radius", &effect->radius, 0.0f, 4.0f)) {
                    editor->project->modified = true;
                }
                if (ImGui::ColorEdit4("Color", effect->color)) {
                    editor->project->modified = true;
                }
                if (ImGui::InputFloat("Start", &effect->start_time, 0.1f, 1.0f)) {
                    editor->project->modified = true;
                }
                if (ImGui::InputFloat("End (-1 = scene end)", &effect->end_time, 0.1f, 1.0f)) {
                    editor->project->modified = true;
                }
                if (ImGui::InputInt("Trigger track (-1 = off)", &effect->trigger_track)) {
                    editor->project->modified = true;
                }
                if (effect->trigger_track < -1) effect->trigger_track = -1;
                if (effect->trigger_track >= editor->project->trigger_track_count) {
                    effect->trigger_track = editor->project->trigger_track_count - 1;
                }
                if (ImGui::InputFloat("Trigger pulse beats", &effect->trigger_pulse_beats, 0.125f, 0.5f, "%.3f")) {
                    editor->project->modified = true;
                }
                if (effect->trigger_pulse_beats < 0.0f) effect->trigger_pulse_beats = 0.0f;
                if (ImGui::Button("Edit Effect Curves")) {
                    editor->selected_cue_index = i;
                    editor->editing_curve_cue_type = CueTypePostEffect;
                    editor->editing_curve_field = -1;
                    editor->editing_curve_index = effect->curve_intensity >= 0
                        ? effect->curve_intensity : effect->curve_threshold;
                    if (editor->editing_curve_index < 0) {
                        editor->editing_curve_index = effect->curve_radius >= 0
                            ? effect->curve_radius : effect->curve_amount;
                    }
                    if (editor->editing_curve_index < 0) {
                        editor->editing_curve_index = effect->curve_color_r;
                    }
                    if (editor->editing_curve_index < 0 &&
                        editor->project->curve_count < rev::runtime::kMaxCurves) {
                        int curve_index = editor->project->curve_count++;
                        editor->project->curves[curve_index] = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(editor->project->curves[curve_index], 0.0f, effect->intensity);
                        rev::curve::AddPoint(editor->project->curves[curve_index], 1.0f, effect->intensity);
                        effect->curve_intensity = curve_index;
                        editor->editing_curve_index = curve_index;
                        editor->project->modified = true;
                    }
                    if (editor->editing_curve_index >= 0) {
                        editor->curve_editor_modal_request_open = true;
                    }
                }
                ImGui::PopID();
            }
            }

            RenderLayerPostEffects(editor, scene->scene_layer_post_effects,
                                   &scene->scene_layer_post_effect_count,
                                   &editor->project->modified, "scene_layer", -1,
                                   editor->selected_scene_index);
            
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
                    if (ImGui::SmallButton("+")) {
                        ShaderCue cue = scene->shader_cues[i];
                        int new_index = AddShaderCue(scene, cue);
                        editor->editing_shader = scene->shader_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeShader;
                        editor->shader_modal_request_open = true;
                        editor->project->modified = true;
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
                cue.curve_rotation = -1;
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
                cue.curve_rotation = -1;
                cue.curve_opacity = -1;
                cue.curve_frame = -1;
                int new_index = AddAnimatedSpriteCue(scene, cue);
                editor->editing_animated_sprite = scene->animated_sprite_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypeAnimatedSprite;
                editor->animated_sprite_modal_request_open = true;
                editor->project->modified = true;
            }

            if (!project_saved) {
                ImGui::EndDisabled();
            }

            if (ImGui::Button("+ Pixel Cue")) {
                PixelCue cue = {};
                snprintf(cue.asset_key, sizeof(cue.asset_key), "pixel_%d.pix", scene->pixel_cue_count);
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
                cue.snap_to_pixels = 1;
                cue.curve_x = -1;
                cue.curve_y = -1;
                cue.curve_scale = -1;
                cue.curve_rotation = -1;
                cue.curve_opacity = -1;
                cue.curve_frame = -1;
                cue.curve_palette_offset = -1;
                int new_index = AddPixelCue(scene, cue);
                editor->editing_pixel = scene->pixel_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypePixel;
                editor->pixel_modal_request_open = true;
                editor->project->modified = true;
            }

            if (ImGui::Button("+ Pixel Emitter")) {
                PixelEmitterCue cue = {};
                cue.curve_x = cue.curve_y = -1;
                cue.curve_scale = cue.curve_rotation = -1;
                cue.curve_opacity = cue.curve_emission_rate = -1;
                cue.curve_speed_min = cue.curve_speed_max = -1;
                cue.curve_lifetime_min = cue.curve_lifetime_max = -1;
                cue.curve_scale_min = cue.curve_scale_max = -1;
                cue.visual_source = 1;
                cue.primitive_shape = 1;
                cue.primitive_color[0] = 1.0f;
                cue.primitive_color[1] = 1.0f;
                cue.primitive_color[2] = 1.0f;
                cue.primitive_color[3] = 1.0f;
                cue.x = 0.5f;
                cue.y = 0.5f;
                cue.scale = 1.0f;
                cue.opacity = 1.0f;
                cue.cue_start = 0.0f;
                cue.cue_end = scene->duration;
                cue.layer_order = 0;
                cue.max_particles = 128;
                cue.emission_rate = 24.0f;
                cue.burst_count = 8;
                cue.duration = scene->duration;
                cue.loop = 1;
                cue.speed_min = 0.05f;
                cue.speed_max = 0.15f;
                cue.lifetime_min = 0.5f;
                cue.lifetime_max = 1.5f;
                cue.scale_min = 0.5f;
                cue.scale_max = 1.0f;
                cue.seed = 1;
                int new_index = AddPixelEmitterCue(scene, cue);
                editor->editing_pixel_emitter = scene->pixel_emitter_cues[new_index];
                editor->selected_cue_index = new_index;
                editor->selected_cue_type = CueTypePixelEmitter;
                editor->pixel_emitter_modal_request_open = true;
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
                cue.blend_mode = 0;
                cue.curve_x = -1;
                cue.curve_y = -1;
                cue.curve_size = -1;
                cue.curve_rotation = -1;
                cue.curve_color_r = -1;
                cue.curve_color_g = -1;
                cue.curve_color_b = -1;
                InitializeTextAnimationConfig(&cue.animation);
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
                cue.curve_rotation = -1;
                cue.curve_opacity = -1;
                cue.curve_color_r = -1;
                cue.curve_color_g = -1;
                cue.curve_color_b = -1;
                cue.curve_wave_amp = -1;
                cue.curve_wave_freq = -1;
                cue.curve_wave_length = -1;
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
                    if (ImGui::SmallButton("+")) {
                        ImageCue cue = scene->image_cues[i];
                        int new_index = AddImageCue(scene, cue);
                        editor->editing_image = scene->image_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeImage;
                        editor->image_modal_request_open = true;
                        editor->project->modified = true;
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
                    if (ImGui::SmallButton("+")) {
                        AnimatedSpriteCue cue = scene->animated_sprite_cues[i];
                        int new_index = AddAnimatedSpriteCue(scene, cue);
                        editor->editing_animated_sprite = scene->animated_sprite_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeAnimatedSprite;
                        editor->animated_sprite_modal_request_open = true;
                        editor->project->modified = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeleteAnimatedSpriteCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
            }

            if (scene->pixel_cue_count > 0) {
                ImGui::Text("Pixel Cues:");
                for (int i = 0; i < scene->pixel_cue_count; ++i) {
                    ImGui::PushID(2750 + i);
                    const char* display_name = scene->pixel_cues[i].asset_key[0] != '\0'
                        ? scene->pixel_cues[i].asset_key : "(pixel asset)";
                    if (ImGui::Button(display_name)) {
                        editor->editing_pixel = scene->pixel_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = CueTypePixel;
                        editor->pixel_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+")) {
                        PixelCue cue = scene->pixel_cues[i];
                        int new_index = AddPixelCue(scene, cue);
                        editor->editing_pixel = scene->pixel_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypePixel;
                        editor->pixel_modal_request_open = true;
                        editor->project->modified = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeletePixelCue(scene, i);
                        editor->project->modified = true;
                    }
                    ImGui::PopID();
                }
            }

            if (scene->pixel_emitter_cue_count > 0) {
                ImGui::Text("Pixel Emitters:");
                for (int i = 0; i < scene->pixel_emitter_cue_count; ++i) {
                    ImGui::PushID(2850 + i);
                    const PixelEmitterCue& cue = scene->pixel_emitter_cues[i];
                    const char* display_name = cue.visual_source == 0
                        ? (cue.asset_key[0] != '\0' ? cue.asset_key : "(pixel asset emitter)")
                        : "(primitive emitter)";
                    if (ImGui::Button(display_name)) {
                        editor->editing_pixel_emitter = cue;
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = CueTypePixelEmitter;
                        editor->pixel_emitter_modal_request_open = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+")) {
                        int new_index = AddPixelEmitterCue(scene, cue);
                        editor->editing_pixel_emitter = scene->pixel_emitter_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypePixelEmitter;
                        editor->pixel_emitter_modal_request_open = true;
                        editor->project->modified = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        DeletePixelEmitterCue(scene, i);
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
                    if (ImGui::SmallButton("+")) {
                        TextCue cue = scene->text_cues[i];
                        int new_index = AddTextCue(scene, cue);
                        editor->editing_text = scene->text_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeText;
                        editor->text_modal_request_open = true;
                        editor->project->modified = true;
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
                    char display_label[64] = {};
                    snprintf(display_label, sizeof(display_label), "Scroll Text %d", i + 1);
                    if (ImGui::Button(display_label)) {
                        editor->editing_scroll_text = scene->scroll_text_cues[i];
                        editor->selected_cue_index = i;
                        editor->selected_cue_type = CueTypeScrollText;
                        editor->scroll_text_modal_request_open = true;
                    }
                    if (ImGui::IsItemHovered() && scene->scroll_text_cues[i].text[0] != '\0') {
                        ImGui::SetTooltip("%s", scene->scroll_text_cues[i].text);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+")) {
                        ScrollTextCue cue = scene->scroll_text_cues[i];
                        int new_index = AddScrollTextCue(scene, cue);
                        editor->editing_scroll_text = scene->scroll_text_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeScrollText;
                        editor->scroll_text_modal_request_open = true;
                        editor->project->modified = true;
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
                    if (ImGui::SmallButton("+")) {
                        MusicCue cue = scene->music_cues[i];
                        int new_index = AddMusicCue(scene, cue);
                        editor->editing_music = scene->music_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeMusic;
                        editor->music_modal_request_open = true;
                        editor->project->modified = true;
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
                    if (ImGui::SmallButton("+")) {
                        MeshCue cue = *mc;
                        int new_index = AddMeshCue(scene, cue);
                        editor->editing_mesh = scene->mesh_cues[new_index];
                        editor->selected_cue_index = new_index;
                        editor->selected_cue_type = CueTypeMesh;
                        editor->mesh_modal_request_open = true;
                        editor->project->modified = true;
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
        
    }
    ImGui::End();
}

void RenderAssetBrowser(EditorContext* editor) {
    if (!editor) return;
    
    bool asset_browser_visible = ImGui::Begin("Asset Browser", &editor->show_asset_browser);
    if (asset_browser_visible) {
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
        
        if (ImGui::CollapsingHeader("Images (.png, .jpg, .webp)", ImGuiTreeNodeFlags_DefaultOpen)) {
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

            // WebP files
            h_find = FindFirstFileA("assets/*.webp", &find_data);

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
        
    }
    ImGui::End();
}

void BuildCurveDisplayLabel(EditorContext* editor, int curve_index, char* out, size_t out_size)
{
    BuildCurveDisplayLabelInternal(editor, curve_index, out, out_size);
}

void RenderCurveEditor(EditorContext* editor) {
    if (!editor || !editor->project) return;
    
    bool curve_editor_visible = ImGui::Begin("Curve Editor", &editor->show_curve_editor);
    if (curve_editor_visible) {
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
                    memcpy(editor->project->curve_names[i], editor->project->curve_names[i + 1],
                           sizeof(editor->project->curve_names[i]));
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
                        memcpy(editor->project->curve_names[j], editor->project->curve_names[j + 1],
                               sizeof(editor->project->curve_names[j]));
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
            if (editor->selected_curve_index >= 0 && editor->selected_curve_index < editor->project->curve_count) {
                ImGui::SetNextItemWidth(360.0f);
                if (ImGui::InputText("Curve name", editor->project->curve_names[editor->selected_curve_index],
                                     sizeof(editor->project->curve_names[editor->selected_curve_index]))) {
                    editor->project->modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Optional name. Leave empty to show the connected asset and parameter automatically.");
                }
            }
        }

        ImGui::Separator();

        // Assign an existing curve to another parameter on the currently selected cue.
        if (editor->selected_curve_index >= 0 && editor->selected_curve_index < editor->project->curve_count &&
            editor->selected_scene_index >= 0 && editor->selected_scene_index < editor->project->scene_count &&
            editor->selected_cue_index >= 0) {
            SceneBlock* scene = &editor->project->scenes[editor->selected_scene_index];
            int* target_fields[128] = {};
            const char* target_names[128] = {};
            int target_count = 0;

            auto add_target = [&](const char* name, int* field) {
                if (!name || !field || target_count >= 128) return;
                target_names[target_count] = name;
                target_fields[target_count] = field;
                ++target_count;
            };
            static char asset_shader_target_names[rev::runtime::kMaxAssetShaders][17][96] = {};
            auto add_asset_shader_targets = [&](AssetShader* shaders, int shader_count) {
                const char* names[] = {
                    "Opacity", "Speed", "Intensity", "Warp", "Exposure", "Fade",
                    "Exposure Ramp", "Fade Ramp", "Palette Low R", "Palette Low G", "Palette Low B",
                    "Palette Mid R", "Palette Mid G", "Palette Mid B", "Palette High R",
                    "Palette High G", "Palette High B"
                };
                if (shader_count > rev::runtime::kMaxAssetShaders) shader_count = rev::runtime::kMaxAssetShaders;
                for (int shader_index = 0; shader_index < shader_count; ++shader_index) {
                    AssetShader* shader = &shaders[shader_index];
                    int* fields[] = {
                        &shader->curve_opacity, &shader->curve_speed, &shader->curve_intensity, &shader->curve_warp,
                        &shader->curve_exposure, &shader->curve_fade, &shader->curve_exposure_ramp, &shader->curve_fade_ramp,
                        &shader->curve_palette_low_r, &shader->curve_palette_low_g, &shader->curve_palette_low_b,
                        &shader->curve_palette_mid_r, &shader->curve_palette_mid_g, &shader->curve_palette_mid_b,
                        &shader->curve_palette_high_r, &shader->curve_palette_high_g, &shader->curve_palette_high_b
                    };
                    for (int parameter = 0; parameter < 17; ++parameter) {
                        snprintf(asset_shader_target_names[shader_index][parameter],
                                 sizeof(asset_shader_target_names[shader_index][parameter]),
                                 "Asset Shader %d %s", shader_index + 1, names[parameter]);
                        add_target(asset_shader_target_names[shader_index][parameter], fields[parameter]);
                    }
                }
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
                add_target("Image Rotation", &cue->curve_rotation);
                add_target("Image Opacity", &cue->curve_opacity);
                for (int i = 0; i < cue->post_effect_count; ++i) {
                    LayerPostEffect* effect = &cue->post_effects[i];
                    add_target("Layer Effect Intensity", &effect->curve_intensity);
                    add_target("Layer Effect Threshold", &effect->curve_threshold);
                    add_target("Layer Effect Radius", &effect->curve_radius);
                    add_target("Layer Effect Color R", &effect->curve_color_r);
                    add_target("Layer Effect Color G", &effect->curve_color_g);
                    add_target("Layer Effect Color B", &effect->curve_color_b);
                    add_target("Layer Effect Color A", &effect->curve_color_a);
                }
                add_asset_shader_targets(cue->shaders, cue->shader_count);
            } else if (editor->selected_cue_type == CueTypeAnimatedSprite && editor->selected_cue_index < scene->animated_sprite_cue_count) {
                AnimatedSpriteCue* cue = &scene->animated_sprite_cues[editor->selected_cue_index];
                add_target("AnimSprite X", &cue->curve_x);
                add_target("AnimSprite Y", &cue->curve_y);
                add_target("AnimSprite Scale", &cue->curve_scale);
                add_target("AnimSprite Rotation", &cue->curve_rotation);
                add_target("AnimSprite Opacity", &cue->curve_opacity);
                add_target("AnimSprite Frame", &cue->curve_frame);
                for (int i = 0; i < cue->post_effect_count; ++i) {
                    LayerPostEffect* effect = &cue->post_effects[i];
                    add_target("Layer Effect Intensity", &effect->curve_intensity);
                    add_target("Layer Effect Threshold", &effect->curve_threshold);
                    add_target("Layer Effect Radius", &effect->curve_radius);
                    add_target("Layer Effect Color R", &effect->curve_color_r);
                    add_target("Layer Effect Color G", &effect->curve_color_g);
                    add_target("Layer Effect Color B", &effect->curve_color_b);
                    add_target("Layer Effect Color A", &effect->curve_color_a);
                }
                add_asset_shader_targets(cue->shaders, cue->shader_count);
            } else if (editor->selected_cue_type == CueTypeText && editor->selected_cue_index < scene->text_cue_count) {
                TextCue* cue = &scene->text_cues[editor->selected_cue_index];
                add_target("Text X", &cue->curve_x);
                add_target("Text Y", &cue->curve_y);
                add_target("Text Size", &cue->curve_size);
                add_target("Text Rotation", &cue->curve_rotation);
                add_target("Text Color R", &cue->curve_color_r);
                add_target("Text Color G", &cue->curve_color_g);
                add_target("Text Color B", &cue->curve_color_b);
            } else if (editor->selected_cue_type == CueTypeScrollText && editor->selected_cue_index < scene->scroll_text_cue_count) {
                ScrollTextCue* cue = &scene->scroll_text_cues[editor->selected_cue_index];
                add_target("Scroll X", &cue->curve_x);
                add_target("Scroll Y", &cue->curve_y);
                add_target("Scroll Speed", &cue->curve_speed);
                add_target("Scroll Size", &cue->curve_size);
                add_target("Scroll Rotation", &cue->curve_rotation);
                add_target("Scroll Opacity", &cue->curve_opacity);
                add_target("Scroll Color R", &cue->curve_color_r);
                add_target("Scroll Color G", &cue->curve_color_g);
                add_target("Scroll Color B", &cue->curve_color_b);
                add_target("Scroll Wave Amp", &cue->curve_wave_amp);
                add_target("Scroll Wave Freq", &cue->curve_wave_freq);
                add_target("Scroll Wave Length", &cue->curve_wave_length);
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
            } else if (editor->selected_cue_type == CueTypePixel && editor->selected_cue_index < scene->pixel_cue_count) {
                PixelCue* cue = &scene->pixel_cues[editor->selected_cue_index];
                add_target("Pixel X Position", &cue->curve_x);
                add_target("Pixel Y Position", &cue->curve_y);
                add_target("Pixel Scale", &cue->curve_scale);
                add_target("Pixel Rotation", &cue->curve_rotation);
                add_target("Pixel Opacity", &cue->curve_opacity);
                add_target("Pixel Frame", &cue->curve_frame);
                add_target("Pixel Palette Offset", &cue->curve_palette_offset);
                for (int i = 0; i < cue->post_effect_count; ++i) {
                    LayerPostEffect* effect = &cue->post_effects[i];
                    add_target("Layer Effect Intensity", &effect->curve_intensity);
                    add_target("Layer Effect Threshold", &effect->curve_threshold);
                    add_target("Layer Effect Radius", &effect->curve_radius);
                    add_target("Layer Effect Color R", &effect->curve_color_r);
                    add_target("Layer Effect Color G", &effect->curve_color_g);
                    add_target("Layer Effect Color B", &effect->curve_color_b);
                    add_target("Layer Effect Color A", &effect->curve_color_a);
                }
                add_asset_shader_targets(cue->shaders, cue->shader_count);
            } else if (editor->selected_cue_type == CueTypePixelEmitter && editor->selected_cue_index < scene->pixel_emitter_cue_count) {
                PixelEmitterCue* cue = &scene->pixel_emitter_cues[editor->selected_cue_index];
                add_target("Emitter X Position", &cue->curve_x);
                add_target("Emitter Y Position", &cue->curve_y);
                add_target("Emitter Scale", &cue->curve_scale);
                add_target("Emitter Rotation", &cue->curve_rotation);
                add_target("Emitter Opacity", &cue->curve_opacity);
                add_target("Emission Rate", &cue->curve_emission_rate);
                add_target("Speed Min", &cue->curve_speed_min);
                add_target("Speed Max", &cue->curve_speed_max);
                add_target("Lifetime Min", &cue->curve_lifetime_min);
                add_target("Lifetime Max", &cue->curve_lifetime_max);
                add_target("Particle Scale Min", &cue->curve_scale_min);
                add_target("Particle Scale Max", &cue->curve_scale_max);
            } else if (editor->selected_cue_type == CueTypePostEffect && editor->selected_cue_index < scene->post_effect_count) {
                PostEffect* effect = &scene->post_effects[editor->selected_cue_index];
                add_target("Post Effect Intensity", &effect->curve_intensity);
                add_target("Post Effect Threshold", &effect->curve_threshold);
                add_target("Post Effect Radius", &effect->curve_radius);
                add_target("Post Effect Color R", &effect->curve_color_r);
                add_target("Post Effect Color G", &effect->curve_color_g);
                add_target("Post Effect Color B", &effect->curve_color_b);
                add_target("Post Effect Color A", &effect->curve_color_a);
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
                    } else if (editor->selected_cue_type == CueTypePixel && editor->selected_cue_index < scene->pixel_cue_count) {
                        editor->editing_pixel = scene->pixel_cues[editor->selected_cue_index];
                    } else if (editor->selected_cue_type == CueTypePixelEmitter && editor->selected_cue_index < scene->pixel_emitter_cue_count) {
                        editor->editing_pixel_emitter = scene->pixel_emitter_cues[editor->selected_cue_index];
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
        
        static float curve_view_zoom = 1.0f;
        static float curve_view_center_t = 0.5f;
        static float curve_view_center_v = 0.5f;
        static int curve_view_index = -1;
        if (curve_view_index != editor->selected_curve_index) {
            curve_view_zoom = 1.0f;
            curve_view_center_t = 0.5f;
            curve_view_center_v = 0.5f;
            curve_view_index = editor->selected_curve_index;
        }
        ImGui::Text("View");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##curve_zoom_out")) curve_view_zoom = fmaxf(1.0f, curve_view_zoom / 1.5f);
        ImGui::SameLine();
        ImGui::Text("%.1fx", curve_view_zoom);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##curve_zoom_in")) curve_view_zoom = fminf(32.0f, curve_view_zoom * 1.5f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##curve_zoom_reset")) {
            curve_view_zoom = 1.0f;
            curve_view_center_t = 0.5f;
            curve_view_center_v = 0.5f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shift+mouse wheel zooms. Middle mouse pans the zoomed view.");

        ImGui::Separator();
        
        // Curve canvas
        if (editor->selected_curve_index >= 0 && editor->selected_curve_index < editor->project->curve_count) {
            rev::curve::Curve* curve = &editor->project->curves[editor->selected_curve_index];
            
            // Canvas setup
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            canvas_size.y = (canvas_size.y > 300.0f) ? canvas_size.y : 300.0f;
            canvas_pos.x += 52.0f;
            canvas_size.x = fmaxf(160.0f, canvas_size.x - 52.0f);
            canvas_size.y = fmaxf(220.0f, canvas_size.y - 38.0f);

            static int selection_curve_index = -1;
            static bool selected_points[4096] = {};
            static int selected_point_count = 0;
            static bool selecting_rectangle = false;
            static ImVec2 rectangle_start = {};
            static ImVec2 rectangle_end = {};
            static bool dragging_selection = false;
            static ImVec2 drag_start = {};
            static float drag_start_t[4096] = {};
            static float drag_start_v[4096] = {};

            if (selection_curve_index != editor->selected_curve_index) {
                memset(selected_points, 0, sizeof(selected_points));
                selected_point_count = 0;
                selection_curve_index = editor->selected_curve_index;
            }

            float graph_min = 0.0f;
            float graph_max = 1.0f;
            for (int i = 0; i < curve->point_count; ++i) {
                graph_min = fminf(graph_min, curve->points[i].v);
                graph_max = fmaxf(graph_max, curve->points[i].v);
            }
            float graph_range = graph_max - graph_min;
            float graph_padding = fmaxf(0.25f, graph_range * 0.12f);
            graph_min -= graph_padding;
            graph_max += graph_padding;
            graph_range = graph_max - graph_min;

            if (curve_view_zoom <= 1.0f) curve_view_center_v = (graph_min + graph_max) * 0.5f;
            float view_t_range = 1.0f / curve_view_zoom;
            float view_v_range = graph_range / curve_view_zoom;
            float view_t_half_range = view_t_range * 0.5f;
            curve_view_center_t = fmaxf(view_t_half_range,
                                        fminf(1.0f - view_t_half_range, curve_view_center_t));
            float view_t_min = curve_view_center_t - view_t_range * 0.5f;
            float view_v_min = curve_view_center_v - view_v_range * 0.5f;
            float view_v_max = curve_view_center_v + view_v_range * 0.5f;

            auto ValueToScreenY = [&](float value) {
                return canvas_pos.y + canvas_size.y - (value - view_v_min) / view_v_range * canvas_size.y;
            };
            auto ScreenToValue = [&](float screen_y) {
                return view_v_min + (canvas_size.y - (screen_y - canvas_pos.y)) / canvas_size.y * view_v_range;
            };
            auto TimeToScreenX = [&](float time) {
                return canvas_pos.x + (time - view_t_min) / view_t_range * canvas_size.x;
            };
            auto ScreenToTime = [&](float screen_x) {
                return view_t_min + (screen_x - canvas_pos.x) / canvas_size.x * view_t_range;
            };
            
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

            for (int i = 0; i <= 5; ++i) {
                float value = view_v_max - (view_v_range * (float)i / 5.0f);
                char value_label[32] = {};
                snprintf(value_label, sizeof(value_label), "%.2f", value);
                draw_list->AddText(ImVec2(canvas_pos.x - 48.0f, ValueToScreenY(value) - 7.0f),
                                   IM_COL32(180, 180, 180, 230), value_label);
            }
            if (graph_min < 0.0f && graph_max > 0.0f) {
                float zero_y = ValueToScreenY(0.0f);
                draw_list->AddLine(ImVec2(canvas_pos.x, zero_y),
                                   ImVec2(canvas_pos.x + canvas_size.x, zero_y),
                                   IM_COL32(130, 130, 130, 220), 1.5f);
            }
            
            // Border
            draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                              IM_COL32(100, 100, 100, 255));
            
            // Bottom labels show normalized frame position and real elapsed time.
            for (int i = 0; i <= 5; ++i) {
                float t = view_t_min + view_t_range * (float)i / 5.0f;
                float seconds = t * curve->duration;
                int whole_seconds = (int)seconds;
                int milliseconds = (int)((seconds - whole_seconds) * 1000.0f + 0.5f);
                if (milliseconds >= 1000) {
                    ++whole_seconds;
                    milliseconds = 0;
                }
                char time_label[48] = {};
                snprintf(time_label, sizeof(time_label), "f %.2f  %d:%03d", t, whole_seconds, milliseconds);
                ImVec2 text_size = ImGui::CalcTextSize(time_label);
                draw_list->AddText(ImVec2(TimeToScreenX(t) - text_size.x * 0.5f,
                                          canvas_pos.y + canvas_size.y + 5.0f),
                                   IM_COL32(180, 180, 180, 230), time_label);
            }

            // Draw curve line
            draw_list->PushClipRect(canvas_pos,
                                    ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);
            if (curve->point_count > 1) {
                const int segments = 100;
                for (int i = 0; i < segments; ++i) {
                    float t0 = (float)i / segments;
                    float t1 = (float)(i + 1) / segments;
                    float v0 = rev::curve::Evaluate(*curve, t0);
                    float v1 = rev::curve::Evaluate(*curve, t1);
                    
                    ImVec2 p0 = ImVec2(TimeToScreenX(t0), ValueToScreenY(v0));
                    ImVec2 p1 = ImVec2(TimeToScreenX(t1), ValueToScreenY(v1));
                    
                    draw_list->AddLine(p0, p1, IM_COL32(100, 200, 255, 255), 2.0f);
                }
            }
            
            // Draw and interact with control points
            ImGui::SetCursorScreenPos(canvas_pos);
            ImGui::InvisibleButton("canvas", canvas_size);
            bool is_hovered = ImGui::IsItemHovered();
            ImVec2 mouse_pos = ImGui::GetMousePos();

            if (is_hovered && ImGui::GetIO().KeyShift && ImGui::GetIO().MouseWheel != 0.0f) {
                float mouse_t = ScreenToTime(mouse_pos.x);
                float mouse_v = ScreenToValue(mouse_pos.y);
                curve_view_zoom = fminf(32.0f, fmaxf(1.0f, curve_view_zoom * powf(1.15f, ImGui::GetIO().MouseWheel)));
                view_t_range = 1.0f / curve_view_zoom;
                view_v_range = graph_range / curve_view_zoom;
                curve_view_center_t = mouse_t + (0.5f - (mouse_pos.x - canvas_pos.x) / canvas_size.x) * view_t_range;
                curve_view_center_v = mouse_v - (0.5f - (mouse_pos.y - canvas_pos.y) / canvas_size.y) * view_v_range;
            }
            if (is_hovered && curve_view_zoom > 1.0f && ImGui::IsMouseDragging(2)) {
                curve_view_center_t -= ImGui::GetIO().MouseDelta.x / canvas_size.x * view_t_range;
                curve_view_center_v += ImGui::GetIO().MouseDelta.y / canvas_size.y * view_v_range;
            }
            
            // Double-click adds a point; double-clicking an existing point does not open another editor.
            bool clicked_on_point = false;
            int hovered_point = -1;
            for (int i = 0; i < curve->point_count; ++i) {
                ImVec2 point_pos = ImVec2(TimeToScreenX(curve->points[i].t),
                                          ValueToScreenY(curve->points[i].v));
                float dx = mouse_pos.x - point_pos.x;
                float dy = mouse_pos.y - point_pos.y;
                if (dx * dx + dy * dy <= 64.0f) {
                    hovered_point = i;
                    break;
                }
            }

            if (is_hovered && ImGui::IsMouseDoubleClicked(0) && hovered_point < 0) {
                float t = ScreenToTime(mouse_pos.x);
                float v = ScreenToValue(mouse_pos.y);
                t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
                rev::curve::AddPoint(*curve, t, v, rev::curve::EaseMode::Linear);
                rev::curve::SortPoints(*curve);
                editor->project->modified = true;
            }
            
            // Draw control points
            for (int i = 0; i < curve->point_count; ++i) {
                rev::curve::Point* pt = &curve->points[i];
                ImVec2 point_pos = ImVec2(TimeToScreenX(pt->t),
                                         ValueToScreenY(pt->v));
                
                float point_radius = 6.0f;
                bool point_hovered = (mouse_pos.x - point_pos.x) * (mouse_pos.x - point_pos.x) +
                                    (mouse_pos.y - point_pos.y) * (mouse_pos.y - point_pos.y) < point_radius * point_radius;
                
                // Shift-click toggles a point; a normal click selects only that point.
                if (point_hovered && ImGui::IsMouseClicked(0)) {
                    clicked_on_point = true;
                    if (ImGui::GetIO().KeyShift) {
                        selected_points[i] = !selected_points[i];
                        selected_point_count += selected_points[i] ? 1 : -1;
                    } else {
                        memset(selected_points, 0, sizeof(selected_points));
                        selected_points[i] = true;
                        selected_point_count = 1;
                    }
                    editor->selected_point_index = i;
                    dragging_selection = true;
                    drag_start = mouse_pos;
                    for (int j = 0; j < curve->point_count && j < 4096; ++j) {
                        drag_start_t[j] = curve->points[j].t;
                        drag_start_v[j] = curve->points[j].v;
                    }
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
                ImU32 point_color = selected_points[i] ? IM_COL32(100, 255, 100, 255) :
                                   (point_hovered ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 255, 255, 255));
                draw_list->AddCircleFilled(point_pos, point_radius, point_color);
                draw_list->AddCircle(point_pos, point_radius, IM_COL32(0, 0, 0, 255), 0, 1.5f);
            }

            if (is_hovered && ImGui::IsMouseClicked(0) && !clicked_on_point) {
                if (ImGui::GetIO().KeyShift) {
                    selecting_rectangle = true;
                    rectangle_start = mouse_pos;
                    rectangle_end = mouse_pos;
                } else {
                    memset(selected_points, 0, sizeof(selected_points));
                    selected_point_count = 0;
                }
            }
            if (selecting_rectangle && ImGui::IsMouseDown(0)) {
                rectangle_end = mouse_pos;
                ImVec2 min_corner(fminf(rectangle_start.x, rectangle_end.x),
                                  fminf(rectangle_start.y, rectangle_end.y));
                ImVec2 max_corner(fmaxf(rectangle_start.x, rectangle_end.x),
                                  fmaxf(rectangle_start.y, rectangle_end.y));
                draw_list->AddRect(min_corner, max_corner, IM_COL32(100, 220, 255, 255), 0.0f, 0, 1.5f);
            }
            if (selecting_rectangle && ImGui::IsMouseReleased(0)) {
                ImVec2 min_corner(fminf(rectangle_start.x, rectangle_end.x),
                                  fminf(rectangle_start.y, rectangle_end.y));
                ImVec2 max_corner(fmaxf(rectangle_start.x, rectangle_end.x),
                                  fmaxf(rectangle_start.y, rectangle_end.y));
                for (int i = 0; i < curve->point_count && i < 4096; ++i) {
                    ImVec2 point_pos(TimeToScreenX(curve->points[i].t),
                                     ValueToScreenY(curve->points[i].v));
                    if (point_pos.x >= min_corner.x && point_pos.x <= max_corner.x &&
                        point_pos.y >= min_corner.y && point_pos.y <= max_corner.y && !selected_points[i]) {
                        selected_points[i] = true;
                        ++selected_point_count;
                        if (editor->selected_point_index < 0) editor->selected_point_index = i;
                    }
                }
                selecting_rectangle = false;
            }
            if (dragging_selection && ImGui::IsMouseDown(0) && selected_point_count > 0) {
                float delta_t = (mouse_pos.x - drag_start.x) / canvas_size.x * view_t_range;
                float delta_v = -(mouse_pos.y - drag_start.y) / canvas_size.y * view_v_range;
                for (int i = 0; i < curve->point_count && i < 4096; ++i) {
                    if (!selected_points[i]) continue;
                    if (i != 0 && i != curve->point_count - 1) {
                        curve->points[i].t = fmaxf(0.0f, fminf(1.0f, drag_start_t[i] + delta_t));
                    }
                    curve->points[i].v = drag_start_v[i] + delta_v;
                }
                editor->project->modified = true;
            }
            if (dragging_selection && ImGui::IsMouseReleased(0)) {
                dragging_selection = false;
                rev::curve::SortPoints(*curve);
            }
            draw_list->PopClipRect();
            
            // Direct point editing. Multi-selection applies a value/ease edit to every selected node.
            if (selected_point_count > 0) {
                ImGui::Separator();
                ImGui::Text("%d selected node%s", selected_point_count, selected_point_count == 1 ? "" : "s");
                if (selected_point_count == 1 && editor->selected_point_index >= 0 &&
                    editor->selected_point_index < curve->point_count) {
                    rev::curve::Point* pt = &curve->points[editor->selected_point_index];
                    float time_value = pt->t;
                    if (ImGui::DragFloat("Frame", &time_value, 0.001f, 0.0f, 1.0f, "%.3f")) {
                        if (editor->selected_point_index != 0 && editor->selected_point_index != curve->point_count - 1)
                            pt->t = fmaxf(0.0f, fminf(1.0f, time_value));
                        editor->project->modified = true;
                    }
                    ImGui::SameLine();
                    float real_time = pt->t * curve->duration;
                    int real_seconds = (int)real_time;
                    int real_milliseconds = (int)((real_time - real_seconds) * 1000.0f + 0.5f);
                    ImGui::Text("Real time %d:%03d", real_seconds, real_milliseconds);
                }
                float value = curve->points[editor->selected_point_index].v;
                if (ImGui::DragFloat("Value", &value, 0.1f, -FLT_MAX, FLT_MAX, "%.3f")) {
                    for (int i = 0; i < curve->point_count && i < 4096; ++i)
                        if (selected_points[i]) curve->points[i].v = value;
                    editor->project->modified = true;
                }
                
                const char* ease_modes[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut", "Smoothstep", "Hold"};
                int current_mode = (int)curve->points[editor->selected_point_index].mode;
                if (ImGui::Combo("Ease Mode", &current_mode, ease_modes, 6)) {
                    for (int i = 0; i < curve->point_count && i < 4096; ++i)
                        if (selected_points[i]) curve->points[i].mode = (rev::curve::EaseMode)current_mode;
                    editor->project->modified = true;
                }
            }
            
            // Instructions
            ImGui::Separator();
            ImGui::TextDisabled("Shift-click: multi-select | Shift-drag: box select | Double-click: add | Drag: move | Right-click: delete");
        } else {
            ImGui::Text("No curve selected. Create or select a curve to edit.");
        }
        
    }
    ImGui::End();
}

void RenderTriggerRecorder(EditorContext* editor) {
    if (!editor || !editor->show_trigger_recorder) return;

    ImGui::Begin("Trigger Recorder", &editor->show_trigger_recorder);
    ImGui::InputText("Track name", editor->recording_track_name,
                     sizeof(editor->recording_track_name));
    ImGui::InputFloat("BPM", &editor->recording_bpm, 1.0f, 10.0f, "%.1f");
    ImGui::InputFloat("Beat offset", &editor->recording_beat_offset, 0.01f, 0.1f, "%.3f");
    ImGui::InputFloat("Quantize beats", &editor->recording_quantize_beats, 0.125f, 0.5f, "%.3f");
    if (editor->recording_quantize_beats <= 0.0f) editor->recording_quantize_beats = 0.5f;
    if (editor->recording_bpm <= 0.0f) editor->recording_bpm = 120.0f;

    bool can_append_curve = editor->recording_curve_target &&
        *editor->recording_curve_target >= 0 &&
        *editor->recording_curve_target < editor->project->curve_count;
    if (can_append_curve) {
        ImGui::Checkbox("Append to existing curve", &editor->recording_append_curve);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Keep the existing curve and add this recording after its current duration.\n"
                              "The curve duration is extended automatically.");
        }
    }

    if (editor->project->curve_count > 0) {
        static char append_curve_labels[rev::runtime::kMaxCurves][256] = {};
        const char* append_curve_items[rev::runtime::kMaxCurves] = {};
        for (int i = 0; i < editor->project->curve_count; ++i) {
            BuildCurveDisplayLabelInternal(editor, i, append_curve_labels[i], sizeof(append_curve_labels[i]));
            append_curve_items[i] = append_curve_labels[i];
        }
        if (editor->recording_append_curve_index < 0 ||
            editor->recording_append_curve_index >= editor->project->curve_count) {
            editor->recording_append_curve_index = can_append_curve
                ? *editor->recording_curve_target : 0;
        }
        ImGui::SetNextItemWidth(360.0f);
        ImGui::Combo("Target curve", &editor->recording_append_curve_index,
                     append_curve_items, editor->project->curve_count, 8);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose the existing curve for append or merge recording.");
        }
    } else {
        ImGui::TextDisabled("No existing curves available for append.");
    }

    if (editor->project->trigger_track_count > 0) {
        ImGui::Text("Recorded tracks");
        for (int i = 0; i < editor->project->trigger_track_count; ++i) {
            TriggerTrack* track = &editor->project->trigger_tracks[i];
            char track_label[128] = {};
            snprintf(track_label, sizeof(track_label), "%d. %s (%d events)",
                     i + 1, track->name, track->event_count);
            if (ImGui::Selectable(track_label, editor->recording_track_index == i)) {
                editor->recording_track_index = i;
                strncpy_s(editor->recording_track_name, sizeof(editor->recording_track_name),
                          track->name, _TRUNCATE);
                editor->recording_bpm = track->timing.bpm;
                editor->recording_beat_offset = track->timing.beat_offset;
            }
        }
    }

    if (!editor->trigger_recording) {
        if (ImGui::Button("Start recording") &&
            editor->project->trigger_track_count < rev::runtime::kMaxTriggerTracks) {
            int index = editor->project->trigger_track_count++;
            TriggerTrack* track = &editor->project->trigger_tracks[index];
            memset(track, 0, sizeof(*track));
            strncpy_s(track->name, sizeof(track->name), editor->recording_track_name, _TRUNCATE);
            track->timing.bpm = editor->recording_bpm;
            track->timing.beat_offset = editor->recording_beat_offset;
            editor->recording_track_index = index;
            editor->current_time = 0.0f;
            editor->playing = true;
            editor->trigger_recording = true;
            editor->project->modified = true;
        }
    } else if (ImGui::Button("Stop recording")) {
        editor->trigger_recording = false;
        editor->playing = false;
        if (editor->recording_curve_target && editor->recording_track_index >= 0) {
            int curve_index = -1;
            if (editor->recording_append_curve &&
                editor->recording_append_curve_index >= 0 &&
                editor->recording_append_curve_index < editor->project->curve_count) {
                curve_index = editor->recording_append_curve_index;
                if (!AppendTriggerTrackToCurve(editor, editor->recording_track_index, curve_index)) {
                    curve_index = -1;
                }
            } else {
                curve_index = CreateTriggerTimingCurve(editor, editor->recording_track_index);
            }
            if (curve_index >= 0) {
                *editor->recording_curve_target = curve_index;
                editor->editing_curve_index = curve_index;
                editor->editing_curve_field = -1;
                snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label),
                         "%s timing", editor->recording_target_label[0] ? editor->recording_target_label : "Parameter");
                editor->curve_editor_modal_request_open = true;
            }
            editor->recording_curve_target = nullptr;
            editor->recording_target_label[0] = '\0';
            editor->recording_append_curve = false;
        }
    }

    if (editor->recording_track_index >= 0 &&
        editor->recording_track_index < editor->project->trigger_track_count) {
        TriggerTrack* track = &editor->project->trigger_tracks[editor->recording_track_index];
        ImGui::Text("Track: %s", track->name);
        ImGui::Text("Events: %d", track->event_count);
        ImGui::Text("Press either Ctrl to record; Esc stops.");
        bool append_target_valid = editor->recording_append_curve_index >= 0 &&
            editor->recording_append_curve_index < editor->project->curve_count;
        ImGui::BeginDisabled(!append_target_valid || editor->trigger_recording);
        if (ImGui::Button("Append recording")) {
            if (AppendTriggerTrackToCurve(editor, editor->recording_track_index,
                                          editor->recording_append_curve_index)) {
                editor->selected_curve_index = editor->recording_append_curve_index;
                editor->editing_curve_index = editor->recording_append_curve_index;
                editor->show_curve_editor = true;
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                          "Recording appended and curve duration extended.", _TRUNCATE);
                editor->build_status_timer = 4.0f;
            } else {
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                          "Could not append recording to curve.", _TRUNCATE);
                editor->build_status_timer = 3.0f;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Append this track's events after the selected curve duration.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!append_target_valid || editor->trigger_recording);
        if (ImGui::Button("Merge recording")) {
            if (MergeTriggerTrackIntoCurve(editor, editor->recording_track_index,
                                           editor->recording_append_curve_index)) {
                editor->selected_curve_index = editor->recording_append_curve_index;
                editor->editing_curve_index = editor->recording_append_curve_index;
                editor->show_curve_editor = true;
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                          "Recording merged into the selected curve.", _TRUNCATE);
                editor->build_status_timer = 4.0f;
            } else {
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                          "Could not merge recording into curve.", _TRUNCATE);
                editor->build_status_timer = 3.0f;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Add this track's events on top of the selected curve without replacing points or changing duration.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Create reusable curve")) {
            int curve_index = CreateTriggerTimingCurve(editor, editor->recording_track_index);
            if (curve_index >= 0) {
                editor->selected_curve_index = curve_index;
                editor->show_curve_editor = true;
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                          "Trigger curve created. Select a parameter in Curve Editor to assign it.", _TRUNCATE);
                editor->build_status_timer = 4.0f;
            } else {
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                          "Could not create trigger curve.", _TRUNCATE);
                editor->build_status_timer = 3.0f;
            }
        }
        if (ImGui::Button("Clear events")) {
            track->event_count = 0;
            editor->project->modified = true;
        }
    }
    ImGui::End();
}

} // namespace editor
} // namespace rev

