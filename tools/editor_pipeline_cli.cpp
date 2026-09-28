#include "rev_editor.h"

#include <cstdio>
#include <cstring>
#include <windows.h>
#include <gdiplus.h>

namespace {

const char* LeafName(const char* path) {
    const char* back = path ? std::strrchr(path, '\\') : nullptr;
    const char* forward = path ? std::strrchr(path, '/') : nullptr;
    const char* leaf = back;
    if (forward && (!leaf || forward > leaf)) leaf = forward;
    return leaf ? leaf + 1 : (path ? path : "");
}

void MakeProjectAssetPath(char* path, size_t size, const char* key) {
    std::snprintf(path, size, "project_assets/%s", LeafName(key));
}

void DisableCurves(rev::runtime::PixelCue* cue) {
    cue->curve_x = cue->curve_y = cue->curve_scale = cue->curve_rotation = -1;
    cue->curve_opacity = cue->curve_frame = cue->curve_palette_offset = -1;
}

void DisableCurves(rev::runtime::PixelEmitterCue* cue) {
    cue->curve_x = cue->curve_y = cue->curve_scale = cue->curve_rotation = -1;
    cue->curve_opacity = cue->curve_emission_rate = -1;
    cue->curve_speed_min = cue->curve_speed_max = -1;
    cue->curve_lifetime_min = cue->curve_lifetime_max = -1;
    cue->curve_scale_min = cue->curve_scale_max = -1;
}

void DisableCurves(rev::runtime::ScrollTextCue* cue) {
    cue->curve_x = cue->curve_y = cue->curve_speed = cue->curve_size = -1;
    cue->curve_rotation = cue->curve_opacity = -1;
    cue->curve_color_r = cue->curve_color_g = cue->curve_color_b = -1;
    cue->curve_wave_amp = cue->curve_wave_freq = cue->curve_wave_length = -1;
    cue->curve_jitter_amp = cue->curve_jitter_freq = -1;
}

void NormalizeAssetPaths(rev::editor::ProjectData* project) {
    for (int s = 0; s < project->scene_count; ++s) {
        rev::editor::SceneBlock& scene = project->scenes[s];
        for (int i = 0; i < scene.image_cue_count; ++i)
            MakeProjectAssetPath(scene.image_cues[i].asset_path,
                                 sizeof(scene.image_cues[i].asset_path),
                                 scene.image_cues[i].asset_key);
        for (int i = 0; i < scene.music_cue_count; ++i)
            MakeProjectAssetPath(scene.music_cues[i].asset_path,
                                 sizeof(scene.music_cues[i].asset_path),
                                 scene.music_cues[i].asset_key);
        for (int i = 0; i < scene.mesh_cue_count; ++i) {
            if (scene.mesh_cues[i].mesh_type == 4) {
                char leaf[128] = {};
                strncpy_s(leaf, LeafName(scene.mesh_cues[i].asset_path), _TRUNCATE);
                MakeProjectAssetPath(scene.mesh_cues[i].asset_path,
                                     sizeof(scene.mesh_cues[i].asset_path), leaf);
            }
        }
        for (int i = 0; i < scene.text_cue_count; ++i) {
            scene.text_cues[i].baked_asset_key[0] = '\0';
            scene.text_cues[i].baked_asset_path[0] = '\0';
            scene.text_cues[i].glyph_atlas_key[0] = '\0';
            scene.text_cues[i].glyph_atlas_path[0] = '\0';
            scene.text_cues[i].glyph_meta_key[0] = '\0';
            scene.text_cues[i].glyph_meta_path[0] = '\0';
        }
    }
}

void AddMissingCueFamilies(rev::editor::ProjectData* project) {
    rev::editor::SceneBlock* scene = &project->scenes[0];

    rev::runtime::PixelCue pixel = {};
    strncpy_s(pixel.asset_key, "white.png", _TRUNCATE);
    MakeProjectAssetPath(pixel.asset_path, sizeof(pixel.asset_path), pixel.asset_key);
    pixel.x = pixel.y = 0.5f; pixel.scale = pixel.opacity = 1.0f;
    pixel.cue_end = 1.0f; pixel.fps = 1.0f; pixel.snap_to_pixels = 1;
    DisableCurves(&pixel);
    rev::editor::AddPixelCue(scene, pixel);

    rev::runtime::PixelEmitterCue particles = {};
    strncpy_s(particles.asset_key, "primitive_particles", _TRUNCATE);
    particles.visual_source = 1; particles.primitive_shape = 1;
    particles.primitive_color[0] = particles.primitive_color[1] =
        particles.primitive_color[2] = particles.primitive_color[3] = 1.0f;
    particles.x = particles.y = 0.5f; particles.scale = particles.opacity = 1.0f;
    particles.cue_end = particles.duration = 1.0f; particles.loop = 1;
    particles.max_particles = 16; particles.emission_rate = 4.0f;
    particles.speed_min = 0.1f; particles.speed_max = 0.2f;
    particles.lifetime_min = 0.5f; particles.lifetime_max = 1.0f;
    particles.scale_min = 0.5f; particles.scale_max = 1.0f;
    particles.direction_x = 1.0f; particles.seed = 1;
    DisableCurves(&particles);
    rev::editor::AddPixelEmitterCue(scene, particles);

    rev::runtime::ScrollTextCue scroll = {};
    strncpy_s(scroll.text, "HiMYM regression scroll", _TRUNCATE);
    strncpy_s(scroll.font_name, "Arial", _TRUNCATE);
    scroll.x = scroll.y = 0.5f; scroll.size = 20.0f;
    scroll.color = {1.0f, 1.0f, 1.0f}; scroll.opacity = 1.0f;
    scroll.cue_end = 1.0f; scroll.speed = 0.2f; scroll.spacing = 1.0f;
    scroll.wrap_gap = 0.2f; scroll.wave_length = 8.0f;
    DisableCurves(&scroll);
    rev::editor::AddScrollTextCue(scene, scroll);

    rev::editor::PostEffect effect = {};
    effect.type = rev::editor::PostEffectVignette; effect.enabled = true;
    effect.intensity = 0.5f; effect.end_time = 1.0f;
    effect.color[0] = effect.color[1] = effect.color[2] = effect.color[3] = 1.0f;
    effect.curve_intensity = effect.curve_threshold = effect.curve_radius = -1;
    effect.curve_color_r = effect.curve_color_g = effect.curve_color_b = -1;
    effect.curve_color_a = effect.curve_amount = effect.trigger_track = -1;
    rev::editor::AddPostEffect(scene, effect);

    rev::runtime::ShaderPipeline& pipeline = project->shader_pipelines[0];
    rev::runtime::InitializeShaderPipeline(&pipeline);
    strncpy_s(pipeline.name, "Regression Pipeline", _TRUNCATE);
    pipeline.passes[rev::runtime::ShaderPassBufferA].enabled = true;
    strncpy_s(pipeline.passes[rev::runtime::ShaderPassBufferA].source_path,
              "project_assets/buffer_a.glsl", _TRUNCATE);
    pipeline.passes[rev::runtime::ShaderPassImage].enabled = true;
    strncpy_s(pipeline.passes[rev::runtime::ShaderPassImage].source_path,
              "project_assets/image.glsl", _TRUNCATE);
    pipeline.passes[rev::runtime::ShaderPassImage].channels[0].kind =
        rev::runtime::ShaderChannelBufferA;
    project->shader_pipeline_count = 1;
    if (scene->shader_cue_count > 0) scene->shader_cues[0].shader_pipeline_index = 0;
}

bool HasEveryCueFamily(const rev::editor::ProjectData* project) {
    int shader = 0, image = 0, sprite = 0, pixel = 0, particles = 0;
    int text = 0, scroll = 0, music = 0, mesh = 0, post = 0;
    for (int s = 0; s < project->scene_count; ++s) {
        const rev::editor::SceneBlock& scene = project->scenes[s];
        shader += scene.shader_cue_count; image += scene.image_cue_count;
        sprite += scene.animated_sprite_cue_count; pixel += scene.pixel_cue_count;
        particles += scene.pixel_emitter_cue_count; text += scene.text_cue_count;
        scroll += scene.scroll_text_cue_count; music += scene.music_cue_count;
        mesh += scene.mesh_cue_count; post += scene.post_effect_count;
    }
    bool pipeline_ok = project->shader_pipeline_count == 1 &&
        std::strcmp(project->shader_pipelines[0].name, "Regression Pipeline") == 0 &&
        project->shader_pipelines[0].passes[rev::runtime::ShaderPassBufferA].enabled &&
        project->shader_pipelines[0].passes[rev::runtime::ShaderPassImage].channels[0].kind ==
            rev::runtime::ShaderChannelBufferA;
    return shader && image && sprite && pixel && particles && text && scroll &&
           music && mesh && post && project->curve_count && pipeline_ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: editor_pipeline_cli <source_project.json> <saved_project.json> <exported_cues.txt>\n");
        return 2;
    }

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok)
        return 3;

    rev::editor::EditorContext* editor = new rev::editor::EditorContext();
    rev::editor::ProjectData* project = new rev::editor::ProjectData();
    project->curves = new rev::curve::Curve[rev::runtime::kMaxCurves]();
    editor->project = project;
    editor->selected_scene_index = editor->selected_cue_index = -1;
    editor->selected_curve_index = editor->editing_curve_index = -1;

    bool ok = rev::editor::LoadProject(editor, argv[1]);
    if (ok && project->scene_count > 0) {
        NormalizeAssetPaths(project);
        AddMissingCueFamilies(project);
        ok = rev::editor::SaveProject(editor, argv[2]);
    }
    if (ok) ok = rev::editor::LoadProject(editor, argv[2]);
    if (ok) ok = HasEveryCueFamily(project);
    if (ok) ok = rev::editor::ExportProject(editor, argv[3]);
    if (ok) ok = rev::editor::ImportFromCues(editor, argv[3]);
    if (ok) ok = project->shader_pipeline_count == 1 &&
        std::strcmp(project->shader_pipelines[0].name, "Regression Pipeline") == 0 &&
        project->shader_pipelines[0].passes[rev::runtime::ShaderPassImage].channels[0].kind ==
            rev::runtime::ShaderChannelBufferA && project->scene_count > 0 &&
        project->scenes[0].shader_cue_count > 0 &&
        project->scenes[0].shader_cues[0].shader_pipeline_index == 0;

    rev::editor::NewProject(editor);
    delete[] project->curves;
    delete project;
    delete editor;
    Gdiplus::GdiplusShutdown(gdiplus_token);

    if (!ok) {
        std::fprintf(stderr, "[editor_pipeline_cli] FAILED\n");
        return 1;
    }
    std::printf("[editor_pipeline_cli] PASS\n");
    return 0;
}
