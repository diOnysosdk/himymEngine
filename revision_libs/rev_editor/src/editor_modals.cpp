#include "editor_internal.h"
#include "rev_shader.h"
#include "rev_mesh.h"
#include "rev_gltf.h"
#include "rev_pixel.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <limits>
#include <windows.h>
#include <gdiplus.h>
#include "imgui.h"

namespace rev {
namespace editor {

static void CloseCueSettingsForRecording(EditorContext* editor)
{
    if (!editor) return;

    switch (editor->selected_cue_type) {
        case CueTypeShader: editor->shader_modal_open = false; break;
        case CueTypeImage: editor->image_modal_open = false; break;
        case CueTypeAnimatedSprite: editor->animated_sprite_modal_open = false; break;
        case CueTypePixel: editor->pixel_modal_open = false; break;
        case CueTypePixelEmitter: editor->pixel_emitter_modal_open = false; break;
        case CueTypeText: editor->text_modal_open = false; break;
        case CueTypeScrollText: editor->scroll_text_modal_open = false; break;
        case CueTypeMesh: editor->mesh_modal_open = false; break;
        default: break;
    }
    ImGui::CloseCurrentPopup();
}

static void DrawCurveRecordButton(EditorContext* editor, int* curve_target,
                                  const char* parameter_label, bool* modified,
                                  const char* id_suffix)
{
    if (!editor || !curve_target || !parameter_label) return;
    ImGui::SameLine();
    char button_label[96] = {};
    snprintf(button_label, sizeof(button_label), "Record##%s", id_suffix ? id_suffix : parameter_label);
    if (ImGui::SmallButton(button_label)) {
        editor->recording_curve_target = curve_target;
        strncpy_s(editor->recording_target_label, sizeof(editor->recording_target_label),
                  parameter_label, _TRUNCATE);
        editor->show_trigger_recorder = true;
        editor->recording_track_index = -1;
        CloseCueSettingsForRecording(editor);
        if (modified) *modified = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Arm Ctrl recording for %s", parameter_label);
    }
}

namespace {

static int PixelPaletteIndex(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (a < 32) return 0;

    static const unsigned char palette[][4] = {
        {0, 0, 0, 0}, {0, 0, 0, 255}, {255, 255, 255, 255},
        {128, 128, 128, 255}, {128, 0, 0, 255}, {255, 64, 0, 255},
        {255, 192, 0, 255}, {255, 255, 0, 255}, {0, 160, 0, 255},
        {0, 192, 192, 255}, {0, 96, 255, 255}, {64, 0, 192, 255},
        {192, 0, 192, 255}, {255, 96, 160, 255}, {96, 64, 32, 255},
        {224, 224, 224, 255}
    };

    int best_index = 1;
    unsigned int best_distance = (std::numeric_limits<unsigned int>::max)();
    for (int i = 1; i < rev::pixel::kMaxPaletteColors; ++i) {
        int dr = (int)r - palette[i][0];
        int dg = (int)g - palette[i][1];
        int db = (int)b - palette[i][2];
        unsigned int distance = (unsigned int)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }
    return best_index;
}

static bool ConvertImageToPixelAnimation(const char* source_path, const char* output_path) {
    if (!source_path || !output_path) return false;

    wchar_t source_wide[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, source_path, -1, source_wide, (int)_countof(source_wide));
    Gdiplus::Bitmap source_image(source_wide);
    if (source_image.GetLastStatus() != Gdiplus::Ok) return false;

    UINT frame_count = 1;
    GUID frame_dimension = GUID{};
    UINT dimension_count = source_image.GetFrameDimensionsCount();
    if (dimension_count > 0) {
        GUID* dimensions = new GUID[dimension_count];
        if (source_image.GetFrameDimensionsList(dimensions, dimension_count) == Gdiplus::Ok) {
            frame_dimension = dimensions[0];
            UINT discovered_count = source_image.GetFrameCount(&frame_dimension);
            if (discovered_count > 0) frame_count = discovered_count;
        }
        delete[] dimensions;
    }

    UINT width = source_image.GetWidth();
    UINT height = source_image.GetHeight();
    if (width == 0 || height == 0 || width > 65535 || height > 65535 || frame_count > 65535) return false;

    rev::pixel::PixelAnimation* animation = rev::pixel::CreateAnimation(
        (uint16_t)width, (uint16_t)height, (uint16_t)frame_count);
    if (!animation) return false;

    static const unsigned char palette[][4] = {
        {0, 0, 0, 0}, {0, 0, 0, 255}, {255, 255, 255, 255},
        {128, 128, 128, 255}, {128, 0, 0, 255}, {255, 64, 0, 255},
        {255, 192, 0, 255}, {255, 255, 0, 255}, {0, 160, 0, 255},
        {0, 192, 192, 255}, {0, 96, 255, 255}, {64, 0, 192, 255},
        {192, 0, 192, 255}, {255, 96, 160, 255}, {96, 64, 32, 255},
        {224, 224, 224, 255}
    };
    for (int i = 0; i < rev::pixel::kMaxPaletteColors; ++i) {
        animation->palette[i] = {palette[i][0], palette[i][1], palette[i][2], palette[i][3]};
    }

    Gdiplus::Rect rect(0, 0, (INT)width, (INT)height);
    for (UINT frame = 0; frame < frame_count; ++frame) {
        if (frame_count > 1 && source_image.SelectActiveFrame(&frame_dimension, frame) != Gdiplus::Ok) {
            rev::pixel::DestroyAnimation(animation);
            return false;
        }

        Gdiplus::BitmapData bitmap_data = {};
        if (source_image.LockBits(&rect, Gdiplus::ImageLockModeRead,
                                  PixelFormat32bppARGB, &bitmap_data) != Gdiplus::Ok) {
            rev::pixel::DestroyAnimation(animation);
            return false;
        }

        const unsigned char* row = static_cast<const unsigned char*>(bitmap_data.Scan0);
        for (UINT y = 0; y < height; ++y) {
            const unsigned char* pixel = row + (size_t)y * bitmap_data.Stride;
            for (UINT x = 0; x < width; ++x) {
                const unsigned char* bgra = pixel + x * 4;
                rev::pixel::SetPixel(animation, (int)frame, (int)x, (int)y,
                                     (uint8_t)PixelPaletteIndex(bgra[2], bgra[1], bgra[0], bgra[3]));
            }
        }
        source_image.UnlockBits(&bitmap_data);
    }

    bool saved = rev::pixel::SaveAnimation(animation, output_path);
    rev::pixel::DestroyAnimation(animation);
    return saved;
}

} // namespace

struct CurveTargetBinding {
    const char* label;
    int* curve_field;
    float base_value;
};

static int BuildCurveTargetsForCurrentCue(EditorContext* editor,
                                          CurveTargetBinding* out_targets,
                                          int max_targets);
static const char* CurveTargetComboGetter(void* data, int index);

static void AddAssetShaderCurveTargets(AssetShader* shaders, int shader_count,
                                       CurveTargetBinding* targets, int max_targets,
                                       int* target_count)
{
    static char labels[rev::runtime::kMaxAssetShaders][17][96] = {};
    const char* names[] = {
        "Opacity", "Speed", "Intensity", "Warp", "Exposure", "Fade",
        "Exposure Ramp", "Fade Ramp", "Palette Low R", "Palette Low G", "Palette Low B",
        "Palette Mid R", "Palette Mid G", "Palette Mid B", "Palette High R",
        "Palette High G", "Palette High B"
    };
    if (!shaders || !targets || !target_count) return;
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
        float values[] = {
            shader->opacity, shader->speed, shader->intensity, shader->warp,
            shader->exposure_base, shader->fade_base, shader->exposure_ramp, shader->fade_ramp,
            shader->palette_low[0], shader->palette_low[1], shader->palette_low[2],
            shader->palette_mid[0], shader->palette_mid[1], shader->palette_mid[2],
            shader->palette_high[0], shader->palette_high[1], shader->palette_high[2]
        };
        for (int parameter = 0; parameter < 17 && *target_count < max_targets; ++parameter) {
            snprintf(labels[shader_index][parameter], sizeof(labels[shader_index][parameter]),
                     "Asset Shader %d %s", shader_index + 1, names[parameter]);
            targets[*target_count] = {labels[shader_index][parameter], fields[parameter], values[parameter]};
            ++*target_count;
        }
    }
}

static const char* GetAssetShaderPresetName(void*, int index) {
    const ShaderPreset* preset = GetPresetById(index);
    return preset ? preset->name : "Unknown Shader";
}

static void AssetShaderToShaderCue(const AssetShader& source, ShaderCue* target) {
    if (!target) return;
    memset(target, 0, sizeof(*target));
    target->shader_scene_id = source.shader_id;
    const ShaderPreset* preset = GetPresetById(source.shader_id);
    if (preset) strncpy_s(target->shader_name, sizeof(target->shader_name), preset->name, _TRUNCATE);
    target->palette_low = {source.palette_low[0], source.palette_low[1], source.palette_low[2]};
    target->palette_mid = {source.palette_mid[0], source.palette_mid[1], source.palette_mid[2]};
    target->palette_high = {source.palette_high[0], source.palette_high[1], source.palette_high[2]};
    target->speed = source.speed;
    target->intensity = source.intensity;
    target->warp = source.warp;
    target->exposure_base = source.exposure_base;
    target->exposure_ramp = source.exposure_ramp;
    target->fade_base = source.fade_base;
    target->fade_ramp = source.fade_ramp;
    target->opacity = source.opacity;
    target->blend_mode = source.blend_mode;
    target->layer_order = source.order;
    target->cue_start = source.start_time;
    target->cue_end = source.end_time;
        target->curve_speed = source.curve_speed;
        target->curve_intensity = source.curve_intensity;
        target->curve_warp = source.curve_warp;
        target->curve_exposure = source.curve_exposure;
        target->curve_fade = source.curve_fade;
        target->curve_opacity = source.curve_opacity;
        target->curve_exposure_ramp = source.curve_exposure_ramp;
        target->curve_fade_ramp = source.curve_fade_ramp;
        target->curve_palette_low_r = source.curve_palette_low_r;
        target->curve_palette_low_g = source.curve_palette_low_g;
        target->curve_palette_low_b = source.curve_palette_low_b;
        target->curve_palette_mid_r = source.curve_palette_mid_r;
        target->curve_palette_mid_g = source.curve_palette_mid_g;
        target->curve_palette_mid_b = source.curve_palette_mid_b;
        target->curve_palette_high_r = source.curve_palette_high_r;
        target->curve_palette_high_g = source.curve_palette_high_g;
        target->curve_palette_high_b = source.curve_palette_high_b;
}

static void ShaderCueToAssetShader(const ShaderCue& source, AssetShader* target) {
    if (!target) return;
    target->shader_id = source.shader_scene_id;
    target->order = source.layer_order;
    target->blend_mode = source.blend_mode;
    target->opacity = source.opacity;
    target->speed = source.speed;
    target->intensity = source.intensity;
    target->warp = source.warp;
    target->exposure_base = source.exposure_base;
    target->exposure_ramp = source.exposure_ramp;
    target->fade_base = source.fade_base;
    target->fade_ramp = source.fade_ramp;
    target->palette_low[0] = source.palette_low.r;
    target->palette_low[1] = source.palette_low.g;
    target->palette_low[2] = source.palette_low.b;
    target->palette_mid[0] = source.palette_mid.r;
    target->palette_mid[1] = source.palette_mid.g;
    target->palette_mid[2] = source.palette_mid.b;
    target->palette_high[0] = source.palette_high.r;
    target->palette_high[1] = source.palette_high.g;
    target->palette_high[2] = source.palette_high.b;
    target->start_time = source.cue_start;
    target->end_time = source.cue_end;
        target->curve_speed = source.curve_speed;
        target->curve_intensity = source.curve_intensity;
        target->curve_warp = source.curve_warp;
        target->curve_exposure = source.curve_exposure;
        target->curve_fade = source.curve_fade;
        target->curve_opacity = source.curve_opacity;
        target->curve_exposure_ramp = source.curve_exposure_ramp;
        target->curve_fade_ramp = source.curve_fade_ramp;
        target->curve_palette_low_r = source.curve_palette_low_r;
        target->curve_palette_low_g = source.curve_palette_low_g;
        target->curve_palette_low_b = source.curve_palette_low_b;
        target->curve_palette_mid_r = source.curve_palette_mid_r;
        target->curve_palette_mid_g = source.curve_palette_mid_g;
        target->curve_palette_mid_b = source.curve_palette_mid_b;
        target->curve_palette_high_r = source.curve_palette_high_r;
        target->curve_palette_high_g = source.curve_palette_high_g;
        target->curve_palette_high_b = source.curve_palette_high_b;
}

void RenderLayerPostEffects(EditorContext* editor, LayerPostEffect* effects, int* effect_count,
                            bool* modified, const char* id_prefix, int cue_type, int cue_index) {
    if (!effects || !effect_count) return;
    ImGui::PushID(id_prefix ? id_prefix : "layer_post_effects");
    static const char* names[] = {
        "HDR Rendering", "ACES Tone Mapping", "Bloom", "Vignette",
        "Color Grading", "Film Grain", "Blue Noise Dithering", "Exponential Fog",
        "FXAA", "Chromatic Aberration", "Camera Shake", "Beat Flash", "Fade In / Fade Out",
        "CRT Warp", "Scanlines", "Lens Distortion", "Palette Cycling", "Heat Distortion",
        "Glitch", "Bloom Pulsing", "Feedback Buffer", "Infinite Zoom", "Recursive Feedback"
    };
    static const char* blend_names[] = { "Alpha", "Additive", "Multiply", "Screen" };
    char label[96] = {};
    snprintf(label, sizeof(label), "Layer Post Effects (%d)##%s", *effect_count,
             id_prefix ? id_prefix : "layer_post_effects");
    if (!ImGui::CollapsingHeader(label)) {
        ImGui::PopID();
        return;
    }

    static int new_type = PostEffectBloom;
    ImGui::SetNextItemWidth(220.0f);
    snprintf(label, sizeof(label), "Effect##%s_add", id_prefix);
    ImGui::Combo(label, &new_type, names, PostEffectCount);
    snprintf(label, sizeof(label), "+ Add Layer Effect##%s_add", id_prefix);
    if (ImGui::Button(label) && *effect_count < rev::runtime::kMaxLayerPostEffects) {
        LayerPostEffect& effect = effects[*effect_count];
        memset(&effect, 0, sizeof(effect));
        effect.type = new_type;
        effect.enabled = true;
        effect.order = *effect_count;
        effect.blend_mode = 0;
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
        ++*effect_count;
        if (modified) *modified = true;
    }
    for (int i = 0; i < *effect_count; ++i) {
        LayerPostEffect& effect = effects[i];
        ImGui::PushID(i);
        bool enabled = effect.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            effect.enabled = enabled;
            if (modified) *modified = true;
        }
        ImGui::SameLine();
        const int type = (effect.type >= 0 && effect.type < PostEffectCount) ? effect.type : 0;
        ImGui::Text("%d. %s", i + 1, names[type]);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            for (int move = i; move + 1 < *effect_count; ++move) effects[move] = effects[move + 1];
            --*effect_count;
            if (modified) *modified = true;
            ImGui::PopID();
            break;
        }
        if (ImGui::SliderFloat("Intensity", &effect.intensity, 0.0f, 2.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &effect.curve_intensity, "Layer Effect Intensity", modified, "layer_effect_intensity");
        if (effect.blend_mode < 0 || effect.blend_mode > 3) effect.blend_mode = 0;
        if (ImGui::Combo("Blend Mode", &effect.blend_mode, blend_names, 4) && modified) *modified = true;
        if (ImGui::SliderFloat("Threshold", &effect.threshold, 0.0f, 4.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &effect.curve_threshold, "Layer Effect Threshold", modified, "layer_effect_threshold");
        if (ImGui::SliderFloat("Radius", &effect.radius, 0.0f, 4.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &effect.curve_radius, "Layer Effect Radius", modified, "layer_effect_radius");
        if (ImGui::ColorEdit4("Color", effect.color) && modified) *modified = true;
        if (ImGui::InputFloat("Start", &effect.start_time, 0.1f, 1.0f) && modified) *modified = true;
        if (ImGui::InputFloat("End (-1 = layer end)", &effect.end_time, 0.1f, 1.0f) && modified) *modified = true;
        if (editor && editor->project) {
            ImGui::SeparatorText("Beat trigger");
            if (ImGui::InputInt("Track index (-1 = off)", &effect.trigger_track) && modified) *modified = true;
            if (effect.trigger_track < -1) effect.trigger_track = -1;
            if (effect.trigger_track >= editor->project->trigger_track_count) {
                effect.trigger_track = editor->project->trigger_track_count - 1;
            }
            if (ImGui::InputFloat("Pulse beats", &effect.trigger_pulse_beats, 0.125f, 0.5f, "%.3f") && modified) *modified = true;
            if (effect.trigger_pulse_beats < 0.0f) effect.trigger_pulse_beats = 0.0f;
        }
        if (ImGui::Button("Edit Layer Effect Curves")) {
            if (editor) {
                editor->selected_cue_index = cue_index;
                editor->editing_curve_cue_type = cue_type;
                editor->editing_curve_field = -1;
                editor->editing_curve_index = effect.curve_intensity >= 0
                    ? effect.curve_intensity : effect.curve_threshold;
                if (editor->editing_curve_index < 0) editor->editing_curve_index = effect.curve_radius;
                if (editor->editing_curve_index < 0) editor->editing_curve_index = effect.curve_color_r;
                if (editor->editing_curve_index < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
                    int curve_index = editor->project->curve_count++;
                    editor->project->curves[curve_index] = rev::curve::CreateCurve(16);
                    rev::curve::AddPoint(editor->project->curves[curve_index], 0.0f, effect.intensity);
                    rev::curve::AddPoint(editor->project->curves[curve_index], 1.0f, effect.intensity);
                    effect.curve_intensity = curve_index;
                    editor->editing_curve_index = curve_index;
                    if (modified) *modified = true;
                }
                if (editor->editing_curve_index >= 0) editor->curve_editor_modal_request_open = true;
            }
        }
        if (editor && editor->project && editor->project->trigger_track_count > 0) {
            static int timing_target = 0;
            static int timing_track = 0;
            const char* timing_names[] = {"Intensity", "Threshold", "Radius", "Color R", "Color G", "Color B", "Color A"};
            int* timing_fields[] = {
                &effect.curve_intensity, &effect.curve_threshold, &effect.curve_radius,
                &effect.curve_color_r, &effect.curve_color_g, &effect.curve_color_b, &effect.curve_color_a
            };
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo("Timing parameter", &timing_target, timing_names, 7);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::InputInt("Track##layer_timing_track", &timing_track);
            if (timing_track < 0) timing_track = 0;
            if (timing_track >= editor->project->trigger_track_count) timing_track = editor->project->trigger_track_count - 1;
            if (ImGui::Button("Load recorded timings##layer_timing")) {
                int curve_index = CreateTriggerTimingCurve(editor, timing_track);
                if (curve_index >= 0) {
                    *timing_fields[timing_target] = curve_index;
                    editor->editing_curve_index = curve_index;
                    editor->editing_curve_cue_type = cue_type;
                    editor->editing_curve_field = -1;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label),
                             "Layer Effect %s timing", timing_names[timing_target]);
                    editor->curve_editor_modal_request_open = true;
                    if (modified) *modified = true;
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Nodes only");
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

static void RenderAssetShaders(EditorContext* editor, AssetShader* shaders, int* shader_count,
                               bool* modified, const char* id_prefix, int cue_type, int cue_index) {
    if (!shaders || !shader_count) return;
    char label[96] = {};
    snprintf(label, sizeof(label), "Asset Shaders (%d)##%s", *shader_count, id_prefix);
    if (!ImGui::CollapsingHeader(label)) return;

    static int new_shader_id = 0;
    ImGui::SetNextItemWidth(220.0f);
    snprintf(label, sizeof(label), "Shader##%s_add", id_prefix);
    if (ImGui::Combo(label, &new_shader_id, GetAssetShaderPresetName,
                     nullptr, g_shader_preset_count, -1) && modified) {
        *modified = true;
    }
    snprintf(label, sizeof(label), "+ Add Asset Shader##%s_add", id_prefix);
    if (ImGui::Button(label) && *shader_count < rev::runtime::kMaxAssetShaders) {
        AssetShader& shader = shaders[*shader_count];
        memset(&shader, 0, sizeof(shader));
        shader.shader_id = new_shader_id;
        shader.enabled = true;
        shader.order = *shader_count;
        shader.opacity = 1.0f;
        shader.speed = 1.0f;
        shader.intensity = 1.0f;
        shader.warp = 0.5f;
        shader.exposure_base = 1.0f;
        shader.fade_base = 1.0f;
        shader.palette_mid[0] = shader.palette_mid[1] = shader.palette_mid[2] = 0.5f;
        shader.palette_high[0] = shader.palette_high[1] = shader.palette_high[2] = 1.0f;
        shader.end_time = -1.0f;
        shader.curve_speed = shader.curve_intensity = shader.curve_warp = -1;
        shader.curve_exposure = shader.curve_fade = shader.curve_opacity = -1;
        shader.curve_exposure_ramp = shader.curve_fade_ramp = -1;
        shader.curve_palette_low_r = shader.curve_palette_low_g = shader.curve_palette_low_b = -1;
        shader.curve_palette_mid_r = shader.curve_palette_mid_g = shader.curve_palette_mid_b = -1;
        shader.curve_palette_high_r = shader.curve_palette_high_g = shader.curve_palette_high_b = -1;
        ++*shader_count;
        if (modified) *modified = true;
    }
    for (int i = 0; i < *shader_count; ++i) {
        AssetShader& shader = shaders[i];
        ImGui::PushID(i);
        bool enabled = shader.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            shader.enabled = enabled;
            if (modified) *modified = true;
        }
        ImGui::SameLine();
        const ShaderPreset* preset = GetPresetById(shader.shader_id);
        char shader_label[128] = {};
        snprintf(shader_label, sizeof(shader_label), "%d. %s", i + 1, preset ? preset->name : "Unknown Shader");
        if (ImGui::Button(shader_label)) {
            editor->selected_cue_type = cue_type;
            editor->selected_cue_index = cue_index;
            editor->shader_modal_asset_mode = true;
            editor->shader_modal_asset_cue_type = cue_type;
            editor->shader_modal_asset_index = i;
            editor->editing_asset_shader = shader;
            AssetShaderToShaderCue(shader, &editor->editing_shader);
            editor->shader_modal_request_open = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            for (int move = i; move + 1 < *shader_count; ++move) shaders[move] = shaders[move + 1];
            --*shader_count;
            if (modified) *modified = true;
            ImGui::PopID();
            break;
        }
        const char* blend_modes[] = {"Alpha", "Additive", "Multiply", "Screen"};
        if (ImGui::Combo("Blend Mode", &shader.blend_mode, blend_modes, 4) && modified) *modified = true;
        if (ImGui::SliderFloat("Opacity", &shader.opacity, 0.0f, 1.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &shader.curve_opacity, "Shader Opacity", modified, "asset_shader_opacity");
        if (ImGui::SliderFloat("Speed", &shader.speed, 0.0f, 4.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &shader.curve_speed, "Shader Speed", modified, "asset_shader_speed");
        if (ImGui::SliderFloat("Intensity", &shader.intensity, 0.0f, 4.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &shader.curve_intensity, "Shader Intensity", modified, "asset_shader_intensity");
        if (ImGui::SliderFloat("Warp", &shader.warp, 0.0f, 1.0f) && modified) *modified = true;
        DrawCurveRecordButton(editor, &shader.curve_warp, "Shader Warp", modified, "asset_shader_warp");
        if (ImGui::InputFloat("Start", &shader.start_time, 0.1f, 1.0f) && modified) *modified = true;
        if (ImGui::InputFloat("End (-1 = asset end)", &shader.end_time, 0.1f, 1.0f) && modified) *modified = true;
        if (ImGui::InputInt("Order", &shader.order) && modified) *modified = true;
        ImGui::PopID();
    }
}

static void RenderCurveReuseSection(EditorContext* editor, const char* id,
                                    int cue_type, int* target_index, int* curve_index)
{
    if (!editor || !editor->project || editor->project->curve_count <= 0) return;
    CurveTargetBinding targets[128] = {};
    int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 128);
    if (target_count <= 0) return;

    ImGui::Separator();
    ImGui::Text("Curve Reuse:");
    if (*target_index < 0 || *target_index >= target_count) *target_index = 0;
    if (*curve_index < 0) *curve_index = 0;
    if (*curve_index >= editor->project->curve_count) *curve_index = editor->project->curve_count - 1;

    ImGui::SetNextItemWidth(240.0f);
    ImGui::PushID(id ? id : "curve_reuse");
    ImGui::Combo("Target Parameter", target_index, CurveTargetComboGetter, targets, target_count);
    ImGui::SetNextItemWidth(240.0f);
    char curve_preview[64] = {};
    BuildCurveDisplayLabel(editor, *curve_index, curve_preview, sizeof(curve_preview));
    if (ImGui::BeginCombo("Curve", curve_preview)) {
        for (int i = 0; i < editor->project->curve_count; ++i) {
            char item[64] = {};
            BuildCurveDisplayLabel(editor, i, item, sizeof(item));
            bool selected = i == *curve_index;
            if (ImGui::Selectable(item, selected)) *curve_index = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Assign Existing Curve")) {
        *targets[*target_index].curve_field = *curve_index;
        editor->project->modified = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit Assigned")) {
        int assigned = *targets[*target_index].curve_field;
        if (assigned >= 0 && assigned < editor->project->curve_count) {
            editor->editing_curve_index = assigned;
            editor->editing_curve_cue_type = cue_type;
            snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[*target_index].label);
            editor->curve_editor_modal_request_open = true;
        }
    }
    ImGui::PopID();
}

static void MarkCurveUsed(bool* used, int curve_index) {
    if (!used) return;
    if (curve_index >= 0 && curve_index < rev::runtime::kMaxCurves) {
        used[curve_index] = true;
    }
}

void BuildCurveUsageMap(ProjectData* project, bool* used) {
    if (!project || !used) return;

    for (int i = 0; i < rev::runtime::kMaxCurves; ++i) used[i] = false;

    for (int s = 0; s < project->scene_count; ++s) {
        SceneBlock* scene = &project->scenes[s];

        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            MarkCurveUsed(used, cue->curve_speed);
            MarkCurveUsed(used, cue->curve_intensity);
            MarkCurveUsed(used, cue->curve_warp);
            MarkCurveUsed(used, cue->curve_exposure);
            MarkCurveUsed(used, cue->curve_fade);
            MarkCurveUsed(used, cue->curve_palette_low_r);
            MarkCurveUsed(used, cue->curve_palette_low_g);
            MarkCurveUsed(used, cue->curve_palette_low_b);
            MarkCurveUsed(used, cue->curve_palette_mid_r);
            MarkCurveUsed(used, cue->curve_palette_mid_g);
            MarkCurveUsed(used, cue->curve_palette_mid_b);
            MarkCurveUsed(used, cue->curve_palette_high_r);
            MarkCurveUsed(used, cue->curve_palette_high_g);
            MarkCurveUsed(used, cue->curve_palette_high_b);
            MarkCurveUsed(used, cue->curve_opacity);
            MarkCurveUsed(used, cue->curve_exposure_ramp);
            MarkCurveUsed(used, cue->curve_fade_ramp);
        }

        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            MarkCurveUsed(used, cue->curve_x);
            MarkCurveUsed(used, cue->curve_y);
            MarkCurveUsed(used, cue->curve_scale);
            MarkCurveUsed(used, cue->curve_opacity);
        }

        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            MarkCurveUsed(used, cue->curve_x);
            MarkCurveUsed(used, cue->curve_y);
            MarkCurveUsed(used, cue->curve_scale);
            MarkCurveUsed(used, cue->curve_opacity);
            MarkCurveUsed(used, cue->curve_frame);
        }

        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            MarkCurveUsed(used, cue->curve_x);
            MarkCurveUsed(used, cue->curve_y);
            MarkCurveUsed(used, cue->curve_size);
            MarkCurveUsed(used, cue->curve_rotation);
            MarkCurveUsed(used, cue->curve_color_r);
            MarkCurveUsed(used, cue->curve_color_g);
            MarkCurveUsed(used, cue->curve_color_b);
        }

        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            MarkCurveUsed(used, cue->curve_x);
            MarkCurveUsed(used, cue->curve_y);
            MarkCurveUsed(used, cue->curve_speed);
            MarkCurveUsed(used, cue->curve_size);
            MarkCurveUsed(used, cue->curve_rotation);
            MarkCurveUsed(used, cue->curve_opacity);
            MarkCurveUsed(used, cue->curve_color_r);
            MarkCurveUsed(used, cue->curve_color_g);
            MarkCurveUsed(used, cue->curve_color_b);
            MarkCurveUsed(used, cue->curve_wave_amp);
            MarkCurveUsed(used, cue->curve_wave_freq);
            MarkCurveUsed(used, cue->curve_wave_length);
            MarkCurveUsed(used, cue->curve_jitter_amp);
            MarkCurveUsed(used, cue->curve_jitter_freq);
        }

        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            MarkCurveUsed(used, cue->curve_mesh_size);
            MarkCurveUsed(used, cue->curve_pos_x);
            MarkCurveUsed(used, cue->curve_pos_y);
            MarkCurveUsed(used, cue->curve_pos_z);
            MarkCurveUsed(used, cue->curve_rot_x);
            MarkCurveUsed(used, cue->curve_rot_y);
            MarkCurveUsed(used, cue->curve_rot_z);
            MarkCurveUsed(used, cue->curve_scale_x);
            MarkCurveUsed(used, cue->curve_scale_y);
            MarkCurveUsed(used, cue->curve_scale_z);
            MarkCurveUsed(used, cue->curve_color_r);
            MarkCurveUsed(used, cue->curve_color_g);
            MarkCurveUsed(used, cue->curve_color_b);
            MarkCurveUsed(used, cue->curve_color_a);
            MarkCurveUsed(used, cue->curve_metallic);
            MarkCurveUsed(used, cue->curve_roughness);
            MarkCurveUsed(used, cue->curve_fov);
        }

        for (int i = 0; i < scene->post_effect_count; ++i) {
            PostEffect* effect = &scene->post_effects[i];
            MarkCurveUsed(used, effect->curve_intensity);
            MarkCurveUsed(used, effect->curve_threshold);
            MarkCurveUsed(used, effect->curve_radius);
            MarkCurveUsed(used, effect->curve_color_r);
            MarkCurveUsed(used, effect->curve_color_g);
            MarkCurveUsed(used, effect->curve_color_b);
            MarkCurveUsed(used, effect->curve_color_a);
            MarkCurveUsed(used, effect->curve_amount);
        }
    }
}

static int AcquireCurveSlot(EditorContext* editor, float base_value, bool* reused_existing) {
    if (reused_existing) *reused_existing = false;
    if (!editor || !editor->project) return -1;

    if (editor->project->curve_count < rev::runtime::kMaxCurves) {
        int idx = editor->project->curve_count;
        rev::curve::Curve& curve = editor->project->curves[idx];
        curve = rev::curve::CreateCurve(16);
        rev::curve::AddPoint(curve, 0.0f, base_value);
        rev::curve::AddPoint(curve, 1.0f, base_value);
        editor->project->curve_count++;
        editor->project->modified = true;
        return idx;
    }

    bool used[rev::runtime::kMaxCurves] = {};
    BuildCurveUsageMap(editor->project, used);
    for (int i = 0; i < editor->project->curve_count; ++i) {
        if (!used[i]) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
            editor->project->curves[i] = rev::curve::CreateCurve(16);
            rev::curve::AddPoint(editor->project->curves[i], 0.0f, base_value);
            rev::curve::AddPoint(editor->project->curves[i], 1.0f, base_value);
            editor->project->modified = true;
            if (reused_existing) *reused_existing = true;
            return i;
        }
    }

    return -1;
}

static float ClampCurveDuration(float duration) {
    if (duration < 0.01f) return 0.01f;
    if (duration > 3600.0f) return 3600.0f;
    return duration;
}

static int BuildCurveTargetsForCurrentCue(EditorContext* editor,
                                          CurveTargetBinding* out_targets,
                                          int max_targets)
{
    if (!editor || !editor->project || !out_targets || max_targets <= 0) return 0;
    if (editor->selected_scene_index < 0 || editor->selected_cue_index < 0) return 0;
    if (!editor->project->scenes || editor->selected_scene_index >= editor->project->scene_count) return 0;

    SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
    if (!scene) return 0;

    int count = 0;
    auto add_target = [&](const char* label, int* field, float base_value) {
        if (!label || !field || count >= max_targets) return;
        out_targets[count].label = label;
        out_targets[count].curve_field = field;
        out_targets[count].base_value = base_value;
        ++count;
    };

    const int cue_index = editor->selected_cue_index;
    switch (editor->editing_curve_cue_type) {
        case CueTypeShader:
            if (cue_index >= scene->shader_cue_count) break;
            {
                ShaderCue* cue = &scene->shader_cues[cue_index];
                add_target("Shader Speed", &cue->curve_speed, cue->speed);
                add_target("Shader Intensity", &cue->curve_intensity, cue->intensity);
                add_target("Shader Warp", &cue->curve_warp, cue->warp);
                add_target("Shader Exposure", &cue->curve_exposure, cue->exposure_base);
                add_target("Shader Fade", &cue->curve_fade, cue->fade_base);
                add_target("Shader Palette Low R", &cue->curve_palette_low_r, cue->palette_low.r);
                add_target("Shader Palette Low G", &cue->curve_palette_low_g, cue->palette_low.g);
                add_target("Shader Palette Low B", &cue->curve_palette_low_b, cue->palette_low.b);
                add_target("Shader Palette Mid R", &cue->curve_palette_mid_r, cue->palette_mid.r);
                add_target("Shader Palette Mid G", &cue->curve_palette_mid_g, cue->palette_mid.g);
                add_target("Shader Palette Mid B", &cue->curve_palette_mid_b, cue->palette_mid.b);
                add_target("Shader Palette High R", &cue->curve_palette_high_r, cue->palette_high.r);
                add_target("Shader Palette High G", &cue->curve_palette_high_g, cue->palette_high.g);
                add_target("Shader Palette High B", &cue->curve_palette_high_b, cue->palette_high.b);
                add_target("Shader Opacity", &cue->curve_opacity, cue->opacity);
                add_target("Shader Exposure Ramp", &cue->curve_exposure_ramp, cue->exposure_ramp);
                add_target("Shader Fade Ramp", &cue->curve_fade_ramp, cue->fade_ramp);
            }
            break;
        case CueTypeImage:
            if (cue_index >= scene->image_cue_count) break;
            {
                ImageCue* cue = &scene->image_cues[cue_index];
                add_target("Image X Position", &cue->curve_x, cue->x);
                add_target("Image Y Position", &cue->curve_y, cue->y);
                add_target("Image Scale", &cue->curve_scale, cue->scale);
                add_target("Image Rotation", &cue->curve_rotation, cue->rotation);
                add_target("Image Opacity", &cue->curve_opacity, cue->opacity);
                for (int i = 0; i < cue->post_effect_count; ++i) {
                    LayerPostEffect* effect = &cue->post_effects[i];
                    add_target("Layer Effect Intensity", &effect->curve_intensity, effect->intensity);
                    add_target("Layer Effect Threshold", &effect->curve_threshold, effect->threshold);
                    add_target("Layer Effect Radius", &effect->curve_radius, effect->radius);
                    add_target("Layer Effect Color R", &effect->curve_color_r, effect->color[0]);
                    add_target("Layer Effect Color G", &effect->curve_color_g, effect->color[1]);
                    add_target("Layer Effect Color B", &effect->curve_color_b, effect->color[2]);
                    add_target("Layer Effect Color A", &effect->curve_color_a, effect->color[3]);
                }
                static char shader_target_labels[rev::runtime::kMaxAssetShaders][17][96] = {};
                for (int shader_index = 0; shader_index < cue->shader_count; ++shader_index) {
                    AssetShader* shader = &cue->shaders[shader_index];
                    const char* names[] = {
                        "Opacity", "Speed", "Intensity", "Warp", "Exposure", "Fade",
                        "Exposure Ramp", "Fade Ramp", "Palette Low R", "Palette Low G", "Palette Low B",
                        "Palette Mid R", "Palette Mid G", "Palette Mid B", "Palette High R",
                        "Palette High G", "Palette High B"
                    };
                    int* fields[] = {
                        &shader->curve_opacity, &shader->curve_speed, &shader->curve_intensity, &shader->curve_warp,
                        &shader->curve_exposure, &shader->curve_fade, &shader->curve_exposure_ramp, &shader->curve_fade_ramp,
                        &shader->curve_palette_low_r, &shader->curve_palette_low_g, &shader->curve_palette_low_b,
                        &shader->curve_palette_mid_r, &shader->curve_palette_mid_g, &shader->curve_palette_mid_b,
                        &shader->curve_palette_high_r, &shader->curve_palette_high_g, &shader->curve_palette_high_b
                    };
                    float values[] = {
                        shader->opacity, shader->speed, shader->intensity, shader->warp,
                        shader->exposure_base, shader->fade_base, shader->exposure_ramp, shader->fade_ramp,
                        shader->palette_low[0], shader->palette_low[1], shader->palette_low[2],
                        shader->palette_mid[0], shader->palette_mid[1], shader->palette_mid[2],
                        shader->palette_high[0], shader->palette_high[1], shader->palette_high[2]
                    };
                    for (int parameter = 0; parameter < 17; ++parameter) {
                        snprintf(shader_target_labels[shader_index][parameter],
                                 sizeof(shader_target_labels[shader_index][parameter]),
                                 "Asset Shader %d %s", shader_index + 1, names[parameter]);
                        add_target(shader_target_labels[shader_index][parameter], fields[parameter], values[parameter]);
                    }
                }
            }
            break;
        case CueTypeAnimatedSprite:
            if (cue_index >= scene->animated_sprite_cue_count) break;
            {
                AnimatedSpriteCue* cue = &scene->animated_sprite_cues[cue_index];
                add_target("AnimSprite X Position", &cue->curve_x, cue->x);
                add_target("AnimSprite Y Position", &cue->curve_y, cue->y);
                add_target("AnimSprite Scale", &cue->curve_scale, cue->scale);
                add_target("AnimSprite Rotation", &cue->curve_rotation, cue->rotation);
                add_target("AnimSprite Opacity", &cue->curve_opacity, cue->opacity);
                add_target("AnimSprite Frame", &cue->curve_frame, (float)cue->start_frame);
                for (int i = 0; i < cue->post_effect_count; ++i) {
                    LayerPostEffect* effect = &cue->post_effects[i];
                    add_target("Layer Effect Intensity", &effect->curve_intensity, effect->intensity);
                    add_target("Layer Effect Threshold", &effect->curve_threshold, effect->threshold);
                    add_target("Layer Effect Radius", &effect->curve_radius, effect->radius);
                    add_target("Layer Effect Color R", &effect->curve_color_r, effect->color[0]);
                    add_target("Layer Effect Color G", &effect->curve_color_g, effect->color[1]);
                    add_target("Layer Effect Color B", &effect->curve_color_b, effect->color[2]);
                    add_target("Layer Effect Color A", &effect->curve_color_a, effect->color[3]);
                }
                AddAssetShaderCurveTargets(cue->shaders, cue->shader_count, out_targets, max_targets, &count);
            }
            break;
        case CueTypePixel:
            if (cue_index >= scene->pixel_cue_count) break;
            {
                PixelCue* cue = &scene->pixel_cues[cue_index];
                add_target("Pixel X Position", &cue->curve_x, cue->x);
                add_target("Pixel Y Position", &cue->curve_y, cue->y);
                add_target("Pixel Scale", &cue->curve_scale, cue->scale);
                add_target("Pixel Rotation", &cue->curve_rotation, cue->rotation);
                add_target("Pixel Opacity", &cue->curve_opacity, cue->opacity);
                add_target("Pixel Frame", &cue->curve_frame, (float)cue->start_frame);
                add_target("Pixel Palette Offset", &cue->curve_palette_offset, (float)cue->palette_offset);
                for (int i = 0; i < cue->post_effect_count; ++i) {
                    LayerPostEffect* effect = &cue->post_effects[i];
                    add_target("Layer Effect Intensity", &effect->curve_intensity, effect->intensity);
                    add_target("Layer Effect Threshold", &effect->curve_threshold, effect->threshold);
                    add_target("Layer Effect Radius", &effect->curve_radius, effect->radius);
                    add_target("Layer Effect Color R", &effect->curve_color_r, effect->color[0]);
                    add_target("Layer Effect Color G", &effect->curve_color_g, effect->color[1]);
                    add_target("Layer Effect Color B", &effect->curve_color_b, effect->color[2]);
                    add_target("Layer Effect Color A", &effect->curve_color_a, effect->color[3]);
                }
                AddAssetShaderCurveTargets(cue->shaders, cue->shader_count, out_targets, max_targets, &count);
            }
            break;
        case CueTypePixelEmitter:
            if (cue_index >= scene->pixel_emitter_cue_count) break;
            {
                PixelEmitterCue* cue = &scene->pixel_emitter_cues[cue_index];
                add_target("Emitter X Position", &cue->curve_x, cue->x);
                add_target("Emitter Y Position", &cue->curve_y, cue->y);
                add_target("Emitter Scale", &cue->curve_scale, cue->scale);
                add_target("Emitter Rotation", &cue->curve_rotation, cue->rotation);
                add_target("Emitter Opacity", &cue->curve_opacity, cue->opacity);
                add_target("Emission Rate", &cue->curve_emission_rate, cue->emission_rate);
                add_target("Speed Min", &cue->curve_speed_min, cue->speed_min);
                add_target("Speed Max", &cue->curve_speed_max, cue->speed_max);
                add_target("Lifetime Min", &cue->curve_lifetime_min, cue->lifetime_min);
                add_target("Lifetime Max", &cue->curve_lifetime_max, cue->lifetime_max);
                add_target("Particle Scale Min", &cue->curve_scale_min, cue->scale_min);
                add_target("Particle Scale Max", &cue->curve_scale_max, cue->scale_max);
            }
            break;
        case CueTypeText:
            if (cue_index >= scene->text_cue_count) break;
            {
                TextCue* cue = &scene->text_cues[cue_index];
                add_target("Text Size", &cue->curve_size, cue->size);
                add_target("Text Rotation", &cue->curve_rotation, cue->rotation);
                add_target("Text Color R", &cue->curve_color_r, cue->color.r);
                add_target("Text Color G", &cue->curve_color_g, cue->color.g);
                add_target("Text Color B", &cue->curve_color_b, cue->color.b);
                add_target("Text X Position", &cue->curve_x, cue->x);
                add_target("Text Y Position", &cue->curve_y, cue->y);
            }
            break;
        case CueTypeScrollText:
            if (cue_index >= scene->scroll_text_cue_count) break;
            {
                ScrollTextCue* cue = &scene->scroll_text_cues[cue_index];
                add_target("Scroll X", &cue->curve_x, cue->x);
                add_target("Scroll Y", &cue->curve_y, cue->y);
                add_target("Scroll Speed", &cue->curve_speed, cue->speed);
                add_target("Scroll Size", &cue->curve_size, cue->size);
                add_target("Scroll Rotation", &cue->curve_rotation, cue->rotation);
                add_target("Scroll Opacity", &cue->curve_opacity, cue->opacity);
                add_target("Scroll Color R", &cue->curve_color_r, cue->color.r);
                add_target("Scroll Color G", &cue->curve_color_g, cue->color.g);
                add_target("Scroll Color B", &cue->curve_color_b, cue->color.b);
                add_target("Scroll Wave Amp", &cue->curve_wave_amp, cue->wave_amp);
                add_target("Scroll Wave Freq", &cue->curve_wave_freq, cue->wave_freq);
                add_target("Scroll Wave Length", &cue->curve_wave_length, cue->wave_length);
                add_target("Scroll Jitter Amp", &cue->curve_jitter_amp, cue->jitter_amp);
                add_target("Scroll Jitter Freq", &cue->curve_jitter_freq, cue->jitter_freq);
            }
            break;
        case CueTypeMesh:
            if (!scene->mesh_cues || cue_index >= scene->mesh_cue_count) break;
            {
                MeshCue* cue = &scene->mesh_cues[cue_index];
                add_target("Mesh Size", &cue->curve_mesh_size, cue->mesh_size);
                add_target("Mesh Pos X", &cue->curve_pos_x, cue->pos[0]);
                add_target("Mesh Pos Y", &cue->curve_pos_y, cue->pos[1]);
                add_target("Mesh Pos Z", &cue->curve_pos_z, cue->pos[2]);
                add_target("Mesh Rot X", &cue->curve_rot_x, cue->rot[0]);
                add_target("Mesh Rot Y", &cue->curve_rot_y, cue->rot[1]);
                add_target("Mesh Rot Z", &cue->curve_rot_z, cue->rot[2]);
                add_target("Mesh Scale X", &cue->curve_scale_x, cue->scale[0]);
                add_target("Mesh Scale Y", &cue->curve_scale_y, cue->scale[1]);
                add_target("Mesh Scale Z", &cue->curve_scale_z, cue->scale[2]);
                add_target("Mesh Color R", &cue->curve_color_r, cue->color[0]);
                add_target("Mesh Color G", &cue->curve_color_g, cue->color[1]);
                add_target("Mesh Color B", &cue->curve_color_b, cue->color[2]);
                add_target("Mesh Color A", &cue->curve_color_a, cue->color[3]);
                add_target("Mesh Metallic", &cue->curve_metallic, cue->metallic);
                add_target("Mesh Roughness", &cue->curve_roughness, cue->roughness);
                add_target("Mesh Camera FOV", &cue->curve_fov, cue->fov_deg);
            }
            break;
        case CueTypePostEffect:
            if (cue_index >= scene->post_effect_count) break;
            {
                PostEffect* effect = &scene->post_effects[cue_index];
                add_target("Post Effect Intensity", &effect->curve_intensity, effect->intensity);
                add_target("Post Effect Threshold", &effect->curve_threshold, effect->threshold);
                add_target("Post Effect Radius", &effect->curve_radius, effect->radius);
                add_target("Post Effect Color R", &effect->curve_color_r, effect->color[0]);
                add_target("Post Effect Color G", &effect->curve_color_g, effect->color[1]);
                add_target("Post Effect Color B", &effect->curve_color_b, effect->color[2]);
                add_target("Post Effect Color A", &effect->curve_color_a, effect->color[3]);
            }
            break;
        default:
            break;
    }

    return count;
}

static int BuildCurveTargetsForAssetShader(EditorContext* editor,
                                           CurveTargetBinding* out_targets,
                                           int max_targets,
                                           AssetShader** out_shader)
{
    if (out_shader) *out_shader = nullptr;
    if (!editor || !editor->project || !out_targets || max_targets <= 0) return 0;
    SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
    if (!scene || editor->shader_modal_asset_index < 0) return 0;

    AssetShader* shader = nullptr;
    if (editor->shader_modal_asset_cue_type == CueTypeImage &&
        editor->selected_cue_index < scene->image_cue_count) {
        ImageCue* cue = &scene->image_cues[editor->selected_cue_index];
        if (editor->shader_modal_asset_index < cue->shader_count) shader = &cue->shaders[editor->shader_modal_asset_index];
    } else if (editor->shader_modal_asset_cue_type == CueTypeAnimatedSprite &&
               editor->selected_cue_index < scene->animated_sprite_cue_count) {
        AnimatedSpriteCue* cue = &scene->animated_sprite_cues[editor->selected_cue_index];
        if (editor->shader_modal_asset_index < cue->shader_count) shader = &cue->shaders[editor->shader_modal_asset_index];
    } else if (editor->shader_modal_asset_cue_type == CueTypePixel &&
               editor->selected_cue_index < scene->pixel_cue_count) {
        PixelCue* cue = &scene->pixel_cues[editor->selected_cue_index];
        if (editor->shader_modal_asset_index < cue->shader_count) shader = &cue->shaders[editor->shader_modal_asset_index];
    }
    if (!shader) return 0;
    if (out_shader) *out_shader = shader;

    int count = 0;
    auto add_target = [&](const char* label, int* field, float base_value) {
        if (label && field && count < max_targets) {
            out_targets[count] = {label, field, base_value};
            ++count;
        }
    };
    add_target("Asset Shader Speed", &shader->curve_speed, shader->speed);
    add_target("Asset Shader Intensity", &shader->curve_intensity, shader->intensity);
    add_target("Asset Shader Warp", &shader->curve_warp, shader->warp);
    add_target("Asset Shader Exposure", &shader->curve_exposure, shader->exposure_base);
    add_target("Asset Shader Fade", &shader->curve_fade, shader->fade_base);
    add_target("Asset Shader Opacity", &shader->curve_opacity, shader->opacity);
    add_target("Asset Shader Exposure Ramp", &shader->curve_exposure_ramp, shader->exposure_ramp);
    add_target("Asset Shader Fade Ramp", &shader->curve_fade_ramp, shader->fade_ramp);
    add_target("Asset Shader Palette Low R", &shader->curve_palette_low_r, shader->palette_low[0]);
    add_target("Asset Shader Palette Low G", &shader->curve_palette_low_g, shader->palette_low[1]);
    add_target("Asset Shader Palette Low B", &shader->curve_palette_low_b, shader->palette_low[2]);
    add_target("Asset Shader Palette Mid R", &shader->curve_palette_mid_r, shader->palette_mid[0]);
    add_target("Asset Shader Palette Mid G", &shader->curve_palette_mid_g, shader->palette_mid[1]);
    add_target("Asset Shader Palette Mid B", &shader->curve_palette_mid_b, shader->palette_mid[2]);
    add_target("Asset Shader Palette High R", &shader->curve_palette_high_r, shader->palette_high[0]);
    add_target("Asset Shader Palette High G", &shader->curve_palette_high_g, shader->palette_high[1]);
    add_target("Asset Shader Palette High B", &shader->curve_palette_high_b, shader->palette_high[2]);
    return count;
}

static const char* CurveTargetComboGetter(void* data, int index)
{
    CurveTargetBinding* items = static_cast<CurveTargetBinding*>(data);
    return items[index].label;
}

static const char* TriggerTrackComboGetter(void* data, int index)
{
    TriggerTrack* tracks = static_cast<TriggerTrack*>(data);
    return tracks[index].name;
}

static void RenderTriggerTimingAssignment(EditorContext* editor,
                                          const char* id,
                                          bool* modified)
{
    if (!editor || !editor->project || editor->project->trigger_track_count <= 0) return;
    CurveTargetBinding targets[128] = {};
    int target_count = editor->shader_modal_asset_mode
        ? BuildCurveTargetsForAssetShader(editor, targets, 128, nullptr)
        : BuildCurveTargetsForCurrentCue(editor, targets, 128);
    if (target_count <= 0) return;

    ImGui::PushID(id ? id : "recorded_timing");
    ImGui::Separator();
    ImGui::Text("Recorded timing");
    static int selected_target = 0;
    static int selected_track = 0;
    if (selected_target >= target_count) selected_target = 0;
    if (selected_track >= editor->project->trigger_track_count) selected_track = 0;

    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("Parameter", &selected_target, CurveTargetComboGetter, targets, target_count);
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("Track", &selected_track, TriggerTrackComboGetter,
                 editor->project->trigger_tracks, editor->project->trigger_track_count);

    if (ImGui::Button("Load recorded timings")) {
        int curve_index = CreateTriggerTimingCurve(editor, selected_track);
        if (curve_index >= 0) {
            *targets[selected_target].curve_field = curve_index;
            editor->editing_curve_index = curve_index;
            editor->editing_curve_cue_type = editor->selected_cue_type;
            snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label),
                     "%s timing", targets[selected_target].label);
            editor->curve_editor_modal_request_open = true;
            editor->project->modified = true;
            if (modified) *modified = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Nodes only; edit values in Curve Editor");
    ImGui::PopID();
}

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
        if (selection_curve_index != editor->editing_curve_index) {
            memset(selected_points, 0, sizeof(selected_points));
            selected_point_count = 0;
            selection_curve_index = editor->editing_curve_index;
        }

        CurveTargetBinding targets[24] = {};
        const int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 24);

        int current_target = editor->editing_curve_field;
        if (current_target < 0 || current_target >= target_count) {
            current_target = -1;
            for (int i = 0; i < target_count; ++i) {
                if (targets[i].curve_field && *targets[i].curve_field == editor->editing_curve_index) {
                    current_target = i;
                    break;
                }
            }
            if (current_target < 0 && target_count > 0) current_target = 0;
        }

        bool curve_slots_exhausted = false;
        auto ActivateTarget = [&](int target_index) {
            if (target_index < 0 || target_index >= target_count) return;
            CurveTargetBinding& target = targets[target_index];
            if (!target.curve_field) return;

            if (*target.curve_field < 0) {
                int new_curve_index = AcquireCurveSlot(editor, target.base_value, nullptr);
                if (new_curve_index < 0) {
                    curve_slots_exhausted = true;
                    return;
                }
                *target.curve_field = new_curve_index;
            }

            editor->editing_curve_index = *target.curve_field;
            editor->editing_curve_field = target_index;
            snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", target.label);
            editor->dragging_point_index = -1;
            editor->selected_point_index = -1;
        };

        if (target_count > 0 && current_target >= 0) {
            editor->editing_curve_field = current_target;
            if (targets[current_target].label) {
                snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[current_target].label);
            }

            if (ImGui::ArrowButton("##curve_prev_param", ImGuiDir_Left)) {
                int prev = (current_target + target_count - 1) % target_count;
                ActivateTarget(prev);
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##curve_next_param", ImGuiDir_Right)) {
                int next = (current_target + 1) % target_count;
                ActivateTarget(next);
            }
            ImGui::SameLine();
            ImGui::Text("Parameter %d/%d", current_target + 1, target_count);
            if (curve_slots_exhausted) {
                ImGui::SameLine();
                ImGui::TextDisabled("(No free curve slots)");
            }
        }

        // Refresh in case parameter navigation switched to a different curve.
        curve = &editor->project->curves[editor->editing_curve_index];

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
            curve->duration = ClampCurveDuration(duration);
            editor->project->modified = true;
        }

        int duration_centis = (int)floorf(curve->duration * 100.0f + 0.5f);
        int duration_seconds = duration_centis / 100;
        int duration_hundredths = duration_centis % 100;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        int duration_seconds_edit = duration_seconds;
        if (ImGui::InputInt("Sec", &duration_seconds_edit, 1, 10)) {
            if (duration_seconds_edit < 0) duration_seconds_edit = 0;
            duration_centis = duration_seconds_edit * 100 + duration_hundredths;
            curve->duration = ClampCurveDuration((float)duration_centis / 100.0f);
            editor->project->modified = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        int duration_hundredths_edit = duration_hundredths;
        if (ImGui::InputInt("1/100", &duration_hundredths_edit, 1, 10)) {
            if (duration_hundredths_edit < 0) duration_hundredths_edit = 0;
            if (duration_hundredths_edit > 99) duration_hundredths_edit = 99;
            duration_centis = duration_seconds * 100 + duration_hundredths_edit;
            curve->duration = ClampCurveDuration((float)duration_centis / 100.0f);
            editor->project->modified = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How long the curve takes to complete (in seconds).\n"
                            "With wrap modes, the curve will loop/pingpong over this duration.");
        }

        static float curve_view_zoom = 1.0f;
        static float curve_view_center_t = 0.5f;
        static float curve_view_center_v = 0.5f;
        static int curve_view_index = -1;
        if (curve_view_index != editor->editing_curve_index) {
            curve_view_zoom = 1.0f;
            curve_view_center_t = 0.5f;
            curve_view_center_v = 0.5f;
            curve_view_index = editor->editing_curve_index;
        }
        ImGui::Text("View");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##modal_curve_zoom_out")) curve_view_zoom = fmaxf(1.0f, curve_view_zoom / 1.5f);
        ImGui::SameLine();
        ImGui::Text("%.1fx", curve_view_zoom);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##modal_curve_zoom_in")) curve_view_zoom = fminf(32.0f, curve_view_zoom * 1.5f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##modal_curve_zoom_reset")) {
            curve_view_zoom = 1.0f;
            curve_view_center_t = 0.5f;
            curve_view_center_v = 0.5f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shift+mouse wheel zooms. Middle mouse pans the zoomed view.");
        
        ImGui::Separator();
        
        // Curve canvas
        ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, 350.0f);
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        canvas_pos.x += 52.0f;
        canvas_size.x = fmaxf(160.0f, canvas_size.x - 52.0f);
        canvas_size.y = fmaxf(220.0f, canvas_size.y - 38.0f);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

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
        draw_list->AddRect(canvas_pos, 
                          ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                          IM_COL32(100, 100, 100, 255));

        // Bottom timecode labels (seconds.hundredths) mapped to current duration.
        {
            const int label_steps = 10;
                char tc[48];
            for (int i = 0; i <= label_steps; ++i) {
                float t_norm = view_t_min + view_t_range * (float)i / (float)label_steps;
                float t_sec = t_norm * curve->duration;
                int tc_centis = (int)floorf(t_sec * 100.0f + 0.5f);
                int tc_seconds = tc_centis / 100;
                int milliseconds = (int)floorf(t_sec * 1000.0f + 0.5f) % 1000;
                snprintf(tc, sizeof(tc), "f %.2f  %d:%03d", t_norm, tc_seconds, milliseconds);
                ImVec2 text_size = ImGui::CalcTextSize(tc);
                float label_x = TimeToScreenX(t_norm) - text_size.x * 0.5f;
                float min_x = canvas_pos.x + 2.0f;
                float max_x = canvas_pos.x + canvas_size.x - text_size.x - 2.0f;
                if (label_x < min_x) label_x = min_x;
                if (label_x > max_x) label_x = max_x;
                float label_y = canvas_pos.y + canvas_size.y - text_size.y - 2.0f;
                draw_list->AddText(ImVec2(label_x, label_y), IM_COL32(170, 170, 170, 220), tc);
            }
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
        
        // Interaction area
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
        
        // First, check if we're clicking on a point (prioritize point interaction)
        bool clicked_on_point = false;
        
        // Draw and interact with control points
        for (int i = 0; i < curve->point_count; ++i) {
            rev::curve::Point* pt = &curve->points[i];
            
            ImVec2 point_pos = ImVec2(TimeToScreenX(pt->t), ValueToScreenY(pt->v));
            
            float point_radius = 6.0f;
            bool point_hovered = (mouse_pos.x - point_pos.x) * (mouse_pos.x - point_pos.x) +
                                (mouse_pos.y - point_pos.y) * (mouse_pos.y - point_pos.y) < point_radius * point_radius;
            
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
            if (selected_points[i]) {
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

        if (is_hovered && ImGui::IsMouseClicked(0) && !clicked_on_point) {
            if (ImGui::GetIO().KeyShift) {
                selecting_rectangle = true;
                rectangle_start = mouse_pos;
                rectangle_end = mouse_pos;
            } else {
                memset(selected_points, 0, sizeof(selected_points));
                selected_point_count = 0;
                editor->selected_point_index = -1;
            }
        }
        if (selecting_rectangle && ImGui::IsMouseDown(0)) {
            rectangle_end = mouse_pos;
            draw_list->AddRect(ImVec2(fminf(rectangle_start.x, rectangle_end.x), fminf(rectangle_start.y, rectangle_end.y)),
                               ImVec2(fmaxf(rectangle_start.x, rectangle_end.x), fmaxf(rectangle_start.y, rectangle_end.y)),
                               IM_COL32(100, 220, 255, 255), 0.0f, 0, 1.5f);
        }
        if (selecting_rectangle && ImGui::IsMouseReleased(0)) {
            ImVec2 min_corner(fminf(rectangle_start.x, rectangle_end.x), fminf(rectangle_start.y, rectangle_end.y));
            ImVec2 max_corner(fmaxf(rectangle_start.x, rectangle_end.x), fmaxf(rectangle_start.y, rectangle_end.y));
            for (int i = 0; i < curve->point_count && i < 4096; ++i) {
                ImVec2 point_pos(TimeToScreenX(curve->points[i].t), ValueToScreenY(curve->points[i].v));
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
                if (i != 0 && i != curve->point_count - 1)
                    curve->points[i].t = fmaxf(0.0f, fminf(1.0f, drag_start_t[i] + delta_t));
                curve->points[i].v = drag_start_v[i] + delta_v;
            }
            editor->project->modified = true;
        }
        if (dragging_selection && ImGui::IsMouseReleased(0)) {
            dragging_selection = false;
            rev::curve::SortPoints(*curve);
        }
        draw_list->PopClipRect();
        
        // Click on canvas background deselects point (only if we didn't click on a point)
        if (is_hovered && ImGui::IsMouseClicked(0) && !clicked_on_point) {
            editor->selected_point_index = -1;
        }
        
        // Add point on double-click (only if not clicking on existing point)
        if (is_hovered && ImGui::IsMouseDoubleClicked(0) && !clicked_on_point) {
            float t = ScreenToTime(mouse_pos.x);
            float v = ScreenToValue(mouse_pos.y);
            t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
            rev::curve::AddPoint(*curve, t, v, rev::curve::EaseMode::Linear);
            rev::curve::SortPoints(*curve);
            editor->project->modified = true;
            memset(selected_points, 0, sizeof(selected_points));
            selected_points[curve->point_count - 1] = true;
            selected_point_count = 1;
            editor->selected_point_index = curve->point_count - 1;
        }
        
        ImGui::Separator();
        
        // Direct point properties
        if (selected_point_count > 0 && editor->selected_point_index >= 0 &&
            editor->selected_point_index < curve->point_count) {
            rev::curve::Point* pt = &curve->points[editor->selected_point_index];
            ImGui::Text("%d selected node%s", selected_point_count, selected_point_count == 1 ? "" : "s");
            if (selected_point_count == 1) {
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
            float value = pt->v;
            if (ImGui::DragFloat("Value", &value, 0.1f, -FLT_MAX, FLT_MAX, "%.3f")) {
                for (int i = 0; i < curve->point_count && i < 4096; ++i)
                    if (selected_points[i]) curve->points[i].v = value;
                editor->project->modified = true;
            }
            
            const char* ease_modes[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut", "Smoothstep", "Hold"};
            int current_mode = (int)pt->mode;
            ImGui::SetNextItemWidth(200);
            if (ImGui::Combo("Ease Mode", &current_mode, ease_modes, 6)) {
                for (int i = 0; i < curve->point_count && i < 4096; ++i)
                    if (selected_points[i]) curve->points[i].mode = (rev::curve::EaseMode)current_mode;
                editor->project->modified = true;
            }
        } else {
            ImGui::TextDisabled("Select nodes to edit their values directly");
        }
        
        ImGui::Separator();
        
        // Controls
        ImGui::Checkbox("Show Grid", &editor->show_curve_grid);
        ImGui::SameLine();
        ImGui::TextDisabled("  |  ");
        ImGui::SameLine();
        ImGui::TextDisabled("Shift-click: multi-select | Shift-drag: box select | Double-click: add | Drag: move | Right-click: delete");
        
        // Point Properties Modal
        if (editor->point_properties_modal_open && editor->selected_point_index < -1 &&
            ImGui::BeginPopupModal("Point Properties", &editor->point_properties_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
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
                        if (cue_type == CueTypeShader && editor->selected_cue_index < scene->shader_cue_count) {
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
                        } else if (cue_type == CueTypeImage && editor->selected_cue_index < scene->image_cue_count) {
                            // Image cue
                            ImageCue* cue = &scene->image_cues[editor->selected_cue_index];
                            if (cue->curve_x == curve_index) cue->curve_x = -1;
                            if (cue->curve_y == curve_index) cue->curve_y = -1;
                            if (cue->curve_scale == curve_index) cue->curve_scale = -1;
                            if (cue->curve_rotation == curve_index) cue->curve_rotation = -1;
                            if (cue->curve_opacity == curve_index) cue->curve_opacity = -1;
                            editor->editing_image = *cue; // Update editing copy
                        } else if (cue_type == CueTypeText && editor->selected_cue_index < scene->text_cue_count) {
                            // Text cue
                            TextCue* cue = &scene->text_cues[editor->selected_cue_index];
                            if (cue->curve_size == curve_index) cue->curve_size = -1;
                            if (cue->curve_color_r == curve_index) cue->curve_color_r = -1;
                            if (cue->curve_color_g == curve_index) cue->curve_color_g = -1;
                            if (cue->curve_color_b == curve_index) cue->curve_color_b = -1;
                            if (cue->curve_x == curve_index) cue->curve_x = -1;
                            if (cue->curve_y == curve_index) cue->curve_y = -1;
                            editor->editing_text = *cue; // Update editing copy
                        } else if (cue_type == CueTypeMesh && editor->selected_cue_index < scene->mesh_cue_count) {
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
                            if (cue->curve_fov == curve_index) cue->curve_fov = -1;
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
    
    // Keep preview readable while this modal is open by disabling dim background.
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool shader_popup_open = ImGui::BeginPopupModal("Shader Parameters", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();

    // ImGui shader modal (NULL = no close button, must use Close)
    if (shader_popup_open) {
        ShaderCue* cue = &editor->editing_shader;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->shader_modal_asset_mode) {
                if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                    SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                    if (scene && editor->shader_modal_asset_cue_type == CueTypeImage &&
                        editor->selected_cue_index < scene->image_cue_count &&
                        editor->shader_modal_asset_index < scene->image_cues[editor->selected_cue_index].shader_count) {
                        ShaderCueToAssetShader(*cue, &scene->image_cues[editor->selected_cue_index].shaders[editor->shader_modal_asset_index]);
                        editor->editing_asset_shader = scene->image_cues[editor->selected_cue_index].shaders[editor->shader_modal_asset_index];
                        editor->project->modified = true;
                    } else if (scene && editor->shader_modal_asset_cue_type == CueTypeAnimatedSprite &&
                               editor->selected_cue_index < scene->animated_sprite_cue_count &&
                               editor->shader_modal_asset_index < scene->animated_sprite_cues[editor->selected_cue_index].shader_count) {
                        ShaderCueToAssetShader(*cue, &scene->animated_sprite_cues[editor->selected_cue_index].shaders[editor->shader_modal_asset_index]);
                        editor->editing_asset_shader = scene->animated_sprite_cues[editor->selected_cue_index].shaders[editor->shader_modal_asset_index];
                        editor->project->modified = true;
                    } else if (scene && editor->shader_modal_asset_cue_type == CueTypePixel &&
                               editor->selected_cue_index < scene->pixel_cue_count &&
                               editor->shader_modal_asset_index < scene->pixel_cues[editor->selected_cue_index].shader_count) {
                        ShaderCueToAssetShader(*cue, &scene->pixel_cues[editor->selected_cue_index].shaders[editor->shader_modal_asset_index]);
                        editor->editing_asset_shader = scene->pixel_cues[editor->selected_cue_index].shaders[editor->shader_modal_asset_index];
                        editor->project->modified = true;
                    }
                }
            } else if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeShader) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->shader_cue_count) {
                    scene->shader_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };
        // Helper lambda for curve buttons
        auto OpenShaderCurve = [&](int& curve_field, const char* label, float current_value) {
            if (curve_field < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                editor->editing_curve_cue_type = CueTypeShader; // Shader
                snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", label);
                editor->curve_editor_modal_request_open = true;
            }
        };

        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypeShader;
        RenderTriggerTimingAssignment(editor, "shader_trigger_timing", &trigger_timing_modified);
        
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
        if (ImGui::CollapsingHeader("Color Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        // Palette Low
        ImGui::Text("Low:");
        if (ImGui::SliderFloat("R##low", &cue->palette_low.r, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_low_r, "Shader Palette Low R", nullptr, "shader_palette_low_r");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_low_r >= 0 ? "[C]##low_r" : "+##low_r")) {
            OpenShaderCurve(cue->curve_palette_low_r, "Shader Palette Low R", cue->palette_low.r);
        }
        
        if (ImGui::SliderFloat("G##low", &cue->palette_low.g, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_low_g, "Shader Palette Low G", nullptr, "shader_palette_low_g");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_low_g >= 0 ? "[C]##low_g" : "+##low_g")) {
            OpenShaderCurve(cue->curve_palette_low_g, "Shader Palette Low G", cue->palette_low.g);
        }
        
        if (ImGui::SliderFloat("B##low", &cue->palette_low.b, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_low_b, "Shader Palette Low B", nullptr, "shader_palette_low_b");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_low_b >= 0 ? "[C]##low_b" : "+##low_b")) {
            OpenShaderCurve(cue->curve_palette_low_b, "Shader Palette Low B", cue->palette_low.b);
        }
        
        // Palette Mid
        ImGui::Text("Mid:");
        if (ImGui::SliderFloat("R##mid", &cue->palette_mid.r, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_mid_r, "Shader Palette Mid R", nullptr, "shader_palette_mid_r");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_mid_r >= 0 ? "[C]##mid_r" : "+##mid_r")) {
            OpenShaderCurve(cue->curve_palette_mid_r, "Shader Palette Mid R", cue->palette_mid.r);
        }
        
        if (ImGui::SliderFloat("G##mid", &cue->palette_mid.g, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_mid_g, "Shader Palette Mid G", nullptr, "shader_palette_mid_g");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_mid_g >= 0 ? "[C]##mid_g" : "+##mid_g")) {
            OpenShaderCurve(cue->curve_palette_mid_g, "Shader Palette Mid G", cue->palette_mid.g);
        }
        
        if (ImGui::SliderFloat("B##mid", &cue->palette_mid.b, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_mid_b, "Shader Palette Mid B", nullptr, "shader_palette_mid_b");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_mid_b >= 0 ? "[C]##mid_b" : "+##mid_b")) {
            OpenShaderCurve(cue->curve_palette_mid_b, "Shader Palette Mid B", cue->palette_mid.b);
        }
        
        // Palette High
        ImGui::Text("High:");
        if (ImGui::SliderFloat("R##high", &cue->palette_high.r, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_high_r, "Shader Palette High R", nullptr, "shader_palette_high_r");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_high_r >= 0 ? "[C]##high_r" : "+##high_r")) {
            OpenShaderCurve(cue->curve_palette_high_r, "Shader Palette High R", cue->palette_high.r);
        }
        
        if (ImGui::SliderFloat("G##high", &cue->palette_high.g, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_high_g, "Shader Palette High G", nullptr, "shader_palette_high_g");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_palette_high_g >= 0 ? "[C]##high_g" : "+##high_g")) {
            OpenShaderCurve(cue->curve_palette_high_g, "Shader Palette High G", cue->palette_high.g);
        }
        
        if (ImGui::SliderFloat("B##high", &cue->palette_high.b, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_palette_high_b, "Shader Palette High B", nullptr, "shader_palette_high_b");
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
        }
        
        ImGui::Separator();
        
        // Animation parameters with curve buttons
        if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        if (ImGui::SliderFloat("Speed", &cue->speed, 0.1f, 5.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_speed, "Shader Speed", nullptr, "shader_speed");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_speed >= 0 ? "[C]##speed" : "+##speed")) {
            OpenShaderCurve(cue->curve_speed, "Shader Speed", cue->speed);
        }
        
        if (ImGui::SliderFloat("Intensity", &cue->intensity, 0.0f, 2.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_intensity, "Shader Intensity", nullptr, "shader_intensity");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_intensity >= 0 ? "[C]##intensity" : "+##intensity")) {
            OpenShaderCurve(cue->curve_intensity, "Shader Intensity", cue->intensity);
        }
        
        if (ImGui::SliderFloat("Warp", &cue->warp, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_warp, "Shader Warp", nullptr, "shader_warp");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_warp >= 0 ? "[C]##warp" : "+##warp")) {
            OpenShaderCurve(cue->curve_warp, "Shader Warp", cue->warp);
        }
        {
            float warp_wrap = cue->warp - floorf(cue->warp);
            if (warp_wrap < 0.0f) warp_wrap += 1.0f;
            float warp_deg = warp_wrap * 360.0f;
            ImGui::Text("Warp %.3f | %.1f deg", cue->warp, warp_deg);
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
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Procedural Noise", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool noise_enabled = cue->noise.enabled != 0;
        if (ImGui::Checkbox("Enabled##noise", &noise_enabled)) {
            cue->noise.enabled = noise_enabled ? 1 : 0;
            AutoSave();
        }
        const char* noise_types[] = {"Value", "fBm", "Voronoi", "Turbulence", "Ridged"};
        if (ImGui::Combo("Type##noise", &cue->noise.type, noise_types, 5)) AutoSave();
        if (ImGui::SliderFloat("Scale##noise", &cue->noise.scale, 0.1f, 32.0f)) AutoSave();
        if (ImGui::SliderFloat("Strength##noise", &cue->noise.strength, 0.0f, 2.0f)) AutoSave();
        if (ImGui::SliderFloat("Octaves##noise", &cue->noise.octaves, 1.0f, 8.0f)) AutoSave();
        if (ImGui::SliderFloat("Lacunarity##noise", &cue->noise.lacunarity, 1.0f, 4.0f)) AutoSave();
        if (ImGui::SliderFloat("Gain##noise", &cue->noise.gain, 0.1f, 0.9f)) AutoSave();
        if (ImGui::SliderFloat("Warp##noise", &cue->noise.warp, 0.0f, 2.0f)) AutoSave();
        if (ImGui::DragFloat2("Motion##noise", &cue->noise.speed_x, 0.01f, -4.0f, 4.0f)) AutoSave();
        if (ImGui::SliderFloat("Seed##noise", &cue->noise.seed, 0.0f, 100.0f)) AutoSave();
        if (ImGui::SliderFloat("Contrast##noise", &cue->noise.contrast, 0.1f, 3.0f)) AutoSave();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Noise Texture Maps")) {
        ImGui::TextDisabled("Optional project-assets-relative images; bound to u_noise_map_0..3.");
        for (int map_index = 0; map_index < 4; ++map_index) {
            char label[64] = {};
            snprintf(label, sizeof(label), "Map %d##noise_map", map_index);
            if (ImGui::InputText(label, cue->noise_textures.paths[map_index],
                                 sizeof(cue->noise_textures.paths[map_index]))) AutoSave();
        }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Virtual 3D Texture Space", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat3("Position XYZ", &cue->position_x, 0.01f, -10.0f, 10.0f, "%.3f")) AutoSave();
        if (ImGui::DragFloat3("Rotation XYZ", &cue->rotation_x, 1.0f, -360.0f, 360.0f, "%.1f deg")) AutoSave();
        if (ImGui::DragFloat3("Motion XYZ", &cue->motion_x, 0.01f, -5.0f, 5.0f, "%.3f")) AutoSave();
        ImGui::TextDisabled("Checkerboard and fog use these as 3D coordinates.");
        }
        
        ImGui::Separator();
        
        // Exposure & fade with curve buttons
        if (ImGui::CollapsingHeader("Exposure and Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Exposure:");
        if (ImGui::SliderFloat("Base##exp", &cue->exposure_base, 0.0f, 2.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_exposure, "Shader Exposure Base", nullptr, "shader_exposure");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_exposure >= 0 ? "[C]##exp_base" : "+##exp_base")) {
            OpenShaderCurve(cue->curve_exposure, "Shader Exposure Base", cue->exposure_base);
        }
        
        if (ImGui::SliderFloat("Ramp##exp", &cue->exposure_ramp, -0.5f, 0.5f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_exposure_ramp, "Shader Exposure Ramp", nullptr, "shader_exposure_ramp");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_exposure_ramp >= 0 ? "[C]##exp_ramp" : "+##exp_ramp")) {
            OpenShaderCurve(cue->curve_exposure_ramp, "Shader Exposure Ramp", cue->exposure_ramp);
        }
        
        ImGui::Text("Fade:");
        if (ImGui::SliderFloat("Base##fade", &cue->fade_base, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_fade, "Shader Fade Base", nullptr, "shader_fade");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_fade >= 0 ? "[C]##fade_base" : "+##fade_base")) {
            OpenShaderCurve(cue->curve_fade, "Shader Fade Base", cue->fade_base);
        }
        
        if (ImGui::SliderFloat("Ramp##fade", &cue->fade_ramp, -0.5f, 0.5f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_fade_ramp, "Shader Fade Ramp", nullptr, "shader_fade_ramp");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_fade_ramp >= 0 ? "[C]##fade_ramp" : "+##fade_ramp")) {
            OpenShaderCurve(cue->curve_fade_ramp, "Shader Fade Ramp", cue->fade_ramp);
        }
        }
        
        ImGui::Separator();

        
        // Timing
        if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Timing:");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade In", &cue->fade_in, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade Out", &cue->fade_out, 0.1f, 1.0f)) AutoSave();
        }
        
        ImGui::Separator();
        
        // Layer controls
        if (ImGui::CollapsingHeader("Layer", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Layer:");
        const char* layer_roles[] = {"Background", "Midground", "Foreground", "Overlay"};
        if (ImGui::Combo("Role", &cue->layer_role, layer_roles, 4)) AutoSave();
        
        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_opacity, "Shader Opacity", nullptr, "shader_opacity");
        ImGui::SameLine();
        if (ImGui::SmallButton(cue->curve_opacity >= 0 ? "[C]##opacity" : "+##opacity")) {
            OpenShaderCurve(cue->curve_opacity, "Shader Opacity", cue->opacity);
        }
        
        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();
        if (ImGui::InputInt("Order", &cue->layer_order)) AutoSave();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Curve Reuse", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Curve Reuse:");
        if (editor->project->curve_count <= 0) {
            ImGui::TextDisabled("No curves available. Create one first.");
        } else {
            CurveTargetBinding targets[128] = {};
            const int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 128);
            static int shader_target_idx = 0;
            static int shader_curve_idx = 0;
            if (shader_target_idx < 0 || shader_target_idx >= target_count) shader_target_idx = 0;
            if (shader_curve_idx < 0) shader_curve_idx = 0;
            if (shader_curve_idx >= editor->project->curve_count) shader_curve_idx = editor->project->curve_count - 1;

            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("Target Parameter##shader_curve_reuse", &shader_target_idx,
                         CurveTargetComboGetter, targets, target_count);

            char curve_preview[64] = {};
            BuildCurveDisplayLabel(editor, shader_curve_idx, curve_preview, sizeof(curve_preview));
            if (ImGui::BeginCombo("Curve##shader_curve_reuse", curve_preview)) {
                for (int i = 0; i < editor->project->curve_count; ++i) {
                    char item[64] = {};
                    BuildCurveDisplayLabel(editor, i, item, sizeof(item));
                    bool selected = (i == shader_curve_idx);
                    if (ImGui::Selectable(item, selected)) shader_curve_idx = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Assign Existing Curve##shader_curve_reuse")) {
                *targets[shader_target_idx].curve_field = shader_curve_idx;
                AutoSave();
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit Assigned##shader_curve_reuse")) {
                int idx = *targets[shader_target_idx].curve_field;
                if (idx >= 0 && idx < editor->project->curve_count) {
                    editor->editing_curve_index = idx;
                    editor->editing_curve_cue_type = CueTypeShader;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[shader_target_idx].label);
                    editor->curve_editor_modal_request_open = true;
                }
            }
        }
        }
        
        ImGui::Separator();
        
        // Close button (changes auto-saved continuously)
        if (ImGui::Button("Close", ImVec2(240, 0))) {
            editor->shader_modal_open = false;
            editor->shader_modal_asset_mode = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    } else {
        // If popup was closed, reset the flag
        if (editor->shader_modal_open) {
            editor->shader_modal_open = false;
            editor->shader_modal_asset_mode = false;
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
                editor->selected_cue_type == CueTypeMusic) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->music_cue_count) {
                    scene->music_cues[editor->selected_cue_index] = *cue;
                    UpdateEditorAudioEffects(editor);
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
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            ReloadEditorAssets(editor);
        }
        
        ImGui::Separator();

        ImGui::Text("Overall Audio Effects");
        bool gain_enabled = editor->project->audio_effects.gain_enabled != 0;
        if (ImGui::Checkbox("Gain", &gain_enabled)) {
            editor->project->audio_effects.gain_enabled = gain_enabled ? 1 : 0;
            AutoSave();
        }
        if (ImGui::SliderFloat("Gain (dB)", &editor->project->audio_effects.gain_db, -24.0f, 24.0f)) AutoSave();
        bool compressor_enabled = editor->project->audio_effects.compressor_enabled != 0;
        if (ImGui::Checkbox("Compressor", &compressor_enabled)) {
            editor->project->audio_effects.compressor_enabled = compressor_enabled ? 1 : 0;
            AutoSave();
        }
        if (ImGui::SliderFloat("Threshold", &editor->project->audio_effects.compressor_threshold, 0.05f, 1.0f)) AutoSave();
        if (ImGui::SliderFloat("Ratio", &editor->project->audio_effects.compressor_ratio, 1.0f, 20.0f)) AutoSave();
        if (ImGui::SliderFloat("Attack (s)", &editor->project->audio_effects.compressor_attack, 0.001f, 1.0f)) AutoSave();
        if (ImGui::SliderFloat("Release (s)", &editor->project->audio_effects.compressor_release, 0.005f, 2.0f)) AutoSave();
        bool widener_enabled = editor->project->audio_effects.widener_enabled != 0;
        if (ImGui::Checkbox("Widener", &widener_enabled)) {
            editor->project->audio_effects.widener_enabled = widener_enabled ? 1 : 0;
            AutoSave();
        }
        if (ImGui::SliderFloat("Width", &editor->project->audio_effects.widener_amount, 0.0f, 2.0f)) AutoSave();
        bool eq_enabled = editor->project->audio_effects.eq_enabled != 0;
        if (ImGui::Checkbox("EQ", &eq_enabled)) {
            editor->project->audio_effects.eq_enabled = eq_enabled ? 1 : 0;
            AutoSave();
        }
        if (ImGui::SliderFloat("EQ Low (dB)", &editor->project->audio_effects.eq_low_db, -18.0f, 18.0f)) AutoSave();
        if (ImGui::SliderFloat("EQ Mid (dB)", &editor->project->audio_effects.eq_mid_db, -18.0f, 18.0f)) AutoSave();
        if (ImGui::SliderFloat("EQ High (dB)", &editor->project->audio_effects.eq_high_db, -18.0f, 18.0f)) AutoSave();
        
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
    
    // Keep preview readable while this modal is open by disabling dim background.
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool image_popup_open = ImGui::BeginPopupModal("Image Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();

    // Image modal (NULL = no close button)
    if (image_popup_open) {
        ImageCue* cue = &editor->editing_image;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeImage) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    scene->image_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };

        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypeImage;
        RenderTriggerTimingAssignment(editor, "image_trigger_timing", &trigger_timing_modified);
        
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
            ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.webp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0WebP\0*.webp\0All Files\0*.*\0";
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
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            ReloadEditorAssets(editor);
        }
        
        ImGui::Separator();
        
        // Position
        ImGui::Text("Position (0.0-1.0):");
        if (ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_x, "Image X", nullptr, "image_x");
        ImGui::SameLine();
        if (cue->curve_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_x")) {
            // Get the actual scene cue to modify it directly
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeImage) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    // Create curve if it doesn't exist
                    if (actual_cue->curve_x < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeImage; // Image
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image X Position");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_y, "Image Y", nullptr, "image_y");
        ImGui::SameLine();
        if (cue->curve_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_y")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeImage) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_y < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeImage;
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
        DrawCurveRecordButton(editor, &cue->curve_scale, "Image Scale", nullptr, "image_scale");
        ImGui::SameLine();
        if (cue->curve_scale >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_scale);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_scale")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeImage) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_scale < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeImage;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image Scale");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Rotation", &cue->rotation, -360.0f, 360.0f, "%.1f deg")) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_rotation, "Image Rotation", nullptr, "image_rotation");
        ImGui::SameLine();
        if (cue->curve_rotation >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_rotation);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_rotation")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0 &&
                editor->selected_cue_type == CueTypeImage) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    if (actual_cue->curve_rotation < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
                        actual_cue->curve_rotation = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_rotation];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->rotation);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->rotation);
                        editor->project->modified = true;
                    }
                    cue->curve_rotation = actual_cue->curve_rotation;
                    if (actual_cue->curve_rotation >= 0 && actual_cue->curve_rotation < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_rotation;
                        editor->editing_curve_cue_type = CueTypeImage;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image Rotation");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");

        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_opacity, "Image Opacity", nullptr, "image_opacity");
        ImGui::SameLine();
        if (cue->curve_opacity >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_opacity);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_img_opacity")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeImage) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->image_cue_count) {
                    ImageCue* actual_cue = &scene->image_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_opacity < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeImage;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Image Opacity");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");

        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();
        
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

        bool layer_effects_modified = false;
        RenderLayerPostEffects(editor, cue->post_effects, &cue->post_effect_count,
                       &layer_effects_modified, "image", CueTypeImage, editor->selected_cue_index);
        if (layer_effects_modified) AutoSave();

        bool asset_shaders_modified = false;
        RenderAssetShaders(editor, cue->shaders, &cue->shader_count,
               &asset_shaders_modified, "image_shaders", CueTypeImage, editor->selected_cue_index);
        if (asset_shaders_modified) AutoSave();
        
        ImGui::Separator();
        
        // Timing
        ImGui::Text("Timing (seconds):");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();

        ImGui::Separator();
        ImGui::Text("Curve Reuse:");
        if (editor->project->curve_count <= 0) {
            ImGui::TextDisabled("No curves available. Create one first.");
        } else {
            CurveTargetBinding targets[128] = {};
            const int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 128);
            static int image_target_idx = 0;
            static int image_curve_idx = 0;
            if (image_target_idx < 0 || image_target_idx >= target_count) image_target_idx = 0;
            if (image_curve_idx < 0) image_curve_idx = 0;
            if (image_curve_idx >= editor->project->curve_count) image_curve_idx = editor->project->curve_count - 1;

            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("Target Parameter##image_curve_reuse", &image_target_idx,
                         CurveTargetComboGetter, targets, target_count);

            char curve_preview[64] = {};
            BuildCurveDisplayLabel(editor, image_curve_idx, curve_preview, sizeof(curve_preview));
            if (ImGui::BeginCombo("Curve##image_curve_reuse", curve_preview)) {
                for (int i = 0; i < editor->project->curve_count; ++i) {
                    char item[64] = {};
                    BuildCurveDisplayLabel(editor, i, item, sizeof(item));
                    bool selected = (i == image_curve_idx);
                    if (ImGui::Selectable(item, selected)) image_curve_idx = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Assign Existing Curve##image_curve_reuse")) {
                *targets[image_target_idx].curve_field = image_curve_idx;
                AutoSave();
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit Assigned##image_curve_reuse")) {
                int idx = *targets[image_target_idx].curve_field;
                if (idx >= 0 && idx < editor->project->curve_count) {
                    editor->editing_curve_index = idx;
                    editor->editing_curve_cue_type = CueTypeImage;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[image_target_idx].label);
                    editor->curve_editor_modal_request_open = true;
                }
            }
        }
        
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

void RenderAnimatedSpriteModal(EditorContext* editor) {
    if (!editor) return;

    if (editor->animated_sprite_modal_request_open) {
        ImGui::OpenPopup("Animated Sprite Settings");
        editor->animated_sprite_modal_open = true;
        editor->animated_sprite_modal_request_open = false;
    }

    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool popup_open = ImGui::BeginPopupModal("Animated Sprite Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();

    if (popup_open) {
        AnimatedSpriteCue* cue = &editor->editing_animated_sprite;

        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 &&
                editor->selected_cue_index >= 0 &&
                editor->selected_cue_type == CueTypeAnimatedSprite) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->animated_sprite_cue_count) {
                    scene->animated_sprite_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };

        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypeAnimatedSprite;
        RenderTriggerTimingAssignment(editor, "sprite_trigger_timing", &trigger_timing_modified);

        auto OpenCurveEditor = [&](int* curve_field, float base_value, const char* label) {
            if (!curve_field || !label) return;
            if (editor->selected_scene_index < 0 || editor->selected_cue_index < 0 ||
                editor->selected_cue_type != CueTypeAnimatedSprite) return;
            SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
            if (!scene || editor->selected_cue_index >= scene->animated_sprite_cue_count) return;
            AnimatedSpriteCue* actual_cue = &scene->animated_sprite_cues[editor->selected_cue_index];

            int* actual_field = nullptr;
            if (curve_field == &cue->curve_x) actual_field = &actual_cue->curve_x;
            else if (curve_field == &cue->curve_y) actual_field = &actual_cue->curve_y;
            else if (curve_field == &cue->curve_scale) actual_field = &actual_cue->curve_scale;
            else if (curve_field == &cue->curve_rotation) actual_field = &actual_cue->curve_rotation;
            else if (curve_field == &cue->curve_opacity) actual_field = &actual_cue->curve_opacity;
            else if (curve_field == &cue->curve_frame) actual_field = &actual_cue->curve_frame;
            if (!actual_field) return;

            if (*actual_field < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
                *actual_field = editor->project->curve_count++;
                auto& curve = editor->project->curves[*actual_field];
                curve = rev::curve::CreateCurve(16);
                rev::curve::AddPoint(curve, 0.0f, base_value);
                rev::curve::AddPoint(curve, 1.0f, base_value);
                editor->project->modified = true;
            }

            *curve_field = *actual_field;
            if (*actual_field >= 0 && *actual_field < editor->project->curve_count) {
                editor->editing_curve_index = *actual_field;
                editor->editing_curve_cue_type = CueTypeAnimatedSprite;
                snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", label);
                editor->curve_editor_modal_request_open = true;
            }
        };

        ImGui::Text("Sprite Name:");
        if (ImGui::InputText("##sprite_name", cue->sprite_name, sizeof(cue->sprite_name))) AutoSave();

        ImGui::Separator();
        ImGui::Text("Frames (semicolon-separated asset keys):");
        if (ImGui::InputTextMultiline("##frame_keys_csv", cue->frame_keys_csv, sizeof(cue->frame_keys_csv), ImVec2(420, 70))) AutoSave();

        ImGui::Text("Frame Paths (semicolon-separated relative paths):");
        if (ImGui::InputTextMultiline("##frame_paths_csv", cue->frame_paths_csv, sizeof(cue->frame_paths_csv), ImVec2(420, 70))) AutoSave();

        if (ImGui::Button("Add Frame")) {
            OPENFILENAMEA ofn = {};
            char filepath[260] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = (HWND)editor->window->hwnd;
            ofn.lpstrFile = filepath;
            ofn.nMaxFile = sizeof(filepath);
            ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.webp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0WebP\0*.webp\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrInitialDir = "assets";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn)) {
                const char* filename = strrchr(filepath, '\\');
                if (!filename) filename = strrchr(filepath, '/');
                if (filename) filename++; else filename = filepath;

                if (editor->project->assets_path[0]) {
                    char dest_path[512] = {};
                    snprintf(dest_path, sizeof(dest_path), "%s\\%s", editor->project->assets_path, filename);
                    if (!CopyFileA(filepath, dest_path, FALSE)) {
                        printf("[ANIM_SPRITE] Warning: could not copy asset to %s (err=%lu)\n", dest_path, GetLastError());
                    }
                }

                if (cue->frame_keys_csv[0] != '\0' && strlen(cue->frame_keys_csv) + 1 < sizeof(cue->frame_keys_csv)) {
                    strcat_s(cue->frame_keys_csv, sizeof(cue->frame_keys_csv), ";");
                }
                strcat_s(cue->frame_keys_csv, sizeof(cue->frame_keys_csv), filename);

                if (cue->frame_paths_csv[0] != '\0' && strlen(cue->frame_paths_csv) + 1 < sizeof(cue->frame_paths_csv)) {
                    strcat_s(cue->frame_paths_csv, sizeof(cue->frame_paths_csv), ";");
                }
                strcat_s(cue->frame_paths_csv, sizeof(cue->frame_paths_csv), filename);
                AutoSave();
            }
        }

        ImGui::Separator();
        ImGui::Text("Position (0.0-1.0):");
        if (ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_x >= 0) { ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_x); ImGui::SameLine(); }
        DrawCurveRecordButton(editor, &cue->curve_x, "Animated Sprite X", nullptr, "animated_sprite_x");
        if (ImGui::SmallButton("+##curve_anim_x")) OpenCurveEditor(&cue->curve_x, cue->x, "AnimSprite X Position");

        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_y >= 0) { ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_y); ImGui::SameLine(); }
        DrawCurveRecordButton(editor, &cue->curve_y, "Animated Sprite Y", nullptr, "animated_sprite_y");
        if (ImGui::SmallButton("+##curve_anim_y")) OpenCurveEditor(&cue->curve_y, cue->y, "AnimSprite Y Position");

        ImGui::Separator();
        ImGui::Text("Transform:");
        if (ImGui::SliderFloat("Scale", &cue->scale, 0.1f, 5.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_scale >= 0) { ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_scale); ImGui::SameLine(); }
        DrawCurveRecordButton(editor, &cue->curve_scale, "Animated Sprite Scale", nullptr, "animated_sprite_scale");
        if (ImGui::SmallButton("+##curve_anim_scale")) OpenCurveEditor(&cue->curve_scale, cue->scale, "AnimSprite Scale");

        if (ImGui::SliderFloat("Rotation", &cue->rotation, -360.0f, 360.0f, "%.1f deg")) AutoSave();
        ImGui::SameLine();
        if (cue->curve_rotation >= 0) { ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_rotation); ImGui::SameLine(); }
        DrawCurveRecordButton(editor, &cue->curve_rotation, "Animated Sprite Rotation", nullptr, "animated_sprite_rotation");
        if (ImGui::SmallButton("+##curve_anim_rotation")) OpenCurveEditor(&cue->curve_rotation, cue->rotation, "AnimSprite Rotation");

        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        ImGui::SameLine();
        if (cue->curve_opacity >= 0) { ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_opacity); ImGui::SameLine(); }
        DrawCurveRecordButton(editor, &cue->curve_opacity, "Animated Sprite Opacity", nullptr, "animated_sprite_opacity");
        if (ImGui::SmallButton("+##curve_anim_opacity")) OpenCurveEditor(&cue->curve_opacity, cue->opacity, "AnimSprite Opacity");

        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();

        ImGui::Separator();
        ImGui::Text("Playback:");
        if (ImGui::InputFloat("FPS", &cue->fps, 0.5f, 1.0f, "%.2f")) {
            if (cue->fps < 0.1f) cue->fps = 0.1f;
            AutoSave();
        }
        const char* playback_modes[] = {"Loop", "Once", "PingPong"};
        if (ImGui::Combo("Mode", &cue->playback_mode, playback_modes, 3)) AutoSave();
        if (ImGui::InputInt("Start Frame", &cue->start_frame, 1, 10)) {
            if (cue->start_frame < 0) cue->start_frame = 0;
            AutoSave();
        }
        ImGui::SameLine();
        if (cue->curve_frame >= 0) { ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_frame); ImGui::SameLine(); }
        if (ImGui::SmallButton("+##curve_anim_frame")) OpenCurveEditor(&cue->curve_frame, (float)cue->start_frame, "AnimSprite Frame");

        ImGui::Separator();
        ImGui::Text("Layer / Effect:");
        if (ImGui::SliderInt("Layer", &cue->layer_order, -10, 10)) AutoSave();
        const char* img_effects[] = {"None", "Fade In/Out"};
        if (ImGui::Combo("Type##anim", &cue->effect_type, img_effects, 2)) AutoSave();
        if (cue->effect_type > 0) {
            if (ImGui::InputFloat("Fade In Start##anim", &cue->fade_in_start, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade In End##anim", &cue->fade_in_end, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out Start##anim", &cue->fade_out_start, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out End##anim", &cue->fade_out_end, 0.1f, 1.0f)) AutoSave();
        }

        bool layer_effects_modified = false;
        RenderLayerPostEffects(editor, cue->post_effects, &cue->post_effect_count,
                       &layer_effects_modified, "animated_sprite", CueTypeAnimatedSprite, editor->selected_cue_index);
        if (layer_effects_modified) AutoSave();

        bool asset_shaders_modified = false;
        RenderAssetShaders(editor, cue->shaders, &cue->shader_count,
               &asset_shaders_modified, "animated_sprite_shaders",
               CueTypeAnimatedSprite, editor->selected_cue_index);
        if (asset_shaders_modified) AutoSave();

         static int animated_target_index = 0;
         static int animated_curve_index = 0;
         RenderCurveReuseSection(editor, "animated_curve_reuse", CueTypeAnimatedSprite,
                        &animated_target_index, &animated_curve_index);

        ImGui::Separator();
        ImGui::Text("Timing (seconds):");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(240, 0))) {
            editor->animated_sprite_modal_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        if (editor->animated_sprite_modal_open) {
            editor->animated_sprite_modal_open = false;
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
    
    // Keep preview readable while this modal is open by disabling dim background.
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool text_popup_open = ImGui::BeginPopupModal("Text Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();

    // Text modal (NULL = no close button)
    if (text_popup_open) {
        TextCue* cue = &editor->editing_text;
        
        // Auto-save: Apply changes to scene continuously
        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    scene->text_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };

        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypeText;
        RenderTriggerTimingAssignment(editor, "text_trigger_timing", &trigger_timing_modified);
        
        ImGui::Text("Text Content:");
        if (ImGui::InputTextMultiline("##text", cue->text, sizeof(cue->text), ImVec2(400, 100))) AutoSave();
        
        ImGui::Separator();
        
        // Font
        ImGui::Text("Font:");
        
        // Font picker combo box
        if (editor->installed_font_count > 0) {
            // Find current font in list
            int current_font_index = -1;
            for (int i = 0; i < editor->installed_font_count; ++i) {
                if (strcmp(editor->installed_fonts[i], cue->font_name) == 0) {
                    current_font_index = i;
                    break;
                }
            }
            
            // If not found, default to first font
            if (current_font_index < 0 && editor->installed_font_count > 0) {
                current_font_index = 0;
                strncpy_s(cue->font_name, sizeof(cue->font_name), 
                         editor->installed_fonts[0], _TRUNCATE);
            }
            
            const char* preview = (current_font_index >= 0) ? 
                                  editor->installed_fonts[current_font_index] : 
                                  "Select Font...";
            
            if (ImGui::BeginCombo("##fontpicker", preview)) {
                for (int i = 0; i < editor->installed_font_count; ++i) {
                    bool is_selected = (i == current_font_index);
                    if (ImGui::Selectable(editor->installed_fonts[i], is_selected)) {
                        strncpy_s(cue->font_name, sizeof(cue->font_name), 
                                 editor->installed_fonts[i], _TRUNCATE);
                        AutoSave();
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            // Fallback to text input if no fonts enumerated
            if (ImGui::InputText("Font Name", cue->font_name, sizeof(cue->font_name))) AutoSave();
        }
        
        if (ImGui::SliderFloat("Size", &cue->size, 8.0f, 128.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_size, "Text Size", nullptr, "text_size");
        ImGui::SameLine();
        if (cue->curve_size >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_size);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_size")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_size < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeText;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Size");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");

        if (ImGui::SliderFloat("Rotation", &cue->rotation, -360.0f, 360.0f)) {
            cue->curve_rotation = -1;
            AutoSave();
        }
        ImGui::SameLine();
        if (cue->curve_rotation >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_rotation);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_rotation")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0 && editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    if (actual_cue->curve_rotation < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
                        actual_cue->curve_rotation = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_rotation];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->rotation);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->rotation);
                        editor->project->modified = true;
                    }
                    cue->curve_rotation = actual_cue->curve_rotation;
                    if (cue->curve_rotation >= 0) {
                        editor->editing_curve_index = cue->curve_rotation;
                        editor->editing_curve_cue_type = CueTypeText;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text Rotation");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        
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
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_color_r < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeText;
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
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_color_g < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeText;
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
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_color_b < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeText;
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
        DrawCurveRecordButton(editor, &cue->curve_x, "Text X", nullptr, "text_x");
        ImGui::SameLine();
        if (cue->curve_x >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_x);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_x")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_x < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeText;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Text X Position");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_y, "Text Y", nullptr, "text_y");
        ImGui::SameLine();
        if (cue->curve_y >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_y);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_txt_y")) {
            if (editor->selected_scene_index >= 0 && 
                editor->selected_cue_index >= 0 && 
                editor->selected_cue_type == CueTypeText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->text_cue_count) {
                    TextCue* actual_cue = &scene->text_cues[editor->selected_cue_index];
                    
                    if (actual_cue->curve_y < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeText;
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
        const char* effects[] = {
            "None",
            "Fade In/Out",
            "Scroll",
            "Line By Line",
            "Typewriter",
            "Sandstorm"
        };
        if (ImGui::Combo("Type", &cue->effect_type, effects, 6)) AutoSave();
        
        if (cue->effect_type > 0) {
            if (ImGui::InputFloat("Fade In Start",  &cue->fade_in_start,  0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade In End",    &cue->fade_in_end,    0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out Start", &cue->fade_out_start, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Fade Out End",   &cue->fade_out_end,   0.1f, 1.0f)) AutoSave();
        }

        ImGui::Separator();
        ImGui::Text("Animation");
        if (cue->animation.version <= 0) {
            InitializeTextAnimationConfig(&cue->animation);
            AutoSave();
        }
        TextAnimationConfig& animation = cue->animation;
        const char* reveal_types[] = {
            "None", "Fade", "Typewriter", "Line By Line", "Word By Word",
            "Character By Character", "Scale In", "Slide In", "Rotate In"
        };
        const char* exit_types[] = {
            "None", "Fade Out", "Reverse Typewriter", "Slide Out", "Scale Out", "Rotate Out"
        };
        const char* easing_types[] = {
            "Linear", "Ease In Quad", "Ease Out Quad", "Ease In Out Quad",
            "Ease In Cubic", "Ease Out Cubic", "Ease In Out Cubic", "Smooth Step", "Smoother Step"
        };
        const char* units[] = {"Object", "Line", "Word", "Character"};
        const char* orders[] = {"Forward", "Reverse", "Center Out", "Outside In", "Random"};
        const char* modifier_types[] = {
            "Glow", "Glow Pulse", "Jitter", "Wave", "Flicker", "Rotation Wave", "Scale Pulse"
        };

        ImGui::Text("Reveal");
        if (ImGui::Combo("Effect##text_reveal", &animation.reveal.type, reveal_types, 9)) AutoSave();
        if (ImGui::InputFloat("Start Offset##text_reveal", &animation.reveal.start_offset, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Duration##text_reveal", &animation.reveal.duration, 0.1f, 1.0f)) AutoSave();
        if (ImGui::Combo("Easing##text_reveal", &animation.reveal.easing, easing_types, 9)) AutoSave();
        if (ImGui::Combo("Unit##text_reveal", &animation.reveal.stagger.unit, units, 4)) AutoSave();
        if (ImGui::Combo("Order##text_reveal", &animation.reveal.stagger.order, orders, 5)) AutoSave();
        if (ImGui::InputFloat("Stagger Delay##text_reveal", &animation.reveal.stagger.delay, 0.01f, 0.1f)) AutoSave();
        if (ImGui::SliderFloat("Overlap##text_reveal", &animation.reveal.stagger.overlap, 0.0f, 1.0f)) AutoSave();
        bool reveal_ignore_whitespace = animation.reveal.stagger.ignore_whitespace != 0;
        if (ImGui::Checkbox("Ignore Whitespace##text_reveal", &reveal_ignore_whitespace)) {
            animation.reveal.stagger.ignore_whitespace = reveal_ignore_whitespace ? 1 : 0;
            AutoSave();
        }
        if (animation.reveal.type == 7) {
            if (ImGui::InputFloat("Distance##text_reveal", &animation.reveal.distance, 0.01f, 0.1f)) AutoSave();
            if (ImGui::InputFloat2("Direction##text_reveal", &animation.reveal.direction_x)) AutoSave();
            bool reveal_fade = animation.reveal.fade != 0;
            if (ImGui::Checkbox("Fade##text_reveal", &reveal_fade)) {
                animation.reveal.fade = reveal_fade ? 1 : 0;
                AutoSave();
            }
        } else if (animation.reveal.type == 6) {
            if (ImGui::InputFloat("Start Scale##text_reveal", &animation.reveal.start_scale, 0.05f, 0.25f)) AutoSave();
            bool reveal_fade = animation.reveal.fade != 0;
            if (ImGui::Checkbox("Fade##text_reveal", &reveal_fade)) {
                animation.reveal.fade = reveal_fade ? 1 : 0;
                AutoSave();
            }
        } else if (animation.reveal.type == 8) {
            if (ImGui::InputFloat("Start Rotation##text_reveal", &animation.reveal.start_rotation, 1.0f, 15.0f)) AutoSave();
            bool reveal_fade = animation.reveal.fade != 0;
            if (ImGui::Checkbox("Fade##text_reveal", &reveal_fade)) {
                animation.reveal.fade = reveal_fade ? 1 : 0;
                AutoSave();
            }
        }

        ImGui::Text("Modifiers (%d/%d)", animation.modifier_count, kMaxTextAnimationModifiers);
        if (animation.modifier_count < kMaxTextAnimationModifiers && ImGui::Button("Add Modifier")) {
            TextModifierConfig& modifier = animation.modifiers[animation.modifier_count++];
            memset(&modifier, 0, sizeof(modifier));
            modifier.enabled = 1;
            modifier.end_time = cue->cue_end - cue->cue_start;
            modifier.speed = 1.0f;
            modifier.frequency = 1.0f;
            modifier.seed = 1;
            AutoSave();
        }
        for (int i = 0; i < animation.modifier_count && i < kMaxTextAnimationModifiers; ++i) {
            TextModifierConfig& modifier = animation.modifiers[i];
            ImGui::PushID(i);
            if (ImGui::Combo("Type##text_modifier", &modifier.type, modifier_types, 7)) AutoSave();
            bool modifier_enabled = modifier.enabled != 0;
            if (ImGui::Checkbox("Enabled##text_modifier", &modifier_enabled)) {
                modifier.enabled = modifier_enabled ? 1 : 0;
                AutoSave();
            }
            if (ImGui::InputFloat("Amount##text_modifier", &modifier.amount, 0.01f, 0.1f)) AutoSave();
            if (ImGui::InputFloat("Speed##text_modifier", &modifier.speed, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Frequency##text_modifier", &modifier.frequency, 0.1f, 1.0f)) AutoSave();
            if (ImGui::InputFloat("Phase##text_modifier", &modifier.phase, 0.1f, 1.0f)) AutoSave();
            if (i > 0 && ImGui::Button("Move Up##text_modifier")) {
                TextModifierConfig previous = animation.modifiers[i - 1];
                animation.modifiers[i - 1] = modifier;
                animation.modifiers[i] = previous;
                AutoSave();
            }
            if (i + 1 < animation.modifier_count && ImGui::Button("Move Down##text_modifier")) {
                TextModifierConfig next = animation.modifiers[i + 1];
                animation.modifiers[i + 1] = modifier;
                animation.modifiers[i] = next;
                AutoSave();
            }
            if (ImGui::Button("Remove##text_modifier")) {
                for (int j = i; j + 1 < animation.modifier_count; ++j)
                    animation.modifiers[j] = animation.modifiers[j + 1];
                --animation.modifier_count;
                AutoSave();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        ImGui::Text("Exit");
        if (ImGui::Combo("Effect##text_exit", &animation.exit.type, exit_types, 6)) AutoSave();
        if (ImGui::InputFloat("Start Offset##text_exit", &animation.exit.start_offset, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Duration##text_exit", &animation.exit.duration, 0.1f, 1.0f)) AutoSave();
        if (ImGui::Combo("Easing##text_exit", &animation.exit.easing, easing_types, 9)) AutoSave();
        if (ImGui::Combo("Unit##text_exit", &animation.exit.stagger.unit, units, 4)) AutoSave();
        if (ImGui::Combo("Order##text_exit", &animation.exit.stagger.order, orders, 5)) AutoSave();
        if (ImGui::InputFloat("Stagger Delay##text_exit", &animation.exit.stagger.delay, 0.01f, 0.1f)) AutoSave();
        if (ImGui::SliderFloat("Overlap##text_exit", &animation.exit.stagger.overlap, 0.0f, 1.0f)) AutoSave();
        bool exit_ignore_whitespace = animation.exit.stagger.ignore_whitespace != 0;
        if (ImGui::Checkbox("Ignore Whitespace##text_exit", &exit_ignore_whitespace)) {
            animation.exit.stagger.ignore_whitespace = exit_ignore_whitespace ? 1 : 0;
            AutoSave();
        }
        if (animation.exit.type == 3) {
            if (ImGui::InputFloat("Distance##text_exit", &animation.exit.distance, 0.01f, 0.1f)) AutoSave();
            if (ImGui::InputFloat2("Direction##text_exit", &animation.exit.direction_x)) AutoSave();
            bool exit_fade = animation.exit.fade != 0;
            if (ImGui::Checkbox("Fade##text_exit", &exit_fade)) {
                animation.exit.fade = exit_fade ? 1 : 0;
                AutoSave();
            }
        } else if (animation.exit.type == 4) {
            if (ImGui::InputFloat("End Scale##text_exit", &animation.exit.end_scale, 0.05f, 0.25f)) AutoSave();
            bool exit_fade = animation.exit.fade != 0;
            if (ImGui::Checkbox("Fade##text_exit", &exit_fade)) {
                animation.exit.fade = exit_fade ? 1 : 0;
                AutoSave();
            }
        } else if (animation.exit.type == 5) {
            if (ImGui::InputFloat("End Rotation##text_exit", &animation.exit.end_rotation, 1.0f, 15.0f)) AutoSave();
            bool exit_fade = animation.exit.fade != 0;
            if (ImGui::Checkbox("Fade##text_exit", &exit_fade)) {
                animation.exit.fade = exit_fade ? 1 : 0;
                AutoSave();
            }
        }

        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();

        if (cue->bake_mode == 1) {
            if (ImGui::Button("Use Dynamic Render")) {
                cue->bake_mode = 0;
                AutoSave();
            }
        } else {
            if (ImGui::Button("Use Baked Sprite")) {
                cue->bake_mode = 1;
                AutoSave();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forced bake exports this cue as a PNG sprite, even for dynamic effects.");
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
        ImGui::Text("Curve Reuse:");
        if (editor->project->curve_count <= 0) {
            ImGui::TextDisabled("No curves available. Create one first.");
        } else {
            CurveTargetBinding targets[128] = {};
            const int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 128);
            static int text_target_idx = 0;
            static int text_curve_idx = 0;
            if (text_target_idx < 0 || text_target_idx >= target_count) text_target_idx = 0;
            if (text_curve_idx < 0) text_curve_idx = 0;
            if (text_curve_idx >= editor->project->curve_count) text_curve_idx = editor->project->curve_count - 1;

            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("Target Parameter##text_curve_reuse", &text_target_idx,
                         CurveTargetComboGetter, targets, target_count);

            char curve_preview[64] = {};
            BuildCurveDisplayLabel(editor, text_curve_idx, curve_preview, sizeof(curve_preview));
            if (ImGui::BeginCombo("Curve##text_curve_reuse", curve_preview)) {
                for (int i = 0; i < editor->project->curve_count; ++i) {
                    char item[64] = {};
                    BuildCurveDisplayLabel(editor, i, item, sizeof(item));
                    bool selected = (i == text_curve_idx);
                    if (ImGui::Selectable(item, selected)) text_curve_idx = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Assign Existing Curve##text_curve_reuse")) {
                *targets[text_target_idx].curve_field = text_curve_idx;
                AutoSave();
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit Assigned##text_curve_reuse")) {
                int idx = *targets[text_target_idx].curve_field;
                if (idx >= 0 && idx < editor->project->curve_count) {
                    editor->editing_curve_index = idx;
                    editor->editing_curve_cue_type = CueTypeText;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[text_target_idx].label);
                    editor->curve_editor_modal_request_open = true;
                }
            }
        }
        
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

void RenderScrollTextModal(EditorContext* editor) {
    if (!editor) return;

    if (editor->scroll_text_modal_request_open) {
        ImGui::OpenPopup("Scroll Text Settings");
        editor->scroll_text_modal_open = true;
        editor->scroll_text_modal_request_open = false;
    }

    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool scroll_popup_open = ImGui::BeginPopupModal("Scroll Text Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();

    if (scroll_popup_open) {
        ScrollTextCue* cue = &editor->editing_scroll_text;

        auto AutoSave = [&]() {
            if (editor->selected_scene_index >= 0 &&
                editor->selected_cue_index >= 0 &&
                editor->selected_cue_type == CueTypeScrollText) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->scroll_text_cue_count) {
                    scene->scroll_text_cues[editor->selected_cue_index] = *cue;
                    editor->project->modified = true;
                }
            }
        };

        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypeScrollText;
        RenderTriggerTimingAssignment(editor, "scroll_trigger_timing", &trigger_timing_modified);

        auto ShowCurveQuickButton = [&](const char* button_id, int* curve_field, float initial_value, const char* curve_label) {
            ImGui::SameLine();
            if (*curve_field >= 0) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", *curve_field);
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(button_id)) {
                if (editor->selected_scene_index >= 0 &&
                    editor->selected_cue_index >= 0 &&
                    editor->selected_cue_type == CueTypeScrollText) {
                    SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                    if (scene && editor->selected_cue_index < scene->scroll_text_cue_count) {
                        ScrollTextCue* actual_cue = &scene->scroll_text_cues[editor->selected_cue_index];
                        int* actual_field = nullptr;
                        if (curve_field == &cue->curve_x) actual_field = &actual_cue->curve_x;
                        else if (curve_field == &cue->curve_y) actual_field = &actual_cue->curve_y;
                        else if (curve_field == &cue->curve_speed) actual_field = &actual_cue->curve_speed;
                        else if (curve_field == &cue->curve_size) actual_field = &actual_cue->curve_size;
                        else if (curve_field == &cue->curve_rotation) actual_field = &actual_cue->curve_rotation;
                        else if (curve_field == &cue->curve_opacity) actual_field = &actual_cue->curve_opacity;
                        else if (curve_field == &cue->curve_color_r) actual_field = &actual_cue->curve_color_r;
                        else if (curve_field == &cue->curve_color_g) actual_field = &actual_cue->curve_color_g;
                        else if (curve_field == &cue->curve_color_b) actual_field = &actual_cue->curve_color_b;
                        else if (curve_field == &cue->curve_wave_amp) actual_field = &actual_cue->curve_wave_amp;
                        else if (curve_field == &cue->curve_wave_freq) actual_field = &actual_cue->curve_wave_freq;
                        else if (curve_field == &cue->curve_wave_length) actual_field = &actual_cue->curve_wave_length;
                        else if (curve_field == &cue->curve_jitter_amp) actual_field = &actual_cue->curve_jitter_amp;
                        else if (curve_field == &cue->curve_jitter_freq) actual_field = &actual_cue->curve_jitter_freq;

                        if (actual_field) {
                            if (*actual_field < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
                                *actual_field = editor->project->curve_count++;
                                auto& curve = editor->project->curves[*actual_field];
                                curve = rev::curve::CreateCurve(16);
                                rev::curve::AddPoint(curve, 0.0f, initial_value);
                                rev::curve::AddPoint(curve, 1.0f, initial_value);
                                editor->project->modified = true;
                            }

                            *curve_field = *actual_field;

                            if (*actual_field >= 0 && *actual_field < editor->project->curve_count) {
                                editor->editing_curve_index = *actual_field;
                                editor->editing_curve_cue_type = CueTypeScrollText;
                                snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", curve_label);
                                editor->curve_editor_modal_request_open = true;
                            }
                        }
                    }
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        };

        const char* style_names[] = {
            "Clean Ticker",
            "Terminal Green",
            "Neon Pulse",
            "Horror Glitch",
            "Ocean Wave",
            "Arcade Scanline"
        };

        ImGui::Text("Content:");
        if (ImGui::InputTextMultiline("##scroll_text", cue->text, sizeof(cue->text), ImVec2(460, 95))) AutoSave();

        ImGui::Separator();

        ImGui::Text("Font:");
        if (editor->installed_font_count > 0) {
            int current_font_index = -1;
            for (int i = 0; i < editor->installed_font_count; ++i) {
                if (strcmp(editor->installed_fonts[i], cue->font_name) == 0) {
                    current_font_index = i;
                    break;
                }
            }
            if (current_font_index < 0 && editor->installed_font_count > 0) {
                current_font_index = 0;
                strncpy_s(cue->font_name, sizeof(cue->font_name), editor->installed_fonts[0], _TRUNCATE);
            }
            const char* preview = (current_font_index >= 0) ? editor->installed_fonts[current_font_index] : "Select Font...";
            if (ImGui::BeginCombo("##scroll_fontpicker", preview)) {
                for (int i = 0; i < editor->installed_font_count; ++i) {
                    bool selected = (i == current_font_index);
                    if (ImGui::Selectable(editor->installed_fonts[i], selected)) {
                        strncpy_s(cue->font_name, sizeof(cue->font_name), editor->installed_fonts[i], _TRUNCATE);
                        AutoSave();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            if (ImGui::InputText("Font Name", cue->font_name, sizeof(cue->font_name))) AutoSave();
        }

        int style_idx = cue->style_id;
        if (style_idx < 0 || style_idx > 5) style_idx = 0;
        if (ImGui::Combo("Style Preset", &style_idx, style_names, 6)) {
            cue->style_id = style_idx;
            AutoSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Preset")) {
            cue->style_id = style_idx;
            switch (style_idx) {
                case 0:
                    cue->blend_mode = 0; cue->speed = 0.25f; cue->wave_amp = 0.0f; cue->wave_freq = 1.0f;
                    cue->jitter_amp = 0.0f; cue->jitter_freq = 1.0f; cue->glow = 0.0f; cue->shadow = 0.0f;
                    cue->outline = 0.0f; cue->chroma_shift = 0.0f; cue->distortion = 0.0f;
                    cue->color = {1.0f, 1.0f, 1.0f};
                    break;
                case 1:
                    cue->blend_mode = 0; cue->speed = 0.33f; cue->wave_amp = 0.004f; cue->wave_freq = 4.0f;
                    cue->jitter_amp = 0.003f; cue->jitter_freq = 20.0f; cue->glow = 0.2f; cue->shadow = 0.5f;
                    cue->outline = 0.25f; cue->chroma_shift = 0.0f; cue->distortion = 0.08f;
                    cue->color = {0.58f, 1.0f, 0.58f};
                    break;
                case 2:
                    cue->blend_mode = 1; cue->speed = 0.22f; cue->wave_amp = 0.012f; cue->wave_freq = 8.0f;
                    cue->jitter_amp = 0.002f; cue->jitter_freq = 6.0f; cue->glow = 0.8f; cue->shadow = 0.2f;
                    cue->outline = 0.5f; cue->chroma_shift = 0.02f; cue->distortion = 0.05f;
                    cue->color = {0.2f, 0.9f, 1.0f};
                    break;
                case 3:
                    cue->blend_mode = 1; cue->speed = 0.42f; cue->wave_amp = 0.018f; cue->wave_freq = 10.0f;
                    cue->jitter_amp = 0.015f; cue->jitter_freq = 30.0f; cue->glow = 0.9f; cue->shadow = 0.85f;
                    cue->outline = 0.75f; cue->chroma_shift = 0.035f; cue->distortion = 0.2f;
                    cue->color = {1.0f, 0.2f, 0.2f};
                    break;
                case 4:
                    cue->blend_mode = 0; cue->speed = 0.18f; cue->wave_amp = 0.03f; cue->wave_freq = 5.0f;
                    cue->jitter_amp = 0.0f; cue->jitter_freq = 1.0f; cue->glow = 0.35f; cue->shadow = 0.1f;
                    cue->outline = 0.15f; cue->chroma_shift = 0.01f; cue->distortion = 0.03f;
                    cue->color = {0.7f, 0.95f, 1.0f};
                    break;
                case 5:
                    cue->blend_mode = 1; cue->speed = 0.36f; cue->wave_amp = 0.002f; cue->wave_freq = 13.0f;
                    cue->jitter_amp = 0.006f; cue->jitter_freq = 17.0f; cue->glow = 0.55f; cue->shadow = 0.3f;
                    cue->outline = 0.65f; cue->chroma_shift = 0.03f; cue->distortion = 0.12f;
                    cue->color = {1.0f, 0.85f, 0.2f};
                    break;
            }
            AutoSave();
        }

        ImGui::Separator();

        const char* directions[] = {"Left", "Right", "Up", "Down"};
        const char* loop_modes[] = {"Loop", "Clamp"};
        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Direction", &cue->direction, directions, 4)) AutoSave();
        if (ImGui::Combo("Loop", &cue->loop_mode, loop_modes, 2)) AutoSave();
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();

        if (cue->bake_mode == 1) {
            if (ImGui::Button("Use Dynamic Render##scroll_bake")) {
                cue->bake_mode = 0;
                AutoSave();
            }
        } else {
            if (ImGui::Button("Use Baked Sprite##scroll_bake")) {
                cue->bake_mode = 1;
                AutoSave();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Bake scroll text into PNG for packed runtime builds.");
        }

        if (ImGui::SliderFloat("X", &cue->x, -0.5f, 1.5f)) AutoSave();
        ShowCurveQuickButton("+##curve_scroll_x", &cue->curve_x, cue->x, "Scroll X Position");
        if (ImGui::SliderFloat("Y", &cue->y, -0.5f, 1.5f)) AutoSave();
        ShowCurveQuickButton("+##curve_scroll_y", &cue->curve_y, cue->y, "Scroll Y Position");
        if (ImGui::SliderFloat("Size", &cue->size, 8.0f, 128.0f)) AutoSave();
        ShowCurveQuickButton("+##curve_scroll_size", &cue->curve_size, cue->size, "Scroll Size");
        if (ImGui::SliderFloat("Rotation", &cue->rotation, -360.0f, 360.0f)) {
            cue->curve_rotation = -1;
            AutoSave();
        }
        ShowCurveQuickButton("+##curve_scroll_rotation", &cue->curve_rotation, cue->rotation, "Scroll Rotation");
        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        ShowCurveQuickButton("+##curve_scroll_opacity", &cue->curve_opacity, cue->opacity, "Scroll Opacity");
        if (ImGui::SliderFloat("Speed", &cue->speed, -2.0f, 2.0f)) AutoSave();
        ShowCurveQuickButton("+##curve_scroll_speed", &cue->curve_speed, cue->speed, "Scroll Speed");
        if (ImGui::SliderFloat("Wrap Gap", &cue->wrap_gap, 0.0f, 1.0f)) AutoSave();
        if (ImGui::SliderFloat("Spacing", &cue->spacing, 0.6f, 3.0f)) AutoSave();
        if (ImGui::SliderFloat("Slant", &cue->slant_deg, -35.0f, 35.0f)) AutoSave();
        if (ImGui::ColorEdit3("Color", &cue->color.r)) AutoSave();
        ImGui::Text("Color Curves:");
        ImGui::Text("R:");
        ShowCurveQuickButton("+##curve_scroll_r", &cue->curve_color_r, cue->color.r, "Scroll Color R");
        ImGui::SameLine();
        ImGui::Text("G:");
        ShowCurveQuickButton("+##curve_scroll_g", &cue->curve_color_g, cue->color.g, "Scroll Color G");
        ImGui::SameLine();
        ImGui::Text("B:");
        ShowCurveQuickButton("+##curve_scroll_b", &cue->curve_color_b, cue->color.b, "Scroll Color B");

        if (ImGui::CollapsingHeader("Advanced FX", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Wave Amp", &cue->wave_amp, 0.0f, 0.08f)) AutoSave();
            ShowCurveQuickButton("+##curve_scroll_wave_amp", &cue->curve_wave_amp, cue->wave_amp, "Scroll Wave Amp");
            if (ImGui::SliderFloat("Wave Freq", &cue->wave_freq, 0.1f, 30.0f)) AutoSave();
            ShowCurveQuickButton("+##curve_scroll_wave_freq", &cue->curve_wave_freq, cue->wave_freq, "Scroll Wave Freq");
            if (ImGui::SliderFloat("Wave Length", &cue->wave_length, 2.0f, 64.0f, "%.1f glyphs")) AutoSave();
            ShowCurveQuickButton("+##curve_scroll_wave_length", &cue->curve_wave_length, cue->wave_length, "Scroll Wave Length");
            if (ImGui::SliderFloat("Jitter Amp", &cue->jitter_amp, 0.0f, 0.08f)) AutoSave();
            ShowCurveQuickButton("+##curve_scroll_jitter_amp", &cue->curve_jitter_amp, cue->jitter_amp, "Scroll Jitter Amp");
            if (ImGui::SliderFloat("Jitter Freq", &cue->jitter_freq, 0.1f, 40.0f)) AutoSave();
            ShowCurveQuickButton("+##curve_scroll_jitter_freq", &cue->curve_jitter_freq, cue->jitter_freq, "Scroll Jitter Freq");
            if (ImGui::SliderFloat("Glow", &cue->glow, 0.0f, 1.0f)) AutoSave();
            if (ImGui::SliderFloat("Shadow", &cue->shadow, 0.0f, 1.0f)) AutoSave();
            if (ImGui::SliderFloat("Outline", &cue->outline, 0.0f, 1.0f)) AutoSave();
            if (ImGui::SliderFloat("Chroma Shift", &cue->chroma_shift, 0.0f, 0.06f)) AutoSave();
            if (ImGui::SliderFloat("Distortion", &cue->distortion, 0.0f, 0.35f)) AutoSave();
        }

        ImGui::Separator();
        ImGui::Text("Timing (seconds):");
        if (ImGui::InputFloat("Start", &cue->cue_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade In Start",  &cue->fade_in_start,  0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade In End",    &cue->fade_in_end,    0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade Out Start", &cue->fade_out_start, 0.1f, 1.0f)) AutoSave();
        if (ImGui::InputFloat("Fade Out End",   &cue->fade_out_end,   0.1f, 1.0f)) AutoSave();
        if (ImGui::SliderInt("Layer", &cue->layer_order, -10, 10)) AutoSave();

        ImGui::Separator();
        ImGui::Text("Curve Reuse:");
        if (editor->project->curve_count <= 0) {
            ImGui::TextDisabled("No curves available. Create one first.");
        } else {
            CurveTargetBinding targets[128] = {};
            const int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 128);
            static int target_idx = 0;
            static int curve_idx = 0;
            if (target_idx < 0 || target_idx >= target_count) target_idx = 0;
            if (curve_idx < 0) curve_idx = 0;
            if (curve_idx >= editor->project->curve_count) curve_idx = editor->project->curve_count - 1;

            ImGui::SetNextItemWidth(240.0f);
            ImGui::Combo("Target##scroll_curve_reuse", &target_idx,
                         CurveTargetComboGetter, targets, target_count);

            char curve_preview[64] = {};
            BuildCurveDisplayLabel(editor, curve_idx, curve_preview, sizeof(curve_preview));
            if (ImGui::BeginCombo("Curve##scroll_curve_reuse", curve_preview)) {
                for (int i = 0; i < editor->project->curve_count; ++i) {
                    char item[64] = {};
                    BuildCurveDisplayLabel(editor, i, item, sizeof(item));
                    bool selected = (i == curve_idx);
                    if (ImGui::Selectable(item, selected)) curve_idx = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Assign Existing Curve##scroll_curve_reuse")) {
                *targets[target_idx].curve_field = curve_idx;
                AutoSave();
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit Assigned##scroll_curve_reuse")) {
                int idx = *targets[target_idx].curve_field;
                if (idx >= 0 && idx < editor->project->curve_count) {
                    editor->editing_curve_index = idx;
                    editor->editing_curve_cue_type = CueTypeScrollText;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[target_idx].label);
                    editor->curve_editor_modal_request_open = true;
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(240, 0))) {
            editor->scroll_text_modal_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        if (editor->scroll_text_modal_open) {
            editor->scroll_text_modal_open = false;
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

                    if (mesh) {
                        mesh->has_imported_light = ir->has_light;
                        mesh->imported_light_pos[0] = ir->light_pos[0];
                        mesh->imported_light_pos[1] = ir->light_pos[1];
                        mesh->imported_light_pos[2] = ir->light_pos[2];
                        mesh->emissive_color[0] = ir->material.emissive[0];
                        mesh->emissive_color[1] = ir->material.emissive[1];
                        mesh->emissive_color[2] = ir->material.emissive[2];
                        mesh->emissive_strength = ir->material.emissive_strength;
                    }
                    
                    // Load texture if present
                    if (mesh && ir->material.base_color_texture[0]) {
                        rev::runtime::ImageTexture tex = {};
                        if (rev::runtime::LoadImageTexture(ir->material.base_color_texture, &tex)) {
                            mesh->base_color_texture = tex.texture_id;
                        }
                    }

                    // Load per-material textures and map to mesh slots.
                    if (mesh && ir->material_count > 0 && ir->materials) {
                        unsigned int* material_textures = new unsigned int[ir->material_count];
                        memset(material_textures, 0, sizeof(unsigned int) * ir->material_count);
                        for (int mat_i = 0; mat_i < ir->material_count; ++mat_i) {
                            const char* tex_path = ir->materials[mat_i].base_color_texture;
                            if (!tex_path[0]) continue;
                            rev::runtime::ImageTexture tex = {};
                            if (rev::runtime::LoadImageTexture(tex_path, &tex)) {
                                material_textures[mat_i] = tex.texture_id;
                            }
                        }
                        for (uint32_t si = 0; si < mesh->material_slot_count; ++si) {
                            rev::mesh::MaterialSlot& slot = mesh->material_slots[si];
                            if (slot.material_index >= 0 && slot.material_index < ir->material_count) {
                                slot.base_color_texture = material_textures[slot.material_index];
                            }
                        }
                        delete[] material_textures;
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

        bool trigger_timing_modified = false;
        if (editor->selected_cue_type == CueTypeMesh &&
            editor->selected_scene_index >= 0 &&
            editor->selected_scene_index < editor->project->scene_count &&
            editor->selected_cue_index >= 0 &&
            editor->selected_cue_index < editor->project->scenes[editor->selected_scene_index].mesh_cue_count) {
            editor->editing_curve_cue_type = CueTypeMesh;
            RenderTriggerTimingAssignment(editor, "mesh_trigger_timing", &trigger_timing_modified);
        }

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
                           cue->emissive_color[0] = 1.0f;
                           cue->emissive_color[1] = 1.0f;
                           cue->emissive_color[2] = 1.0f;
                           if (cue->emissive_strength <= 0.0f) cue->emissive_strength = 1.0f;
                        printf("[GLTF] Material: name=\"%s\" base=(%.2f,%.2f,%.2f) metallic=%.2f roughness=%.2f\n",
                               mat.name[0] ? mat.name : "(unnamed)",
                               mat.base_color[0], mat.base_color[1], mat.base_color[2],
                               mat.metallic, mat.roughness);
                           printf("[GLTF] Emissive: color=(%.2f,%.2f,%.2f) strength=%.2f\n",
                               mat.emissive[0], mat.emissive[1], mat.emissive[2], mat.emissive_strength);
                        if (mat.base_color_texture[0]) {
                            printf("[GLTF] Base color texture: %s\n", mat.base_color_texture);
                        } else {
                            printf("[GLTF] No base color texture extracted\n");
                        }
                    }
                    if (ir) rev::gltf::FreeImportResult(ir);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload##gltf")) {
                ReloadEditorAssets(editor);
            }
            if (cue->asset_path[0])
                ImGui::TextDisabled("%s", cue->asset_path);
            else
                ImGui::TextDisabled("No file selected");
            ImGui::TextDisabled("mesh_size / mesh_param not used for external meshes");

            bool use_imported_light = cue->use_imported_light != 0;
            if (ImGui::Checkbox("Use glTF Light", &use_imported_light)) {
                cue->use_imported_light = use_imported_light ? 1 : 0;
                AutoSave();
            }
            bool use_imported_camera = cue->use_imported_camera != 0;
            if (ImGui::Checkbox("Use glTF Camera", &use_imported_camera)) {
                cue->use_imported_camera = use_imported_camera ? 1 : 0;
                AutoSave();
            }
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
                        if (actual_cue->curve_mesh_size < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                            editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_pos_x < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_pos_y < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_pos_z < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_rot_x < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_rot_y < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_rot_z < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_scale_x < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_scale_y < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_scale_z < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_color_r < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_color_g < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_color_b < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
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
                    if (actual_cue->curve_color_a < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Color A");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit A curve");
        
        if (ImGui::SliderFloat("Metallic",  &cue->metallic,  0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_metallic, "Mesh Metallic", nullptr, "mesh_metallic");
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
                    if (actual_cue->curve_metallic < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Metallic");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");
        
        if (ImGui::SliderFloat("Roughness", &cue->roughness, 0.0f, 1.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_roughness, "Mesh Roughness", nullptr, "mesh_roughness");
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
                    if (actual_cue->curve_roughness < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
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
                        editor->editing_curve_cue_type = CueTypeMesh;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Roughness");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");

        if (ImGui::ColorEdit3("Emissive Tint", cue->emissive_color)) AutoSave();
        if (ImGui::SliderFloat("Emissive Strength", &cue->emissive_strength, 0.0f, 10.0f)) AutoSave();

        if (ImGui::SliderFloat("Camera FOV", &cue->fov_deg, 10.0f, 120.0f)) AutoSave();
        DrawCurveRecordButton(editor, &cue->curve_fov, "Mesh Camera FOV", nullptr, "mesh_fov");
        ImGui::SameLine();
        if (cue->curve_fov >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[C%d]", cue->curve_fov);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("+##curve_mesh_fov")) {
            if (editor->selected_scene_index >= 0 && editor->selected_cue_index >= 0) {
                SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
                if (scene && editor->selected_cue_index < scene->mesh_cue_count) {
                    MeshCue* actual_cue = &scene->mesh_cues[editor->selected_cue_index];
                    if (actual_cue->curve_fov < 0 && editor->project->curve_count < rev::runtime::kMaxCurves) {
                        actual_cue->curve_fov = editor->project->curve_count++;
                        auto& curve = editor->project->curves[actual_cue->curve_fov];
                        curve = rev::curve::CreateCurve(16);
                        rev::curve::AddPoint(curve, 0.0f, actual_cue->fov_deg);
                        rev::curve::AddPoint(curve, 1.0f, actual_cue->fov_deg);
                        editor->project->modified = true;
                    }
                    cue->curve_fov = actual_cue->curve_fov;
                    if (actual_cue->curve_fov >= 0 && actual_cue->curve_fov < editor->project->curve_count) {
                        editor->editing_curve_index = actual_cue->curve_fov;
                        editor->editing_curve_cue_type = CueTypeMesh;
                        snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "Mesh Camera FOV");
                        editor->curve_editor_modal_request_open = true;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add/edit animation curve");

        const char* cull_mode_names[] = { "Off", "Back", "Front" };
        if (ImGui::Combo("Face Culling", &cue->cull_mode, cull_mode_names, 3)) AutoSave();

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
        ImGui::Text("Curve Reuse:");
        if (editor->project->curve_count <= 0) {
            ImGui::TextDisabled("No curves available. Create one first.");
        } else {
            CurveTargetBinding targets[128] = {};
            const int target_count = BuildCurveTargetsForCurrentCue(editor, targets, 128);
            static int mesh_target_idx = 0;
            static int mesh_curve_idx = 0;
            if (mesh_target_idx < 0 || mesh_target_idx >= target_count) mesh_target_idx = 0;
            if (mesh_curve_idx < 0) mesh_curve_idx = 0;
            if (mesh_curve_idx >= editor->project->curve_count) mesh_curve_idx = editor->project->curve_count - 1;

            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("Target Parameter##mesh_curve_reuse", &mesh_target_idx,
                         CurveTargetComboGetter, targets, target_count);

            char curve_preview[64] = {};
            BuildCurveDisplayLabel(editor, mesh_curve_idx, curve_preview, sizeof(curve_preview));
            if (ImGui::BeginCombo("Curve##mesh_curve_reuse", curve_preview)) {
                for (int i = 0; i < editor->project->curve_count; ++i) {
                    char item[64] = {};
                    BuildCurveDisplayLabel(editor, i, item, sizeof(item));
                    bool selected = (i == mesh_curve_idx);
                    if (ImGui::Selectable(item, selected)) mesh_curve_idx = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Assign Existing Curve##mesh_curve_reuse")) {
                *targets[mesh_target_idx].curve_field = mesh_curve_idx;
                AutoSave();
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit Assigned##mesh_curve_reuse")) {
                int idx = *targets[mesh_target_idx].curve_field;
                if (idx >= 0 && idx < editor->project->curve_count) {
                    editor->editing_curve_index = idx;
                    editor->editing_curve_cue_type = CueTypeMesh;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", targets[mesh_target_idx].label);
                    editor->curve_editor_modal_request_open = true;
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

void RenderPixelModal(EditorContext* editor) {
    if (!editor) return;

    if (editor->pixel_modal_request_open) {
        ImGui::OpenPopup("Pixel Cue Settings");
        editor->pixel_modal_open = true;
        editor->pixel_modal_request_open = false;
    }

    if (ImGui::BeginPopupModal("Pixel Cue Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        PixelCue* cue = &editor->editing_pixel;

        auto AutoSave = [&]() {
            if (editor->selected_scene_index < 0 || editor->selected_cue_index < 0 ||
                editor->selected_cue_type != CueTypePixel) return;
            SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
            if (scene && editor->selected_cue_index < scene->pixel_cue_count) {
                scene->pixel_cues[editor->selected_cue_index] = *cue;
                editor->project->modified = true;
            }
        };

        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypePixel;
        RenderTriggerTimingAssignment(editor, "pixel_trigger_timing", &trigger_timing_modified);

        ImGui::Text("Indexed Pixel Asset");
        if (ImGui::InputText("Asset Key", cue->asset_key, sizeof(cue->asset_key))) AutoSave();
        ImGui::SameLine();
        if (ImGui::Button("Browse##pixel")) {
            OPENFILENAMEA ofn = {};
            char filepath[512] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = editor->window ? (HWND)editor->window->hwnd : nullptr;
            ofn.lpstrFile = filepath;
            ofn.nMaxFile = sizeof(filepath);
            ofn.lpstrFilter = "Images and Pixel Animations\0*.pix;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0Pixel Animations\0*.pix\0Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrInitialDir = editor->project->assets_path[0]
                ? editor->project->assets_path : "assets";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn)) {
                const char* filename = strrchr(filepath, '\\');
                if (!filename) filename = strrchr(filepath, '/');
                if (filename) ++filename; else filename = filepath;

                const char* extension = strrchr(filename, '.');
                bool source_is_pix = extension && _stricmp(extension, ".pix") == 0;
                char output_name[256] = {};
                if (source_is_pix) {
                    strncpy_s(output_name, filename, _TRUNCATE);
                } else {
                    size_t stem_length = extension
                        ? (size_t)(extension - filename) : strlen(filename);
                    if (stem_length >= sizeof(output_name) - 5) stem_length = sizeof(output_name) - 6;
                    memcpy(output_name, filename, stem_length);
                    strcpy_s(output_name + stem_length, sizeof(output_name) - stem_length, ".pix");
                }

                char destination[512] = {};
                if (editor->project->assets_path[0]) {
                    snprintf(destination, sizeof(destination), "%s\\%s",
                             editor->project->assets_path, output_name);
                } else {
                    const char* last_slash = strrchr(filepath, '\\');
                    if (!last_slash) last_slash = strrchr(filepath, '/');
                    size_t directory_length = last_slash
                        ? (size_t)(last_slash - filepath + 1) : 0;
                    if (directory_length >= sizeof(destination) - sizeof(output_name)) {
                        printf("[PIXEL] Source path is too long.\n");
                        destination[0] = '\0';
                    } else {
                        memcpy(destination, filepath, directory_length);
                        strcpy_s(destination + directory_length,
                                 sizeof(destination) - directory_length, output_name);
                    }
                }

                bool converted = destination[0] != '\0' && (source_is_pix
                    ? (CopyFileA(filepath, destination, FALSE) || GetLastError() == ERROR_FILE_EXISTS)
                    : ConvertImageToPixelAnimation(filepath, destination));
                if (!converted) {
                    printf("[PIXEL] Could not create asset %s (err=%lu)\n",
                           destination, GetLastError());
                } else {
                    strncpy_s(cue->asset_key, output_name, _TRUNCATE);
                    cue->asset_path[0] = '\0';
                    AutoSave();
                }
            }
        }
        if (ImGui::InputText("Asset Path", cue->asset_path, sizeof(cue->asset_path))) AutoSave();

        bool asset_shaders_modified = false;
        RenderAssetShaders(editor, cue->shaders, &cue->shader_count,
                   &asset_shaders_modified, "pixel_shaders", CueTypePixel,
                   editor->selected_cue_index);
        if (asset_shaders_modified) AutoSave();

        bool layer_effects_modified = false;
        RenderLayerPostEffects(editor, cue->post_effects, &cue->post_effect_count,
                       &layer_effects_modified, "pixel", CueTypePixel,
                       editor->selected_cue_index);
        if (layer_effects_modified) AutoSave();

        static int pixel_target_index = 0;
        static int pixel_curve_index = 0;
        RenderCurveReuseSection(editor, "pixel_curve_reuse", CueTypePixel,
                    &pixel_target_index, &pixel_curve_index);

        ImGui::Separator();
        if (ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f)) AutoSave();
        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        if (ImGui::SliderFloat("Scale", &cue->scale, 0.1f, 16.0f)) AutoSave();
        if (ImGui::SliderFloat("Rotation", &cue->rotation, -360.0f, 360.0f)) AutoSave();
        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        bool snap_to_pixels = cue->snap_to_pixels != 0;
        if (ImGui::Checkbox("Snap To Pixels", &snap_to_pixels)) {
            cue->snap_to_pixels = snap_to_pixels ? 1 : 0;
            AutoSave();
        }

        ImGui::Separator();
        if (ImGui::InputFloat("FPS", &cue->fps, 0.5f, 1.0f)) {
            if (cue->fps < 0.1f) cue->fps = 0.1f;
            AutoSave();
        }
        const char* playback_modes[] = {"Loop", "Once", "PingPong"};
        if (ImGui::Combo("Playback", &cue->playback_mode, playback_modes, 3)) AutoSave();
        if (ImGui::InputInt("Start Frame", &cue->start_frame)) AutoSave();
        if (ImGui::InputInt("Palette Offset", &cue->palette_offset)) AutoSave();
        if (ImGui::InputInt("Palette Cycle Speed", &cue->palette_cycle_speed)) AutoSave();
        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();
        if (ImGui::SliderInt("Layer", &cue->layer_order, -10, 10)) AutoSave();

        ImGui::Separator();
        if (ImGui::InputFloat("Start", &cue->cue_start)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end)) AutoSave();

        if (ImGui::Button("Close", ImVec2(240, 0))) {
            editor->pixel_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (editor->pixel_modal_open) {
        editor->pixel_modal_open = false;
    }
}

void RenderPixelEmitterModal(EditorContext* editor) {
    if (!editor) return;

    if (editor->pixel_emitter_modal_request_open) {
        ImGui::OpenPopup("Pixel Emitter Settings");
        editor->pixel_emitter_modal_open = true;
        editor->pixel_emitter_modal_request_open = false;
    }

    if (ImGui::BeginPopupModal("Pixel Emitter Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        PixelEmitterCue* cue = &editor->editing_pixel_emitter;
        auto AutoSave = [&]() {
            if (editor->selected_scene_index < 0 || editor->selected_cue_index < 0 ||
                editor->selected_cue_type != CueTypePixelEmitter) return;
            SceneBlock* scene = GetScene(editor, editor->selected_scene_index);
            if (scene && editor->selected_cue_index < scene->pixel_emitter_cue_count) {
                scene->pixel_emitter_cues[editor->selected_cue_index] = *cue;
                editor->project->modified = true;
            }
        };
        bool trigger_timing_modified = false;
        editor->editing_curve_cue_type = CueTypePixelEmitter;
        RenderTriggerTimingAssignment(editor, "pixel_emitter_trigger_timing", &trigger_timing_modified);
        if (trigger_timing_modified) AutoSave();
        auto CurveControl = [&](const char* target_label, int* curve_field, float base_value) {
            if (!curve_field) return;
            ImGui::SameLine();
            ImGui::PushID(target_label);
            bool assigned = *curve_field >= 0;
            if (ImGui::SmallButton(assigned ? "Edit" : "+")) {
                if (!assigned) {
                    int curve_index = AcquireCurveSlot(editor, base_value, nullptr);
                    if (curve_index >= 0) {
                        *curve_field = curve_index;
                        AutoSave();
                    }
                }
                if (*curve_field >= 0) {
                    editor->editing_curve_cue_type = CueTypePixelEmitter;
                    editor->editing_curve_field = -1;
                    editor->editing_curve_index = *curve_field;
                    snprintf(editor->editing_curve_label, sizeof(editor->editing_curve_label), "%s", target_label);
                    editor->curve_editor_modal_request_open = true;
                }
            }
            if (*curve_field >= 0) {
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::SmallButton("X")) {
                    *curve_field = -1;
                    AutoSave();
                }
            }
            DrawCurveRecordButton(editor, curve_field, target_label, nullptr, target_label);
            ImGui::PopID();
        };

        ImGui::Text("Pixel Particle Emitter");
        const char* source_modes[] = {"Image Asset", "Basic Shape"};
        if (cue->visual_source < 0 || cue->visual_source > 1) cue->visual_source = 1;
        if (ImGui::Combo("Visual Source", &cue->visual_source, source_modes, 2)) AutoSave();

        if (cue->visual_source == 0) {
            if (ImGui::InputText("Asset Key", cue->asset_key, sizeof(cue->asset_key))) AutoSave();
            ImGui::SameLine();
            if (ImGui::Button("Browse##pixel_emitter")) {
                OPENFILENAMEA ofn = {};
                char filepath[512] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = editor->window ? (HWND)editor->window->hwnd : nullptr;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.gif;*.tif;*.tiff\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrInitialDir = editor->project->assets_path[0]
                    ? editor->project->assets_path : "assets";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameA(&ofn)) {
                    const char* filename = strrchr(filepath, '\\');
                    if (!filename) filename = strrchr(filepath, '/');
                    if (filename) ++filename; else filename = filepath;
                    strncpy_s(cue->asset_key, filename, _TRUNCATE);
                    cue->asset_path[0] = '\0';
                    if (editor->project->assets_path[0]) {
                        char destination[512] = {};
                        snprintf(destination, sizeof(destination), "%s\\%s",
                                 editor->project->assets_path, filename);
                        if (!CopyFileA(filepath, destination, FALSE) && GetLastError() != ERROR_FILE_EXISTS) {
                            printf("[PIXEL EMITTER] Warning: could not copy asset to %s (err=%lu)\n",
                                   destination, GetLastError());
                        }
                    }
                    AutoSave();
                }
            }
            if (ImGui::InputText("Asset Path", cue->asset_path, sizeof(cue->asset_path))) AutoSave();
            ImGui::TextDisabled("Use PNG, JPEG, WebP, GIF, TIFF, BMP, or another supported image format.");
        } else {
            const char* shape_names[] = {"Square", "Circle", "Triangle", "Diamond"};
            if (cue->primitive_shape < 0 || cue->primitive_shape > 3) cue->primitive_shape = 1;
            if (ImGui::Combo("Shape", &cue->primitive_shape, shape_names, 4)) AutoSave();
            if (ImGui::ColorEdit4("Shape Color", cue->primitive_color)) AutoSave();
        }

        ImGui::Separator();
        if (ImGui::SliderFloat("X", &cue->x, 0.0f, 1.0f)) AutoSave();
        CurveControl("Emitter X Position", &cue->curve_x, cue->x);
        if (ImGui::SliderFloat("Y", &cue->y, 0.0f, 1.0f)) AutoSave();
        CurveControl("Emitter Y Position", &cue->curve_y, cue->y);
        if (ImGui::SliderFloat("Scale", &cue->scale, 0.05f, 16.0f)) AutoSave();
        CurveControl("Emitter Scale", &cue->curve_scale, cue->scale);
        if (ImGui::SliderFloat("Rotation", &cue->rotation, -360.0f, 360.0f)) AutoSave();
        CurveControl("Emitter Rotation", &cue->curve_rotation, cue->rotation);
        if (ImGui::SliderFloat("Opacity", &cue->opacity, 0.0f, 1.0f)) AutoSave();
        CurveControl("Emitter Opacity", &cue->curve_opacity, cue->opacity);

        ImGui::Separator();
        if (ImGui::InputInt("Max Particles", &cue->max_particles)) {
            if (cue->max_particles < 1) cue->max_particles = 1;
            AutoSave();
        }
        if (ImGui::InputFloat("Emission Rate", &cue->emission_rate)) {
            if (cue->emission_rate < 0.0f) cue->emission_rate = 0.0f;
            AutoSave();
        }
        CurveControl("Emission Rate", &cue->curve_emission_rate, cue->emission_rate);
        if (ImGui::InputInt("Burst Count", &cue->burst_count)) {
            if (cue->burst_count < 0) cue->burst_count = 0;
            AutoSave();
        }
        if (ImGui::InputFloat("Particle Duration", &cue->duration)) AutoSave();
        bool loop = cue->loop != 0;
        if (ImGui::Checkbox("Loop", &loop)) {
            cue->loop = loop ? 1 : 0;
            AutoSave();
        }
        if (ImGui::SliderFloat("Speed Min", &cue->speed_min, 0.0f, 2.0f)) AutoSave();
        CurveControl("Speed Min", &cue->curve_speed_min, cue->speed_min);
        if (ImGui::SliderFloat("Speed Max", &cue->speed_max, 0.0f, 2.0f)) AutoSave();
        CurveControl("Speed Max", &cue->curve_speed_max, cue->speed_max);
        if (ImGui::SliderFloat("Lifetime Min", &cue->lifetime_min, 0.01f, 10.0f)) AutoSave();
        CurveControl("Lifetime Min", &cue->curve_lifetime_min, cue->lifetime_min);
        if (ImGui::SliderFloat("Lifetime Max", &cue->lifetime_max, 0.01f, 10.0f)) AutoSave();
        CurveControl("Lifetime Max", &cue->curve_lifetime_max, cue->lifetime_max);
        if (ImGui::SliderFloat("Scale Min", &cue->scale_min, 0.01f, 4.0f)) AutoSave();
        CurveControl("Particle Scale Min", &cue->curve_scale_min, cue->scale_min);
        if (ImGui::SliderFloat("Scale Max", &cue->scale_max, 0.01f, 4.0f)) AutoSave();
        CurveControl("Particle Scale Max", &cue->curve_scale_max, cue->scale_max);

        const char* blend_modes[] = {"Alpha", "Add", "Multiply", "Screen"};
        if (ImGui::Combo("Blend", &cue->blend_mode, blend_modes, 4)) AutoSave();
        if (ImGui::SliderInt("Layer", &cue->layer_order, -10, 10)) AutoSave();
        if (ImGui::InputFloat("Start", &cue->cue_start)) AutoSave();
        if (ImGui::InputFloat("End", &cue->cue_end)) AutoSave();

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(240, 0))) {
            editor->pixel_emitter_modal_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (editor->pixel_emitter_modal_open) {
        editor->pixel_emitter_modal_open = false;
    }
}

} // namespace editor
} // namespace rev


