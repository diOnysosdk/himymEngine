#include "rev_editor.h"
#include "rev_shader.h"
#include "rev_pack.h"
#include "rev_mesh.h"
#include "rev_gltf.h"
#include "rev_xm.h"
#include "rev_pixel.h"
#include "rev_particles.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <windows.h>
#include <mmsystem.h>
#include <gl/gl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

// OpenGL constants not in gl.h
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA 0x8003
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif

// NOTE: This file requires Dear ImGui to be fully functional
// See revision_libs/rev_editor/README.md for setup instructions

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include "editor_internal.h"

// Forward declare the ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Message callback for rev_platform
static long long ImGuiMessageCallback(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
    return ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wparam, (LPARAM)lparam);
}

namespace rev {
namespace editor {

static bool ParseLayerPostEffectField(const char* line, LayerPostEffect* effects, int* effect_count);
static bool ParseAssetShaderField(const char* line, AssetShader* shaders, int* shader_count);
static void NormalizeAssetShaderCount(AssetShader* shaders, int* shader_count);

struct EditorAudioState {
    static const int kBufferCount = 4;
    static const int kFramesPerBuffer = 2048;

    HWAVEOUT wave_out = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> playing{false};
    std::atomic<unsigned int> generation{0};
    std::mutex player_mutex;
    rev::xm::Player* player = nullptr;
    char cue_key[512] = {};
    float cue_start = 0.0f;
    float last_time = 0.0f;
    bool initialized = false;
};

static bool FileExists(const char* path);

static bool ResolveEditorPixelPath(const ProjectData* project, const PixelCue* cue,
                                   char* out_path, size_t out_size) {
    if (!project || !cue || !out_path || out_size == 0) return false;
    out_path[0] = '\0';

    const char* candidates[4] = { cue->asset_path, nullptr, nullptr, nullptr };
    char project_asset_path[512] = {};
    char workspace_path[512] = {};
    char workspace_asset_path[512] = {};
    if (project->assets_path[0] && cue->asset_key[0]) {
        snprintf(project_asset_path, sizeof(project_asset_path), "%s\\%s",
                 project->assets_path, cue->asset_key);
        candidates[1] = project_asset_path;
    }
    if (project->workspace_path[0] && cue->asset_path[0]) {
        snprintf(workspace_path, sizeof(workspace_path), "%s\\%s",
                 project->workspace_path, cue->asset_path);
        candidates[2] = workspace_path;
    }
    if (project->workspace_path[0] && cue->asset_key[0]) {
        snprintf(workspace_asset_path, sizeof(workspace_asset_path), "%s\\project_assets\\%s",
                 project->workspace_path, cue->asset_key);
        candidates[3] = workspace_asset_path;
    }
    for (const char* candidate : candidates) {
        if (candidate && candidate[0] && FileExists(candidate)) {
            strncpy_s(out_path, out_size, candidate, _TRUNCATE);
            return true;
        }
    }
    return false;
}

static bool UploadEditorPixelFrame(const rev::pixel::PixelAnimation* animation,
                                   int frame_index, int palette_offset,
                                   unsigned int* out_texture) {
    if (!animation || !animation->pixels || !out_texture || animation->width == 0 ||
        animation->height == 0 || animation->frame_count == 0 || animation->palette_count == 0) {
            return false;
    }

    size_t pixel_count = (size_t)animation->width * animation->height;
    unsigned char* rgba = new unsigned char[pixel_count * 4];
    int frame = frame_index % animation->frame_count;
    if (frame < 0) frame += animation->frame_count;
    for (size_t i = 0; i < pixel_count; ++i) {
        int palette_index = animation->pixels[(size_t)frame * pixel_count + i];
        palette_index = (palette_index + palette_offset) % animation->palette_count;
        if (palette_index < 0) palette_index += animation->palette_count;
        const rev::pixel::PixelColor& color = animation->palette[palette_index];
        rgba[i * 4 + 0] = color.r;
        rgba[i * 4 + 1] = color.g;
        rgba[i * 4 + 2] = color.b;
        rgba[i * 4 + 3] = color.a;
    }

    glGenTextures(1, out_texture);
    glBindTexture(GL_TEXTURE_2D, *out_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, animation->width, animation->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    delete[] rgba;
    return *out_texture != 0;
}

static bool ResolveEditorPixelEmitterPath(const ProjectData* project, const PixelEmitterCue* cue,
                                          char* out_path, size_t out_size) {
    if (!project || !cue || !out_path || out_size == 0) return false;
    PixelCue pixel = {};
    strncpy_s(pixel.asset_key, cue->asset_key, _TRUNCATE);
    strncpy_s(pixel.asset_path, cue->asset_path, _TRUNCATE);
    return ResolveEditorPixelPath(project, &pixel, out_path, out_size);
}

static bool UploadPrimitiveEmitterTexture(const PixelEmitterCue* cue, unsigned int* out_texture) {
    if (!cue || !out_texture) return false;
    const int size = 16;
    unsigned char pixels[size * size * 4] = {};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float nx = ((float)x + 0.5f) / size * 2.0f - 1.0f;
            float ny = ((float)y + 0.5f) / size * 2.0f - 1.0f;
            bool filled = true;
            if (cue->primitive_shape == rev::particles::PrimitiveShapeCircle) {
                filled = nx * nx + ny * ny <= 1.0f;
            } else if (cue->primitive_shape == rev::particles::PrimitiveShapeTriangle) {
                filled = ny >= -1.0f && ny <= 1.0f && fabsf(nx) <= (1.0f - ny) * 0.5f;
            } else if (cue->primitive_shape == rev::particles::PrimitiveShapeDiamond) {
                filled = fabsf(nx) + fabsf(ny) <= 1.0f;
            }
            size_t index = ((size_t)y * size + x) * 4;
            pixels[index + 0] = (unsigned char)(cue->primitive_color[0] * 255.0f);
            pixels[index + 1] = (unsigned char)(cue->primitive_color[1] * 255.0f);
            pixels[index + 2] = (unsigned char)(cue->primitive_color[2] * 255.0f);
            pixels[index + 3] = filled ? (unsigned char)(cue->primitive_color[3] * 255.0f) : 0;
        }
    }
    glGenTextures(1, out_texture);
    glBindTexture(GL_TEXTURE_2D, *out_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return *out_texture != 0;
}
struct EditorNoiseTextureCacheEntry {
    char path[512];
    unsigned int texture_id;
};

static EditorNoiseTextureCacheEntry g_noise_texture_cache[32] = {};

static unsigned int GetEditorNoiseTexture(EditorContext* editor, const char* declared_path) {
    if (!editor || !editor->project || !declared_path || !declared_path[0]) return 0;

    char resolved[512] = {};
    if (strchr(declared_path, ':') || declared_path[0] == '\\' || declared_path[0] == '/') {
        strncpy_s(resolved, declared_path, _TRUNCATE);
    } else if (editor->project->assets_path[0]) {
        snprintf(resolved, sizeof(resolved), "%s\\%s", editor->project->assets_path, declared_path);
        if (!FileExists(resolved) && editor->project->workspace_path[0]) {
            snprintf(resolved, sizeof(resolved), "%s\\%s", editor->project->workspace_path, declared_path);
        }
    }
    if (!resolved[0] || !FileExists(resolved)) return 0;

    for (EditorNoiseTextureCacheEntry& entry : g_noise_texture_cache) {
        if (entry.texture_id != 0 && strcmp(entry.path, resolved) == 0) return entry.texture_id;
    }

    rev::runtime::ImageTexture image = {};
    if (!rev::runtime::LoadImageTexture(resolved, &image) || image.texture_id == 0) return 0;
    for (EditorNoiseTextureCacheEntry& entry : g_noise_texture_cache) {
        if (entry.texture_id == 0) {
            strncpy_s(entry.path, resolved, _TRUNCATE);
            entry.texture_id = image.texture_id;
            return entry.texture_id;
        }
    }
    glDeleteTextures(1, &image.texture_id);
    return 0;
}

static void ParseShaderNoiseMapPaths(const char* line, char paths[4][512]) {
    if (!line || !paths) return;
    const char* field = line;
    for (int delimiter = 0; delimiter < 63; ++delimiter) {
        field = strchr(field, '|');
        if (!field) return;
        ++field;
    }
    for (int map_index = 0; map_index < 4; ++map_index) {
        const char* end = strchr(field, '|');
        size_t length = end ? (size_t)(end - field) : strcspn(field, "\r\n");
        if (length >= 512) length = 511;
        if (length == 1 && field[0] == '-') {
            paths[map_index][0] = '\0';
        } else {
            memcpy(paths[map_index], field, length);
            paths[map_index][length] = '\0';
        }
        if (!end) break;
        field = end + 1;
    }
}

static void FillEditorAudioBuffer(EditorAudioState* state, float* output, int frame_count) {
    memset(output, 0, (size_t)frame_count * 2 * sizeof(float));
    if (!state->playing.load(std::memory_order_acquire)) return;

    std::lock_guard<std::mutex> lock(state->player_mutex);
    if (state->player) rev::xm::Update(state->player, output, frame_count);
}

void UpdateEditorAudioEffects(EditorContext* editor) {
    EditorAudioState* state = editor ? (EditorAudioState*)editor->audio_state : nullptr;
    if (!state || !editor->project) return;
    std::lock_guard<std::mutex> lock(state->player_mutex);
    if (state->player) rev::xm::SetAudioEffects(state->player, &editor->project->audio_effects);
}

static DWORD WINAPI EditorAudioThreadProc(LPVOID param) {
    EditorAudioState* state = (EditorAudioState*)param;
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 32;
    format.nBlockAlign = (WORD)(format.nChannels * sizeof(float));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    float buffers[EditorAudioState::kBufferCount][EditorAudioState::kFramesPerBuffer * 2] = {};
    WAVEHDR headers[EditorAudioState::kBufferCount] = {};
    for (int i = 0; i < EditorAudioState::kBufferCount; ++i) {
        headers[i].lpData = (LPSTR)buffers[i];
        headers[i].dwBufferLength = sizeof(buffers[i]);
        waveOutPrepareHeader(state->wave_out, &headers[i], sizeof(WAVEHDR));
        FillEditorAudioBuffer(state, buffers[i], EditorAudioState::kFramesPerBuffer);
        waveOutWrite(state->wave_out, &headers[i], sizeof(WAVEHDR));
    }
    unsigned int generation = state->generation.load(std::memory_order_acquire);

    while (!state->stop.load(std::memory_order_acquire)) {
        unsigned int current_generation = state->generation.load(std::memory_order_acquire);
        if (current_generation != generation) {
            waveOutReset(state->wave_out);
            for (int i = 0; i < EditorAudioState::kBufferCount; ++i) {
                FillEditorAudioBuffer(state, buffers[i], EditorAudioState::kFramesPerBuffer);
                waveOutWrite(state->wave_out, &headers[i], sizeof(WAVEHDR));
            }
            generation = current_generation;
        }
        for (int i = 0; i < EditorAudioState::kBufferCount; ++i) {
            if ((headers[i].dwFlags & WHDR_DONE) == 0) continue;
            FillEditorAudioBuffer(state, buffers[i], EditorAudioState::kFramesPerBuffer);
            waveOutWrite(state->wave_out, &headers[i], sizeof(WAVEHDR));
        }
        Sleep(1);
    }

    waveOutReset(state->wave_out);
    for (int i = 0; i < EditorAudioState::kBufferCount; ++i) {
        waveOutUnprepareHeader(state->wave_out, &headers[i], sizeof(WAVEHDR));
    }
    return 0;
}

static EditorAudioState* CreateEditorAudio() {
    EditorAudioState* state = new EditorAudioState();
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 32;
    format.nBlockAlign = (WORD)(format.nChannels * sizeof(float));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (waveOutOpen(&state->wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        delete state;
        return nullptr;
    }
    state->thread = CreateThread(nullptr, 0, EditorAudioThreadProc, state, 0, nullptr);
    if (!state->thread) {
        waveOutClose(state->wave_out);
        delete state;
        return nullptr;
    }
    return state;
}

static void DestroyEditorAudio(EditorAudioState* state) {
    if (!state) return;
    state->stop.store(true, std::memory_order_release);
    if (state->thread) {
        WaitForSingleObject(state->thread, 2000);
        CloseHandle(state->thread);
        state->thread = nullptr;
    }
    if (state->wave_out) {
        waveOutClose(state->wave_out);
        state->wave_out = nullptr;
    }
    std::lock_guard<std::mutex> lock(state->player_mutex);
    if (state->player) {
        rev::xm::DestroyPlayer(state->player);
        state->player = nullptr;
    }
    delete state;
}

static bool ReadEditorFile(const char* path, std::vector<unsigned char>* bytes) {
    if (!path || !path[0] || !bytes) return false;
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    bytes->resize((size_t)size);
        bool ok = fread(bytes->data(), 1, bytes->size(), f) == bytes->size();
    fclose(f);
    return ok;
}

static bool ResolveEditorMusicPath(const ProjectData* project, const MusicCue* cue,
                                   char* out_path, size_t out_size) {
    if (!project || !cue || !out_path || out_size == 0) return false;
    out_path[0] = '\0';
    const char* candidates[4] = { cue->asset_path, nullptr, nullptr, nullptr };
    char asset_path[512] = {};
    char workspace_path[512] = {};
    char project_asset_path[512] = {};
    if (project->assets_path[0] && cue->asset_key[0]) {
        snprintf(asset_path, sizeof(asset_path), "%s\\%s", project->assets_path, cue->asset_key);
        candidates[1] = asset_path;
    }
    if (project->workspace_path[0] && cue->asset_path[0]) {
        snprintf(workspace_path, sizeof(workspace_path), "%s\\%s", project->workspace_path, cue->asset_path);
        candidates[2] = workspace_path;
    }
    if (project->assets_path[0] && cue->asset_key[0]) {
        snprintf(project_asset_path, sizeof(project_asset_path), "%s\\%s", project->assets_path, cue->asset_key);
        candidates[3] = project_asset_path;
    }
    for (const char* candidate : candidates) {
        if (candidate && candidate[0] && FileExists(candidate)) {
            strncpy_s(out_path, out_size, candidate, _TRUNCATE);
            return true;
        }
    }
    return false;
}

static bool FindEditorMusicCue(EditorContext* editor, float time, const MusicCue** out_cue,
                               float* out_start) {
    if (!editor || !editor->project || !out_cue || !out_start) return false;
    float scene_start = 0.0f;
    const MusicCue* best = nullptr;
    float best_start = -1.0f;
    for (int si = 0; si < editor->project->scene_count; ++si) {
        SceneBlock* scene = &editor->project->scenes[si];
        float local_time = time - scene_start;
        if (local_time >= 0.0f && local_time <= scene->duration) {
            for (int i = 0; i < scene->music_cue_count; ++i) {
                const MusicCue* cue = &scene->music_cues[i];
                float cue_end = cue->cue_end < 0.0f ? scene->duration : cue->cue_end;
                bool active = local_time >= cue->cue_start && local_time <= cue_end;
                bool carried = editor->project->music_persist_across_scenes && local_time >= cue->cue_start;
                float absolute_start = scene_start + cue->cue_start;
                if ((active || carried) && absolute_start >= best_start) {
                    best = cue;
                    best_start = absolute_start;
                }
            }
            break;
        }
        if (editor->project->music_persist_across_scenes && local_time >= scene->duration) {
            for (int i = 0; i < scene->music_cue_count; ++i) {
                const MusicCue* cue = &scene->music_cues[i];
                float absolute_start = scene_start + cue->cue_start;
                if (absolute_start <= time && absolute_start >= best_start) {
                    best = cue;
                    best_start = absolute_start;
                }
            }
        }
        scene_start += scene->duration;
    }
    if (!best) return false;
    *out_cue = best;
    *out_start = best_start;
    return true;
}

static bool ReplaceEditorMusic(EditorContext* editor, const MusicCue* cue, float cue_start,
                               float timeline_time) {
    EditorAudioState* state = editor ? (EditorAudioState*)editor->audio_state : nullptr;
    if (!state || !cue) return false;
    char path[512] = {};
    std::vector<unsigned char> bytes;
    if (!ResolveEditorMusicPath(editor->project, cue, path, sizeof(path)) ||
        !ReadEditorFile(path, &bytes)) return false;
    rev::xm::Player* replacement = rev::xm::CreatePlayer(bytes.data(), bytes.size());
    if (!replacement) return false;
    rev::xm::SetAudioEffects(replacement, &editor->project->audio_effects);

    float offset = timeline_time - cue_start;
    if (offset < 0.0f) offset = 0.0f;
    float duration = rev::xm::GetDuration(replacement);
    if (duration > 0.0f && editor->project->loop_music) offset = fmodf(offset, duration);
    if (duration > 0.0f && offset > duration) offset = duration;
    float scratch[2048 * 2] = {};
    int frames_left = (int)(offset * 48000.0f);
    while (frames_left > 0) {
        int frames = frames_left > 2048 ? 2048 : frames_left;
        rev::xm::Update(replacement, scratch, frames);
        frames_left -= frames;
    }

    {
        std::lock_guard<std::mutex> lock(state->player_mutex);
        if (state->player) rev::xm::DestroyPlayer(state->player);
        state->player = replacement;
        strncpy_s(state->cue_key, sizeof(state->cue_key), path, _TRUNCATE);
        state->cue_start = cue_start;
        state->generation.fetch_add(1, std::memory_order_release);
    }
    return true;
}

static void SyncEditorAudio(EditorContext* editor, float delta_time) {
    EditorAudioState* state = editor ? (EditorAudioState*)editor->audio_state : nullptr;
    if (!state) return;
    const MusicCue* cue = nullptr;
    float cue_start = 0.0f;
    bool has_cue = FindEditorMusicCue(editor, editor->current_time, &cue, &cue_start);
    bool playing_changed = state->initialized && state->playing.load(std::memory_order_acquire) != editor->playing;
    float time_delta = editor->current_time - state->last_time;
    bool timeline_jump = !state->initialized || playing_changed ||
        (!editor->playing && fabsf(time_delta) > 0.0001f) ||
        (editor->playing && fabsf(time_delta - delta_time) > 0.03f);
    char resolved_path[512] = {};
    bool path_resolved = has_cue && ResolveEditorMusicPath(editor->project, cue, resolved_path, sizeof(resolved_path));
    bool same_track = has_cue && path_resolved && strcmp(state->cue_key, resolved_path) == 0;
    bool cue_changed = has_cue && path_resolved &&
        (!same_track || (!editor->project->music_persist_across_scenes &&
                         fabsf(state->cue_start - cue_start) > 0.0001f));

    if (!has_cue) {
        std::lock_guard<std::mutex> lock(state->player_mutex);
        if (state->player) {
            rev::xm::DestroyPlayer(state->player);
            state->player = nullptr;
        }
        state->cue_key[0] = '\0';
        state->generation.fetch_add(1, std::memory_order_release);
    } else if (timeline_jump || cue_changed) {
        ReplaceEditorMusic(editor, cue, cue_start, editor->current_time);
    }

    state->playing.store(editor->playing && has_cue, std::memory_order_release);
    state->last_time = editor->current_time;
    state->initialized = true;
}

static void ResetEditorAudio(EditorContext* editor) {
    EditorAudioState* state = editor ? (EditorAudioState*)editor->audio_state : nullptr;
    if (!state) return;
    state->playing.store(false, std::memory_order_release);
    state->generation.fetch_add(1, std::memory_order_release);
    std::lock_guard<std::mutex> lock(state->player_mutex);
    if (state->player) {
        rev::xm::DestroyPlayer(state->player);
        state->player = nullptr;
    }
    state->cue_key[0] = '\0';
    state->initialized = false;
}

// Helper: get file modification time as uint64 (Windows FILETIME)
uint64_t GetFileModificationTime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fileInfo)) {
        ULARGE_INTEGER ull;
        ull.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
        return ull.QuadPart;
    }
    return 0;
}

static bool DirectoryExists(const char* path) {
    if (!path || !path[0]) return false;
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

static bool FileExists(const char* path) {
    if (!path || !path[0]) return false;
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static void NormalizeSlashes(char* path) {
    if (!path) return;
    for (char* p = path; *p; ++p) {
        if (*p == '/') *p = '\\';
    }
}

// Some environments leave stale Windows SDK variables behind (for example
// WindowsSdkDir pointing at a removed toolchain folder). That can make
// CMake/MSBuild fail with SDK-not-found errors even when a valid SDK exists.
static void SanitizeWindowsSdkEnvironmentForBuild() {
    char sdk_dir[512] = {};
    size_t sdk_dir_size = 0;
    getenv_s(&sdk_dir_size, sdk_dir, sizeof(sdk_dir), "WindowsSdkDir");
    if (sdk_dir_size > 0 && !DirectoryExists(sdk_dir)) {
        printf("[Build] Warning: Ignoring invalid WindowsSdkDir='%s'\n", sdk_dir);
        _putenv_s("WindowsSdkDir", "");
    }

    char sdk_ver[128] = {};
    size_t sdk_ver_size = 0;
    getenv_s(&sdk_ver_size, sdk_ver, sizeof(sdk_ver), "WindowsSDKVersion");
    if (sdk_ver_size == 0) return;

    // Validate version against the default Windows Kits location used by VS.
    char sdk_path[512] = {};
    snprintf(sdk_path, sizeof(sdk_path), "C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\%s", sdk_ver);
    NormalizeSlashes(sdk_path);
    size_t len = strlen(sdk_path);
    while (len > 0 && (sdk_path[len - 1] == '\\' || sdk_path[len - 1] == ' ')) {
        sdk_path[len - 1] = '\0';
        --len;
    }

    if (!DirectoryExists(sdk_path)) {
        printf("[Build] Warning: Ignoring invalid WindowsSDKVersion='%s'\n", sdk_ver);
        _putenv_s("WindowsSDKVersion", "");
    }
}

static bool CopyBuiltExeToProjectOutput(EditorContext* editor,
                                        const char* exe_name,
                                        char* out_dest_path,
                                        size_t out_dest_path_size) {
    if (!editor || !exe_name || !exe_name[0]) return false;

    const char* project_root = editor->project->workspace_path[0]
        ? editor->project->workspace_path
        : editor->startup_dir;

    char src_path[512] = {};
    snprintf(src_path, sizeof(src_path), "%s\\build\\bin\\Release\\%s", editor->startup_dir, exe_name);
    if (!FileExists(src_path)) {
        printf("[Build] Warning: source executable not found: %s\n", src_path);
        return false;
    }

    char bin_dir[512] = {};
    char release_dir[512] = {};
    snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", project_root);
    snprintf(release_dir, sizeof(release_dir), "%s\\Release", bin_dir);
    CreateDirectoryA(bin_dir, NULL);
    CreateDirectoryA(release_dir, NULL);

    char dest_path[512] = {};
    snprintf(dest_path, sizeof(dest_path), "%s\\%s", release_dir, exe_name);
    if (!CopyFileA(src_path, dest_path, FALSE)) {
        printf("[Build] Warning: could not copy %s to %s (err=%lu)\n", src_path, dest_path, GetLastError());
        return false;
    }

    if (out_dest_path && out_dest_path_size > 0) {
        strncpy_s(out_dest_path, out_dest_path_size, dest_path, _TRUNCATE);
    }

    printf("[Build] Copied %s to %s\n", exe_name, dest_path);
    return true;
}

static bool EnsureProjectOutputReleaseDir(EditorContext* editor,
                                          char* out_release_dir,
                                          size_t out_release_dir_size) {
    if (!editor) return false;

    const char* project_root = editor->project->workspace_path[0]
        ? editor->project->workspace_path
        : editor->startup_dir;

    char bin_dir[512] = {};
    char release_dir[512] = {};
    snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", project_root);
    snprintf(release_dir, sizeof(release_dir), "%s\\Release", bin_dir);

    CreateDirectoryA(bin_dir, NULL);
    CreateDirectoryA(release_dir, NULL);

    if (out_release_dir && out_release_dir_size > 0) {
        strncpy_s(out_release_dir, out_release_dir_size, release_dir, _TRUNCATE);
    }

    return DirectoryExists(release_dir);
}

static void JsonEscapeString(const char* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; ++si) {
        char c = src[si];
        if (c == '"' || c == '\\') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = c;
        } else if (c == '\n') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = 'r';
        } else if (c == '\t') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = 't';
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

static void ApplySpriteBlendMode(int blend_mode) {
    switch (blend_mode) {
        case 1: // Additive
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case 2: // Multiply
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case 3: // Screen
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        case 0:
        default: // Alpha
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

static void ApplyShaderLayerBlendMode(int blend_mode, float opacity) {
    typedef void (*PFNGLBLENDCOLORPROC)(float red, float green, float blue, float alpha);
    static PFNGLBLENDCOLORPROC glBlendColor_fn = nullptr;
    static bool blend_color_loaded = false;
    if (!blend_color_loaded) {
        glBlendColor_fn = (PFNGLBLENDCOLORPROC)wglGetProcAddress("glBlendColor");
        blend_color_loaded = true;
    }

    float a = opacity;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;

    bool has_blend_color = (glBlendColor_fn != nullptr);
    if (has_blend_color) {
        glBlendColor_fn(0.0f, 0.0f, 0.0f, a);
    }

    switch (blend_mode) {
        case 1: // Additive
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
            break;
        case 2: // Multiply
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
            break;
        case 3: // Screen
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_SRC_COLOR);
            break;
        case 0:
        default: // Alpha
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
            break;
    }
}

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float ComputeShaderCueEnvelope(float local_time, float cue_duration, float fade_in, float fade_out) {
    float env = 1.0f;

    if (fade_in > 0.0001f) {
        env *= Clamp01(local_time / fade_in);
    }

    if (fade_out > 0.0001f && cue_duration > 0.0001f) {
        float time_to_end = cue_duration - local_time;
        env *= Clamp01(time_to_end / fade_out);
    }

    return env;
}

static void JsonUnescapeString(const char* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; ++si) {
        if (src[si] == '\\' && src[si + 1] != '\0') {
            ++si;
            if (src[si] == 'n') dst[di++] = '\n';
            else if (src[si] == 'r') dst[di++] = '\r';
            else if (src[si] == 't') dst[di++] = '\t';
            else dst[di++] = src[si];
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static bool SerializeTextAnimation(const TextAnimationConfig* config,
                                   char* out, size_t out_size)
{
    if (!config || !out || out_size == 0) return false;
    size_t used = 0;
    int modifier_count = config->modifier_count;
    if (modifier_count < 0) modifier_count = 0;
    if (modifier_count > kMaxTextAnimationModifiers) modifier_count = kMaxTextAnimationModifiers;
    const TextRevealConfig& reveal = config->reveal;
    const TextExitConfig& exit = config->exit;
    int written = snprintf(out, out_size, "%d|%d|%.6f|%.6f|%d|%d|%d|%.6f|%.6f|%u|%d|%.6f|%.6f|%.6f|%.6f|%.6f|%d|%u|%d|%.6f|%.6f|%d|%d|%d|%.6f|%.6f|%u|%d|%.6f|%.6f|%.6f|%.6f|%.6f|%d|%u",
        config->version, reveal.type, reveal.start_offset, reveal.duration, reveal.easing,
        reveal.stagger.unit, reveal.stagger.order, reveal.stagger.delay, reveal.stagger.overlap,
        reveal.stagger.random_seed, reveal.stagger.ignore_whitespace, reveal.distance,
        reveal.start_scale, reveal.start_rotation, reveal.direction_x, reveal.direction_y,
        reveal.fade, reveal.seed,
        exit.type, exit.start_offset, exit.duration, exit.easing,
        exit.stagger.unit, exit.stagger.order, exit.stagger.delay, exit.stagger.overlap,
        exit.stagger.random_seed, exit.stagger.ignore_whitespace, exit.distance,
        exit.end_scale, exit.end_rotation, exit.direction_x, exit.direction_y,
        exit.fade, exit.seed);
    if (written < 0 || (size_t)written >= out_size) return false;
    used = (size_t)written;
    int count_written = snprintf(out + used, out_size - used, "|%d", modifier_count);
    if (count_written < 0 || (size_t)count_written >= out_size - used) return false;
    used += (size_t)count_written;
    for (int i = 0; i < modifier_count; ++i) {
        const TextModifierConfig& modifier = config->modifiers[i];
        int modifier_written = snprintf(out + used, out_size - used,
            "|%d|%d|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%u",
            modifier.type, modifier.enabled, modifier.start_time, modifier.end_time,
            modifier.amount, modifier.speed, modifier.frequency, modifier.phase, modifier.seed);
        if (modifier_written < 0 || (size_t)modifier_written >= out_size - used) return false;
        used += (size_t)modifier_written;
    }
    return true;
}

static bool DeserializeTextAnimation(const char* serialized, TextAnimationConfig* config)
{
    if (!serialized || !config) return false;
    InitializeTextAnimationConfig(config);
    char buffer[4096] = {};
    strncpy_s(buffer, serialized, _TRUNCATE);
    char* context = nullptr;
    char* cursor = strtok_s(buffer, "|", &context);
    auto next_int = [&]() { char* token = cursor; cursor = strtok_s(nullptr, "|", &context); return token ? atoi(token) : 0; };
    auto next_uint = [&]() { char* token = cursor; cursor = strtok_s(nullptr, "|", &context); return token ? (unsigned int)strtoul(token, nullptr, 10) : 0U; };
    auto next_float = [&]() { char* token = cursor; cursor = strtok_s(nullptr, "|", &context); return token ? (float)atof(token) : 0.0f; };
    if (!cursor) return false;
    config->version = next_int();
    TextRevealConfig& reveal = config->reveal;
    reveal.type = next_int(); reveal.start_offset = next_float(); reveal.duration = next_float(); reveal.easing = next_int();
    reveal.stagger.unit = next_int(); reveal.stagger.order = next_int(); reveal.stagger.delay = next_float(); reveal.stagger.overlap = next_float();
    reveal.stagger.random_seed = next_uint(); reveal.stagger.ignore_whitespace = next_int(); reveal.distance = next_float();
    reveal.start_scale = next_float(); reveal.start_rotation = next_float(); reveal.direction_x = next_float(); reveal.direction_y = next_float();
    reveal.fade = next_int(); reveal.seed = next_uint();
    TextExitConfig& exit = config->exit;
    exit.type = next_int(); exit.start_offset = next_float(); exit.duration = next_float(); exit.easing = next_int();
    exit.stagger.unit = next_int(); exit.stagger.order = next_int(); exit.stagger.delay = next_float(); exit.stagger.overlap = next_float();
    exit.stagger.random_seed = next_uint(); exit.stagger.ignore_whitespace = next_int(); exit.distance = next_float();
    exit.end_scale = next_float(); exit.end_rotation = next_float(); exit.direction_x = next_float(); exit.direction_y = next_float();
    exit.fade = next_int(); exit.seed = next_uint();
    config->modifier_count = next_int();
    if (config->modifier_count < 0) config->modifier_count = 0;
    if (config->modifier_count > kMaxTextAnimationModifiers) config->modifier_count = kMaxTextAnimationModifiers;
    for (int i = 0; i < config->modifier_count; ++i) {
        TextModifierConfig& modifier = config->modifiers[i];
        modifier.type = next_int(); modifier.enabled = next_int(); modifier.start_time = next_float();
        modifier.end_time = next_float(); modifier.amount = next_float(); modifier.speed = next_float();
        modifier.frequency = next_float(); modifier.phase = next_float(); modifier.seed = next_uint();
    }
    return true;
}

static bool ParseJsonStringValue(const char* line, char* out, size_t out_size) {
    if (!line || !out || out_size == 0) return false;

    const char* colon = strchr(line, ':');
    if (!colon) return false;

    const char* p = colon + 1;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') return false;
    ++p;

    char escaped[1024] = {};
    size_t ei = 0;
    while (*p && ei + 1 < sizeof(escaped)) {
        if (*p == '"' && (p == line || *(p - 1) != '\\')) break;
        escaped[ei++] = *p++;
    }
    escaped[ei] = '\0';

    JsonUnescapeString(escaped, out, out_size);
    return true;
}

static bool GetPngEncoderClsid(CLSID* clsid) {
    if (!clsid) return false;
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;

    BYTE* image_codec_memory = new BYTE[size];
    Gdiplus::ImageCodecInfo* image_codecs = (Gdiplus::ImageCodecInfo*)image_codec_memory;
    if (Gdiplus::GetImageEncoders(num, size, image_codecs) != Gdiplus::Ok) {
        delete[] image_codec_memory;
        return false;
    }

    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(image_codecs[i].MimeType, L"image/png") == 0) {
            *clsid = image_codecs[i].Clsid;
            found = true;
            break;
        }
    }

    delete[] image_codec_memory;
    return found;
}

struct FontEnumData {
    char** fonts;
    int count;
    int capacity;
};

static int CALLBACK EnumFontFamExProc(const LOGFONTA* lpelfe, const TEXTMETRICA*, DWORD, LPARAM lParam) {
    FontEnumData* data = (FontEnumData*)lParam;
    if (!data || !lpelfe) return 1;

    const char* face = lpelfe->lfFaceName;
    if (!face || face[0] == '\0') return 1;

    for (int i = 0; i < data->count; ++i) {
        if (strcmp(data->fonts[i], face) == 0) {
            return 1;
        }
    }

    if (data->count >= data->capacity) {
        int new_capacity = (data->capacity > 0) ? (data->capacity * 2) : 64;
        char** new_fonts = new char*[new_capacity];
        for (int i = 0; i < data->count; ++i) {
            new_fonts[i] = data->fonts[i];
        }
        delete[] data->fonts;
        data->fonts = new_fonts;
        data->capacity = new_capacity;
    }

    size_t len = strlen(face);
    char* copy = new char[len + 1];
    strcpy_s(copy, len + 1, face);
    data->fonts[data->count++] = copy;
    return 1;
}

static void EnumerateInstalledFonts(char*** out_fonts, int* out_count) {
    if (!out_fonts || !out_count) return;
    *out_fonts = nullptr;
    *out_count = 0;

    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        return;
    }

    FontEnumData data = {};
    LOGFONTA lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;

    EnumFontFamiliesExA(hdc, &lf, (FONTENUMPROCA)EnumFontFamExProc, (LPARAM)&data, 0);
    ReleaseDC(nullptr, hdc);

    if (data.count == 0) {
        data.fonts = new char*[1];
        data.fonts[0] = new char[6];
        strcpy_s(data.fonts[0], 6, "Arial");
        data.count = 1;
        data.capacity = 1;
    }

    *out_fonts = data.fonts;
    *out_count = data.count;
}

static bool BakeTextCueToPng(const TextCue* cue, const char* output_path) {
    if (!cue || !output_path || cue->text[0] == '\0' || cue->font_name[0] == '\0') return false;

    wchar_t wtext[256] = {};
    wchar_t wfont[64] = {};
    wchar_t wpath[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, cue->text, -1, wtext, (int)_countof(wtext));
    MultiByteToWideChar(CP_UTF8, 0, cue->font_name, -1, wfont, (int)_countof(wfont));
    MultiByteToWideChar(CP_UTF8, 0, output_path, -1, wpath, (int)_countof(wpath));

    Gdiplus::Bitmap temp_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics temp_g(&temp_bitmap);
    Gdiplus::Font font_obj(wfont, (Gdiplus::REAL)cue->size,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    // Do NOT set StringFormatFlagsNoWrap — it suppresses embedded \n line breaks.
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    Gdiplus::RectF layout(0.0f, 0.0f, 8192.0f, 8192.0f);
    Gdiplus::RectF bounds;
    temp_g.MeasureString(wtext, -1, &font_obj, layout, &format, &bounds);

    int width  = (int)(bounds.Width)  + 32;
    int height = (int)(bounds.Height) + 32;
    if (width  < 8) width  = 8;
    if (height < 8) height = 8;

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics gfx(&bitmap);
    gfx.Clear(Gdiplus::Color(0, 0, 0, 0));
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        255,
        (BYTE)(cue->color.r * 255.0f),
        (BYTE)(cue->color.g * 255.0f),
        (BYTE)(cue->color.b * 255.0f)));

    Gdiplus::RectF draw_rect(8.0f, 8.0f, (float)width - 16.0f, (float)height - 16.0f);
    gfx.DrawString(wtext, -1, &font_obj, draw_rect, &format, &brush);

    CLSID png_clsid = {};
    if (!GetPngEncoderClsid(&png_clsid)) return false;
    return bitmap.Save(wpath, &png_clsid, nullptr) == Gdiplus::Ok;
}

static bool BakeScrollTextCueToPng(const ScrollTextCue* cue, const char* output_path) {
    if (!cue || !output_path || cue->text[0] == '\0' || cue->font_name[0] == '\0') return false;

    char bake_text[1024] = {};
    if (cue->direction == 0 || cue->direction == 1) {
        snprintf(bake_text, sizeof(bake_text), "%s   %s", cue->text, cue->text);
    } else {
        strncpy_s(bake_text, sizeof(bake_text), cue->text, _TRUNCATE);
    }

    wchar_t wtext[1024] = {};
    wchar_t wfont[64] = {};
    wchar_t wpath[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, bake_text, -1, wtext, (int)_countof(wtext));
    MultiByteToWideChar(CP_UTF8, 0, cue->font_name, -1, wfont, (int)_countof(wfont));
    MultiByteToWideChar(CP_UTF8, 0, output_path, -1, wpath, (int)_countof(wpath));

    float bake_size = cue->size;
    if (bake_size < 4.0f) bake_size = 4.0f;

    Gdiplus::Bitmap temp_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics temp_g(&temp_bitmap);
    Gdiplus::Font font_obj(wfont, (Gdiplus::REAL)bake_size,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    Gdiplus::RectF layout(0.0f, 0.0f, 16384.0f, 4096.0f);
    Gdiplus::RectF bounds;
    temp_g.MeasureString(wtext, -1, &font_obj, layout, &format, &bounds);

    int width  = (int)(bounds.Width * ((cue->spacing > 0.01f) ? cue->spacing : 0.01f)) + 32;
    int height = (int)(bounds.Height) + 32;
    if (width < 8) width = 8;
    if (height < 8) height = 8;

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics gfx(&bitmap);
    gfx.Clear(Gdiplus::Color(0, 0, 0, 0));
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        255,
        (BYTE)(cue->color.r * 255.0f),
        (BYTE)(cue->color.g * 255.0f),
        (BYTE)(cue->color.b * 255.0f)));

    Gdiplus::RectF draw_rect(8.0f, 8.0f, (float)width - 16.0f, (float)height - 16.0f);
    gfx.DrawString(wtext, -1, &font_obj, draw_rect, &format, &brush);

    CLSID png_clsid = {};
    if (!GetPngEncoderClsid(&png_clsid)) return false;
    return bitmap.Save(wpath, &png_clsid, nullptr) == Gdiplus::Ok;
}

static bool IsFileReadableWithRetry(const char* path, int max_attempts, int sleep_ms) {
    if (!path || path[0] == '\0') return false;
    if (max_attempts < 1) max_attempts = 1;
    if (sleep_ms < 0) sleep_ms = 0;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        FILE* f = nullptr;
        fopen_s(&f, path, "rb");
        if (f) {
            fclose(f);
            return true;
        }
        if (attempt + 1 < max_attempts && sleep_ms > 0) {
            Sleep((DWORD)sleep_ms);
        }
    }
    return false;
}

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
    editor->selected_cue_type = CueTypeShader;
    editor->current_time = 0.0f;
    editor->playing = false;
    editor->show_trigger_recorder = false;
    editor->trigger_recording = false;
    editor->recording_track_index = -1;
    editor->recording_curve_target = nullptr;
    editor->recording_target_label[0] = '\0';
    editor->recording_append_curve = false;
    editor->recording_append_curve_index = -1;
    editor->recording_bpm = 120.0f;
    editor->recording_beat_offset = 0.0f;
    editor->recording_quantize_beats = 0.5f;
    strncpy_s(editor->recording_track_name, sizeof(editor->recording_track_name), "Ctrl Triggers", _TRUNCATE);
    editor->show_preview = true;
    editor->preview_fbo = 0;
    editor->preview_texture = 0;
    editor->preview_depth = 0;
    editor->layer_fbo = 0;
    editor->layer_texture = 0;
    editor->post_frame_rendered = false;
    editor->preview_vao = 0;
    editor->preview_width = 1920;
    editor->preview_height = 1080;
    editor->preview_initialized = false;
    editor->preview_shader = nullptr;
    editor->sprite_shader = nullptr;
    editor->preview_current_shader_id = -1;
    memset(&editor->preview_text_atlas, 0, sizeof(editor->preview_text_atlas));
    editor->preview_text_atlas_font[0] = '\0';
    editor->preview_text_atlas_size = 0.0f;
    memset(&editor->preview_scroll_atlas, 0, sizeof(editor->preview_scroll_atlas));
    editor->preview_scroll_atlas_font[0] = '\0';
    editor->preview_scroll_atlas_size = 0.0f;
    editor->shader_modal_open = false;
    editor->shader_modal_request_open = false;
    editor->music_modal_open = false;
    editor->music_modal_request_open = false;
    editor->image_modal_open = false;
    editor->image_modal_request_open = false;
    editor->animated_sprite_modal_open = false;
    editor->animated_sprite_modal_request_open = false;
    memset(&editor->editing_animated_sprite, 0, sizeof(editor->editing_animated_sprite));
    editor->editing_animated_sprite.fps = 12.0f;
    editor->editing_animated_sprite.playback_mode = 0;
    editor->editing_animated_sprite.start_frame = 0;
    editor->editing_animated_sprite.curve_x = -1;
    editor->editing_animated_sprite.curve_y = -1;
    editor->editing_animated_sprite.curve_scale = -1;
    editor->editing_animated_sprite.curve_opacity = -1;
    editor->editing_animated_sprite.curve_frame = -1;
    editor->text_modal_open = false;
    editor->text_modal_request_open = false;
    editor->scroll_text_modal_open = false;
    editor->scroll_text_modal_request_open = false;
    memset(&editor->editing_scroll_text, 0, sizeof(editor->editing_scroll_text));
    strncpy_s(editor->editing_scroll_text.font_name, sizeof(editor->editing_scroll_text.font_name), "Arial", _TRUNCATE);
    editor->editing_scroll_text.size = 40.0f;
    editor->editing_scroll_text.color = {1.0f, 1.0f, 1.0f};
    editor->editing_scroll_text.opacity = 1.0f;
    editor->editing_scroll_text.speed = 0.25f;
    editor->editing_scroll_text.wrap_gap = 0.2f;
    editor->editing_scroll_text.spacing = 1.0f;
    editor->editing_scroll_text.wave_freq = 1.0f;
    editor->editing_scroll_text.wave_length = 9.0f;
    editor->editing_scroll_text.jitter_freq = 1.0f;
    editor->editing_scroll_text.bake_mode = 0;
    editor->editing_scroll_text.curve_x = -1;
    editor->editing_scroll_text.curve_y = -1;
    editor->editing_scroll_text.curve_speed = -1;
    editor->editing_scroll_text.curve_size = -1;
    editor->editing_scroll_text.curve_opacity = -1;
    editor->editing_scroll_text.curve_color_r = -1;
    editor->editing_scroll_text.curve_color_g = -1;
    editor->editing_scroll_text.curve_color_b = -1;
    editor->editing_scroll_text.curve_wave_amp = -1;
    editor->editing_scroll_text.curve_wave_freq = -1;
    editor->editing_scroll_text.curve_wave_length = -1;
    editor->editing_scroll_text.curve_jitter_amp = -1;
    editor->editing_scroll_text.curve_jitter_freq = -1;
    editor->installed_fonts = nullptr;
    editor->installed_font_count = 0;
    editor->mesh_modal_open = false;
    editor->mesh_modal_request_open = false;
    memset(&editor->editing_mesh, 0, sizeof(editor->editing_mesh));
    editor->editing_mesh.scale[0] = editor->editing_mesh.scale[1] = editor->editing_mesh.scale[2] = 1.0f;
    editor->mesh_cache_count = 0;
    memset(editor->mesh_cache, 0, sizeof(editor->mesh_cache));
    editor->mesh_shader = nullptr;
    editor->selected_curve_index = -1;
    editor->dragging_point_index = -1;
    editor->selected_point_index = -1;
    editor->show_curve_grid = true;
    editor->curve_editor_modal_open = false;
    editor->curve_editor_modal_request_open = false;
    editor->point_properties_modal_open = false;
    editor->editing_curve_index = -1;
    editor->editing_curve_cue_type = -1;
    editor->editing_curve_field = -1;
    editor->editing_curve_label[0] = '\0';
    editor->build_status_message[0] = '\0';
    editor->build_status_timer = 0.0f;

    // Capture startup working directory before any file dialog can mutate it
    GetCurrentDirectoryA(sizeof(editor->startup_dir), editor->startup_dir);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static char imgui_ini_path[MAX_PATH] = {};
    char module_path[MAX_PATH] = {};
    DWORD module_path_length = GetModuleFileNameA(NULL, module_path, sizeof(module_path));
    if (module_path_length > 0 && module_path_length < sizeof(module_path)) {
        char* separator = strrchr(module_path, '\\');
        if (separator) {
            *separator = '\0';
            snprintf(imgui_ini_path, sizeof(imgui_ini_path), "%s\\imgui.ini", module_path);
            if (!FileExists(imgui_ini_path)) {
                char workspace_ini_path[MAX_PATH] = {};
                snprintf(workspace_ini_path, sizeof(workspace_ini_path), "%s\\imgui.ini", editor->startup_dir);
                if (FileExists(workspace_ini_path)) {
                    CopyFileA(workspace_ini_path, imgui_ini_path, TRUE);
                }
            }
            io.IniFilename = imgui_ini_path;
        }
    }
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
    
    // Allocate fixed curve array (max kMaxCurves curves)
    editor->project->curves = new rev::curve::Curve[rev::runtime::kMaxCurves];
    for (int i = 0; i < rev::runtime::kMaxCurves; ++i) {
        editor->project->curves[i].points = nullptr;
        editor->project->curves[i].point_count = 0;
        editor->project->curves[i].capacity = 0;
    }
    editor->project->curve_count = 0;
    editor->project->trigger_track_count = 0;
    
    editor->project->modified = false;
    editor->project->total_duration = 0.0f;
    editor->project->loop_intro = false;
    editor->project->loop_music = false;
    editor->project->music_persist_across_scenes = false;
    editor->project->runtime_fullscreen = true;
    strncpy_s(editor->project->runtime_title, sizeof(editor->project->runtime_title), "HiMYM - Minimal Intro Test", _TRUNCATE);
    editor->project->audio_effects = {};
    editor->project->audio_effects.compressor_threshold = 0.7f;
    editor->project->audio_effects.compressor_ratio = 4.0f;
    editor->project->audio_effects.compressor_attack = 0.01f;
    editor->project->audio_effects.compressor_release = 0.12f;
    editor->project->audio_effects.widener_amount = 1.0f;
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    memset(editor->project->workspace_path, 0, sizeof(editor->project->workspace_path));
    memset(editor->project->assets_path, 0, sizeof(editor->project->assets_path));

    editor->audio_state = CreateEditorAudio();
    
    // Enumerate installed Windows fonts for font picker
    EnumerateInstalledFonts(&editor->installed_fonts, &editor->installed_font_count);
    printf("Enumerated %d installed fonts\n", editor->installed_font_count);
    
    return editor;
}

void DestroyEditor(EditorContext* editor) {
    if (!editor) return;

    DestroyEditorAudio((EditorAudioState*)editor->audio_state);
    editor->audio_state = nullptr;
    
    // Unregister message callback
    rev::platform::SetMessageCallback(editor->window, nullptr);
    
    // Cleanup project
    if (editor->project) {
        // Clean up scenes
        for (int i = 0; i < editor->project->scene_count; ++i) {
            SceneBlock* scene = &editor->project->scenes[i];
            delete[] scene->shader_cues;
            delete[] scene->image_cues;
            delete[] scene->animated_sprite_cues;
            delete[] scene->pixel_cues;
            delete[] scene->pixel_emitter_cues;
            delete[] scene->text_cues;
            delete[] scene->scroll_text_cues;
            delete[] scene->music_cues;
            delete[] scene->mesh_cues;
            delete[] scene->post_effects;
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
    
    // Cleanup installed fonts list
    if (editor->installed_fonts) {
        for (int i = 0; i < editor->installed_font_count; ++i) {
            delete[] editor->installed_fonts[i];
        }
        delete[] editor->installed_fonts;
    }
    
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    delete editor;
}

static void RebaseProjectAssetPath(char* path, size_t path_size, const char* assets_path) {
    if (!path || path_size == 0 || !path[0] || !assets_path || !assets_path[0]) return;

    const char* filename = strrchr(path, '\\');
    const char* slash = strrchr(path, '/');
    if (slash && (!filename || slash > filename)) filename = slash;
    filename = filename ? filename + 1 : path;
    if (!filename[0]) return;

    char local_path[512] = {};
    snprintf(local_path, sizeof(local_path), "%s\\%s", assets_path, filename);
    if (FileExists(local_path) &&
        (strchr(path, ':') || path[0] == '\\' || path[0] == '/')) {
        strncpy_s(path, path_size, local_path, _TRUNCATE);
    }
}

static void RebaseProjectAssetList(char* paths, size_t paths_size, const char* assets_path) {
    if (!paths || !paths[0]) return;
    char copy[8192] = {};
    strncpy_s(copy, sizeof(copy), paths, _TRUNCATE);
    paths[0] = '\0';

    char* context = nullptr;
    for (char* token = strtok_s(copy, ";", &context); token; token = strtok_s(nullptr, ";", &context)) {
        char rebased[512] = {};
        strncpy_s(rebased, sizeof(rebased), token, _TRUNCATE);
        RebaseProjectAssetPath(rebased, sizeof(rebased), assets_path);
        if (paths[0]) strncat_s(paths, paths_size, ";", _TRUNCATE);
        strncat_s(paths, paths_size, rebased, _TRUNCATE);
    }
}

static void RebaseLoadedProjectAssets(ProjectData* project) {
    if (!project || !project->assets_path[0]) return;
    for (int scene_index = 0; scene_index < project->scene_count; ++scene_index) {
        SceneBlock* scene = &project->scenes[scene_index];
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            for (int map = 0; map < 4; ++map)
                RebaseProjectAssetPath(scene->shader_cues[i].noise_textures.paths[map],
                                       sizeof(scene->shader_cues[i].noise_textures.paths[map]), project->assets_path);
        }
        for (int i = 0; i < scene->image_cue_count; ++i)
            RebaseProjectAssetPath(scene->image_cues[i].asset_path, sizeof(scene->image_cues[i].asset_path), project->assets_path);
        for (int i = 0; i < scene->animated_sprite_cue_count; ++i)
            RebaseProjectAssetList(scene->animated_sprite_cues[i].frame_paths_csv,
                                   sizeof(scene->animated_sprite_cues[i].frame_paths_csv), project->assets_path);
        for (int i = 0; i < scene->pixel_cue_count; ++i)
            RebaseProjectAssetPath(scene->pixel_cues[i].asset_path, sizeof(scene->pixel_cues[i].asset_path), project->assets_path);
        for (int i = 0; i < scene->pixel_emitter_cue_count; ++i)
            RebaseProjectAssetPath(scene->pixel_emitter_cues[i].asset_path, sizeof(scene->pixel_emitter_cues[i].asset_path), project->assets_path);
        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            RebaseProjectAssetPath(cue->baked_asset_path, sizeof(cue->baked_asset_path), project->assets_path);
            RebaseProjectAssetPath(cue->glyph_atlas_path, sizeof(cue->glyph_atlas_path), project->assets_path);
            RebaseProjectAssetPath(cue->glyph_meta_path, sizeof(cue->glyph_meta_path), project->assets_path);
        }
        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            RebaseProjectAssetPath(cue->baked_asset_path, sizeof(cue->baked_asset_path), project->assets_path);
            RebaseProjectAssetPath(cue->glyph_atlas_path, sizeof(cue->glyph_atlas_path), project->assets_path);
            RebaseProjectAssetPath(cue->glyph_meta_path, sizeof(cue->glyph_meta_path), project->assets_path);
        }
        for (int i = 0; i < scene->music_cue_count; ++i)
            RebaseProjectAssetPath(scene->music_cues[i].asset_path, sizeof(scene->music_cues[i].asset_path), project->assets_path);
        for (int i = 0; i < scene->mesh_cue_count; ++i)
            RebaseProjectAssetPath(scene->mesh_cues[i].asset_path, sizeof(scene->mesh_cues[i].asset_path), project->assets_path);
    }
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
    bool in_animated_sprite_cues = false;
    bool in_pixel_cues = false;
    bool in_pixel_emitter_cues = false;
    bool in_text_cues = false;
    bool in_scroll_text_cues = false;
    bool in_music_cues = false;
    bool in_mesh_cues = false;
    bool in_post_effects = false;
    bool in_scene_layer_post_effects = false;
    bool in_curves = false;
    bool in_curve_points = false;
    bool in_trigger_tracks = false;
    bool in_trigger_events = false;
    TriggerTrack* current_trigger_track = nullptr;
    
    ShaderCue current_shader_cue = {};
    // Initialize shader curve fields to -1 (no curve)
    current_shader_cue.curve_speed = current_shader_cue.curve_intensity = current_shader_cue.curve_warp = -1;
    current_shader_cue.curve_exposure = current_shader_cue.curve_fade = -1;
    current_shader_cue.curve_palette_low_r = current_shader_cue.curve_palette_low_g = current_shader_cue.curve_palette_low_b = -1;
    current_shader_cue.curve_palette_mid_r = current_shader_cue.curve_palette_mid_g = current_shader_cue.curve_palette_mid_b = -1;
    current_shader_cue.curve_palette_high_r = current_shader_cue.curve_palette_high_g = current_shader_cue.curve_palette_high_b = -1;
    current_shader_cue.curve_opacity = current_shader_cue.curve_exposure_ramp = current_shader_cue.curve_fade_ramp = -1;
    
    ImageCue current_image_cue = {};
    current_image_cue.curve_rotation = -1;
    AnimatedSpriteCue current_animated_sprite_cue = {};
    current_animated_sprite_cue.fps = 12.0f;
    current_animated_sprite_cue.playback_mode = 0;
    current_animated_sprite_cue.start_frame = 0;
    current_animated_sprite_cue.curve_x = -1;
    current_animated_sprite_cue.curve_y = -1;
    current_animated_sprite_cue.curve_scale = -1;
    current_animated_sprite_cue.curve_rotation = -1;
    current_animated_sprite_cue.curve_opacity = -1;
    current_animated_sprite_cue.curve_frame = -1;
    PixelCue current_pixel_cue = {};
    current_pixel_cue.fps = 12.0f;
    current_pixel_cue.playback_mode = 0;
    current_pixel_cue.snap_to_pixels = 1;
    current_pixel_cue.curve_x = -1;
    current_pixel_cue.curve_y = -1;
    current_pixel_cue.curve_scale = -1;
    current_pixel_cue.curve_rotation = -1;

    auto InitializeAssetShaderCurves = [](AssetShader* shaders) {
        if (!shaders) return;
        for (int i = 0; i < rev::runtime::kMaxAssetShaders; ++i) {
            AssetShader& shader = shaders[i];
            shader.curve_speed = shader.curve_intensity = shader.curve_warp = -1;
            shader.curve_exposure = shader.curve_fade = shader.curve_opacity = -1;
            shader.curve_exposure_ramp = shader.curve_fade_ramp = -1;
            shader.curve_palette_low_r = shader.curve_palette_low_g = shader.curve_palette_low_b = -1;
            shader.curve_palette_mid_r = shader.curve_palette_mid_g = shader.curve_palette_mid_b = -1;
            shader.curve_palette_high_r = shader.curve_palette_high_g = shader.curve_palette_high_b = -1;
        }
    };
    InitializeAssetShaderCurves(current_image_cue.shaders);
    InitializeAssetShaderCurves(current_animated_sprite_cue.shaders);
    InitializeAssetShaderCurves(current_pixel_cue.shaders);
    current_pixel_cue.curve_opacity = -1;
    current_pixel_cue.curve_frame = -1;
    current_pixel_cue.curve_palette_offset = -1;
    PixelEmitterCue current_pixel_emitter_cue = {};
    current_pixel_emitter_cue.curve_x = current_pixel_emitter_cue.curve_y = -1;
    current_pixel_emitter_cue.curve_scale = current_pixel_emitter_cue.curve_rotation = -1;
    current_pixel_emitter_cue.curve_opacity = current_pixel_emitter_cue.curve_emission_rate = -1;
    current_pixel_emitter_cue.curve_speed_min = current_pixel_emitter_cue.curve_speed_max = -1;
    current_pixel_emitter_cue.curve_lifetime_min = current_pixel_emitter_cue.curve_lifetime_max = -1;
    current_pixel_emitter_cue.curve_scale_min = current_pixel_emitter_cue.curve_scale_max = -1;
    TextCue current_text_cue = {};
    current_text_cue.curve_x = -1;
    current_text_cue.curve_y = -1;
    current_text_cue.curve_size = -1;
    current_text_cue.curve_rotation = -1;
    current_text_cue.curve_color_r = -1;
    current_text_cue.curve_color_g = -1;
    current_text_cue.curve_color_b = -1;
    current_text_cue.blend_mode = 0;
    current_text_cue.bake_mode = 0;
    ScrollTextCue current_scroll_text_cue = {};
    strncpy_s(current_scroll_text_cue.font_name, sizeof(current_scroll_text_cue.font_name), "Arial", _TRUNCATE);
    current_scroll_text_cue.size = 40.0f;
    current_scroll_text_cue.color = {1.0f, 1.0f, 1.0f};
    current_scroll_text_cue.opacity = 1.0f;
    current_scroll_text_cue.wrap_gap = 0.2f;
    current_scroll_text_cue.speed = 0.25f;
    current_scroll_text_cue.spacing = 1.0f;
    current_scroll_text_cue.wave_freq = 1.0f;
    current_scroll_text_cue.wave_length = 9.0f;
    current_scroll_text_cue.jitter_freq = 1.0f;
    current_scroll_text_cue.bake_mode = 0;
    current_scroll_text_cue.curve_x = -1;
    current_scroll_text_cue.curve_y = -1;
    current_scroll_text_cue.curve_speed = -1;
    current_scroll_text_cue.curve_size = -1;
    current_scroll_text_cue.curve_rotation = -1;
    current_scroll_text_cue.curve_opacity = -1;
    current_scroll_text_cue.curve_color_r = -1;
    current_scroll_text_cue.curve_color_g = -1;
    current_scroll_text_cue.curve_color_b = -1;
    current_scroll_text_cue.curve_wave_amp = -1;
    current_scroll_text_cue.curve_wave_freq = -1;
    current_scroll_text_cue.curve_wave_length = -1;
    current_scroll_text_cue.curve_jitter_amp = -1;
    current_scroll_text_cue.curve_jitter_freq = -1;
    MusicCue current_music_cue = {};
    MeshCue current_mesh_cue = {};
    current_mesh_cue.scale[0] = current_mesh_cue.scale[1] = current_mesh_cue.scale[2] = 1.0f;
    current_mesh_cue.emissive_color[0] = 1.0f;
    current_mesh_cue.emissive_color[1] = 1.0f;
    current_mesh_cue.emissive_color[2] = 1.0f;
    current_mesh_cue.roughness = 0.5f;
    current_mesh_cue.fov_deg = 45.0f;
    current_mesh_cue.cull_mode = 0;
    current_mesh_cue.use_imported_light = 0;
    current_mesh_cue.use_imported_camera = 0;
    current_mesh_cue.curve_pos_x = current_mesh_cue.curve_pos_y = current_mesh_cue.curve_pos_z = -1;
    current_mesh_cue.curve_rot_x = current_mesh_cue.curve_rot_y = current_mesh_cue.curve_rot_z = -1;
    current_mesh_cue.curve_scale_x = current_mesh_cue.curve_scale_y = current_mesh_cue.curve_scale_z = -1;
    current_mesh_cue.curve_color_r = current_mesh_cue.curve_color_g = current_mesh_cue.curve_color_b = current_mesh_cue.curve_color_a = -1;
    current_mesh_cue.curve_mesh_size = current_mesh_cue.curve_metallic = current_mesh_cue.curve_roughness = current_mesh_cue.curve_fov = -1;
    PostEffect current_post_effect = {};
    current_post_effect.enabled = true;
    current_post_effect.intensity = 1.0f;
    current_post_effect.threshold = 1.0f;
    current_post_effect.radius = 1.0f;
    current_post_effect.color[0] = current_post_effect.color[1] = current_post_effect.color[2] = current_post_effect.color[3] = 1.0f;
    current_post_effect.end_time = -1.0f;
    current_post_effect.curve_intensity = current_post_effect.curve_threshold = current_post_effect.curve_radius = -1;
    current_post_effect.curve_color_r = current_post_effect.curve_color_g = current_post_effect.curve_color_b = current_post_effect.curve_color_a = -1;
    current_post_effect.curve_amount = -1;
    rev::curve::Curve* current_curve = nullptr;
    
    char scene_name[64] = {};
    float scene_duration = 0.0f;
    bool has_name = false;
    bool has_duration = false;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        int indent = 0;
        while (line[indent] == ' ' || line[indent] == '\t') indent++;
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

        int bool_value = 0;
        if (sscanf_s(start, "\"loop_intro\": %d", &bool_value) == 1) {
            editor->project->loop_intro = (bool_value != 0);
            continue;
        }
        if (sscanf_s(start, "\"loop_music\": %d", &bool_value) == 1) {
            editor->project->loop_music = (bool_value != 0);
            continue;
        }
        if (sscanf_s(start, "\"music_persist_across_scenes\": %d", &bool_value) == 1) {
            editor->project->music_persist_across_scenes = (bool_value != 0);
            continue;
        }
        if (sscanf_s(start, "\"runtime_fullscreen\": %d", &bool_value) == 1) {
            editor->project->runtime_fullscreen = (bool_value != 0);
            continue;
        }
        if (strstr(start, "\"runtime_title\":")) {
            ParseJsonStringValue(start, editor->project->runtime_title,
                                 sizeof(editor->project->runtime_title));
            continue;
        }
        if (sscanf_s(start, "\"audio_gain_enabled\": %d", &bool_value) == 1) {
            editor->project->audio_effects.gain_enabled = bool_value;
            continue;
        }
        if (sscanf_s(start, "\"audio_gain_db\": %f", &editor->project->audio_effects.gain_db) == 1) continue;
        if (sscanf_s(start, "\"audio_compressor_enabled\": %d", &bool_value) == 1) {
            editor->project->audio_effects.compressor_enabled = bool_value;
            continue;
        }
        if (sscanf_s(start, "\"audio_compressor_threshold\": %f", &editor->project->audio_effects.compressor_threshold) == 1) continue;
        if (sscanf_s(start, "\"audio_compressor_ratio\": %f", &editor->project->audio_effects.compressor_ratio) == 1) continue;
        if (sscanf_s(start, "\"audio_compressor_attack\": %f", &editor->project->audio_effects.compressor_attack) == 1) continue;
        if (sscanf_s(start, "\"audio_compressor_release\": %f", &editor->project->audio_effects.compressor_release) == 1) continue;
        if (sscanf_s(start, "\"audio_widener_enabled\": %d", &bool_value) == 1) {
            editor->project->audio_effects.widener_enabled = bool_value;
            continue;
        }
        if (sscanf_s(start, "\"audio_widener_amount\": %f", &editor->project->audio_effects.widener_amount) == 1) continue;
        if (sscanf_s(start, "\"audio_eq_enabled\": %d", &bool_value) == 1) {
            editor->project->audio_effects.eq_enabled = bool_value;
            continue;
        }
        if (sscanf_s(start, "\"audio_eq_low_db\": %f", &editor->project->audio_effects.eq_low_db) == 1) continue;
        if (sscanf_s(start, "\"audio_eq_mid_db\": %f", &editor->project->audio_effects.eq_mid_db) == 1) continue;
        if (sscanf_s(start, "\"audio_eq_high_db\": %f", &editor->project->audio_effects.eq_high_db) == 1) continue;
        
        // Detect sections
        if (strstr(start, "\"scenes\":")) {
            in_scenes = true;
            continue;
        }
        if (strstr(start, "\"curves\":")) {
            in_scenes = false;
            in_curves = true;
            in_trigger_tracks = false;
            in_trigger_events = false;
            continue;
        }
        if (strstr(start, "\"trigger_tracks\":")) {
            in_scenes = false;
            in_curves = false;
            in_trigger_tracks = true;
            continue;
        }

        if (in_trigger_tracks) {
            if (strstr(start, "\"name\":") && indent == 4 &&
                editor->project->trigger_track_count < rev::runtime::kMaxTriggerTracks) {
                current_trigger_track = &editor->project->trigger_tracks[editor->project->trigger_track_count++];
                memset(current_trigger_track, 0, sizeof(*current_trigger_track));
                ParseJsonStringValue(start, current_trigger_track->name, sizeof(current_trigger_track->name));
            } else if (current_trigger_track && strstr(start, "\"bpm\":")) {
                sscanf_s(start, "\"bpm\": %f", &current_trigger_track->timing.bpm);
            } else if (current_trigger_track && strstr(start, "\"beat_offset\":")) {
                sscanf_s(start, "\"beat_offset\": %f", &current_trigger_track->timing.beat_offset);
            } else if (current_trigger_track && strstr(start, "\"events\":")) {
                in_trigger_events = true;
            } else if (in_trigger_events && current_trigger_track && strstr(start, "{\"beat\":")) {
                float beat = 0.0f;
                int value = 0;
                if (sscanf_s(start, "{\"beat\": %f, \"value\": %d", &beat, &value) == 2) {
                    rev::runtime::AddTriggerEvent(current_trigger_track, beat, value);
                }
            } else if (in_trigger_events && strstr(start, "]")) {
                in_trigger_events = false;
            }
            continue;
        }
        
        // Scene name and duration detection (inside scenes array)
        if (in_scenes && indent == 6 && strstr(start, "\"name\":")) {
            // Nested cue objects also contain duration fields. A new scene
            // must never inherit one of those stale values.
            has_duration = false;
            if (ParseJsonStringValue(start, scene_name, sizeof(scene_name))) {
                has_name = true;
            }
        }
        if (in_scenes && indent == 6 && strstr(start, "\"duration\":")) {
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
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = false;
            in_post_effects = false;
            in_scene_layer_post_effects = false;
        }
        
        // Section detection
        if (strstr(start, "\"shader_cues\":")) {
            in_shader_cues = true;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"image_cues\":")) {
            in_shader_cues = false;
            in_image_cues = true;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"animated_sprite_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = true;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
        } else if (strstr(start, "\"pixel_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = true;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"pixel_emitter_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = true;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"text_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = true;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"scroll_text_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = true;
            in_music_cues = false;
        } else if (strstr(start, "\"music_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = true;
            in_mesh_cues = false;
        } else if (strstr(start, "\"mesh_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = true;
            in_post_effects = false;
        } else if (strstr(start, "\"scene_layer_post_effects\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = false;
            in_post_effects = false;
            in_scene_layer_post_effects = true;
        } else if (strstr(start, "\"post_effects\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_animated_sprite_cues = false;
            in_pixel_cues = false;
            in_pixel_emitter_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = false;
            in_post_effects = true;
            in_scene_layer_post_effects = false;
        } else if (strstr(start, "\"curves\":")) {
            in_curves = true;
        }

        if (in_scene_layer_post_effects && current_scene) {
            ParseLayerPostEffectField(start, current_scene->scene_layer_post_effects,
                                      &current_scene->scene_layer_post_effect_count);
        }

        if (in_post_effects && current_scene) {
            if (strstr(start, "\"type\":")) {
                sscanf_s(start, "\"type\": %d", &current_post_effect.type);
            } else if (strstr(start, "\"enabled\":")) {
                sscanf_s(start, "\"enabled\": %d", &bool_value);
                current_post_effect.enabled = (bool_value != 0);
            } else if (strstr(start, "\"order\":")) {
                sscanf_s(start, "\"order\": %d", &current_post_effect.order);
            } else if (strstr(start, "\"intensity\":")) {
                sscanf_s(start, "\"intensity\": %f", &current_post_effect.intensity);
            } else if (strstr(start, "\"threshold\":")) {
                sscanf_s(start, "\"threshold\": %f", &current_post_effect.threshold);
            } else if (strstr(start, "\"radius\":")) {
                sscanf_s(start, "\"radius\": %f", &current_post_effect.radius);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f, %f]",
                    &current_post_effect.color[0], &current_post_effect.color[1],
                    &current_post_effect.color[2], &current_post_effect.color[3]);
            } else if (strstr(start, "\"start_time\":")) {
                sscanf_s(start, "\"start_time\": %f", &current_post_effect.start_time);
            } else if (strstr(start, "\"end_time\":")) {
                sscanf_s(start, "\"end_time\": %f", &current_post_effect.end_time);
            } else if (strstr(start, "\"curve_intensity\":")) {
                sscanf_s(start, "\"curve_intensity\": %d", &current_post_effect.curve_intensity);
            } else if (strstr(start, "\"curve_threshold\":")) {
                sscanf_s(start, "\"curve_threshold\": %d", &current_post_effect.curve_threshold);
            } else if (strstr(start, "\"curve_radius\":")) {
                sscanf_s(start, "\"curve_radius\": %d", &current_post_effect.curve_radius);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_post_effect.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_post_effect.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_post_effect.curve_color_b);
            } else if (strstr(start, "\"curve_color_a\":")) {
                sscanf_s(start, "\"curve_color_a\": %d", &current_post_effect.curve_color_a);
            } else if (strstr(start, "\"curve_amount\":")) {
                sscanf_s(start, "\"curve_amount\": %d", &current_post_effect.curve_amount);
            } else if (indent == 8 && start[0] == '}' && current_post_effect.type >= 0) {
                AddPostEffect(current_scene, current_post_effect);
                memset(&current_post_effect, 0, sizeof(current_post_effect));
                current_post_effect.enabled = true;
                current_post_effect.intensity = 1.0f;
                current_post_effect.threshold = 1.0f;
                current_post_effect.radius = 1.0f;
                current_post_effect.color[0] = current_post_effect.color[1] = current_post_effect.color[2] = current_post_effect.color[3] = 1.0f;
                current_post_effect.curve_intensity = current_post_effect.curve_threshold = current_post_effect.curve_radius = -1;
                current_post_effect.curve_color_r = current_post_effect.curve_color_g = current_post_effect.curve_color_b = current_post_effect.curve_color_a = -1;
                current_post_effect.curve_amount = -1;
                current_post_effect.end_time = -1.0f;
            }
        }

        // Parse animated sprite cue fields
        if (in_animated_sprite_cues && current_scene) {
            if (ParseAssetShaderField(start, current_animated_sprite_cue.shaders,
                                      &current_animated_sprite_cue.shader_count)) {
                // Parsed a per-asset shader field.
            } else if (ParseLayerPostEffectField(start, current_animated_sprite_cue.post_effects,
                                          &current_animated_sprite_cue.post_effect_count)) {
                // Parsed a per-layer post-effect field.
            } else if (strstr(start, "\"sprite_name\":")) {
                ParseJsonStringValue(start, current_animated_sprite_cue.sprite_name, sizeof(current_animated_sprite_cue.sprite_name));
            } else if (strstr(start, "\"frame_keys_csv\":")) {
                ParseJsonStringValue(start, current_animated_sprite_cue.frame_keys_csv, sizeof(current_animated_sprite_cue.frame_keys_csv));
            } else if (strstr(start, "\"frame_paths_csv\":")) {
                ParseJsonStringValue(start, current_animated_sprite_cue.frame_paths_csv, sizeof(current_animated_sprite_cue.frame_paths_csv));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_animated_sprite_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_animated_sprite_cue.y);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": %f", &current_animated_sprite_cue.scale);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": %f", &current_animated_sprite_cue.rotation);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_animated_sprite_cue.opacity);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_animated_sprite_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_animated_sprite_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_animated_sprite_cue.cue_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_animated_sprite_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_animated_sprite_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_animated_sprite_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_animated_sprite_cue.fade_out_end);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_animated_sprite_cue.layer_order);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_animated_sprite_cue.blend_mode);
            } else if (strstr(start, "\"fps\":")) {
                sscanf_s(start, "\"fps\": %f", &current_animated_sprite_cue.fps);
            } else if (strstr(start, "\"playback_mode\":")) {
                sscanf_s(start, "\"playback_mode\": %d", &current_animated_sprite_cue.playback_mode);
            } else if (strstr(start, "\"start_frame\":")) {
                sscanf_s(start, "\"start_frame\": %d", &current_animated_sprite_cue.start_frame);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_animated_sprite_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_animated_sprite_cue.curve_y);
            } else if (strstr(start, "\"curve_scale\":")) {
                sscanf_s(start, "\"curve_scale\": %d", &current_animated_sprite_cue.curve_scale);
            } else if (strstr(start, "\"curve_rotation\":")) {
                sscanf_s(start, "\"curve_rotation\": %d", &current_animated_sprite_cue.curve_rotation);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_animated_sprite_cue.curve_opacity);
            } else if (strstr(start, "\"curve_frame\":")) {
                sscanf_s(start, "\"curve_frame\": %d", &current_animated_sprite_cue.curve_frame);
            } else if (indent == 8 && start[0] == '}' && current_animated_sprite_cue.frame_keys_csv[0] != '\0') {
                NormalizeAssetShaderCount(current_animated_sprite_cue.shaders,
                                          &current_animated_sprite_cue.shader_count);
                AddAnimatedSpriteCue(current_scene, current_animated_sprite_cue);
                memset(&current_animated_sprite_cue, 0, sizeof(current_animated_sprite_cue));
                current_animated_sprite_cue.fps = 12.0f;
                current_animated_sprite_cue.playback_mode = 0;
                current_animated_sprite_cue.start_frame = 0;
                current_animated_sprite_cue.curve_x = -1;
                current_animated_sprite_cue.curve_y = -1;
                current_animated_sprite_cue.curve_scale = -1;
                current_animated_sprite_cue.curve_rotation = -1;
                current_animated_sprite_cue.curve_opacity = -1;
                current_animated_sprite_cue.curve_frame = -1;
            }
        }

        // Parse indexed pixel cue fields
        if (in_pixel_cues && current_scene) {
            if (ParseAssetShaderField(start, current_pixel_cue.shaders,
                                      &current_pixel_cue.shader_count)) {
            } else if (ParseLayerPostEffectField(start, current_pixel_cue.post_effects,
                                                  &current_pixel_cue.post_effect_count)) {
            } else if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_pixel_cue.asset_key, sizeof(current_pixel_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                ParseJsonStringValue(start, current_pixel_cue.asset_path, sizeof(current_pixel_cue.asset_path));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_pixel_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_pixel_cue.y);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": %f", &current_pixel_cue.scale);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": %f", &current_pixel_cue.rotation);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_pixel_cue.opacity);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_pixel_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_pixel_cue.cue_end);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_pixel_cue.layer_order);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_pixel_cue.blend_mode);
            } else if (strstr(start, "\"fps\":")) {
                sscanf_s(start, "\"fps\": %f", &current_pixel_cue.fps);
            } else if (strstr(start, "\"playback_mode\":")) {
                sscanf_s(start, "\"playback_mode\": %d", &current_pixel_cue.playback_mode);
            } else if (strstr(start, "\"start_frame\":")) {
                sscanf_s(start, "\"start_frame\": %d", &current_pixel_cue.start_frame);
            } else if (strstr(start, "\"palette_offset\":")) {
                sscanf_s(start, "\"palette_offset\": %d", &current_pixel_cue.palette_offset);
            } else if (strstr(start, "\"palette_cycle_speed\":")) {
                sscanf_s(start, "\"palette_cycle_speed\": %d", &current_pixel_cue.palette_cycle_speed);
            } else if (strstr(start, "\"snap_to_pixels\":")) {
                sscanf_s(start, "\"snap_to_pixels\": %d", &current_pixel_cue.snap_to_pixels);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_pixel_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_pixel_cue.curve_y);
            } else if (strstr(start, "\"curve_scale\":")) {
                sscanf_s(start, "\"curve_scale\": %d", &current_pixel_cue.curve_scale);
            } else if (strstr(start, "\"curve_rotation\":")) {
                sscanf_s(start, "\"curve_rotation\": %d", &current_pixel_cue.curve_rotation);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_pixel_cue.curve_opacity);
            } else if (strstr(start, "\"curve_frame\":")) {
                sscanf_s(start, "\"curve_frame\": %d", &current_pixel_cue.curve_frame);
            } else if (strstr(start, "\"curve_palette_offset\":")) {
                sscanf_s(start, "\"curve_palette_offset\": %d", &current_pixel_cue.curve_palette_offset);
            } else if (indent == 8 && start[0] == '}' && current_pixel_cue.asset_key[0] != '\0') {
                NormalizeAssetShaderCount(current_pixel_cue.shaders,
                                          &current_pixel_cue.shader_count);
                AddPixelCue(current_scene, current_pixel_cue);
                memset(&current_pixel_cue, 0, sizeof(current_pixel_cue));
                current_pixel_cue.fps = 12.0f;
                current_pixel_cue.playback_mode = 0;
                current_pixel_cue.snap_to_pixels = 1;
                current_pixel_cue.curve_x = -1;
                current_pixel_cue.curve_y = -1;
                current_pixel_cue.curve_scale = -1;
                current_pixel_cue.curve_rotation = -1;
                current_pixel_cue.curve_opacity = -1;
                current_pixel_cue.curve_frame = -1;
                current_pixel_cue.curve_palette_offset = -1;
            }
        }
        
        // Parse shader cue fields
        if (in_shader_cues && current_scene) {
            if (strstr(start, "\"shader_name\":")) {
                ParseJsonStringValue(start, current_shader_cue.shader_name, sizeof(current_shader_cue.shader_name));
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
            } else if (strstr(start, "\"position\":")) {
                sscanf_s(start, "\"position\": [%f, %f, %f]",
                    &current_shader_cue.position_x,
                    &current_shader_cue.position_y,
                    &current_shader_cue.position_z);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": [%f, %f, %f]",
                    &current_shader_cue.rotation_x,
                    &current_shader_cue.rotation_y,
                    &current_shader_cue.rotation_z);
            } else if (strstr(start, "\"motion\":")) {
                sscanf_s(start, "\"motion\": [%f, %f, %f]",
                    &current_shader_cue.motion_x,
                    &current_shader_cue.motion_y,
                    &current_shader_cue.motion_z);
            } else if (strstr(start, "\"noise_enabled\":")) {
                sscanf_s(start, "\"noise_enabled\": %d", &current_shader_cue.noise.enabled);
            } else if (strstr(start, "\"noise_type\":")) {
                sscanf_s(start, "\"noise_type\": %d", &current_shader_cue.noise.type);
            } else if (strstr(start, "\"noise_scale\":")) {
                sscanf_s(start, "\"noise_scale\": %f", &current_shader_cue.noise.scale);
            } else if (strstr(start, "\"noise_strength\":")) {
                sscanf_s(start, "\"noise_strength\": %f", &current_shader_cue.noise.strength);
            } else if (strstr(start, "\"noise_octaves\":")) {
                sscanf_s(start, "\"noise_octaves\": %f", &current_shader_cue.noise.octaves);
            } else if (strstr(start, "\"noise_lacunarity\":")) {
                sscanf_s(start, "\"noise_lacunarity\": %f", &current_shader_cue.noise.lacunarity);
            } else if (strstr(start, "\"noise_gain\":")) {
                sscanf_s(start, "\"noise_gain\": %f", &current_shader_cue.noise.gain);
            } else if (strstr(start, "\"noise_warp\":")) {
                sscanf_s(start, "\"noise_warp\": %f", &current_shader_cue.noise.warp);
            } else if (strstr(start, "\"noise_speed\":")) {
                sscanf_s(start, "\"noise_speed\": [%f, %f]",
                    &current_shader_cue.noise.speed_x,
                    &current_shader_cue.noise.speed_y);
            } else if (strstr(start, "\"noise_seed\":")) {
                sscanf_s(start, "\"noise_seed\": %f", &current_shader_cue.noise.seed);
            } else if (strstr(start, "\"noise_contrast\":")) {
                sscanf_s(start, "\"noise_contrast\": %f", &current_shader_cue.noise.contrast);
            } else if (strstr(start, "\"noise_map_0\":")) {
                ParseJsonStringValue(start, current_shader_cue.noise_textures.paths[0], sizeof(current_shader_cue.noise_textures.paths[0]));
            } else if (strstr(start, "\"noise_map_1\":")) {
                ParseJsonStringValue(start, current_shader_cue.noise_textures.paths[1], sizeof(current_shader_cue.noise_textures.paths[1]));
            } else if (strstr(start, "\"noise_map_2\":")) {
                ParseJsonStringValue(start, current_shader_cue.noise_textures.paths[2], sizeof(current_shader_cue.noise_textures.paths[2]));
            } else if (strstr(start, "\"noise_map_3\":")) {
                ParseJsonStringValue(start, current_shader_cue.noise_textures.paths[3], sizeof(current_shader_cue.noise_textures.paths[3]));
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
            } else if (strstr(start, "\"curve_palette_low_r\":")) {
                sscanf_s(start, "\"curve_palette_low_r\": %d", &current_shader_cue.curve_palette_low_r);
            } else if (strstr(start, "\"curve_palette_low_g\":")) {
                sscanf_s(start, "\"curve_palette_low_g\": %d", &current_shader_cue.curve_palette_low_g);
            } else if (strstr(start, "\"curve_palette_low_b\":")) {
                sscanf_s(start, "\"curve_palette_low_b\": %d", &current_shader_cue.curve_palette_low_b);
            } else if (strstr(start, "\"curve_palette_mid_r\":")) {
                sscanf_s(start, "\"curve_palette_mid_r\": %d", &current_shader_cue.curve_palette_mid_r);
            } else if (strstr(start, "\"curve_palette_mid_g\":")) {
                sscanf_s(start, "\"curve_palette_mid_g\": %d", &current_shader_cue.curve_palette_mid_g);
            } else if (strstr(start, "\"curve_palette_mid_b\":")) {
                sscanf_s(start, "\"curve_palette_mid_b\": %d", &current_shader_cue.curve_palette_mid_b);
            } else if (strstr(start, "\"curve_palette_high_r\":")) {
                sscanf_s(start, "\"curve_palette_high_r\": %d", &current_shader_cue.curve_palette_high_r);
            } else if (strstr(start, "\"curve_palette_high_g\":")) {
                sscanf_s(start, "\"curve_palette_high_g\": %d", &current_shader_cue.curve_palette_high_g);
            } else if (strstr(start, "\"curve_palette_high_b\":")) {
                sscanf_s(start, "\"curve_palette_high_b\": %d", &current_shader_cue.curve_palette_high_b);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_shader_cue.curve_opacity);
            } else if (strstr(start, "\"curve_exposure_ramp\":")) {
                sscanf_s(start, "\"curve_exposure_ramp\": %d", &current_shader_cue.curve_exposure_ramp);
            } else if (strstr(start, "\"curve_fade_ramp\":")) {
                sscanf_s(start, "\"curve_fade_ramp\": %d", &current_shader_cue.curve_fade_ramp);
            } else if (indent == 8 && start[0] == '}' && current_shader_cue.shader_name[0] != '\0') {
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
            if (ParseAssetShaderField(start, current_image_cue.shaders,
                                      &current_image_cue.shader_count)) {
                // Parsed a per-asset shader field.
            } else if (ParseLayerPostEffectField(start, current_image_cue.post_effects,
                                          &current_image_cue.post_effect_count)) {
                // Parsed a per-layer post-effect field.
            } else if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_image_cue.asset_key, sizeof(current_image_cue.asset_key));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_image_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_image_cue.y);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": %f", &current_image_cue.scale);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": %f", &current_image_cue.rotation);
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
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_image_cue.blend_mode);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_image_cue.layer_order);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_image_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_image_cue.curve_y);
            } else if (strstr(start, "\"curve_scale\":")) {
                sscanf_s(start, "\"curve_scale\": %d", &current_image_cue.curve_scale);
            } else if (strstr(start, "\"curve_rotation\":")) {
                sscanf_s(start, "\"curve_rotation\": %d", &current_image_cue.curve_rotation);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_image_cue.curve_opacity);
            } else if (indent == 8 && start[0] == '}' && current_image_cue.asset_key[0] != '\0') {
                // End of image cue object - add it
                // Initialize curve fields to -1 if not loaded (backwards compatibility)
                if (current_image_cue.curve_x == 0 && current_image_cue.curve_y == 0 && 
                    current_image_cue.curve_scale == 0 && current_image_cue.curve_opacity == 0) {
                    current_image_cue.curve_x = -1;
                    current_image_cue.curve_y = -1;
                    current_image_cue.curve_scale = -1;
                    current_image_cue.curve_rotation = -1;
                    current_image_cue.curve_opacity = -1;
                }
                NormalizeAssetShaderCount(current_image_cue.shaders, &current_image_cue.shader_count);
                printf("[LoadProject] Loaded image cue: %s pos=(%.2f,%.2f) scale=%.2f\n",
                       current_image_cue.asset_key, current_image_cue.x, current_image_cue.y, current_image_cue.scale);
                AddImageCue(current_scene, current_image_cue);
                memset(&current_image_cue, 0, sizeof(current_image_cue));
                current_image_cue.curve_x = current_image_cue.curve_y = current_image_cue.curve_scale = -1;
                current_image_cue.curve_rotation = current_image_cue.curve_opacity = -1;
            }
        }

        if (in_pixel_emitter_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_pixel_emitter_cue.asset_key,
                                     sizeof(current_pixel_emitter_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                ParseJsonStringValue(start, current_pixel_emitter_cue.asset_path,
                                     sizeof(current_pixel_emitter_cue.asset_path));
            } else if (strstr(start, "\"visual_source\":")) {
                sscanf_s(start, "\"visual_source\": %d", &current_pixel_emitter_cue.visual_source);
            } else if (strstr(start, "\"primitive_shape\":")) {
                sscanf_s(start, "\"primitive_shape\": %d", &current_pixel_emitter_cue.primitive_shape);
            } else if (strstr(start, "\"primitive_color_r\":")) {
                sscanf_s(start, "\"primitive_color_r\": %f", &current_pixel_emitter_cue.primitive_color[0]);
            } else if (strstr(start, "\"primitive_color_g\":")) {
                sscanf_s(start, "\"primitive_color_g\": %f", &current_pixel_emitter_cue.primitive_color[1]);
            } else if (strstr(start, "\"primitive_color_b\":")) {
                sscanf_s(start, "\"primitive_color_b\": %f", &current_pixel_emitter_cue.primitive_color[2]);
            } else if (strstr(start, "\"primitive_color_a\":")) {
                sscanf_s(start, "\"primitive_color_a\": %f", &current_pixel_emitter_cue.primitive_color[3]);
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_pixel_emitter_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_pixel_emitter_cue.y);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": %f", &current_pixel_emitter_cue.scale);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": %f", &current_pixel_emitter_cue.rotation);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_pixel_emitter_cue.opacity);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_pixel_emitter_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_pixel_emitter_cue.cue_end);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_pixel_emitter_cue.layer_order);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_pixel_emitter_cue.blend_mode);
            } else if (strstr(start, "\"max_particles\":")) {
                sscanf_s(start, "\"max_particles\": %d", &current_pixel_emitter_cue.max_particles);
            } else if (strstr(start, "\"emission_rate\":")) {
                sscanf_s(start, "\"emission_rate\": %f", &current_pixel_emitter_cue.emission_rate);
            } else if (strstr(start, "\"burst_count\":")) {
                sscanf_s(start, "\"burst_count\": %d", &current_pixel_emitter_cue.burst_count);
            } else if (strstr(start, "\"duration\":")) {
                sscanf_s(start, "\"duration\": %f", &current_pixel_emitter_cue.duration);
            } else if (strstr(start, "\"loop\":")) {
                sscanf_s(start, "\"loop\": %d", &current_pixel_emitter_cue.loop);
            } else if (strstr(start, "\"speed_min\":")) {
                sscanf_s(start, "\"speed_min\": %f", &current_pixel_emitter_cue.speed_min);
            } else if (strstr(start, "\"speed_max\":")) {
                sscanf_s(start, "\"speed_max\": %f", &current_pixel_emitter_cue.speed_max);
            } else if (strstr(start, "\"lifetime_min\":")) {
                sscanf_s(start, "\"lifetime_min\": %f", &current_pixel_emitter_cue.lifetime_min);
            } else if (strstr(start, "\"lifetime_max\":")) {
                sscanf_s(start, "\"lifetime_max\": %f", &current_pixel_emitter_cue.lifetime_max);
            } else if (strstr(start, "\"scale_min\":")) {
                sscanf_s(start, "\"scale_min\": %f", &current_pixel_emitter_cue.scale_min);
            } else if (strstr(start, "\"scale_max\":")) {
                sscanf_s(start, "\"scale_max\": %f", &current_pixel_emitter_cue.scale_max);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_pixel_emitter_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_pixel_emitter_cue.curve_y);
            } else if (strstr(start, "\"curve_scale\":")) {
                sscanf_s(start, "\"curve_scale\": %d", &current_pixel_emitter_cue.curve_scale);
            } else if (strstr(start, "\"curve_rotation\":")) {
                sscanf_s(start, "\"curve_rotation\": %d", &current_pixel_emitter_cue.curve_rotation);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_pixel_emitter_cue.curve_opacity);
            } else if (strstr(start, "\"curve_emission_rate\":")) {
                sscanf_s(start, "\"curve_emission_rate\": %d", &current_pixel_emitter_cue.curve_emission_rate);
            } else if (strstr(start, "\"curve_speed_min\":")) {
                sscanf_s(start, "\"curve_speed_min\": %d", &current_pixel_emitter_cue.curve_speed_min);
            } else if (strstr(start, "\"curve_speed_max\":")) {
                sscanf_s(start, "\"curve_speed_max\": %d", &current_pixel_emitter_cue.curve_speed_max);
            } else if (strstr(start, "\"curve_lifetime_min\":")) {
                sscanf_s(start, "\"curve_lifetime_min\": %d", &current_pixel_emitter_cue.curve_lifetime_min);
            } else if (strstr(start, "\"curve_lifetime_max\":")) {
                sscanf_s(start, "\"curve_lifetime_max\": %d", &current_pixel_emitter_cue.curve_lifetime_max);
            } else if (strstr(start, "\"curve_scale_min\":")) {
                sscanf_s(start, "\"curve_scale_min\": %d", &current_pixel_emitter_cue.curve_scale_min);
            } else if (strstr(start, "\"curve_scale_max\":")) {
                sscanf_s(start, "\"curve_scale_max\": %d", &current_pixel_emitter_cue.curve_scale_max);
            } else if (strstr(start, "\"seed\":")) {
                sscanf_s(start, "\"seed\": %u", &current_pixel_emitter_cue.seed);
            } else if (indent == 8 && start[0] == '}' &&
                       (current_pixel_emitter_cue.asset_key[0] != '\0' ||
                        current_pixel_emitter_cue.visual_source == 1)) {
                AddPixelEmitterCue(current_scene, current_pixel_emitter_cue);
                memset(&current_pixel_emitter_cue, 0, sizeof(current_pixel_emitter_cue));
                current_pixel_emitter_cue.curve_x = current_pixel_emitter_cue.curve_y = -1;
                current_pixel_emitter_cue.curve_scale = current_pixel_emitter_cue.curve_rotation = -1;
                current_pixel_emitter_cue.curve_opacity = current_pixel_emitter_cue.curve_emission_rate = -1;
                current_pixel_emitter_cue.curve_speed_min = current_pixel_emitter_cue.curve_speed_max = -1;
                current_pixel_emitter_cue.curve_lifetime_min = current_pixel_emitter_cue.curve_lifetime_max = -1;
                current_pixel_emitter_cue.curve_scale_min = current_pixel_emitter_cue.curve_scale_max = -1;
            }
        }

        // Parse text cue fields
        if (in_text_cues && current_scene) {
            if (strstr(start, "\"text\":")) {
                // Robust parse: handle both valid JSON escaped text and legacy multiline text
                // that may span multiple physical lines in old project files.
                char escaped_text[512] = {};
                size_t out_len = 0;
                char* colon = strchr(start, ':');
                if (colon) {
                    char* p = colon + 1;
                    while (*p == ' ' || *p == '\t') ++p;
                    if (*p == '"') {
                        ++p; // skip opening quote
                        bool done = false;
                        char continuation[1024];

                        while (!done && out_len + 1 < sizeof(escaped_text)) {
                            for (; *p && out_len + 1 < sizeof(escaped_text); ++p) {
                                // End at unescaped quote
                                if (*p == '"' && (p == start || *(p - 1) != '\\')) {
                                    done = true;
                                    break;
                                }
                                if (*p == '\r' || *p == '\n') continue;
                                escaped_text[out_len++] = *p;
                            }

                            if (done) break;

                            // Legacy recovery: text continued on next line in malformed JSON.
                            if (out_len + 1 < sizeof(escaped_text)) {
                                escaped_text[out_len++] = '\\';
                            }
                            if (out_len + 1 < sizeof(escaped_text)) {
                                escaped_text[out_len++] = 'n';
                            }

                            if (!fgets(continuation, sizeof(continuation), f)) break;
                            p = continuation;
                            while (*p == ' ' || *p == '\t') ++p;
                        }
                        escaped_text[out_len] = '\0';
                    }
                }

                if (escaped_text[0] != '\0') {
                    JsonUnescapeString(escaped_text, current_text_cue.text, sizeof(current_text_cue.text));
                }
            } else if (strstr(start, "\"font_name\":")) {
                ParseJsonStringValue(start, current_text_cue.font_name, sizeof(current_text_cue.font_name));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_text_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_text_cue.y);
            } else if (strstr(start, "\"size\":")) {
                sscanf_s(start, "\"size\": %f", &current_text_cue.size);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f]",
                    &current_text_cue.color.r, &current_text_cue.color.g, &current_text_cue.color.b);
            } else if (strstr(start, "\"animation\":")) {
                char serialized_animation[4096] = {};
                ParseJsonStringValue(start, serialized_animation, sizeof(serialized_animation));
                DeserializeTextAnimation(serialized_animation, &current_text_cue.animation);
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
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_text_cue.blend_mode);
            } else if (strstr(start, "\"bake_mode\":")) {
                sscanf_s(start, "\"bake_mode\": %d", &current_text_cue.bake_mode);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_text_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_text_cue.curve_y);
            } else if (strstr(start, "\"curve_size\":")) {
                sscanf_s(start, "\"curve_size\": %d", &current_text_cue.curve_size);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": %f", &current_text_cue.rotation);
            } else if (strstr(start, "\"curve_rotation\":")) {
                sscanf_s(start, "\"curve_rotation\": %d", &current_text_cue.curve_rotation);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_text_cue.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_text_cue.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_text_cue.curve_color_b);
            } else if (strstr(start, "\"baked_asset_key\":")) {
                ParseJsonStringValue(start, current_text_cue.baked_asset_key, sizeof(current_text_cue.baked_asset_key));
            } else if (strstr(start, "\"baked_asset_path\":")) {
                ParseJsonStringValue(start, current_text_cue.baked_asset_path, sizeof(current_text_cue.baked_asset_path));
            } else if (strstr(start, "\"glyph_atlas_key\":")) {
                ParseJsonStringValue(start, current_text_cue.glyph_atlas_key, sizeof(current_text_cue.glyph_atlas_key));
            } else if (strstr(start, "\"glyph_atlas_path\":")) {
                ParseJsonStringValue(start, current_text_cue.glyph_atlas_path, sizeof(current_text_cue.glyph_atlas_path));
            } else if (strstr(start, "\"glyph_meta_key\":")) {
                ParseJsonStringValue(start, current_text_cue.glyph_meta_key, sizeof(current_text_cue.glyph_meta_key));
            } else if (strstr(start, "\"glyph_meta_path\":")) {
                ParseJsonStringValue(start, current_text_cue.glyph_meta_path, sizeof(current_text_cue.glyph_meta_path));
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_text_cue.layer_order);
            } else if (indent == 8 && start[0] == '}' && current_text_cue.text[0] != '\0') {
                AddTextCue(current_scene, current_text_cue);
                memset(&current_text_cue, 0, sizeof(current_text_cue));
                current_text_cue.curve_x = -1;
                current_text_cue.curve_y = -1;
                current_text_cue.curve_size = -1;
                current_text_cue.curve_rotation = -1;
                current_text_cue.curve_color_r = -1;
                current_text_cue.curve_color_g = -1;
                current_text_cue.curve_color_b = -1;
                current_text_cue.blend_mode = 0;
                current_text_cue.bake_mode = 0;
                InitializeTextAnimationConfig(&current_text_cue.animation);
            }
        }

        // Parse scroll text cue fields
        if (in_scroll_text_cues && current_scene) {
            if (strstr(start, "\"text\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.text, sizeof(current_scroll_text_cue.text));
            } else if (strstr(start, "\"font_name\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.font_name, sizeof(current_scroll_text_cue.font_name));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_scroll_text_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_scroll_text_cue.y);
            } else if (strstr(start, "\"size\":")) {
                sscanf_s(start, "\"size\": %f", &current_scroll_text_cue.size);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f]",
                    &current_scroll_text_cue.color.r, &current_scroll_text_cue.color.g, &current_scroll_text_cue.color.b);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_scroll_text_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_scroll_text_cue.cue_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_scroll_text_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_scroll_text_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_scroll_text_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_scroll_text_cue.fade_out_end);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_scroll_text_cue.opacity);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_scroll_text_cue.layer_order);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_scroll_text_cue.blend_mode);
            } else if (strstr(start, "\"style_id\":")) {
                sscanf_s(start, "\"style_id\": %d", &current_scroll_text_cue.style_id);
            } else if (strstr(start, "\"direction\":")) {
                sscanf_s(start, "\"direction\": %d", &current_scroll_text_cue.direction);
            } else if (strstr(start, "\"loop_mode\":")) {
                sscanf_s(start, "\"loop_mode\": %d", &current_scroll_text_cue.loop_mode);
            } else if (strstr(start, "\"speed\":")) {
                sscanf_s(start, "\"speed\": %f", &current_scroll_text_cue.speed);
            } else if (strstr(start, "\"wrap_gap\":")) {
                sscanf_s(start, "\"wrap_gap\": %f", &current_scroll_text_cue.wrap_gap);
            } else if (strstr(start, "\"spacing\":")) {
                sscanf_s(start, "\"spacing\": %f", &current_scroll_text_cue.spacing);
            } else if (strstr(start, "\"slant_deg\":")) {
                sscanf_s(start, "\"slant_deg\": %f", &current_scroll_text_cue.slant_deg);
            } else if (strstr(start, "\"wave_amp\":")) {
                sscanf_s(start, "\"wave_amp\": %f", &current_scroll_text_cue.wave_amp);
            } else if (strstr(start, "\"wave_freq\":")) {
                sscanf_s(start, "\"wave_freq\": %f", &current_scroll_text_cue.wave_freq);
            } else if (strstr(start, "\"wave_length\":")) {
                sscanf_s(start, "\"wave_length\": %f", &current_scroll_text_cue.wave_length);
            } else if (strstr(start, "\"jitter_amp\":")) {
                sscanf_s(start, "\"jitter_amp\": %f", &current_scroll_text_cue.jitter_amp);
            } else if (strstr(start, "\"jitter_freq\":")) {
                sscanf_s(start, "\"jitter_freq\": %f", &current_scroll_text_cue.jitter_freq);
            } else if (strstr(start, "\"glow\":")) {
                sscanf_s(start, "\"glow\": %f", &current_scroll_text_cue.glow);
            } else if (strstr(start, "\"shadow\":")) {
                sscanf_s(start, "\"shadow\": %f", &current_scroll_text_cue.shadow);
            } else if (strstr(start, "\"outline\":")) {
                sscanf_s(start, "\"outline\": %f", &current_scroll_text_cue.outline);
            } else if (strstr(start, "\"chroma_shift\":")) {
                sscanf_s(start, "\"chroma_shift\": %f", &current_scroll_text_cue.chroma_shift);
            } else if (strstr(start, "\"distortion\":")) {
                sscanf_s(start, "\"distortion\": %f", &current_scroll_text_cue.distortion);
            } else if (strstr(start, "\"bake_mode\":")) {
                sscanf_s(start, "\"bake_mode\": %d", &current_scroll_text_cue.bake_mode);
            } else if (strstr(start, "\"baked_asset_key\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.baked_asset_key, sizeof(current_scroll_text_cue.baked_asset_key));
            } else if (strstr(start, "\"baked_asset_path\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.baked_asset_path, sizeof(current_scroll_text_cue.baked_asset_path));
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_scroll_text_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_scroll_text_cue.curve_y);
            } else if (strstr(start, "\"curve_speed\":")) {
                sscanf_s(start, "\"curve_speed\": %d", &current_scroll_text_cue.curve_speed);
            } else if (strstr(start, "\"curve_size\":")) {
                sscanf_s(start, "\"curve_size\": %d", &current_scroll_text_cue.curve_size);
            } else if (strstr(start, "\"rotation\":")) {
                sscanf_s(start, "\"rotation\": %f", &current_scroll_text_cue.rotation);
            } else if (strstr(start, "\"curve_rotation\":")) {
                sscanf_s(start, "\"curve_rotation\": %d", &current_scroll_text_cue.curve_rotation);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_scroll_text_cue.curve_opacity);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_scroll_text_cue.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_scroll_text_cue.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_scroll_text_cue.curve_color_b);
            } else if (strstr(start, "\"curve_wave_amp\":")) {
                sscanf_s(start, "\"curve_wave_amp\": %d", &current_scroll_text_cue.curve_wave_amp);
            } else if (strstr(start, "\"curve_wave_freq\":")) {
                sscanf_s(start, "\"curve_wave_freq\": %d", &current_scroll_text_cue.curve_wave_freq);
            } else if (strstr(start, "\"curve_wave_length\":")) {
                sscanf_s(start, "\"curve_wave_length\": %d", &current_scroll_text_cue.curve_wave_length);
            } else if (strstr(start, "\"curve_jitter_amp\":")) {
                sscanf_s(start, "\"curve_jitter_amp\": %d", &current_scroll_text_cue.curve_jitter_amp);
            } else if (strstr(start, "\"curve_jitter_freq\":")) {
                sscanf_s(start, "\"curve_jitter_freq\": %d", &current_scroll_text_cue.curve_jitter_freq);
            } else if (indent == 8 && start[0] == '}' && current_scroll_text_cue.text[0] != '\0') {
                AddScrollTextCue(current_scene, current_scroll_text_cue);
                memset(&current_scroll_text_cue, 0, sizeof(current_scroll_text_cue));
                strncpy_s(current_scroll_text_cue.font_name, sizeof(current_scroll_text_cue.font_name), "Arial", _TRUNCATE);
                current_scroll_text_cue.size = 40.0f;
                current_scroll_text_cue.color = {1.0f, 1.0f, 1.0f};
                current_scroll_text_cue.opacity = 1.0f;
                current_scroll_text_cue.wrap_gap = 0.2f;
                current_scroll_text_cue.speed = 0.25f;
                current_scroll_text_cue.spacing = 1.0f;
                current_scroll_text_cue.wave_freq = 1.0f;
                current_scroll_text_cue.wave_length = 9.0f;
                current_scroll_text_cue.jitter_freq = 1.0f;
                current_scroll_text_cue.bake_mode = 0;
                current_scroll_text_cue.curve_x = -1;
                current_scroll_text_cue.curve_y = -1;
                current_scroll_text_cue.curve_speed = -1;
                current_scroll_text_cue.curve_size = -1;
                current_scroll_text_cue.curve_rotation = -1;
                current_scroll_text_cue.curve_opacity = -1;
                current_scroll_text_cue.curve_color_r = -1;
                current_scroll_text_cue.curve_color_g = -1;
                current_scroll_text_cue.curve_color_b = -1;
                current_scroll_text_cue.curve_wave_amp = -1;
                current_scroll_text_cue.curve_wave_freq = -1;
                current_scroll_text_cue.curve_wave_length = -1;
                current_scroll_text_cue.curve_jitter_amp = -1;
                current_scroll_text_cue.curve_jitter_freq = -1;
            }
        }

        // Parse music cue fields
        if (in_music_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_music_cue.asset_key, sizeof(current_music_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                ParseJsonStringValue(start, current_music_cue.asset_path, sizeof(current_music_cue.asset_path));
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_music_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_music_cue.cue_end);
            } else if (indent == 8 && start[0] == '}' && current_music_cue.asset_key[0] != '\0') {
                printf("[LoadProject] Loaded music cue: %s path=%s\n",
                       current_music_cue.asset_key, current_music_cue.asset_path);
                AddMusicCue(current_scene, current_music_cue);
                memset(&current_music_cue, 0, sizeof(current_music_cue));
            }
        }

        // Parse mesh cue fields
        if (in_mesh_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_mesh_cue.asset_key, sizeof(current_mesh_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                ParseJsonStringValue(start, current_mesh_cue.asset_path, sizeof(current_mesh_cue.asset_path));
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
            } else if (strstr(start, "\"emissive_color\":")) {
                sscanf_s(start, "\"emissive_color\": [%f, %f, %f]",
                    &current_mesh_cue.emissive_color[0],
                    &current_mesh_cue.emissive_color[1],
                    &current_mesh_cue.emissive_color[2]);
            } else if (strstr(start, "\"emissive_strength\":")) {
                sscanf_s(start, "\"emissive_strength\": %f", &current_mesh_cue.emissive_strength);
            } else if (strstr(start, "\"fov_deg\":")) {
                sscanf_s(start, "\"fov_deg\": %f", &current_mesh_cue.fov_deg);
            } else if (strstr(start, "\"cull_mode\":")) {
                sscanf_s(start, "\"cull_mode\": %d", &current_mesh_cue.cull_mode);
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
            } else if (strstr(start, "\"curve_pos_x\":")) {
                sscanf_s(start, "\"curve_pos_x\": %d", &current_mesh_cue.curve_pos_x);
            } else if (strstr(start, "\"curve_pos_y\":")) {
                sscanf_s(start, "\"curve_pos_y\": %d", &current_mesh_cue.curve_pos_y);
            } else if (strstr(start, "\"curve_pos_z\":")) {
                sscanf_s(start, "\"curve_pos_z\": %d", &current_mesh_cue.curve_pos_z);
            } else if (strstr(start, "\"curve_rot_x\":")) {
                sscanf_s(start, "\"curve_rot_x\": %d", &current_mesh_cue.curve_rot_x);
            } else if (strstr(start, "\"curve_rot_y\":")) {
                sscanf_s(start, "\"curve_rot_y\": %d", &current_mesh_cue.curve_rot_y);
            } else if (strstr(start, "\"curve_rot_z\":")) {
                sscanf_s(start, "\"curve_rot_z\": %d", &current_mesh_cue.curve_rot_z);
            } else if (strstr(start, "\"curve_scale_x\":")) {
                sscanf_s(start, "\"curve_scale_x\": %d", &current_mesh_cue.curve_scale_x);
            } else if (strstr(start, "\"curve_scale_y\":")) {
                sscanf_s(start, "\"curve_scale_y\": %d", &current_mesh_cue.curve_scale_y);
            } else if (strstr(start, "\"curve_scale_z\":")) {
                sscanf_s(start, "\"curve_scale_z\": %d", &current_mesh_cue.curve_scale_z);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_mesh_cue.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_mesh_cue.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_mesh_cue.curve_color_b);
            } else if (strstr(start, "\"curve_color_a\":")) {
                sscanf_s(start, "\"curve_color_a\": %d", &current_mesh_cue.curve_color_a);
            } else if (strstr(start, "\"curve_mesh_size\":")) {
                sscanf_s(start, "\"curve_mesh_size\": %d", &current_mesh_cue.curve_mesh_size);
            } else if (strstr(start, "\"curve_metallic\":")) {
                sscanf_s(start, "\"curve_metallic\": %d", &current_mesh_cue.curve_metallic);
            } else if (strstr(start, "\"curve_roughness\":")) {
                sscanf_s(start, "\"curve_roughness\": %d", &current_mesh_cue.curve_roughness);
            } else if (strstr(start, "\"curve_fov\":")) {
                sscanf_s(start, "\"curve_fov\": %d", &current_mesh_cue.curve_fov);
            } else if (strstr(start, "\"use_imported_light\":")) {
                if (sscanf_s(start, "\"use_imported_light\": %d", &current_mesh_cue.use_imported_light) != 1) {
                    current_mesh_cue.use_imported_light = (strstr(start, "true") != nullptr) ? 1 : 0;
                }
            } else if (strstr(start, "\"use_imported_camera\":")) {
                if (sscanf_s(start, "\"use_imported_camera\": %d", &current_mesh_cue.use_imported_camera) != 1) {
                    current_mesh_cue.use_imported_camera = (strstr(start, "true") != nullptr) ? 1 : 0;
                }
            } else if (indent == 8 && start[0] == '}' && current_mesh_cue.asset_key[0] != '\0') {
                AddMeshCue(current_scene, current_mesh_cue);
                MeshCue blank = {};
                blank.scale[0] = blank.scale[1] = blank.scale[2] = 1.0f;
                blank.emissive_color[0] = 1.0f;
                blank.emissive_color[1] = 1.0f;
                blank.emissive_color[2] = 1.0f;
                blank.roughness = 0.5f;
                blank.fov_deg = 45.0f;
                blank.cull_mode = 0;
                blank.use_imported_light = 0;
                blank.use_imported_camera = 0;
                blank.curve_pos_x = blank.curve_pos_y = blank.curve_pos_z = -1;
                blank.curve_rot_x = blank.curve_rot_y = blank.curve_rot_z = -1;
                blank.curve_scale_x = blank.curve_scale_y = blank.curve_scale_z = -1;
                blank.curve_color_r = blank.curve_color_g = blank.curve_color_b = blank.curve_color_a = -1;
                blank.curve_mesh_size = blank.curve_metallic = blank.curve_roughness = blank.curve_fov = -1;
                current_mesh_cue = blank;
            }
        }

        // Parse curve data
        if (in_curves && !in_curve_points) {
            // Create new curve if we haven't yet for this curve object
            if (!current_curve && editor->project->curve_count < rev::runtime::kMaxCurves) {
                if (strstr(start, "\"wrap_mode\":") || strstr(start, "\"points\":")) {
                    current_curve = &editor->project->curves[editor->project->curve_count++];
                    *current_curve = rev::curve::CreateCurve();
                }
            }
            
            // Parse wrap mode
            if (current_curve && strstr(start, "\"wrap_mode\":")) {
                char wrap_mode[32];
                if (sscanf_s(start, "\"wrap_mode\": \"%31[^\"]\"", wrap_mode, (unsigned)sizeof(wrap_mode)) == 1) {
                    if (strcmp(wrap_mode, "loop") == 0) current_curve->wrap_mode = rev::curve::WrapMode::Loop;
                    else if (strcmp(wrap_mode, "pingpong") == 0) current_curve->wrap_mode = rev::curve::WrapMode::PingPong;
                    else if (strcmp(wrap_mode, "mirror") == 0) current_curve->wrap_mode = rev::curve::WrapMode::Mirror;
                    else current_curve->wrap_mode = rev::curve::WrapMode::Clamp;
                }
            }
            
            // Parse duration
            if (current_curve && strstr(start, "\"duration\":")) {
                float duration;
                if (sscanf_s(start, "\"duration\": %f", &duration) == 1) {
                    current_curve->duration = duration;
                }
            }
            if (current_curve && strstr(start, "\"name\":")) {
                sscanf_s(start, "\"name\": \"%127[^\"]\"",
                         editor->project->curve_names[editor->project->curve_count - 1],
                         (unsigned)sizeof(editor->project->curve_names[0]));
            }
            
            // Start parsing points
            if (strstr(start, "\"points\":")) {
                in_curve_points = true;
            }
        }
        
        // Parse individual curve points
        if (in_curve_points && current_curve && strstr(start, "{\"t\":")) {
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

    editor->project->total_duration = 0.0f;
    for (int i = 0; i < editor->project->scene_count; ++i) {
        if (editor->project->scenes[i].duration > 0.0f) {
            editor->project->total_duration += editor->project->scenes[i].duration;
        }
    }
    
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

    RebaseLoadedProjectAssets(editor->project);
    
    editor->project->modified = false;
    
    return true;
}

static bool ParseLayerPostEffectField(const char* line, LayerPostEffect* effects, int* effect_count) {
    if (!line || !effects || !effect_count) return false;
    if (sscanf_s(line, "\"post_effect_count\": %d", effect_count) == 1) return true;
    int index = -1;
    if (sscanf_s(line, "\"post_effect_%d_", &index) != 1 ||
        index < 0 || index >= rev::runtime::kMaxLayerPostEffects) return false;
    int int_value = 0;
    float float_value = 0.0f;
    if (sscanf_s(line, "\"post_effect_%d_type\": %d", &index, &int_value) == 2) effects[index].type = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_enabled\": %d", &index, &int_value) == 2) effects[index].enabled = int_value != 0;
    else if (sscanf_s(line, "\"post_effect_%d_order\": %d", &index, &int_value) == 2) effects[index].order = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_blend_mode\": %d", &index, &int_value) == 2) effects[index].blend_mode = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_intensity\": %f", &index, &float_value) == 2) effects[index].intensity = float_value;
    else if (sscanf_s(line, "\"post_effect_%d_threshold\": %f", &index, &float_value) == 2) effects[index].threshold = float_value;
    else if (sscanf_s(line, "\"post_effect_%d_radius\": %f", &index, &float_value) == 2) effects[index].radius = float_value;
    else if (sscanf_s(line, "\"post_effect_%d_color\": [%f, %f, %f, %f]", &index,
                      &effects[index].color[0], &effects[index].color[1],
                      &effects[index].color[2], &effects[index].color[3]) == 5) {}
    else if (sscanf_s(line, "\"post_effect_%d_start\": %f", &index, &float_value) == 2) effects[index].start_time = float_value;
    else if (sscanf_s(line, "\"post_effect_%d_end\": %f", &index, &float_value) == 2) effects[index].end_time = float_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_intensity\": %d", &index, &int_value) == 2) effects[index].curve_intensity = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_threshold\": %d", &index, &int_value) == 2) effects[index].curve_threshold = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_radius\": %d", &index, &int_value) == 2) effects[index].curve_radius = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_color_r\": %d", &index, &int_value) == 2) effects[index].curve_color_r = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_color_g\": %d", &index, &int_value) == 2) effects[index].curve_color_g = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_color_b\": %d", &index, &int_value) == 2) effects[index].curve_color_b = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_color_a\": %d", &index, &int_value) == 2) effects[index].curve_color_a = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_curve_amount\": %d", &index, &int_value) == 2) effects[index].curve_amount = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_trigger_track\": %d", &index, &int_value) == 2) effects[index].trigger_track = int_value;
    else if (sscanf_s(line, "\"post_effect_%d_trigger_pulse_beats\": %f", &index, &float_value) == 2) effects[index].trigger_pulse_beats = float_value;
    else return false;
    return true;
}

static bool ParseAssetShaderField(const char* line, AssetShader* shaders, int* shader_count) {
    if (!line || !shaders || !shader_count) return false;
    int index = -1;
    int int_value = 0;
    float float_value = 0.0f;
    if (sscanf_s(line, "\"asset_shader_count\": %d", shader_count) == 1) return true;
    if (sscanf_s(line, "\"asset_shader_%d_", &index) != 1 ||
        index < 0 || index >= rev::runtime::kMaxAssetShaders) return false;
    if (sscanf_s(line, "\"asset_shader_%d_id\": %d", &index, &int_value) == 2) shaders[index].shader_id = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_enabled\": %d", &index, &int_value) == 2) shaders[index].enabled = int_value != 0;
    else if (sscanf_s(line, "\"asset_shader_%d_order\": %d", &index, &int_value) == 2) shaders[index].order = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_blend_mode\": %d", &index, &int_value) == 2) shaders[index].blend_mode = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_opacity\": %f", &index, &float_value) == 2) shaders[index].opacity = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_speed\": %f", &index, &float_value) == 2) shaders[index].speed = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_intensity\": %f", &index, &float_value) == 2) shaders[index].intensity = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_warp\": %f", &index, &float_value) == 2) shaders[index].warp = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_exposure_base\": %f", &index, &float_value) == 2) shaders[index].exposure_base = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_exposure_ramp\": %f", &index, &float_value) == 2) shaders[index].exposure_ramp = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_fade_base\": %f", &index, &float_value) == 2) shaders[index].fade_base = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_fade_ramp\": %f", &index, &float_value) == 2) shaders[index].fade_ramp = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_start\": %f", &index, &float_value) == 2) shaders[index].start_time = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_end\": %f", &index, &float_value) == 2) shaders[index].end_time = float_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_speed\": %d", &index, &int_value) == 2) shaders[index].curve_speed = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_intensity\": %d", &index, &int_value) == 2) shaders[index].curve_intensity = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_warp\": %d", &index, &int_value) == 2) shaders[index].curve_warp = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_exposure\": %d", &index, &int_value) == 2) shaders[index].curve_exposure = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_fade\": %d", &index, &int_value) == 2) shaders[index].curve_fade = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_opacity\": %d", &index, &int_value) == 2) shaders[index].curve_opacity = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_exposure_ramp\": %d", &index, &int_value) == 2) shaders[index].curve_exposure_ramp = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_fade_ramp\": %d", &index, &int_value) == 2) shaders[index].curve_fade_ramp = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_low_r\": %d", &index, &int_value) == 2) shaders[index].curve_palette_low_r = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_low_g\": %d", &index, &int_value) == 2) shaders[index].curve_palette_low_g = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_low_b\": %d", &index, &int_value) == 2) shaders[index].curve_palette_low_b = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_mid_r\": %d", &index, &int_value) == 2) shaders[index].curve_palette_mid_r = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_mid_g\": %d", &index, &int_value) == 2) shaders[index].curve_palette_mid_g = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_mid_b\": %d", &index, &int_value) == 2) shaders[index].curve_palette_mid_b = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_high_r\": %d", &index, &int_value) == 2) shaders[index].curve_palette_high_r = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_high_g\": %d", &index, &int_value) == 2) shaders[index].curve_palette_high_g = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_curve_palette_high_b\": %d", &index, &int_value) == 2) shaders[index].curve_palette_high_b = int_value;
    else if (sscanf_s(line, "\"asset_shader_%d_palette_low\": [%f, %f, %f]", &index,
                      &shaders[index].palette_low[0], &shaders[index].palette_low[1], &shaders[index].palette_low[2]) == 4) {}
    else if (sscanf_s(line, "\"asset_shader_%d_palette_mid\": [%f, %f, %f]", &index,
                      &shaders[index].palette_mid[0], &shaders[index].palette_mid[1], &shaders[index].palette_mid[2]) == 4) {}
    else if (sscanf_s(line, "\"asset_shader_%d_palette_high\": [%f, %f, %f]", &index,
                      &shaders[index].palette_high[0], &shaders[index].palette_high[1], &shaders[index].palette_high[2]) == 4) {}
    else return false;
    return true;
}

static void NormalizeAssetShaderCount(AssetShader* shaders, int* shader_count) {
    if (!shaders || !shader_count) return;
    if (*shader_count < 0) *shader_count = 0;
    if (*shader_count > rev::runtime::kMaxAssetShaders) {
        *shader_count = rev::runtime::kMaxAssetShaders;
    }
    for (int i = 0; i < rev::runtime::kMaxAssetShaders; ++i) {
        if (shaders[i].enabled && i >= *shader_count) {
            *shader_count = i + 1;
        }
    }
}

static void ParseLayerPostEffectsPipe(char* data, int base_field_count,
                                      LayerPostEffect* effects, int* effect_count) {
    if (!data || !effects || !effect_count) return;
    char* field = data;
    for (int i = 0; i < base_field_count; ++i) {
        field = strchr(field, '|');
        if (!field) return;
        ++field;
    }
    int count = atoi(field);
    if (count < 0) count = 0;
    if (count > rev::runtime::kMaxLayerPostEffects) count = rev::runtime::kMaxLayerPostEffects;
    *effect_count = count;
    for (int i = 0; i < count; ++i) {
        field = strchr(field, '|');
        if (!field) break;
        ++field;
        LayerPostEffect& effect = effects[i];
        int enabled = 0;
        effect.blend_mode = 0;
        int parsed = sscanf_s(field,
            "%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            &effect.type, &enabled, &effect.order,
            &effect.intensity, &effect.threshold, &effect.radius,
            &effect.color[0], &effect.color[1], &effect.color[2], &effect.color[3],
            &effect.start_time, &effect.end_time,
            &effect.curve_intensity, &effect.curve_threshold, &effect.curve_radius,
            &effect.curve_color_r, &effect.curve_color_g, &effect.curve_color_b,
            &effect.curve_color_a, &effect.curve_amount, &effect.blend_mode);
        effect.enabled = enabled != 0;
        if (parsed < 21) effect.blend_mode = 0;
        if (parsed < 20) {
            *effect_count = i;
            break;
        }
    }
}

static void WriteLayerPostEffectFields(FILE* f, const LayerPostEffect* effects, int effect_count) {
    fprintf(f, "          \"post_effect_count\": %d,\n", effect_count);
    for (int i = 0; i < rev::runtime::kMaxLayerPostEffects; ++i) {
        const LayerPostEffect& effect = effects[i];
        fprintf(f, "          \"post_effect_%d_type\": %d,\n", i, effect.type);
        fprintf(f, "          \"post_effect_%d_enabled\": %d,\n", i, effect.enabled ? 1 : 0);
        fprintf(f, "          \"post_effect_%d_order\": %d,\n", i, effect.order);
        fprintf(f, "          \"post_effect_%d_blend_mode\": %d,\n", i, effect.blend_mode);
        fprintf(f, "          \"post_effect_%d_intensity\": %.3f,\n", i, effect.intensity);
        fprintf(f, "          \"post_effect_%d_threshold\": %.3f,\n", i, effect.threshold);
        fprintf(f, "          \"post_effect_%d_radius\": %.3f,\n", i, effect.radius);
        fprintf(f, "          \"post_effect_%d_color\": [%.3f, %.3f, %.3f, %.3f],\n", i,
                effect.color[0], effect.color[1], effect.color[2], effect.color[3]);
        fprintf(f, "          \"post_effect_%d_start\": %.3f,\n", i, effect.start_time);
        fprintf(f, "          \"post_effect_%d_end\": %.3f,\n", i, effect.end_time);
        fprintf(f, "          \"post_effect_%d_curve_intensity\": %d,\n", i, effect.curve_intensity);
        fprintf(f, "          \"post_effect_%d_curve_threshold\": %d,\n", i, effect.curve_threshold);
        fprintf(f, "          \"post_effect_%d_curve_radius\": %d,\n", i, effect.curve_radius);
        fprintf(f, "          \"post_effect_%d_curve_color_r\": %d,\n", i, effect.curve_color_r);
        fprintf(f, "          \"post_effect_%d_curve_color_g\": %d,\n", i, effect.curve_color_g);
        fprintf(f, "          \"post_effect_%d_curve_color_b\": %d,\n", i, effect.curve_color_b);
        fprintf(f, "          \"post_effect_%d_curve_color_a\": %d,\n", i, effect.curve_color_a);
        fprintf(f, "          \"post_effect_%d_curve_amount\": %d,\n", i, effect.curve_amount);
        fprintf(f, "          \"post_effect_%d_trigger_track\": %d,\n", i, effect.trigger_track);
        fprintf(f, "          \"post_effect_%d_trigger_pulse_beats\": %.3f%s\n", i,
            effect.trigger_pulse_beats,
            (i == rev::runtime::kMaxLayerPostEffects - 1) ? "" : ",");
    }
}

static void WriteAssetShaderFields(FILE* f, const AssetShader* shaders, int shader_count) {
    fprintf(f, "          \"asset_shader_count\": %d,\n", shader_count);
    for (int i = 0; i < rev::runtime::kMaxAssetShaders; ++i) {
        const AssetShader& shader = shaders[i];
        fprintf(f, "          \"asset_shader_%d_id\": %d,\n", i, shader.shader_id);
        fprintf(f, "          \"asset_shader_%d_enabled\": %d,\n", i, shader.enabled ? 1 : 0);
        fprintf(f, "          \"asset_shader_%d_order\": %d,\n", i, shader.order);
        fprintf(f, "          \"asset_shader_%d_blend_mode\": %d,\n", i, shader.blend_mode);
        fprintf(f, "          \"asset_shader_%d_opacity\": %.3f,\n", i, shader.opacity);
        fprintf(f, "          \"asset_shader_%d_speed\": %.3f,\n", i, shader.speed);
        fprintf(f, "          \"asset_shader_%d_intensity\": %.3f,\n", i, shader.intensity);
        fprintf(f, "          \"asset_shader_%d_warp\": %.3f,\n", i, shader.warp);
        fprintf(f, "          \"asset_shader_%d_exposure_base\": %.3f,\n", i, shader.exposure_base);
        fprintf(f, "          \"asset_shader_%d_exposure_ramp\": %.3f,\n", i, shader.exposure_ramp);
        fprintf(f, "          \"asset_shader_%d_fade_base\": %.3f,\n", i, shader.fade_base);
        fprintf(f, "          \"asset_shader_%d_fade_ramp\": %.3f,\n", i, shader.fade_ramp);
        fprintf(f, "          \"asset_shader_%d_palette_low\": [%.3f, %.3f, %.3f],\n", i, shader.palette_low[0], shader.palette_low[1], shader.palette_low[2]);
        fprintf(f, "          \"asset_shader_%d_palette_mid\": [%.3f, %.3f, %.3f],\n", i, shader.palette_mid[0], shader.palette_mid[1], shader.palette_mid[2]);
        fprintf(f, "          \"asset_shader_%d_palette_high\": [%.3f, %.3f, %.3f],\n", i, shader.palette_high[0], shader.palette_high[1], shader.palette_high[2]);
        fprintf(f, "          \"asset_shader_%d_start\": %.3f,\n", i, shader.start_time);
        fprintf(f, "          \"asset_shader_%d_end\": %.3f,\n", i, shader.end_time);
        fprintf(f, "          \"asset_shader_%d_curve_speed\": %d,\n", i, shader.curve_speed);
        fprintf(f, "          \"asset_shader_%d_curve_intensity\": %d,\n", i, shader.curve_intensity);
        fprintf(f, "          \"asset_shader_%d_curve_warp\": %d,\n", i, shader.curve_warp);
        fprintf(f, "          \"asset_shader_%d_curve_exposure\": %d,\n", i, shader.curve_exposure);
        fprintf(f, "          \"asset_shader_%d_curve_fade\": %d,\n", i, shader.curve_fade);
        fprintf(f, "          \"asset_shader_%d_curve_opacity\": %d,\n", i, shader.curve_opacity);
        fprintf(f, "          \"asset_shader_%d_curve_exposure_ramp\": %d,\n", i, shader.curve_exposure_ramp);
        fprintf(f, "          \"asset_shader_%d_curve_fade_ramp\": %d,\n", i, shader.curve_fade_ramp);
        fprintf(f, "          \"asset_shader_%d_curve_palette_low_r\": %d,\n", i, shader.curve_palette_low_r);
        fprintf(f, "          \"asset_shader_%d_curve_palette_low_g\": %d,\n", i, shader.curve_palette_low_g);
        fprintf(f, "          \"asset_shader_%d_curve_palette_low_b\": %d,\n", i, shader.curve_palette_low_b);
        fprintf(f, "          \"asset_shader_%d_curve_palette_mid_r\": %d,\n", i, shader.curve_palette_mid_r);
        fprintf(f, "          \"asset_shader_%d_curve_palette_mid_g\": %d,\n", i, shader.curve_palette_mid_g);
        fprintf(f, "          \"asset_shader_%d_curve_palette_mid_b\": %d,\n", i, shader.curve_palette_mid_b);
        fprintf(f, "          \"asset_shader_%d_curve_palette_high_r\": %d,\n", i, shader.curve_palette_high_r);
        fprintf(f, "          \"asset_shader_%d_curve_palette_high_g\": %d,\n", i, shader.curve_palette_high_g);
        fprintf(f, "          \"asset_shader_%d_curve_palette_high_b\": %d,\n", i, shader.curve_palette_high_b);
    }
}

bool SaveProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;

    editor->project->total_duration = 0.0f;
    for (int i = 0; i < editor->project->scene_count; ++i) {
        if (editor->project->scenes[i].duration > 0.0f) {
            editor->project->total_duration += editor->project->scenes[i].duration;
        }
    }
    
    FILE* f = nullptr;
    fopen_s(&f, path, "w");
    if (!f) return false;
    
    char escaped_workspace[1024] = {};
    JsonEscapeString(editor->project->workspace_path, escaped_workspace, sizeof(escaped_workspace));

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"workspace_path\": \"%s\",\n", escaped_workspace);
    fprintf(f, "  \"total_duration\": %.3f,\n", editor->project->total_duration);
    fprintf(f, "  \"loop_intro\": %d,\n", editor->project->loop_intro ? 1 : 0);
    fprintf(f, "  \"loop_music\": %d,\n", editor->project->loop_music ? 1 : 0);
    fprintf(f, "  \"music_persist_across_scenes\": %d,\n", editor->project->music_persist_across_scenes ? 1 : 0);
    char escaped_runtime_title[256] = {};
    JsonEscapeString(editor->project->runtime_title, escaped_runtime_title, sizeof(escaped_runtime_title));
    fprintf(f, "  \"runtime_fullscreen\": %d,\n", editor->project->runtime_fullscreen ? 1 : 0);
    fprintf(f, "  \"runtime_title\": \"%s\",\n", escaped_runtime_title);
    fprintf(f, "  \"audio_gain_enabled\": %d,\n", editor->project->audio_effects.gain_enabled);
    fprintf(f, "  \"audio_gain_db\": %.3f,\n", editor->project->audio_effects.gain_db);
    fprintf(f, "  \"audio_compressor_enabled\": %d,\n", editor->project->audio_effects.compressor_enabled);
    fprintf(f, "  \"audio_compressor_threshold\": %.3f,\n", editor->project->audio_effects.compressor_threshold);
    fprintf(f, "  \"audio_compressor_ratio\": %.3f,\n", editor->project->audio_effects.compressor_ratio);
    fprintf(f, "  \"audio_compressor_attack\": %.3f,\n", editor->project->audio_effects.compressor_attack);
    fprintf(f, "  \"audio_compressor_release\": %.3f,\n", editor->project->audio_effects.compressor_release);
    fprintf(f, "  \"audio_widener_enabled\": %d,\n", editor->project->audio_effects.widener_enabled);
    fprintf(f, "  \"audio_widener_amount\": %.3f,\n", editor->project->audio_effects.widener_amount);
    fprintf(f, "  \"audio_eq_enabled\": %d,\n", editor->project->audio_effects.eq_enabled);
    fprintf(f, "  \"audio_eq_low_db\": %.3f,\n", editor->project->audio_effects.eq_low_db);
    fprintf(f, "  \"audio_eq_mid_db\": %.3f,\n", editor->project->audio_effects.eq_mid_db);
    fprintf(f, "  \"audio_eq_high_db\": %.3f,\n", editor->project->audio_effects.eq_high_db);
    fprintf(f, "  \"scenes\": [\n");
    
    // Save scenes
    for (int s = 0; s < editor->project->scene_count; ++s) {
        SceneBlock* scene = &editor->project->scenes[s];
        char escaped_scene_name[128] = {};
        JsonEscapeString(scene->name, escaped_scene_name, sizeof(escaped_scene_name));
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", escaped_scene_name);
        fprintf(f, "      \"duration\": %.3f,\n", scene->duration);
        
        // Shader cues
        fprintf(f, "      \"shader_cues\": [\n");
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            char escaped_shader_name[128] = {};
            JsonEscapeString(cue->shader_name, escaped_shader_name, sizeof(escaped_shader_name));
            fprintf(f, "        {\n");
            fprintf(f, "          \"shader_name\": \"%s\",\n", escaped_shader_name);
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
            fprintf(f, "          \"position\": [%.3f, %.3f, %.3f],\n",
                cue->position_x, cue->position_y, cue->position_z);
            fprintf(f, "          \"rotation\": [%.3f, %.3f, %.3f],\n",
                cue->rotation_x, cue->rotation_y, cue->rotation_z);
            fprintf(f, "          \"motion\": [%.3f, %.3f, %.3f],\n",
                cue->motion_x, cue->motion_y, cue->motion_z);
            fprintf(f, "          \"noise_enabled\": %d,\n", cue->noise.enabled);
            fprintf(f, "          \"noise_type\": %d,\n", cue->noise.type);
            fprintf(f, "          \"noise_scale\": %.3f,\n", cue->noise.scale);
            fprintf(f, "          \"noise_strength\": %.3f,\n", cue->noise.strength);
            fprintf(f, "          \"noise_octaves\": %.3f,\n", cue->noise.octaves);
            fprintf(f, "          \"noise_lacunarity\": %.3f,\n", cue->noise.lacunarity);
            fprintf(f, "          \"noise_gain\": %.3f,\n", cue->noise.gain);
            fprintf(f, "          \"noise_warp\": %.3f,\n", cue->noise.warp);
            fprintf(f, "          \"noise_speed\": [%.3f, %.3f],\n", cue->noise.speed_x, cue->noise.speed_y);
            fprintf(f, "          \"noise_seed\": %.3f,\n", cue->noise.seed);
            fprintf(f, "          \"noise_contrast\": %.3f,\n", cue->noise.contrast);
            for (int map_index = 0; map_index < 4; ++map_index) {
                char map_key[32] = {};
                char escaped_map[1024] = {};
                snprintf(map_key, sizeof(map_key), "noise_map_%d", map_index);
                JsonEscapeString(cue->noise_textures.paths[map_index], escaped_map, sizeof(escaped_map));
                fprintf(f, "          \"%s\": \"%s\",\n", map_key, escaped_map);
            }
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
            fprintf(f, "          \"curve_fade\": %d,\n", cue->curve_fade);
            fprintf(f, "          \"curve_palette_low_r\": %d,\n", cue->curve_palette_low_r);
            fprintf(f, "          \"curve_palette_low_g\": %d,\n", cue->curve_palette_low_g);
            fprintf(f, "          \"curve_palette_low_b\": %d,\n", cue->curve_palette_low_b);
            fprintf(f, "          \"curve_palette_mid_r\": %d,\n", cue->curve_palette_mid_r);
            fprintf(f, "          \"curve_palette_mid_g\": %d,\n", cue->curve_palette_mid_g);
            fprintf(f, "          \"curve_palette_mid_b\": %d,\n", cue->curve_palette_mid_b);
            fprintf(f, "          \"curve_palette_high_r\": %d,\n", cue->curve_palette_high_r);
            fprintf(f, "          \"curve_palette_high_g\": %d,\n", cue->curve_palette_high_g);
            fprintf(f, "          \"curve_palette_high_b\": %d,\n", cue->curve_palette_high_b);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_exposure_ramp\": %d,\n", cue->curve_exposure_ramp);
            fprintf(f, "          \"curve_fade_ramp\": %d\n", cue->curve_fade_ramp);
            fprintf(f, "        }%s\n", (i < scene->shader_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Image cues
        fprintf(f, "      \"image_cues\": [\n");
        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            char escaped_asset_key[256] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", escaped_asset_key);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"scale\": %.3f,\n", cue->scale);
            fprintf(f, "          \"rotation\": %.3f,\n", cue->rotation);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_scale\": %d,\n", cue->curve_scale);
            fprintf(f, "          \"curve_rotation\": %d,\n", cue->curve_rotation);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            WriteAssetShaderFields(f, cue->shaders, cue->shader_count);
            WriteLayerPostEffectFields(f, cue->post_effects, cue->post_effect_count);
            fprintf(f, "        }%s\n", (i < scene->image_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Animated sprite cues
        fprintf(f, "      \"animated_sprite_cues\": [\n");
        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            char escaped_sprite_name[128] = {};
            char escaped_frame_keys[4096] = {};
            char escaped_frame_paths[4096] = {};
            JsonEscapeString(cue->sprite_name, escaped_sprite_name, sizeof(escaped_sprite_name));
            JsonEscapeString(cue->frame_keys_csv, escaped_frame_keys, sizeof(escaped_frame_keys));
            JsonEscapeString(cue->frame_paths_csv, escaped_frame_paths, sizeof(escaped_frame_paths));
            fprintf(f, "        {\n");
            fprintf(f, "          \"sprite_name\": \"%s\",\n", escaped_sprite_name);
            fprintf(f, "          \"frame_keys_csv\": \"%s\",\n", escaped_frame_keys);
            fprintf(f, "          \"frame_paths_csv\": \"%s\",\n", escaped_frame_paths);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"scale\": %.3f,\n", cue->scale);
            fprintf(f, "          \"rotation\": %.3f,\n", cue->rotation);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"fps\": %.3f,\n", cue->fps);
            fprintf(f, "          \"playback_mode\": %d,\n", cue->playback_mode);
            fprintf(f, "          \"start_frame\": %d,\n", cue->start_frame);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_scale\": %d,\n", cue->curve_scale);
            fprintf(f, "          \"curve_rotation\": %d,\n", cue->curve_rotation);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_frame\": %d,\n", cue->curve_frame);
            WriteAssetShaderFields(f, cue->shaders, cue->shader_count);
            WriteLayerPostEffectFields(f, cue->post_effects, cue->post_effect_count);
            fprintf(f, "        }%s\n", (i < scene->animated_sprite_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Indexed pixel cues
        fprintf(f, "      \"pixel_cues\": [\n");
        for (int i = 0; i < scene->pixel_cue_count; ++i) {
            PixelCue* cue = &scene->pixel_cues[i];
            char escaped_asset_key[256] = {};
            char escaped_asset_path[1024] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            JsonEscapeString(cue->asset_path, escaped_asset_path, sizeof(escaped_asset_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", escaped_asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n", escaped_asset_path);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"scale\": %.3f,\n", cue->scale);
            fprintf(f, "          \"rotation\": %.3f,\n", cue->rotation);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"fps\": %.3f,\n", cue->fps);
            fprintf(f, "          \"playback_mode\": %d,\n", cue->playback_mode);
            fprintf(f, "          \"start_frame\": %d,\n", cue->start_frame);
            fprintf(f, "          \"palette_offset\": %d,\n", cue->palette_offset);
            fprintf(f, "          \"palette_cycle_speed\": %d,\n", cue->palette_cycle_speed);
            fprintf(f, "          \"snap_to_pixels\": %d,\n", cue->snap_to_pixels);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_scale\": %d,\n", cue->curve_scale);
            fprintf(f, "          \"curve_rotation\": %d,\n", cue->curve_rotation);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_frame\": %d,\n", cue->curve_frame);
            fprintf(f, "          \"curve_palette_offset\": %d,\n", cue->curve_palette_offset);
            WriteAssetShaderFields(f, cue->shaders, cue->shader_count);
            WriteLayerPostEffectFields(f, cue->post_effects, cue->post_effect_count);
            fprintf(f, "        }%s\n", (i < scene->pixel_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        fprintf(f, "      \"pixel_emitter_cues\": [\n");
        for (int i = 0; i < scene->pixel_emitter_cue_count; ++i) {
            PixelEmitterCue* cue = &scene->pixel_emitter_cues[i];
            char escaped_asset_key[256] = {};
            char escaped_asset_path[1024] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            JsonEscapeString(cue->asset_path, escaped_asset_path, sizeof(escaped_asset_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", escaped_asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n", escaped_asset_path);
            fprintf(f, "          \"visual_source\": %d,\n", cue->visual_source);
            fprintf(f, "          \"primitive_shape\": %d,\n", cue->primitive_shape);
            fprintf(f, "          \"primitive_color_r\": %.3f,\n", cue->primitive_color[0]);
            fprintf(f, "          \"primitive_color_g\": %.3f,\n", cue->primitive_color[1]);
            fprintf(f, "          \"primitive_color_b\": %.3f,\n", cue->primitive_color[2]);
            fprintf(f, "          \"primitive_color_a\": %.3f,\n", cue->primitive_color[3]);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"scale\": %.3f,\n", cue->scale);
            fprintf(f, "          \"rotation\": %.3f,\n", cue->rotation);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"max_particles\": %d,\n", cue->max_particles);
            fprintf(f, "          \"emission_rate\": %.3f,\n", cue->emission_rate);
            fprintf(f, "          \"burst_count\": %d,\n", cue->burst_count);
            fprintf(f, "          \"duration\": %.3f,\n", cue->duration);
            fprintf(f, "          \"loop\": %d,\n", cue->loop);
            fprintf(f, "          \"speed_min\": %.3f,\n", cue->speed_min);
            fprintf(f, "          \"speed_max\": %.3f,\n", cue->speed_max);
            fprintf(f, "          \"lifetime_min\": %.3f,\n", cue->lifetime_min);
            fprintf(f, "          \"lifetime_max\": %.3f,\n", cue->lifetime_max);
            fprintf(f, "          \"scale_min\": %.3f,\n", cue->scale_min);
            fprintf(f, "          \"scale_max\": %.3f,\n", cue->scale_max);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_scale\": %d,\n", cue->curve_scale);
            fprintf(f, "          \"curve_rotation\": %d,\n", cue->curve_rotation);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_emission_rate\": %d,\n", cue->curve_emission_rate);
            fprintf(f, "          \"curve_speed_min\": %d,\n", cue->curve_speed_min);
            fprintf(f, "          \"curve_speed_max\": %d,\n", cue->curve_speed_max);
            fprintf(f, "          \"curve_lifetime_min\": %d,\n", cue->curve_lifetime_min);
            fprintf(f, "          \"curve_lifetime_max\": %d,\n", cue->curve_lifetime_max);
            fprintf(f, "          \"curve_scale_min\": %d,\n", cue->curve_scale_min);
            fprintf(f, "          \"curve_scale_max\": %d,\n", cue->curve_scale_max);
            fprintf(f, "          \"seed\": %u\n", cue->seed);
            fprintf(f, "        }%s\n", (i < scene->pixel_emitter_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        fprintf(f, "      \"scene_layer_post_effects\": {\n");
        WriteLayerPostEffectFields(f, scene->scene_layer_post_effects,
                       scene->scene_layer_post_effect_count);
        fprintf(f, "      },\n");
        
        // Text cues
        fprintf(f, "      \"text_cues\": [\n");
        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            char escaped_text[512] = {};
            char escaped_font[128] = {};
            char escaped_baked_key[128] = {};
            char escaped_baked_path[1024] = {};
            char escaped_glyph_atlas_key[128] = {};
            char escaped_glyph_atlas_path[1024] = {};
            char escaped_glyph_meta_key[128] = {};
            char escaped_glyph_meta_path[1024] = {};
            JsonEscapeString(cue->text, escaped_text, sizeof(escaped_text));
            JsonEscapeString(cue->font_name, escaped_font, sizeof(escaped_font));
            JsonEscapeString(cue->baked_asset_key, escaped_baked_key, sizeof(escaped_baked_key));
            JsonEscapeString(cue->baked_asset_path, escaped_baked_path, sizeof(escaped_baked_path));
            JsonEscapeString(cue->glyph_atlas_key, escaped_glyph_atlas_key, sizeof(escaped_glyph_atlas_key));
            JsonEscapeString(cue->glyph_atlas_path, escaped_glyph_atlas_path, sizeof(escaped_glyph_atlas_path));
            JsonEscapeString(cue->glyph_meta_key, escaped_glyph_meta_key, sizeof(escaped_glyph_meta_key));
            JsonEscapeString(cue->glyph_meta_path, escaped_glyph_meta_path, sizeof(escaped_glyph_meta_path));
            char serialized_animation[4096] = {};
            char escaped_animation[4096] = {};
            SerializeTextAnimation(&cue->animation, serialized_animation, sizeof(serialized_animation));
            JsonEscapeString(serialized_animation, escaped_animation, sizeof(escaped_animation));
            fprintf(f, "        {\n");
            fprintf(f, "          \"text\": \"%s\",\n", escaped_text);
            fprintf(f, "          \"font_name\": \"%s\",\n", escaped_font);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"size\": %.3f,\n", cue->size);
            fprintf(f, "          \"rotation\": %.3f,\n", cue->rotation);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f],\n",
                cue->color.r, cue->color.g, cue->color.b);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"bake_mode\": %d,\n", cue->bake_mode);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_size\": %d,\n", cue->curve_size);
            fprintf(f, "          \"curve_rotation\": %d,\n", cue->curve_rotation);
            fprintf(f, "          \"curve_color_r\": %d,\n", cue->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", cue->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", cue->curve_color_b);
            fprintf(f, "          \"baked_asset_key\": \"%s\",\n", escaped_baked_key);
            fprintf(f, "          \"baked_asset_path\": \"%s\",\n", escaped_baked_path);
            fprintf(f, "          \"glyph_atlas_key\": \"%s\",\n", escaped_glyph_atlas_key);
            fprintf(f, "          \"glyph_atlas_path\": \"%s\",\n", escaped_glyph_atlas_path);
            fprintf(f, "          \"glyph_meta_key\": \"%s\",\n", escaped_glyph_meta_key);
            fprintf(f, "          \"glyph_meta_path\": \"%s\",\n", escaped_glyph_meta_path);
            fprintf(f, "          \"animation\": \"%s\",\n", escaped_animation);
            fprintf(f, "          \"layer_order\": %d\n", cue->layer_order);
            fprintf(f, "        }%s\n", (i < scene->text_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Scroll text cues
        fprintf(f, "      \"scroll_text_cues\": [\n");
        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            char escaped_text[1024] = {};
            char escaped_font[128] = {};
            char escaped_baked_key[128] = {};
            char escaped_baked_path[1024] = {};
            JsonEscapeString(cue->text, escaped_text, sizeof(escaped_text));
            JsonEscapeString(cue->font_name, escaped_font, sizeof(escaped_font));
            JsonEscapeString(cue->baked_asset_key, escaped_baked_key, sizeof(escaped_baked_key));
            JsonEscapeString(cue->baked_asset_path, escaped_baked_path, sizeof(escaped_baked_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"text\": \"%s\",\n", escaped_text);
            fprintf(f, "          \"font_name\": \"%s\",\n", escaped_font);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"size\": %.3f,\n", cue->size);
            fprintf(f, "          \"rotation\": %.3f,\n", cue->rotation);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f],\n", cue->color.r, cue->color.g, cue->color.b);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"style_id\": %d,\n", cue->style_id);
            fprintf(f, "          \"direction\": %d,\n", cue->direction);
            fprintf(f, "          \"loop_mode\": %d,\n", cue->loop_mode);
            fprintf(f, "          \"speed\": %.3f,\n", cue->speed);
            fprintf(f, "          \"wrap_gap\": %.3f,\n", cue->wrap_gap);
            fprintf(f, "          \"spacing\": %.3f,\n", cue->spacing);
            fprintf(f, "          \"slant_deg\": %.3f,\n", cue->slant_deg);
            fprintf(f, "          \"wave_amp\": %.3f,\n", cue->wave_amp);
            fprintf(f, "          \"wave_freq\": %.3f,\n", cue->wave_freq);
            fprintf(f, "          \"wave_length\": %.3f,\n", cue->wave_length);
            fprintf(f, "          \"jitter_amp\": %.3f,\n", cue->jitter_amp);
            fprintf(f, "          \"jitter_freq\": %.3f,\n", cue->jitter_freq);
            fprintf(f, "          \"glow\": %.3f,\n", cue->glow);
            fprintf(f, "          \"shadow\": %.3f,\n", cue->shadow);
            fprintf(f, "          \"outline\": %.3f,\n", cue->outline);
            fprintf(f, "          \"chroma_shift\": %.3f,\n", cue->chroma_shift);
            fprintf(f, "          \"distortion\": %.3f,\n", cue->distortion);
            fprintf(f, "          \"bake_mode\": %d,\n", cue->bake_mode);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_speed\": %d,\n", cue->curve_speed);
            fprintf(f, "          \"curve_size\": %d,\n", cue->curve_size);
            fprintf(f, "          \"curve_rotation\": %d,\n", cue->curve_rotation);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_color_r\": %d,\n", cue->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", cue->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", cue->curve_color_b);
            fprintf(f, "          \"curve_wave_amp\": %d,\n", cue->curve_wave_amp);
            fprintf(f, "          \"curve_wave_freq\": %d,\n", cue->curve_wave_freq);
            fprintf(f, "          \"curve_wave_length\": %d,\n", cue->curve_wave_length);
            fprintf(f, "          \"curve_jitter_amp\": %d,\n", cue->curve_jitter_amp);
            fprintf(f, "          \"curve_jitter_freq\": %d,\n", cue->curve_jitter_freq);
            fprintf(f, "          \"baked_asset_key\": \"%s\",\n", escaped_baked_key);
            fprintf(f, "          \"baked_asset_path\": \"%s\"\n", escaped_baked_path);
            fprintf(f, "        }%s\n", (i < scene->scroll_text_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Music cues
        fprintf(f, "      \"music_cues\": [\n");
        for (int i = 0; i < scene->music_cue_count; ++i) {
            MusicCue* cue = &scene->music_cues[i];
            char escaped_asset_key[128] = {};
            char escaped_asset_path[1024] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            JsonEscapeString(cue->asset_path, escaped_asset_path, sizeof(escaped_asset_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", escaped_asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n", escaped_asset_path);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f\n", cue->cue_end);
            fprintf(f, "        }%s\n", (i < scene->music_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Scene post-production effects
        fprintf(f, "      \"post_effects\": [\n");
        for (int i = 0; i < scene->post_effect_count; ++i) {
            PostEffect* effect = &scene->post_effects[i];
            fprintf(f, "        {\n");
            fprintf(f, "          \"type\": %d,\n", effect->type);
            fprintf(f, "          \"enabled\": %d,\n", effect->enabled ? 1 : 0);
            fprintf(f, "          \"order\": %d,\n", effect->order);
            fprintf(f, "          \"intensity\": %.3f,\n", effect->intensity);
            fprintf(f, "          \"threshold\": %.3f,\n", effect->threshold);
            fprintf(f, "          \"radius\": %.3f,\n", effect->radius);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f, %.3f],\n",
                effect->color[0], effect->color[1], effect->color[2], effect->color[3]);
            fprintf(f, "          \"start_time\": %.3f,\n", effect->start_time);
            fprintf(f, "          \"end_time\": %.3f,\n", effect->end_time);
            fprintf(f, "          \"curve_intensity\": %d,\n", effect->curve_intensity);
            fprintf(f, "          \"curve_threshold\": %d,\n", effect->curve_threshold);
            fprintf(f, "          \"curve_radius\": %d,\n", effect->curve_radius);
            fprintf(f, "          \"curve_color_r\": %d,\n", effect->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", effect->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", effect->curve_color_b);
            fprintf(f, "          \"curve_color_a\": %d,\n", effect->curve_color_a);
            fprintf(f, "          \"curve_amount\": %d\n", effect->curve_amount);
            fprintf(f, "        }%s\n", (i < scene->post_effect_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Mesh cues
        fprintf(f, "      \"mesh_cues\": [\n");
        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            char escaped_asset_key[128] = {};
            char escaped_asset_path[1024] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            JsonEscapeString(cue->asset_path, escaped_asset_path, sizeof(escaped_asset_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n",   escaped_asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n",  escaped_asset_path);
            fprintf(f, "          \"mesh_type\": %d,\n",        cue->mesh_type);
            fprintf(f, "          \"pos\": [%.3f, %.3f, %.3f],\n",   cue->pos[0],   cue->pos[1],   cue->pos[2]);
            fprintf(f, "          \"rot\": [%.3f, %.3f, %.3f],\n",   cue->rot[0],   cue->rot[1],   cue->rot[2]);
            fprintf(f, "          \"scale\": [%.3f, %.3f, %.3f],\n", cue->scale[0], cue->scale[1], cue->scale[2]);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f, %.3f],\n", cue->color[0], cue->color[1], cue->color[2], cue->color[3]);
            fprintf(f, "          \"mesh_size\": %.3f,\n",  cue->mesh_size);
            fprintf(f, "          \"mesh_param\": %.3f,\n", cue->mesh_param);
            fprintf(f, "          \"metallic\": %.3f,\n",   cue->metallic);
            fprintf(f, "          \"roughness\": %.3f,\n",  cue->roughness);
            fprintf(f, "          \"emissive_color\": [%.3f, %.3f, %.3f],\n",
                cue->emissive_color[0], cue->emissive_color[1], cue->emissive_color[2]);
            fprintf(f, "          \"emissive_strength\": %.3f,\n", cue->emissive_strength);
            fprintf(f, "          \"fov_deg\": %.3f,\n",    cue->fov_deg);
            fprintf(f, "          \"cull_mode\": %d,\n",    cue->cull_mode);
            fprintf(f, "          \"effect_type\": %d,\n",  cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n",  cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n",    cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n",  cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n",    cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n",   cue->fade_out_end);
            fprintf(f, "          \"layer_order\": %d,\n",   cue->layer_order);
            fprintf(f, "          \"curve_pos_x\": %d,\n", cue->curve_pos_x);
            fprintf(f, "          \"curve_pos_y\": %d,\n", cue->curve_pos_y);
            fprintf(f, "          \"curve_pos_z\": %d,\n", cue->curve_pos_z);
            fprintf(f, "          \"curve_rot_x\": %d,\n", cue->curve_rot_x);
            fprintf(f, "          \"curve_rot_y\": %d,\n", cue->curve_rot_y);
            fprintf(f, "          \"curve_rot_z\": %d,\n", cue->curve_rot_z);
            fprintf(f, "          \"curve_scale_x\": %d,\n", cue->curve_scale_x);
            fprintf(f, "          \"curve_scale_y\": %d,\n", cue->curve_scale_y);
            fprintf(f, "          \"curve_scale_z\": %d,\n", cue->curve_scale_z);
            fprintf(f, "          \"curve_color_r\": %d,\n", cue->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", cue->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", cue->curve_color_b);
            fprintf(f, "          \"curve_color_a\": %d,\n", cue->curve_color_a);
            fprintf(f, "          \"curve_mesh_size\": %d,\n", cue->curve_mesh_size);
            fprintf(f, "          \"curve_metallic\": %d,\n", cue->curve_metallic);
            fprintf(f, "          \"curve_roughness\": %d,\n", cue->curve_roughness);
            fprintf(f, "          \"curve_fov\": %d,\n", cue->curve_fov);
            fprintf(f, "          \"use_imported_light\": %d,\n", cue->use_imported_light);
            fprintf(f, "          \"use_imported_camera\": %d\n", cue->use_imported_camera);
            fprintf(f, "        }%s\n", (i < scene->mesh_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ]\n");
        
        fprintf(f, "    }%s\n", (s < editor->project->scene_count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n");
    
    // Save curves
    fprintf(f, "  \"trigger_tracks\": [\n");
    for (int t = 0; t < editor->project->trigger_track_count; ++t) {
        TriggerTrack* track = &editor->project->trigger_tracks[t];
        char escaped_name[128] = {};
        JsonEscapeString(track->name, escaped_name, sizeof(escaped_name));
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", escaped_name);
        fprintf(f, "      \"bpm\": %.3f,\n", track->timing.bpm);
        fprintf(f, "      \"beat_offset\": %.3f,\n", track->timing.beat_offset);
        fprintf(f, "      \"events\": [\n");
        for (int e = 0; e < track->event_count; ++e) {
            fprintf(f, "        {\"beat\": %.3f, \"value\": %d}%s\n",
                    track->events[e].beat, track->events[e].value,
                    (e < track->event_count - 1) ? "," : "");
        }
        fprintf(f, "      ]\n");
        fprintf(f, "    }%s\n", (t < editor->project->trigger_track_count - 1) ? "," : "");
    }
    fprintf(f, "  ],\n");

    // Save curves
    fprintf(f, "  \"curves\": [\n");
    for (int c = 0; c < editor->project->curve_count; ++c) {
        rev::curve::Curve* curve = &editor->project->curves[c];
        fprintf(f, "    {\n");
        
        // Save wrap mode
        const char* wrap_mode_str = "clamp";
        switch (curve->wrap_mode) {
            case rev::curve::WrapMode::Clamp: wrap_mode_str = "clamp"; break;
            case rev::curve::WrapMode::Loop: wrap_mode_str = "loop"; break;
            case rev::curve::WrapMode::PingPong: wrap_mode_str = "pingpong"; break;
            case rev::curve::WrapMode::Mirror: wrap_mode_str = "mirror"; break;
        }
        fprintf(f, "      \"wrap_mode\": \"%s\",\n", wrap_mode_str);
        fprintf(f, "      \"duration\": %.3f,\n", curve->duration);
        char escaped_curve_name[256] = {};
        JsonEscapeString(editor->project->curve_names[c], escaped_curve_name, sizeof(escaped_curve_name));
        fprintf(f, "      \"name\": \"%s\",\n", escaped_curve_name);
        
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

    ResetEditorAudio(editor);
    editor->project->audio_effects.compressor_threshold = 0.7f;
    editor->project->audio_effects.compressor_ratio = 4.0f;
    editor->project->audio_effects.compressor_attack = 0.01f;
    editor->project->audio_effects.compressor_release = 0.12f;
    editor->project->audio_effects.widener_amount = 1.0f;
    
    // Clean up existing scenes
    for (int i = 0; i < editor->project->scene_count; ++i) {
        SceneBlock* scene = &editor->project->scenes[i];
        delete[] scene->shader_cues;
        delete[] scene->image_cues;
        delete[] scene->animated_sprite_cues;
        delete[] scene->pixel_cues;
        delete[] scene->pixel_emitter_cues;
        delete[] scene->text_cues;
        delete[] scene->scroll_text_cues;
        delete[] scene->music_cues;
        delete[] scene->mesh_cues;
        delete[] scene->post_effects;
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
    editor->project->trigger_track_count = 0;
    
    editor->project->total_duration = 0.0f;  // Will be updated as scenes are added
    editor->project->loop_intro = false;
    editor->project->loop_music = false;
    editor->project->music_persist_across_scenes = false;
    editor->project->runtime_fullscreen = true;
    strncpy_s(editor->project->runtime_title, sizeof(editor->project->runtime_title), "HiMYM - Minimal Intro Test", _TRUNCATE);
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    memset(editor->project->workspace_path, 0, sizeof(editor->project->workspace_path));
    memset(editor->project->assets_path, 0, sizeof(editor->project->assets_path));
    editor->project->modified = false;

    editor->selected_scene_index = -1;
    editor->selected_cue_index = -1;
    editor->selected_curve_index = -1;
    editor->editing_curve_index = -1;
    editor->current_time = 0.0f;
    editor->playing = false;
    editor->trigger_recording = false;
    editor->recording_track_index = -1;
    editor->recording_append_curve = false;
    editor->recording_append_curve_index = -1;

    // Note: GetScene(0) returns nullptr when scene_count==0
    // First scene will be created via AddScene when user adds content
    
    // Clear mesh cache — meshes belong to the old project's GPU context
    for (int i = 0; i < editor->mesh_cache_count; ++i) {
        if (editor->mesh_cache[i].mesh) {
            rev::mesh::DestroyMesh((rev::mesh::Mesh*)editor->mesh_cache[i].mesh);
            editor->mesh_cache[i].mesh = nullptr;
        }
    }
    editor->mesh_cache_count = 0;

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

    if (editor->trigger_recording) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            editor->trigger_recording = false;
            editor->playing = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl, false) ||
                   ImGui::IsKeyPressed(ImGuiKey_RightCtrl, false)) {
            if (editor->recording_track_index >= 0 &&
                editor->recording_track_index < editor->project->trigger_track_count) {
                TriggerTrack* track = &editor->project->trigger_tracks[editor->recording_track_index];
                float beat_duration = rev::runtime::GetBeatDurationSeconds(track->timing.bpm);
                if (beat_duration > 0.0f) {
                    float beat = (editor->current_time - track->timing.beat_offset) / beat_duration;
                    beat = rev::runtime::QuantizeTriggerBeat(beat, editor->recording_quantize_beats);
                    if (rev::runtime::AddTriggerEvent(track, beat, 1)) {
                        editor->project->modified = true;
                    }
                }
            }
        }
    }
    
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

    RenderTriggerRecorder(editor);
    
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

    if (editor->animated_sprite_modal_open || editor->animated_sprite_modal_request_open) {
        RenderAnimatedSpriteModal(editor);
    }

    if (editor->pixel_modal_open || editor->pixel_modal_request_open) {
        RenderPixelModal(editor);
    }
    if (editor->pixel_emitter_modal_open || editor->pixel_emitter_modal_request_open) {
        RenderPixelEmitterModal(editor);
    }
    
    if (editor->text_modal_open || editor->text_modal_request_open) {
        RenderTextModal(editor);
    }

    if (editor->scroll_text_modal_open || editor->scroll_text_modal_request_open) {
        RenderScrollTextModal(editor);
    }

    if (editor->mesh_modal_open || editor->mesh_modal_request_open) {
        RenderMeshModal(editor);
    }
    
    if (editor->curve_editor_modal_open || editor->curve_editor_modal_request_open) {
        RenderCurveEditorModal(editor);
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
        
        bool build_status_visible = ImGui::Begin("BuildStatus", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | 
            ImGuiWindowFlags_AlwaysAutoResize);
        if (build_status_visible) {
            ImGui::Text("%s", editor->build_status_message);
        }
        ImGui::End();
        
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

static const char* AssetFileName(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

static bool AssetNameMatches(const char* file_name, const char* reference) {
    return file_name && file_name[0] && reference && reference[0] &&
           _stricmp(file_name, AssetFileName(reference)) == 0;
}

static bool ResolveProjectMeshPath(const ProjectData* project, const MeshCue* cue,
                                   char* out_path, size_t out_size) {
    if (!project || !cue || !out_path || out_size == 0) return false;
    out_path[0] = '\0';
    const char* candidates[3] = {cue->asset_path, nullptr, nullptr};
    char workspace_path[512] = {};
    char asset_path[512] = {};
    if (project->workspace_path[0] && cue->asset_path[0]) {
        snprintf(workspace_path, sizeof(workspace_path), "%s\\%s",
                 project->workspace_path, cue->asset_path);
        candidates[1] = workspace_path;
    }
    if (project->assets_path[0] && cue->asset_key[0]) {
        snprintf(asset_path, sizeof(asset_path), "%s\\%s",
                 project->assets_path, cue->asset_key);
        candidates[2] = asset_path;
    }
    for (const char* candidate : candidates) {
        if (candidate && candidate[0] && FileExists(candidate)) {
            strncpy_s(out_path, out_size, candidate, _TRUNCATE);
            return true;
        }
    }
    return false;
}

static bool ProjectMeshUsesAssetFile(const ProjectData* project, const MeshCue* cue,
                                     const char* file_name) {
    if (!project || !cue || cue->mesh_type != 4 || !file_name || !file_name[0]) return false;

    char mesh_path[512] = {};
    if (!ResolveProjectMeshPath(project, cue, mesh_path, sizeof(mesh_path))) return false;

    char extracted_paths[64][512] = {};
    int extracted_count = rev::gltf::ExtractTextures(
        mesh_path, project->assets_path, extracted_paths, (int)_countof(extracted_paths));
    for (int i = 0; i < extracted_count; ++i) {
        if (AssetNameMatches(file_name, extracted_paths[i])) return true;
    }
    return false;
}

static bool ProjectUsesAssetFile(const ProjectData* project, const char* file_name) {
    if (!project || !file_name || !file_name[0]) return false;

    for (int scene_index = 0; scene_index < project->scene_count; ++scene_index) {
        const SceneBlock* scene = &project->scenes[scene_index];
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            const ShaderCue* cue = &scene->shader_cues[i];
            for (int map = 0; map < 4; ++map)
                if (AssetNameMatches(file_name, cue->noise_textures.paths[map])) return true;
        }
        for (int i = 0; i < scene->image_cue_count; ++i) {
            const ImageCue* cue = &scene->image_cues[i];
            if (AssetNameMatches(file_name, cue->asset_key) || AssetNameMatches(file_name, cue->asset_path)) return true;
        }
        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            const AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
            char keys[4096] = {};
            char paths[8192] = {};
            strncpy_s(keys, cue->frame_keys_csv, _TRUNCATE);
            strncpy_s(paths, cue->frame_paths_csv, _TRUNCATE);
            char* context = nullptr;
            for (char* token = strtok_s(keys, ";", &context); token; token = strtok_s(nullptr, ";", &context))
                if (AssetNameMatches(file_name, token)) return true;
            context = nullptr;
            for (char* token = strtok_s(paths, ";", &context); token; token = strtok_s(nullptr, ";", &context))
                if (AssetNameMatches(file_name, token)) return true;
        }
        for (int i = 0; i < scene->pixel_cue_count; ++i) {
            const PixelCue* cue = &scene->pixel_cues[i];
            if (AssetNameMatches(file_name, cue->asset_key) || AssetNameMatches(file_name, cue->asset_path)) return true;
        }
        for (int i = 0; i < scene->pixel_emitter_cue_count; ++i) {
            const PixelEmitterCue* cue = &scene->pixel_emitter_cues[i];
            if (cue->visual_source != 0) continue;
            if (AssetNameMatches(file_name, cue->asset_key) || AssetNameMatches(file_name, cue->asset_path)) return true;
        }
        for (int i = 0; i < scene->text_cue_count; ++i) {
            const TextCue* cue = &scene->text_cues[i];
            if (AssetNameMatches(file_name, cue->baked_asset_key) || AssetNameMatches(file_name, cue->baked_asset_path) ||
                AssetNameMatches(file_name, cue->glyph_atlas_key) || AssetNameMatches(file_name, cue->glyph_atlas_path) ||
                AssetNameMatches(file_name, cue->glyph_meta_key) || AssetNameMatches(file_name, cue->glyph_meta_path)) return true;
        }
        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            const ScrollTextCue* cue = &scene->scroll_text_cues[i];
            if (AssetNameMatches(file_name, cue->baked_asset_key) || AssetNameMatches(file_name, cue->baked_asset_path) ||
                AssetNameMatches(file_name, cue->glyph_atlas_key) || AssetNameMatches(file_name, cue->glyph_atlas_path) ||
                AssetNameMatches(file_name, cue->glyph_meta_key) || AssetNameMatches(file_name, cue->glyph_meta_path)) return true;
        }
        for (int i = 0; i < scene->music_cue_count; ++i) {
            const MusicCue* cue = &scene->music_cues[i];
            if (AssetNameMatches(file_name, cue->asset_key) || AssetNameMatches(file_name, cue->asset_path)) return true;
        }
        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            const MeshCue* cue = &scene->mesh_cues[i];
            if (cue->mesh_type == 4 &&
                (AssetNameMatches(file_name, cue->asset_key) || AssetNameMatches(file_name, cue->asset_path) ||
                 ProjectMeshUsesAssetFile(project, cue, file_name))) return true;
        }
    }
    return false;
}

static int CleanUnusedProjectAssets(EditorContext* editor) {
    if (!editor || !editor->project || !editor->project->assets_path[0]) return -1;

    char search_path[512] = {};
    snprintf(search_path, sizeof(search_path), "%s\\*.*", editor->project->assets_path);
    WIN32_FIND_DATAA find_data = {};
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) return -1;

    int removed = 0;
    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (ProjectUsesAssetFile(editor->project, find_data.cFileName)) continue;

        char file_path[512] = {};
        snprintf(file_path, sizeof(file_path), "%s\\%s", editor->project->assets_path, find_data.cFileName);
        if (DeleteFileA(file_path)) {
            ++removed;
            printf("[Assets] Removed unused asset: %s\n", file_path);
        }
    } while (FindNextFileA(find_handle, &find_data));
    FindClose(find_handle);
    return removed;
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
            ImGui::MenuItem("Trigger Recorder", nullptr, &editor->show_trigger_recorder);
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
            if (ImGui::MenuItem("Pack Project (No Compile)")) {
                PackProject(editor);
            }
            if (ImGui::MenuItem("Clean Unused Project Assets")) {
                int removed = CleanUnusedProjectAssets(editor);
                if (removed < 0) {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
                } else {
                    char message[128] = {};
                    snprintf(message, sizeof(message), "Removed %d unused project asset(s).", removed);
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), message, _TRUNCATE);
                    editor->project->modified = true;
                }
                editor->build_status_timer = 5.0f;
            }
            if (ImGui::MenuItem("Pack, Build and Run")) {
                PackBuildAndRun(editor);
            }
            if (ImGui::MenuItem("Build Screen Saver (.scr)")) {
                BuildScreenSaver(editor);
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

// Panel and modal implementations moved to editor_panels.cpp and editor_modals.cpp

bool ImportFromCues(EditorContext* editor, const char* cues_path) {
    if (!editor || !cues_path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;
    
    printf("[ImportFromCues] Reading %s\n", cues_path);
    
    // Clear existing project and create a default scene
    NewProject(editor);
    
    char line[1024];
    enum Section { NONE, SHADER_CUES, IMAGE_CUES, ANIMATED_SPRITE_CUES, PIXEL_CUES, PIXEL_EMITTER_CUES, TEXT_CUES, SCROLL_TEXT_CUES, MUSIC_CUES, POST_EFFECTS, CURVES, METADATA };
    Section current_section = NONE;
    
    float total_duration = 10.0f; // Default
    int intro_loop_setting = 0;
    int music_loop_setting = 0;
    int music_persist_setting = 0;
    int runtime_fullscreen_setting = 1;
    char runtime_title_setting[128] = "HiMYM - Minimal Intro Test";
    AudioEffects audio_effects = {};
    audio_effects.compressor_threshold = 0.7f;
    audio_effects.compressor_ratio = 4.0f;
    audio_effects.compressor_attack = 0.01f;
    audio_effects.compressor_release = 0.12f;
    audio_effects.widener_amount = 1.0f;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\n' || *start == '\r' || *start == '\0' || *start == '#') continue;
        
        // Section detection
        if (strstr(start, "[shader_cues]")) { current_section = SHADER_CUES; continue; }
        if (strstr(start, "[image_cues]")) { current_section = IMAGE_CUES; continue; }
        if (strstr(start, "[animated_sprite_cues]")) { current_section = ANIMATED_SPRITE_CUES; continue; }
        if (strstr(start, "[pixel_cues]")) { current_section = PIXEL_CUES; continue; }
        if (strstr(start, "[pixel_emitter_cues]")) { current_section = PIXEL_EMITTER_CUES; continue; }
        if (strstr(start, "[text_cues]")) { current_section = TEXT_CUES; continue; }
        if (strstr(start, "[scroll_text_cues]")) { current_section = SCROLL_TEXT_CUES; continue; }
        if (strstr(start, "[music_cues]")) { current_section = MUSIC_CUES; continue; }
        if (strstr(start, "[post_effects]")) { current_section = POST_EFFECTS; continue; }
        if (strstr(start, "[curves]")) { current_section = CURVES; continue; }
        if (strstr(start, "[metadata]")) { current_section = METADATA; continue; }
        
        // Parse metadata
        if (current_section == METADATA) {
            if (sscanf_s(start, "total_duration=%f", &total_duration) == 1) {
                printf("[ImportFromCues] Total duration: %.2f\n", total_duration);
            } else if (sscanf_s(start, "intro_loop=%d", &intro_loop_setting) == 1) {
                // parsed below
            } else if (sscanf_s(start, "music_loop=%d", &music_loop_setting) == 1) {
                // parsed below
            } else if (sscanf_s(start, "music_persist=%d", &music_persist_setting) == 1) {
                // parsed below
            } else if (sscanf_s(start, "runtime_fullscreen=%d", &runtime_fullscreen_setting) == 1) {
                // parsed below
            } else if (strncmp(start, "runtime_title=", 14) == 0) {
                strncpy_s(runtime_title_setting, sizeof(runtime_title_setting), start + 14, _TRUNCATE);
                size_t title_len = strcspn(runtime_title_setting, "\r\n");
                runtime_title_setting[title_len] = '\0';
            } else if (sscanf_s(start, "audio_gain_enabled=%d", &audio_effects.gain_enabled) == 1) {
            } else if (sscanf_s(start, "audio_gain_db=%f", &audio_effects.gain_db) == 1) {
            } else if (sscanf_s(start, "audio_compressor_enabled=%d", &audio_effects.compressor_enabled) == 1) {
            } else if (sscanf_s(start, "audio_compressor_threshold=%f", &audio_effects.compressor_threshold) == 1) {
            } else if (sscanf_s(start, "audio_compressor_ratio=%f", &audio_effects.compressor_ratio) == 1) {
            } else if (sscanf_s(start, "audio_compressor_attack=%f", &audio_effects.compressor_attack) == 1) {
            } else if (sscanf_s(start, "audio_compressor_release=%f", &audio_effects.compressor_release) == 1) {
            } else if (sscanf_s(start, "audio_widener_enabled=%d", &audio_effects.widener_enabled) == 1) {
            } else if (sscanf_s(start, "audio_widener_amount=%f", &audio_effects.widener_amount) == 1) {
            } else if (sscanf_s(start, "audio_eq_enabled=%d", &audio_effects.eq_enabled) == 1) {
            } else if (sscanf_s(start, "audio_eq_low_db=%f", &audio_effects.eq_low_db) == 1) {
            } else if (sscanf_s(start, "audio_eq_mid_db=%f", &audio_effects.eq_mid_db) == 1) {
            } else if (sscanf_s(start, "audio_eq_high_db=%f", &audio_effects.eq_high_db) == 1) {
            }
            continue;
        }
        
        // Parse shader cues
        if (current_section == SHADER_CUES) {
            ShaderCue cue = {};
            ResetShaderValues(&cue);
            int shader_id;
            float abs_start, abs_end;
            
            int parsed = sscanf_s(start,
                "%d|"
                "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|"
                "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|"
                "%d|%f|%d|%d|"
                "%d|%d|%d|%d|%d|%d|%d|%d|%d|"
                "%d|%d|%d|%d|%d|%d|%d|%"
                "d|"
                "%f|%f|%f|%f|%f|%f|%f|%f|%f|"
                "%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f",
                &shader_id,
                &cue.palette_low.r, &cue.palette_low.g, &cue.palette_low.b,
                &cue.palette_mid.r, &cue.palette_mid.g, &cue.palette_mid.b,
                &cue.palette_high.r, &cue.palette_high.g, &cue.palette_high.b,
                &cue.speed, &cue.intensity, &cue.warp,
                &cue.exposure_base, &cue.exposure_ramp,
                &cue.fade_base, &cue.fade_ramp,
                &abs_start, &abs_end, &cue.fade_in, &cue.fade_out,
                &cue.layer_role, &cue.opacity, &cue.blend_mode, &cue.layer_order,
                &cue.curve_speed, &cue.curve_intensity, &cue.curve_warp,
                &cue.curve_exposure, &cue.curve_fade,
                &cue.curve_palette_low_r, &cue.curve_palette_low_g, &cue.curve_palette_low_b,
                &cue.curve_palette_mid_r, &cue.curve_palette_mid_g, &cue.curve_palette_mid_b,
                &cue.curve_palette_high_r, &cue.curve_palette_high_g, &cue.curve_palette_high_b,
                &cue.curve_opacity, &cue.curve_exposure_ramp, &cue.curve_fade_ramp,
                &cue.position_x, &cue.position_y, &cue.position_z,
                &cue.rotation_x, &cue.rotation_y, &cue.rotation_z,
                &cue.motion_x, &cue.motion_y, &cue.motion_z,
                &cue.noise.enabled, &cue.noise.type, &cue.noise.scale,
                &cue.noise.strength, &cue.noise.octaves, &cue.noise.lacunarity,
                &cue.noise.gain, &cue.noise.warp, &cue.noise.speed_x,
                &cue.noise.speed_y, &cue.noise.seed, &cue.noise.contrast
            );
            ParseShaderNoiseMapPaths(start, cue.noise_textures.paths);
            
            if (parsed >= 18) { // At least basic params
                cue.shader_scene_id = shader_id;
                cue.cue_start = abs_start;
                cue.cue_end = abs_end;
                
                // Set shader name based on ID
                const char* preset_name = "Unknown";
                for (int i = 0; i < g_shader_preset_count; i++) {
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
        
        // Parse image cues: ...|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|blend_mode(optional)
        if (current_section == IMAGE_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            ImageCue cue = {};
            cue.curve_rotation = -1;
            size_t key_len = (size_t)(p1 - start);
            if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
            strncpy_s(cue.asset_key, start, key_len);
            char* p2 = strchr(p1 + 1, '|'); // skip asset_path field
            if (!p2) continue;
            float abs_start = 0.0f, abs_end = 0.0f;
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f|%d|%d|%d|%d|%d|%f|%d",
                &cue.x, &cue.y, &cue.scale, &cue.opacity,
                &abs_start, &abs_end, &cue.layer_order,
                &cue.effect_type, &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.curve_x, &cue.curve_y, &cue.curve_scale, &cue.curve_opacity,
                &cue.blend_mode, &cue.rotation, &cue.curve_rotation
            );
            if (parsed >= 7) {
                if (parsed < 17) {
                    cue.blend_mode = (parsed >= 13) ? cue.curve_x : 0;
                    cue.curve_x = cue.curve_y = cue.curve_scale = cue.curve_opacity = -1;
                }
                cue.cue_start = abs_start;
                cue.cue_end = abs_end;
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                SceneBlock* scene = &editor->project->scenes[0];
                ParseLayerPostEffectsPipe(start, 21, cue.post_effects, &cue.post_effect_count);
                AddImageCue(scene, cue);
                printf("[ImportFromCues] Imported image cue: %s\n", cue.asset_key);
            }
            continue;
        }

        // Parse animated sprite cues:
        // sprite_name|frame_keys_csv|frame_paths_csv|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|blend_mode|fps|playback_mode|start_frame|curve_x|curve_y|curve_scale|curve_opacity|curve_frame
        if (current_section == ANIMATED_SPRITE_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            AnimatedSpriteCue cue = {};
            cue.fps = 12.0f;
            cue.playback_mode = 0;
            cue.start_frame = 0;
            cue.curve_x = cue.curve_y = cue.curve_scale = cue.curve_rotation = cue.curve_opacity = cue.curve_frame = -1;

            size_t sprite_name_len = (size_t)(p1 - start);
            if (sprite_name_len >= sizeof(cue.sprite_name)) sprite_name_len = sizeof(cue.sprite_name) - 1;
            strncpy_s(cue.sprite_name, start, sprite_name_len);

            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t keys_len = (size_t)(p2 - (p1 + 1));
            if (keys_len >= sizeof(cue.frame_keys_csv)) keys_len = sizeof(cue.frame_keys_csv) - 1;
            strncpy_s(cue.frame_keys_csv, p1 + 1, keys_len);

            char* p3 = strchr(p2 + 1, '|');
            if (!p3) continue;
            size_t paths_len = (size_t)(p3 - (p2 + 1));
            if (paths_len >= sizeof(cue.frame_paths_csv)) paths_len = sizeof(cue.frame_paths_csv) - 1;
            strncpy_s(cue.frame_paths_csv, p2 + 1, paths_len);

            int parsed = sscanf_s(p3 + 1, "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f|%d|%f|%d|%d|%d|%d|%d|%d|%d|%f|%d",
                &cue.x, &cue.y, &cue.scale, &cue.opacity,
                &cue.cue_start, &cue.cue_end,
                &cue.layer_order, &cue.effect_type,
                &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.blend_mode, &cue.fps, &cue.playback_mode, &cue.start_frame,
                &cue.curve_x, &cue.curve_y, &cue.curve_scale, &cue.curve_opacity, &cue.curve_frame,
                &cue.rotation, &cue.curve_rotation);
            if (parsed >= 6) {
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                SceneBlock* scene = &editor->project->scenes[0];
                ParseLayerPostEffectsPipe(start, 26, cue.post_effects, &cue.post_effect_count);
                AddAnimatedSpriteCue(scene, cue);
                printf("[ImportFromCues] Imported animated sprite cue: %s\n", cue.sprite_name);
            }
            continue;
        }

        // Parse pixel cues:
        // asset_key|asset_path|x|y|scale|rotation|opacity|cue_start|cue_end|layer_order|blend_mode|fps|playback_mode|start_frame|palette_offset|palette_cycle_speed|snap_to_pixels|curve_x|curve_y|curve_scale|curve_rotation|curve_opacity|curve_frame|curve_palette_offset
        if (current_section == PIXEL_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            PixelCue cue = {};
            cue.fps = 12.0f;
            cue.snap_to_pixels = 1;
            cue.curve_x = cue.curve_y = cue.curve_scale = cue.curve_rotation = cue.curve_opacity = cue.curve_frame = cue.curve_palette_offset = -1;
            size_t key_len = (size_t)(p1 - start);
            if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
            strncpy_s(cue.asset_key, start, key_len);
            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t path_len = (size_t)(p2 - (p1 + 1));
            if (path_len >= sizeof(cue.asset_path)) path_len = sizeof(cue.asset_path) - 1;
            strncpy_s(cue.asset_path, p1 + 1, path_len);
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%f|%d|%d|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
                &cue.x, &cue.y, &cue.scale, &cue.rotation, &cue.opacity,
                &cue.cue_start, &cue.cue_end, &cue.layer_order, &cue.blend_mode,
                &cue.fps, &cue.playback_mode, &cue.start_frame, &cue.palette_offset,
                &cue.palette_cycle_speed, &cue.snap_to_pixels, &cue.curve_x, &cue.curve_y,
                &cue.curve_scale, &cue.curve_rotation, &cue.curve_opacity, &cue.curve_frame,
                &cue.curve_palette_offset);
            if (parsed >= 7) {
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                SceneBlock* scene = &editor->project->scenes[0];
                ParseLayerPostEffectsPipe(start, 25, cue.post_effects, &cue.post_effect_count);
                AddPixelCue(scene, cue);
            }
            continue;
        }

        if (current_section == PIXEL_EMITTER_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            PixelEmitterCue cue = {};
            cue.curve_x = cue.curve_y = -1;
            cue.curve_scale = cue.curve_rotation = -1;
            cue.curve_opacity = cue.curve_emission_rate = -1;
            cue.curve_speed_min = cue.curve_speed_max = -1;
            cue.curve_lifetime_min = cue.curve_lifetime_max = -1;
            cue.curve_scale_min = cue.curve_scale_max = -1;
            size_t key_len = (size_t)(p1 - start);
            if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
            strncpy_s(cue.asset_key, start, key_len);
            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t path_len = (size_t)(p2 - (p1 + 1));
            if (path_len >= sizeof(cue.asset_path)) path_len = sizeof(cue.asset_path) - 1;
            strncpy_s(cue.asset_path, p1 + 1, path_len);
            int parsed = sscanf_s(p2 + 1,
                "%d|%d|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%f|%d|%f|%d|%f|%f|%f|%f|%f|%f|%u",
                &cue.visual_source, &cue.primitive_shape, &cue.x, &cue.y,
                &cue.scale, &cue.rotation, &cue.opacity, &cue.cue_start, &cue.cue_end,
                &cue.layer_order, &cue.blend_mode, &cue.max_particles, &cue.emission_rate,
                &cue.burst_count, &cue.duration, &cue.loop, &cue.speed_min, &cue.speed_max,
                &cue.lifetime_min, &cue.lifetime_max, &cue.scale_min, &cue.scale_max, &cue.seed);
            if (parsed >= 9) {
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                SceneBlock* scene = &editor->project->scenes[0];
                AddPixelEmitterCue(scene, cue);
            }
            continue;
        }

        // Parse text cues: ...|layer_order|blend_mode|curve_x|curve_y|curve_size|curve_color_r|curve_color_g|curve_color_b|bake_mode|baked_asset_key|baked_asset_path(optional)
        if (current_section == TEXT_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            TextCue cue = {};
            size_t text_len = (size_t)(p1 - start);
            if (text_len >= sizeof(cue.text)) text_len = sizeof(cue.text) - 1;
            char encoded_text[256] = {};
            strncpy_s(encoded_text, sizeof(encoded_text), start, text_len);
            JsonUnescapeString(encoded_text, cue.text, sizeof(cue.text));
            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t font_len = (size_t)(p2 - (p1 + 1));
            if (font_len >= sizeof(cue.font_name)) font_len = sizeof(cue.font_name) - 1;
            strncpy_s(cue.font_name, p1 + 1, font_len);
            float size_f = 0.0f;
            char baked_key[64] = {};
            char baked_path[512] = {};
            int blend_mode = 0;
            int bake_mode = 0;
            bool parsed_with_bake_mode = true;
            bool parsed_with_blend_mode = true;
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%d|%*d|%*d|%*d|%*d|%*d|%*d|%d|%63[^|]|%511[^|\r\n]",
                &cue.x, &cue.y, &size_f,
                &cue.color.r, &cue.color.g, &cue.color.b,
                &cue.effect_type, &cue.cue_start, &cue.cue_end,
                &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.layer_order,
                &blend_mode,
                &bake_mode,
                baked_key, (unsigned)_countof(baked_key),
                baked_path, (unsigned)_countof(baked_path));
            if (parsed < 16) {
                // Backward compatibility: older exports had no blend_mode column.
                parsed_with_blend_mode = false;
                parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%*d|%*d|%*d|%*d|%*d|%*d|%d|%63[^|]|%511[^|\r\n]",
                    &cue.x, &cue.y, &size_f,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.effect_type, &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order,
                    &bake_mode,
                    baked_key, (unsigned)_countof(baked_key),
                    baked_path, (unsigned)_countof(baked_path));
            }
            if (parsed < 16) {
                // Backward compatibility: older exports had no bake_mode column.
                parsed_with_bake_mode = false;
                parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%*d|%*d|%*d|%*d|%*d|%*d|%63[^|]|%511[^|\r\n]",
                    &cue.x, &cue.y, &size_f,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.effect_type, &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order,
                    baked_key, (unsigned)_countof(baked_key),
                    baked_path, (unsigned)_countof(baked_path));
                bake_mode = 0;
            }
            if (parsed >= 9) {
                cue.size = size_f;
                cue.blend_mode = (parsed_with_blend_mode && parsed >= 15) ? blend_mode : 0;
                cue.bake_mode = (parsed_with_bake_mode && parsed >= (parsed_with_blend_mode ? 16 : 15)) ? bake_mode : 0;
                if (parsed_with_bake_mode) {
                    if (parsed >= (parsed_with_blend_mode ? 17 : 16)) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
                    if (parsed >= (parsed_with_blend_mode ? 18 : 17)) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);
                } else {
                    if (parsed >= (parsed_with_blend_mode ? 16 : 15)) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
                    if (parsed >= (parsed_with_blend_mode ? 17 : 16)) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);
                }
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                AddTextCue(&editor->project->scenes[0], cue);
                printf("[ImportFromCues] Imported text cue: %s\n", cue.text);
            }
            continue;
        }

        // Parse scroll text cues:
        // text|font_name|x|y|size|color_r|color_g|color_b|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|style_id|direction|speed|spacing|wave_amp|wave_freq|glow|opacity|wrap_gap|slant_deg|jitter_amp|jitter_freq|shadow|outline|curve_x|curve_y|curve_speed|curve_size|curve_opacity|curve_color_r|curve_color_g|curve_color_b|curve_wave_amp|curve_wave_freq|curve_jitter_amp|curve_jitter_freq|loop_mode|chroma_shift|distortion|bake_mode|baked_asset_key|baked_asset_path|wave_length|curve_wave_length
        if (current_section == SCROLL_TEXT_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            ScrollTextCue cue = {};
            cue.opacity = 1.0f;
            cue.size = 40.0f;
            cue.speed = 0.25f;
            cue.wrap_gap = 0.2f;
            cue.spacing = 1.0f;
            cue.wave_freq = 1.0f;
            cue.wave_length = 9.0f;
            cue.jitter_freq = 1.0f;
            cue.curve_x = cue.curve_y = cue.curve_speed = cue.curve_size = cue.curve_opacity = -1;
            cue.curve_color_r = cue.curve_color_g = cue.curve_color_b = -1;
            cue.curve_wave_amp = cue.curve_wave_freq = cue.curve_jitter_amp = cue.curve_jitter_freq = -1;
            cue.curve_wave_length = -1;

            size_t text_len = (size_t)(p1 - start);
            if (text_len >= sizeof(cue.text)) text_len = sizeof(cue.text) - 1;
            char encoded_text[512] = {};
            strncpy_s(encoded_text, sizeof(encoded_text), start, text_len);
            JsonUnescapeString(encoded_text, cue.text, sizeof(cue.text));

            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t font_len = (size_t)(p2 - (p1 + 1));
            if (font_len >= sizeof(cue.font_name)) font_len = sizeof(cue.font_name) - 1;
            strncpy_s(cue.font_name, p1 + 1, font_len);

            char baked_key[64] = {};
            char baked_path[512] = {};

            int parsed = sscanf_s(p2 + 1,
                "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%d|%63[^|]|%511[^|\r\n]",
                &cue.x, &cue.y, &cue.size,
                &cue.color.r, &cue.color.g, &cue.color.b,
                &cue.cue_start, &cue.cue_end,
                &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.layer_order, &cue.blend_mode, &cue.style_id, &cue.direction,
                &cue.speed, &cue.spacing, &cue.wave_amp, &cue.wave_freq,
                &cue.glow, &cue.opacity, &cue.wrap_gap, &cue.slant_deg,
                &cue.jitter_amp, &cue.jitter_freq, &cue.shadow, &cue.outline,
                &cue.curve_x, &cue.curve_y, &cue.curve_speed, &cue.curve_size, &cue.curve_opacity,
                &cue.curve_color_r, &cue.curve_color_g, &cue.curve_color_b,
                &cue.curve_wave_amp, &cue.curve_wave_freq, &cue.curve_jitter_amp, &cue.curve_jitter_freq,
                &cue.loop_mode, &cue.chroma_shift, &cue.distortion,
                &cue.bake_mode,
                baked_key, (unsigned)_countof(baked_key),
                baked_path, (unsigned)_countof(baked_path));

            if (parsed >= 45) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
            if (parsed >= 46) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);

            char* last_separator = strrchr(p2 + 1, '|');
            if (last_separator) {
                char* previous_separator = last_separator - 1;
                while (previous_separator > p2 + 1 && *previous_separator != '|') --previous_separator;
                if (previous_separator > p2 + 1) {
                    float wave_length = 0.0f;
                    int curve_wave_length = -1;
                    if (sscanf_s(previous_separator, "|%f", &wave_length) == 1 &&
                        sscanf_s(last_separator, "|%d", &curve_wave_length) == 1) {
                        cue.wave_length = wave_length;
                        cue.curve_wave_length = curve_wave_length;
                    }
                }
            }

            if (parsed < 44) {
                parsed = sscanf_s(p2 + 1,
                    "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%d|%63[^|]|%511[^|\r\n]",
                    &cue.x, &cue.y, &cue.size,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order, &cue.blend_mode, &cue.style_id, &cue.direction,
                    &cue.speed, &cue.spacing, &cue.wave_amp, &cue.wave_freq,
                    &cue.glow, &cue.opacity, &cue.wrap_gap, &cue.slant_deg,
                    &cue.jitter_amp, &cue.jitter_freq, &cue.shadow, &cue.outline,
                    &cue.curve_x, &cue.curve_y, &cue.curve_speed, &cue.curve_size, &cue.curve_opacity,
                    &cue.curve_color_r, &cue.curve_color_g, &cue.curve_color_b,
                    &cue.loop_mode, &cue.chroma_shift, &cue.distortion,
                    &cue.bake_mode,
                    baked_key, (unsigned)_countof(baked_key),
                    baked_path, (unsigned)_countof(baked_path));
                if (parsed >= 40) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
                if (parsed >= 41) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);
            }

            if (parsed < 39) {
                parsed = sscanf_s(p2 + 1,
                    "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f",
                    &cue.x, &cue.y, &cue.size,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order, &cue.blend_mode, &cue.style_id, &cue.direction,
                    &cue.speed, &cue.spacing, &cue.wave_amp, &cue.wave_freq,
                    &cue.glow, &cue.opacity, &cue.wrap_gap, &cue.slant_deg,
                    &cue.jitter_amp, &cue.jitter_freq, &cue.shadow, &cue.outline,
                    &cue.curve_x, &cue.curve_y, &cue.curve_speed, &cue.curve_size, &cue.curve_opacity,
                    &cue.curve_color_r, &cue.curve_color_g, &cue.curve_color_b,
                    &cue.loop_mode, &cue.chroma_shift, &cue.distortion);
            }

            if (parsed >= 8) {
                if (editor->project->scene_count == 0) {
                    AddScene(editor, "Imported Scene", total_duration);
                }
                AddScrollTextCue(&editor->project->scenes[0], cue);
                printf("[ImportFromCues] Imported scroll text cue: %s\n", cue.text);
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

        if (current_section == POST_EFFECTS) {
            PostEffect effect = {};
            effect.curve_intensity = effect.curve_threshold = effect.curve_radius = -1;
            effect.curve_color_r = effect.curve_color_g = effect.curve_color_b = effect.curve_color_a = -1;
            effect.curve_amount = -1;
            int scene_index = 0;
            int enabled = 1;
            float curve_amount_legacy = 0.0f;
            int parsed = sscanf_s(start, "%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d",
                &scene_index, &effect.type, &enabled, &effect.order,
                &effect.intensity, &effect.threshold, &effect.radius,
                &effect.color[0], &effect.color[1], &effect.color[2], &effect.color[3],
                &effect.start_time, &effect.end_time, &curve_amount_legacy,
                &effect.curve_intensity, &effect.curve_threshold, &effect.curve_radius,
                &effect.curve_color_r, &effect.curve_color_g, &effect.curve_color_b,
                &effect.curve_color_a, &effect.curve_amount);
            if (parsed >= 14) {
                if (parsed < 22) {
                    effect.curve_intensity = effect.curve_threshold = effect.curve_radius = -1;
                    effect.curve_color_r = effect.curve_color_g = effect.curve_color_b = effect.curve_color_a = -1;
                    effect.curve_amount = -1;
                }
                effect.enabled = enabled != 0;
                if (editor->project->scene_count == 0) {
                    AddScene(editor, "Imported Scene", 10.0f);
                }
                int target_scene = scene_index;
                if (target_scene < 0 || target_scene >= editor->project->scene_count) target_scene = 0;
                SceneBlock* scene = &editor->project->scenes[target_scene];
                float scene_start = 0.0f;
                for (int si = 0; si < target_scene; ++si) scene_start += editor->project->scenes[si].duration;
                effect.start_time -= scene_start;
                effect.end_time = effect.end_time < 0.0f ? -1.0f : effect.end_time - scene_start;
                AddPostEffect(scene, effect);
            }
            continue;
        }
    }

    fclose(f);
    
    // Update project metadata
    if (editor->project) {
        editor->project->total_duration = total_duration;
        editor->project->loop_intro = (intro_loop_setting != 0);
        editor->project->loop_music = (music_loop_setting != 0);
        editor->project->music_persist_across_scenes = (music_persist_setting != 0);
        editor->project->runtime_fullscreen = (runtime_fullscreen_setting != 0);
        strncpy_s(editor->project->runtime_title, sizeof(editor->project->runtime_title), runtime_title_setting, _TRUNCATE);
        editor->project->audio_effects = audio_effects;
    }
    
    printf("[ImportFromCues] Import complete!\n");
    return true;
}

static void WriteLayerPostEffectsPipe(FILE* f, const LayerPostEffect* effects, int effect_count) {
    fprintf(f, "|%d", effect_count);
    for (int i = 0; i < effect_count && i < rev::runtime::kMaxLayerPostEffects; ++i) {
        const LayerPostEffect& effect = effects[i];
        fprintf(f, "|%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                effect.type, effect.enabled ? 1 : 0, effect.order,
                effect.intensity, effect.threshold, effect.radius,
                effect.color[0], effect.color[1], effect.color[2], effect.color[3],
                effect.start_time, effect.end_time,
                effect.curve_intensity, effect.curve_threshold, effect.curve_radius,
                effect.curve_color_r, effect.curve_color_g, effect.curve_color_b,
                effect.curve_color_a, effect.curve_amount, effect.blend_mode);
    }
}

static void WriteAssetShadersPipe(FILE* f, const AssetShader* shaders, int shader_count) {
    fprintf(f, "|%d", shader_count);
    for (int i = 0; i < shader_count && i < rev::runtime::kMaxAssetShaders; ++i) {
        const AssetShader& shader = shaders[i];
        fprintf(f, "|%d,%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
                shader.shader_id, shader.enabled ? 1 : 0, shader.order, shader.blend_mode,
                shader.opacity, shader.speed, shader.intensity, shader.warp,
                shader.exposure_base, shader.exposure_ramp, shader.fade_base, shader.fade_ramp,
                shader.palette_low[0], shader.palette_low[1], shader.palette_low[2],
                shader.palette_mid[0], shader.palette_mid[1], shader.palette_mid[2],
                shader.palette_high[0], shader.palette_high[1], shader.palette_high[2],
                shader.start_time, shader.end_time);
    }
}

bool ExportProject(EditorContext* editor, const char* output_path) {
    if (!editor || !output_path) return false;

    editor->project->total_duration = 0.0f;
    for (int i = 0; i < editor->project->scene_count; ++i) {
        if (editor->project->scenes[i].duration > 0.0f) {
            editor->project->total_duration += editor->project->scenes[i].duration;
        }
    }

    FILE* f = nullptr;
    fopen_s(&f, output_path, "w");
    if (!f) return false;

    // [shader_cues] section
    fprintf(f, "[shader_cues]\n");
    fprintf(f, "# shader_scene_id|palette_low_r|palette_low_g|palette_low_b|palette_mid_r|palette_mid_g|palette_mid_b|palette_high_r|palette_high_g|palette_high_b|speed|intensity|warp|exposure_base|exposure_ramp|fade_base|fade_ramp|cue_start|cue_end|fade_in|fade_out|layer_role|opacity|blend_mode|layer_order|curve_speed|curve_intensity|curve_warp|curve_exposure|curve_fade|curve_palette_low_r|curve_palette_low_g|curve_palette_low_b|curve_palette_mid_r|curve_palette_mid_g|curve_palette_mid_b|curve_palette_high_r|curve_palette_high_g|curve_palette_high_b|curve_opacity|curve_exposure_ramp|curve_fade_ramp|position_x|position_y|position_z|rotation_x|rotation_y|rotation_z|motion_x|motion_y|motion_z|noise_enabled|noise_type|noise_scale|noise_strength|noise_octaves|noise_lacunarity|noise_gain|noise_warp|noise_speed_x|noise_speed_y|noise_seed|noise_contrast|noise_map_0|noise_map_1|noise_map_2|noise_map_3\n");
    
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
            
            fprintf(f,
                "%d|"
                "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|"
                "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|"
                "%d|%.3f|%d|%d|"
                "%d|%d|%d|%d|%d|%d|%d|%d|%d|"
                "%d|%d|%d|%d|%d|%d|%d|%d|"
                "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|"
                "%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%s|%s|%s|%s\n",
                cue->shader_scene_id,
                cue->palette_low.r, cue->palette_low.g, cue->palette_low.b,
                cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b,
                cue->palette_high.r, cue->palette_high.g, cue->palette_high.b,
                cue->speed, cue->intensity, cue->warp,
                cue->exposure_base, cue->exposure_ramp,
                cue->fade_base, cue->fade_ramp,
                abs_start, abs_end, cue->fade_in, cue->fade_out,
                cue->layer_role, cue->opacity, cue->blend_mode, cue->layer_order,
                cue->curve_speed, cue->curve_intensity, cue->curve_warp,
                cue->curve_exposure, cue->curve_fade,
                cue->curve_palette_low_r, cue->curve_palette_low_g, cue->curve_palette_low_b,
                cue->curve_palette_mid_r, cue->curve_palette_mid_g, cue->curve_palette_mid_b,
                cue->curve_palette_high_r, cue->curve_palette_high_g, cue->curve_palette_high_b,
                cue->curve_opacity, cue->curve_exposure_ramp, cue->curve_fade_ramp,
                cue->position_x, cue->position_y, cue->position_z,
                cue->rotation_x, cue->rotation_y, cue->rotation_z,
                cue->motion_x, cue->motion_y, cue->motion_z,
                cue->noise.enabled, cue->noise.type, cue->noise.scale,
                cue->noise.strength, cue->noise.octaves, cue->noise.lacunarity,
                cue->noise.gain, cue->noise.warp, cue->noise.speed_x,
                cue->noise.speed_y, cue->noise.seed, cue->noise.contrast,
                cue->noise_textures.paths[0][0] ? cue->noise_textures.paths[0] : "-",
                cue->noise_textures.paths[1][0] ? cue->noise_textures.paths[1] : "-",
                cue->noise_textures.paths[2][0] ? cue->noise_textures.paths[2] : "-",
                cue->noise_textures.paths[3][0] ? cue->noise_textures.paths[3] : "-"
            );
            
            shader_cue_id++;
        }
    }
    
    fprintf(f, "\n");

    // [post_effects] section. Times are absolute in the exported timeline.
    fprintf(f, "[post_effects]\n");
    fprintf(f, "# scene_index|type|enabled|order|intensity|threshold|radius|color_r|color_g|color_b|color_a|start_time|end_time|legacy_curve_amount|curve_intensity|curve_threshold|curve_radius|curve_color_r|curve_color_g|curve_color_b|curve_color_a|curve_amount\n");
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        for (int i = 0; i < scene_idx; ++i) scene_start += editor->project->scenes[i].duration;
        for (int effect_idx = 0; effect_idx < scene->post_effect_count; ++effect_idx) {
            PostEffect* effect = &scene->post_effects[effect_idx];
            float abs_start = scene_start + effect->start_time;
            float abs_end = effect->end_time < 0.0f ? -1.0f : scene_start + effect->end_time;
            fprintf(f, "%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|-1|%d|%d|%d|%d|%d|%d|%d|%d\n",
                scene_idx, effect->type, effect->enabled ? 1 : 0, effect->order,
                effect->intensity, effect->threshold, effect->radius,
                effect->color[0], effect->color[1], effect->color[2], effect->color[3],
                abs_start, abs_end,
                effect->curve_intensity, effect->curve_threshold, effect->curve_radius,
                effect->curve_color_r, effect->curve_color_g, effect->curve_color_b,
                effect->curve_color_a, effect->curve_amount);
        }
    }

    fprintf(f, "\n");

    fprintf(f, "[scene_layer_post_effects]\n");
    fprintf(f, "# scene_start|scene_end|effect_count|type,enabled,order,intensity,threshold,radius,color_r,color_g,color_b,color_a,start_time,end_time,curve_intensity,curve_threshold,curve_radius,curve_color_r,curve_color_g,curve_color_b,curve_color_a,curve_amount,blend_mode...\n");
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        for (int i = 0; i < scene_idx; ++i) scene_start += editor->project->scenes[i].duration;
        fprintf(f, "%.3f|%.3f", scene_start, scene_start + scene->duration);
        WriteLayerPostEffectsPipe(f, scene->scene_layer_post_effects,
                                  scene->scene_layer_post_effect_count);
        fprintf(f, "\n");
    }

    fprintf(f, "\n");
    
    // [image_cues] section
    fprintf(f, "[image_cues]\n");
    fprintf(f, "# asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|curve_x|curve_y|curve_scale|curve_opacity|blend_mode|rotation|curve_rotation\n");
    
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
            float abs_fade_in_start  = scene_start + cue->fade_in_start;
            float abs_fade_in_end    = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end   = scene_start + cue->fade_out_end;
            
            // Construct workspace-root-relative path: rel_assets_prefix/asset_key
            char full_path[640];
            snprintf(full_path, sizeof(full_path), "%s/%s", rel_assets_prefix, cue->asset_key);
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%.3f|%d",
                cue->asset_key, full_path, cue->x, cue->y, cue->scale, cue->opacity,
                abs_start, abs_end, cue->layer_order,
                cue->effect_type, abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->curve_x, cue->curve_y, cue->curve_scale, cue->curve_opacity,
                cue->blend_mode, cue->rotation, cue->curve_rotation
            );
            WriteLayerPostEffectsPipe(f, cue->post_effects, cue->post_effect_count);
            WriteAssetShadersPipe(f, cue->shaders, cue->shader_count);
            fprintf(f, "\n");
        }
    }
    
    fprintf(f, "\n");

    // [animated_sprite_cues] section
    fprintf(f, "[animated_sprite_cues]\n");
    fprintf(f, "# sprite_name|frame_keys_csv|frame_paths_csv|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|blend_mode|fps|playback_mode|start_frame|curve_x|curve_y|curve_scale|curve_opacity|curve_frame|rotation|curve_rotation\n");

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;

        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }

        for (int cue_idx = 0; cue_idx < scene->animated_sprite_cue_count; ++cue_idx) {
            AnimatedSpriteCue* cue = &scene->animated_sprite_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = (cue->cue_end < 0.0f) ? (scene_start + scene->duration) : (scene_start + cue->cue_end);
            float abs_fade_in_start  = scene_start + cue->fade_in_start;
            float abs_fade_in_end    = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end   = scene_start + cue->fade_out_end;

            fprintf(f, "%s|%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%d|%d|%d|%d|%d|%d|%d|%.3f|%d",
                cue->sprite_name, cue->frame_keys_csv, cue->frame_paths_csv,
                cue->x, cue->y, cue->scale, cue->opacity,
                abs_start, abs_end,
                cue->layer_order, cue->effect_type,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->blend_mode,
                cue->fps, cue->playback_mode, cue->start_frame,
                cue->curve_x, cue->curve_y, cue->curve_scale, cue->curve_opacity, cue->curve_frame,
                cue->rotation, cue->curve_rotation);
            WriteLayerPostEffectsPipe(f, cue->post_effects, cue->post_effect_count);
            WriteAssetShadersPipe(f, cue->shaders, cue->shader_count);
            fprintf(f, "\n");
        }
    }

    fprintf(f, "\n");

    // [pixel_cues] section
    fprintf(f, "[pixel_cues]\n");
    fprintf(f, "# asset_key|asset_path|x|y|scale|rotation|opacity|cue_start|cue_end|layer_order|blend_mode|fps|playback_mode|start_frame|palette_offset|palette_cycle_speed|snap_to_pixels|curve_x|curve_y|curve_scale|curve_rotation|curve_opacity|curve_frame|curve_palette_offset\n");
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        for (int i = 0; i < scene_idx; ++i) scene_start += editor->project->scenes[i].duration;
        for (int cue_idx = 0; cue_idx < scene->pixel_cue_count; ++cue_idx) {
            PixelCue* cue = &scene->pixel_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = (cue->cue_end < 0.0f) ? (scene_start + scene->duration) : (scene_start + cue->cue_end);
            char asset_path[1024] = {};
            if (cue->asset_path[0] != '\0') {
                strncpy_s(asset_path, cue->asset_path, _TRUNCATE);
            } else {
                snprintf(asset_path, sizeof(asset_path), "%s/%s", rel_assets_prefix, cue->asset_key);
            }
            for (char* p = asset_path; *p; ++p) if (*p == '\\') *p = '/';
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
                cue->asset_key, asset_path, cue->x, cue->y, cue->scale, cue->rotation, cue->opacity,
                abs_start, abs_end, cue->layer_order, cue->blend_mode, cue->fps, cue->playback_mode,
                cue->start_frame, cue->palette_offset, cue->palette_cycle_speed, cue->snap_to_pixels,
                cue->curve_x, cue->curve_y, cue->curve_scale, cue->curve_rotation, cue->curve_opacity,
                cue->curve_frame, cue->curve_palette_offset);
            WriteLayerPostEffectsPipe(f, cue->post_effects, cue->post_effect_count);
            WriteAssetShadersPipe(f, cue->shaders, cue->shader_count);
            fprintf(f, "\n");
        }
    }

    fprintf(f, "\n[pixel_emitter_cues]\n");
    fprintf(f, "# asset_key|asset_path|visual_source|primitive_shape|x|y|scale|rotation|opacity|cue_start|cue_end|layer_order|blend_mode|max_particles|emission_rate|burst_count|duration|loop|speed_min|speed_max|lifetime_min|lifetime_max|scale_min|scale_max|seed|primitive_color_r|primitive_color_g|primitive_color_b|primitive_color_a|curve_x|curve_y|curve_scale|curve_rotation|curve_opacity|curve_emission_rate|curve_speed_min|curve_speed_max|curve_lifetime_min|curve_lifetime_max|curve_scale_min|curve_scale_max\n");
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        for (int i = 0; i < scene_idx; ++i) scene_start += editor->project->scenes[i].duration;
        for (int cue_idx = 0; cue_idx < scene->pixel_emitter_cue_count; ++cue_idx) {
            PixelEmitterCue* cue = &scene->pixel_emitter_cues[cue_idx];
            char asset_path[1024] = {};
            if (cue->asset_path[0] != '\0') {
                strncpy_s(asset_path, cue->asset_path, _TRUNCATE);
            } else if (cue->asset_key[0] != '\0') {
                snprintf(asset_path, sizeof(asset_path), "%s/%s", rel_assets_prefix, cue->asset_key);
            }
            for (char* p = asset_path; *p; ++p) if (*p == '\\') *p = '/';
            float abs_start = scene_start + cue->cue_start;
            float abs_end = cue->cue_end < 0.0f ? scene_start + scene->duration : scene_start + cue->cue_end;
            fprintf(f, "%s|%s|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%.3f|%d|%.3f|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%u|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n",
                cue->asset_key, asset_path, cue->visual_source, cue->primitive_shape,
                cue->x, cue->y, cue->scale, cue->rotation, cue->opacity,
                abs_start, abs_end, cue->layer_order, cue->blend_mode,
                cue->max_particles, cue->emission_rate, cue->burst_count, cue->duration,
                cue->loop, cue->speed_min, cue->speed_max, cue->lifetime_min,
                cue->lifetime_max, cue->scale_min, cue->scale_max, cue->seed,
                cue->primitive_color[0], cue->primitive_color[1], cue->primitive_color[2], cue->primitive_color[3],
                cue->curve_x, cue->curve_y, cue->curve_scale, cue->curve_rotation,
                cue->curve_opacity, cue->curve_emission_rate, cue->curve_speed_min, cue->curve_speed_max,
                cue->curve_lifetime_min, cue->curve_lifetime_max, cue->curve_scale_min, cue->curve_scale_max);
        }
    }

    fprintf(f, "\n");
    
    // [text_cues] section
    fprintf(f, "[text_cues]\n");
    fprintf(f, "# text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|curve_x|curve_y|curve_size|curve_color_r|curve_color_g|curve_color_b|bake_mode|baked_asset_key|baked_asset_path|glyph_atlas_key|glyph_atlas_path|glyph_meta_key|glyph_meta_path|animation\n");
    
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
            
            // Encode newlines as literal \\n for CSV compatibility
            char encoded_text[256];
            const char* src = cue->text;
            char* dst = encoded_text;
            while (*src && (dst - encoded_text) < 254) {
                if (*src == '\n') {
                    *dst++ = '\\';
                    *dst++ = 'n';
                } else {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';

            // Bake every text cue as a packed fallback. Dynamic glyph rendering remains preferred,
            // but a machine without the authored font can still display the cue.
            char baked_asset_key[64] = {};
            char baked_asset_path[512] = {};
            char glyph_atlas_key[64] = {};
            char glyph_atlas_path[512] = {};
            char glyph_meta_key[64] = {};
            char glyph_meta_path[512] = {};
            char serialized_animation[4096] = {};
            SerializeTextAnimation(&cue->animation, serialized_animation, sizeof(serialized_animation));
            if (cue->text[0] != '\0') {
                snprintf(baked_asset_key, sizeof(baked_asset_key), "text_s%02d_c%02d.png", scene_idx, cue_idx);

                char baked_abs_path[640] = {};
                snprintf(baked_abs_path, sizeof(baked_abs_path), "%s\\%s", editor->project->assets_path, baked_asset_key);

                if (BakeTextCueToPng(cue, baked_abs_path)) {
                    if (IsFileReadableWithRetry(baked_abs_path, 60, 25)) {
                        snprintf(baked_asset_path, sizeof(baked_asset_path), "%s/%s", rel_assets_prefix, baked_asset_key);
                        strncpy_s(cue->baked_asset_key, sizeof(cue->baked_asset_key), baked_asset_key, _TRUNCATE);
                        strncpy_s(cue->baked_asset_path, sizeof(cue->baked_asset_path), baked_asset_path, _TRUNCATE);
                    } else {
                        printf("[ExportProject] WARNING: baked text exists but is not readable yet: %s\n", baked_abs_path);
                        baked_asset_key[0] = '\0';
                        baked_asset_path[0] = '\0';
                        cue->baked_asset_key[0] = '\0';
                        cue->baked_asset_path[0] = '\0';
                    }
                } else {
                    cue->baked_asset_key[0] = '\0';
                    cue->baked_asset_path[0] = '\0';
                }
            }

            if (cue->text[0] != '\0') {
                snprintf(glyph_atlas_key, sizeof(glyph_atlas_key), "text_glyph_s%02d_c%02d.png", scene_idx, cue_idx);
                snprintf(glyph_meta_key, sizeof(glyph_meta_key), "text_glyph_s%02d_c%02d.txt", scene_idx, cue_idx);
                char atlas_abs_path[640] = {};
                char meta_abs_path[640] = {};
                snprintf(atlas_abs_path, sizeof(atlas_abs_path), "%s\\%s", editor->project->assets_path, glyph_atlas_key);
                snprintf(meta_abs_path, sizeof(meta_abs_path), "%s\\%s", editor->project->assets_path, glyph_meta_key);
                if (rev::runtime::SaveTextGlyphAtlas(cue->font_name, cue->size, atlas_abs_path, meta_abs_path) &&
                    IsFileReadableWithRetry(atlas_abs_path, 60, 25) && IsFileReadableWithRetry(meta_abs_path, 60, 25)) {
                    snprintf(glyph_atlas_path, sizeof(glyph_atlas_path), "%s/%s", rel_assets_prefix, glyph_atlas_key);
                    snprintf(glyph_meta_path, sizeof(glyph_meta_path), "%s/%s", rel_assets_prefix, glyph_meta_key);
                    strncpy_s(cue->glyph_atlas_key, sizeof(cue->glyph_atlas_key), glyph_atlas_key, _TRUNCATE);
                    strncpy_s(cue->glyph_atlas_path, sizeof(cue->glyph_atlas_path), glyph_atlas_path, _TRUNCATE);
                    strncpy_s(cue->glyph_meta_key, sizeof(cue->glyph_meta_key), glyph_meta_key, _TRUNCATE);
                    strncpy_s(cue->glyph_meta_path, sizeof(cue->glyph_meta_path), glyph_meta_path, _TRUNCATE);
                } else {
                    cue->glyph_atlas_key[0] = '\0';
                    cue->glyph_atlas_path[0] = '\0';
                    cue->glyph_meta_key[0] = '\0';
                    cue->glyph_meta_path[0] = '\0';
                }
            }
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s|%s|%s|%s|%s|%s|%.3f|%d|%s\n",
                encoded_text, cue->font_name, cue->x, cue->y, cue->size,
                cue->color.r, cue->color.g, cue->color.b,
                cue->effect_type, abs_start, abs_end,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->layer_order,
                cue->blend_mode,
                cue->curve_x, cue->curve_y, cue->curve_size,
                cue->curve_color_r, cue->curve_color_g, cue->curve_color_b,
                cue->bake_mode,
                baked_asset_key, baked_asset_path,
                glyph_atlas_key, glyph_atlas_path, glyph_meta_key, glyph_meta_path,
                cue->rotation, cue->curve_rotation, serialized_animation
            );
        }
    }
    
    fprintf(f, "\n");

    // [scroll_text_cues] section
    fprintf(f, "[scroll_text_cues]\n");
    fprintf(f, "# text|font_name|x|y|size|color_r|color_g|color_b|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|style_id|direction|speed|spacing|wave_amp|wave_freq|glow|opacity|wrap_gap|slant_deg|jitter_amp|jitter_freq|shadow|outline|curve_x|curve_y|curve_speed|curve_size|curve_opacity|curve_color_r|curve_color_g|curve_color_b|curve_wave_amp|curve_wave_freq|curve_jitter_amp|curve_jitter_freq|loop_mode|chroma_shift|distortion|bake_mode|baked_asset_key|baked_asset_path|glyph_atlas_key|glyph_atlas_path|glyph_meta_key|glyph_meta_path|wave_length|curve_wave_length|rotation|curve_rotation\n");

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }

        for (int cue_idx = 0; cue_idx < scene->scroll_text_cue_count; ++cue_idx) {
            ScrollTextCue* cue = &scene->scroll_text_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            float abs_fade_in_start = scene_start + cue->fade_in_start;
            float abs_fade_in_end = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end = scene_start + cue->fade_out_end;

            char encoded_text[512] = {};
            const char* src = cue->text;
            char* dst = encoded_text;
            while (*src && (dst - encoded_text) < 510) {
                if (*src == '\n') {
                    *dst++ = '\\';
                    *dst++ = 'n';
                } else {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';

            char baked_asset_key[64] = {};
            char baked_asset_path[512] = {};
            // Keep a packed fallback even for dynamic scroll cues when the destination lacks the font.
            if (cue->text[0] != '\0') {
                snprintf(baked_asset_key, sizeof(baked_asset_key), "scroll_s%02d_c%02d.png", scene_idx, cue_idx);

                char baked_abs_path[640] = {};
                snprintf(baked_abs_path, sizeof(baked_abs_path), "%s\\%s", editor->project->assets_path, baked_asset_key);

                if (BakeScrollTextCueToPng(cue, baked_abs_path)) {
                    if (IsFileReadableWithRetry(baked_abs_path, 60, 25)) {
                        snprintf(baked_asset_path, sizeof(baked_asset_path), "%s/%s", rel_assets_prefix, baked_asset_key);
                        strncpy_s(cue->baked_asset_key, sizeof(cue->baked_asset_key), baked_asset_key, _TRUNCATE);
                        strncpy_s(cue->baked_asset_path, sizeof(cue->baked_asset_path), baked_asset_path, _TRUNCATE);
                    } else {
                        printf("[ExportProject] WARNING: baked scroll text exists but is not readable yet: %s\n", baked_abs_path);
                        cue->baked_asset_key[0] = '\0';
                        cue->baked_asset_path[0] = '\0';
                    }
                } else {
                    cue->baked_asset_key[0] = '\0';
                    cue->baked_asset_path[0] = '\0';
                }
            }

            if (baked_asset_key[0] == '\0' && cue->baked_asset_key[0] != '\0') {
                strncpy_s(baked_asset_key, sizeof(baked_asset_key), cue->baked_asset_key, _TRUNCATE);
            }
            if (baked_asset_path[0] == '\0' && cue->baked_asset_path[0] != '\0') {
                strncpy_s(baked_asset_path, sizeof(baked_asset_path), cue->baked_asset_path, _TRUNCATE);
            }

            char glyph_atlas_key[64] = {};
            char glyph_atlas_path[512] = {};
            char glyph_meta_key[64] = {};
            char glyph_meta_path[512] = {};
            if (cue->text[0] != '\0') {
                snprintf(glyph_atlas_key, sizeof(glyph_atlas_key), "scroll_glyph_s%02d_c%02d.png", scene_idx, cue_idx);
                snprintf(glyph_meta_key, sizeof(glyph_meta_key), "scroll_glyph_s%02d_c%02d.txt", scene_idx, cue_idx);
                char atlas_abs_path[640] = {};
                char meta_abs_path[640] = {};
                snprintf(atlas_abs_path, sizeof(atlas_abs_path), "%s\\%s", editor->project->assets_path, glyph_atlas_key);
                snprintf(meta_abs_path, sizeof(meta_abs_path), "%s\\%s", editor->project->assets_path, glyph_meta_key);
                if (rev::runtime::SaveTextGlyphAtlas(cue->font_name, cue->size, atlas_abs_path, meta_abs_path) &&
                    IsFileReadableWithRetry(atlas_abs_path, 60, 25) && IsFileReadableWithRetry(meta_abs_path, 60, 25)) {
                    snprintf(glyph_atlas_path, sizeof(glyph_atlas_path), "%s/%s", rel_assets_prefix, glyph_atlas_key);
                    snprintf(glyph_meta_path, sizeof(glyph_meta_path), "%s/%s", rel_assets_prefix, glyph_meta_key);
                    strncpy_s(cue->glyph_atlas_key, sizeof(cue->glyph_atlas_key), glyph_atlas_key, _TRUNCATE);
                    strncpy_s(cue->glyph_atlas_path, sizeof(cue->glyph_atlas_path), glyph_atlas_path, _TRUNCATE);
                    strncpy_s(cue->glyph_meta_key, sizeof(cue->glyph_meta_key), glyph_meta_key, _TRUNCATE);
                    strncpy_s(cue->glyph_meta_path, sizeof(cue->glyph_meta_path), glyph_meta_path, _TRUNCATE);
                }
            }

            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%.3f|%d|%s|%s|%s|%s|%s|%s|%.3f|%d|%.3f|%d\n",
                encoded_text, cue->font_name,
                cue->x, cue->y, cue->size,
                cue->color.r, cue->color.g, cue->color.b,
                abs_start, abs_end,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->layer_order, cue->blend_mode, cue->style_id, cue->direction,
                cue->speed, cue->spacing, cue->wave_amp, cue->wave_freq,
                cue->glow, cue->opacity, cue->wrap_gap, cue->slant_deg,
                cue->jitter_amp, cue->jitter_freq, cue->shadow, cue->outline,
                cue->curve_x, cue->curve_y, cue->curve_speed, cue->curve_size, cue->curve_opacity,
                cue->curve_color_r, cue->curve_color_g, cue->curve_color_b,
                cue->curve_wave_amp, cue->curve_wave_freq, cue->curve_jitter_amp, cue->curve_jitter_freq,
                cue->loop_mode, cue->chroma_shift, cue->distortion,
                cue->bake_mode,
                baked_asset_key, baked_asset_path,
                glyph_atlas_key, glyph_atlas_path, glyph_meta_key, glyph_meta_path,
                cue->wave_length, cue->curve_wave_length,
                cue->rotation, cue->curve_rotation);
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
    fprintf(f, "# asset_key|asset_path|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|metallic|roughness|curve_pos_x|curve_pos_y|curve_pos_z|curve_rot_x|curve_rot_y|curve_rot_z|curve_scale_x|curve_scale_y|curve_scale_z|curve_color_r|curve_color_g|curve_color_b|curve_color_a|curve_mesh_size|curve_metallic|curve_roughness|fov_deg|cull_mode|curve_fov|use_imported_light|use_imported_camera|emissive_r|emissive_g|emissive_b|emissive_strength\n");

    // Packed runtime resolves mesh assets by key. Ensure keys are unique in export,
    // even when authored cues reused defaults like "mesh_0" in multiple scenes.
    char used_mesh_keys[512][64] = {};
    int used_mesh_key_count = 0;

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

            char export_asset_key[64] = {};
            if (cue->asset_key[0]) {
                strncpy_s(export_asset_key, sizeof(export_asset_key), cue->asset_key, _TRUNCATE);
            } else {
                snprintf(export_asset_key, sizeof(export_asset_key), "mesh_s%02d_c%02d", scene_idx, cue_idx);
            }

            bool duplicate_key = false;
            for (int k = 0; k < used_mesh_key_count; ++k) {
                if (strcmp(used_mesh_keys[k], export_asset_key) == 0) {
                    duplicate_key = true;
                    break;
                }
            }
            if (duplicate_key) {
                snprintf(export_asset_key, sizeof(export_asset_key), "%s_s%02d_c%02d", cue->asset_key[0] ? cue->asset_key : "mesh", scene_idx, cue_idx);
            }

            if (used_mesh_key_count < (int)(sizeof(used_mesh_keys) / sizeof(used_mesh_keys[0]))) {
                strncpy_s(used_mesh_keys[used_mesh_key_count], sizeof(used_mesh_keys[used_mesh_key_count]), export_asset_key, _TRUNCATE);
                used_mesh_key_count++;
            }

            fprintf(f, "%s|%s|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f\n",
                export_asset_key, cue->asset_path, cue->mesh_type,
                cue->pos[0],   cue->pos[1],   cue->pos[2],
                cue->rot[0],   cue->rot[1],   cue->rot[2],
                cue->scale[0], cue->scale[1], cue->scale[2],
                cue->color[0], cue->color[1], cue->color[2], cue->color[3],
                cue->mesh_size, cue->mesh_param,
                abs_start, abs_end, cue->layer_order, cue->effect_type,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->metallic, cue->roughness,
                cue->curve_pos_x, cue->curve_pos_y, cue->curve_pos_z,
                cue->curve_rot_x, cue->curve_rot_y, cue->curve_rot_z,
                cue->curve_scale_x, cue->curve_scale_y, cue->curve_scale_z,
                cue->curve_color_r, cue->curve_color_g, cue->curve_color_b, cue->curve_color_a,
                cue->curve_mesh_size, cue->curve_metallic, cue->curve_roughness,
                cue->fov_deg, cue->cull_mode, cue->curve_fov,
                cue->use_imported_light, cue->use_imported_camera,
                cue->emissive_color[0], cue->emissive_color[1], cue->emissive_color[2], cue->emissive_strength
            );
        }
    }

    fprintf(f, "\n");
    
    // [curves] section
    fprintf(f, "[curves]\n");
    fprintf(f, "# curve_id|wrap_mode|duration|point_count\n");
    
    for (int i = 0; i < editor->project->curve_count; ++i) {
        rev::curve::Curve* curve = &editor->project->curves[i];
        
        const char* wrap_mode_str = "clamp";
        switch (curve->wrap_mode) {
            case rev::curve::WrapMode::Clamp: wrap_mode_str = "clamp"; break;
            case rev::curve::WrapMode::Loop: wrap_mode_str = "loop"; break;
            case rev::curve::WrapMode::PingPong: wrap_mode_str = "pingpong"; break;
            case rev::curve::WrapMode::Mirror: wrap_mode_str = "mirror"; break;
        }
        
        fprintf(f, "%d|%s|%.3f|%d\n", i, wrap_mode_str, curve->duration, curve->point_count);
        
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
            
            fprintf(f, "  %.3f|%.3f|%.3f|%.3f|%s\n",
                pt->t, pt->v, pt->in_ease, pt->out_ease, mode_str
            );
        }
    }
    
    fprintf(f, "\n");
    
    // [metadata] section
    fprintf(f, "[metadata]\n");
    fprintf(f, "total_duration=%.3f\n", editor->project->total_duration);
    fprintf(f, "scene_count=%d\n", editor->project->scene_count);
    fprintf(f, "intro_loop=%d\n", editor->project->loop_intro ? 1 : 0);
    fprintf(f, "music_loop=%d\n", editor->project->loop_music ? 1 : 0);
    fprintf(f, "music_persist=%d\n", editor->project->music_persist_across_scenes ? 1 : 0);
    fprintf(f, "runtime_fullscreen=%d\n", editor->project->runtime_fullscreen ? 1 : 0);
    fprintf(f, "runtime_title=%s\n", editor->project->runtime_title);
    fprintf(f, "audio_gain_enabled=%d\n", editor->project->audio_effects.gain_enabled);
    fprintf(f, "audio_gain_db=%.3f\n", editor->project->audio_effects.gain_db);
    fprintf(f, "audio_compressor_enabled=%d\n", editor->project->audio_effects.compressor_enabled);
    fprintf(f, "audio_compressor_threshold=%.3f\n", editor->project->audio_effects.compressor_threshold);
    fprintf(f, "audio_compressor_ratio=%.3f\n", editor->project->audio_effects.compressor_ratio);
    fprintf(f, "audio_compressor_attack=%.3f\n", editor->project->audio_effects.compressor_attack);
    fprintf(f, "audio_compressor_release=%.3f\n", editor->project->audio_effects.compressor_release);
    fprintf(f, "audio_widener_enabled=%d\n", editor->project->audio_effects.widener_enabled);
    fprintf(f, "audio_widener_amount=%.3f\n", editor->project->audio_effects.widener_amount);
    fprintf(f, "audio_eq_enabled=%d\n", editor->project->audio_effects.eq_enabled);
    fprintf(f, "audio_eq_low_db=%.3f\n", editor->project->audio_effects.eq_low_db);
    fprintf(f, "audio_eq_mid_db=%.3f\n", editor->project->audio_effects.eq_mid_db);
    fprintf(f, "audio_eq_high_db=%.3f\n", editor->project->audio_effects.eq_high_db);
    
    fclose(f);
    return true;
}

bool BuildAndRun(EditorContext* editor) {
    if (!editor) return false;

    SanitizeWindowsSdkEnvironmentForBuild();

    printf("\n=== Build and Run (Both Targets) ===\n");

    // Step 1: compute project-relative cues path
    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    char project_release_dir[512] = {};
    if (EnsureProjectOutputReleaseDir(editor, project_release_dir, sizeof(project_release_dir))) {
        printf("[Build] Project output directory ready: %s\n", project_release_dir);
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

    // Step 3: Build both minimal_intro and minimal_intro_packed
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Building both targets...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 2: Building minimal_intro...\n");
    char build_cmd[768];
    snprintf(build_cmd, sizeof(build_cmd),
             "cmake --build \"%s\\build\" --config Release --target minimal_intro",
             editor->startup_dir);
    int build_result = system(build_cmd);
    if (build_result != 0) {
        printf("ERROR: Build minimal_intro failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Build complete.\n");

    // Step 2b: Regenerate packed_assets.h for this project before packed build.
    // Without this step, minimal_intro_packed can be stale and show default content.
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

    printf("Step 2b: Packing assets for packed build (cache: %s)...\n", pack_cache_path);
    rev::pack::PackResult pack_result = rev::pack::PackAssets(
        cues_path,
        packed_header_path,
        pack_cache_path,
        editor->startup_dir
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
    if (pack_result.optional_skipped > 0) {
        printf("[Pack] Warning: %d optional baked text asset(s) were skipped.\n", pack_result.optional_skipped);
    }

    printf("Step 2c: Building minimal_intro_packed...\n");
    snprintf(build_cmd, sizeof(build_cmd),
             "cmake --build \"%s\\build\" --config Release --target minimal_intro_packed",
             editor->startup_dir);
    build_result = system(build_cmd);
    if (build_result != 0) {
        printf("ERROR: Build minimal_intro_packed failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Pack build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Packed build complete.\n");

    // Step 3: Copy artifacts to project-local bin/Release.
    char project_minimal_intro_path[512] = {};
    char project_minimal_packed_path[512] = {};
    bool copied_intro = CopyBuiltExeToProjectOutput(editor, "minimal_intro.exe",
                                                    project_minimal_intro_path,
                                                    sizeof(project_minimal_intro_path));
    bool copied_packed = CopyBuiltExeToProjectOutput(editor, "minimal_intro_packed.exe",
                                                     project_minimal_packed_path,
                                                     sizeof(project_minimal_packed_path));
    if (!copied_intro || !copied_packed) {
        printf("[Build] Warning: one or more project-local executable copies failed.\n");
    }

    // Step 4: Launch minimal_intro (non-packed version with external cues)
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    printf("Step 3: Launching intro (%s)...\n", cues_path);
    const char* launch_path = copied_intro
        ? project_minimal_intro_path
        : "";
    if (!launch_path[0]) {
        launch_path = nullptr;
    }
    char fallback_launch_path[512] = {};
    if (!launch_path) {
        snprintf(fallback_launch_path, sizeof(fallback_launch_path), "%s\\build\\bin\\Release\\minimal_intro.exe", editor->startup_dir);
        launch_path = fallback_launch_path;
    }
    char run_command[768];
    snprintf(run_command, sizeof(run_command),
             "start \"\" \"%s\" \"%s\"",
             launch_path, cues_path);
    int run_result = system(run_command);
    
    if (run_result == 0) {
        printf("Both intros built successfully! Launching minimal_intro...\n");
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Both built! Intro launched!", _TRUNCATE);
        editor->build_status_timer = 3.0f;
    } else {
        printf("ERROR: Failed to launch intro (exit code %d)\n", run_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Failed to launch intro.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
    }
    
    return (run_result == 0);
}

bool PackProject(EditorContext* editor) {
    if (!editor) return false;

    printf("\n=== Pack Project (No Compile) ===\n");

    const char* project_root = editor->project->workspace_path[0]
        ? editor->project->workspace_path
        : nullptr;
    if (!project_root) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Exporting project...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    if (!ExportProject(editor, cues_path)) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Export failed!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    char pack_cache_path[512] = {};
    snprintf(pack_cache_path, sizeof(pack_cache_path), "%s/pack_cache.txt",
             editor->project->workspace_path[0] ? editor->project->workspace_path
                                                 : editor->startup_dir);
    for (char* p = pack_cache_path; *p; ++p) if (*p == '\\') *p = '/';

    char build_dir[512] = {};
    char packed_header_path[512] = {};
    snprintf(build_dir, sizeof(build_dir), "%s\\build", project_root);
    CreateDirectoryA(build_dir, NULL);
    snprintf(packed_header_path, sizeof(packed_header_path), "%s\\packed_assets.h", build_dir);

    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Packing assets...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    rev::pack::PackResult pack_result = rev::pack::PackAssets(
        cues_path,
        packed_header_path,
        pack_cache_path,
        project_root);

    if (!pack_result.ok) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Pack failed: %s", pack_result.error);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), msg, _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }

    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Project packed. Send the project folder and packed_assets.h.", _TRUNCATE);
    editor->build_status_timer = 10.0f;
    printf("Pack complete: %d total, %d packed, %d skipped.\n",
           pack_result.total, pack_result.packed, pack_result.skipped);
    return true;
}

bool PackBuildAndRun(EditorContext* editor) {
    if (!editor) return false;

    SanitizeWindowsSdkEnvironmentForBuild();

    printf("\n=== Pack, Build and Run ===\n");

    // Step 1: compute project-relative cues path
    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    char project_release_dir[512] = {};
    if (EnsureProjectOutputReleaseDir(editor, project_release_dir, sizeof(project_release_dir))) {
        printf("[Build] Project output directory ready: %s\n", project_release_dir);
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
        editor->startup_dir     // exported cue asset paths are workspace-root-relative
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
    if (pack_result.optional_skipped > 0) {
        printf("[Pack] Warning: %d optional baked text asset(s) were skipped.\n", pack_result.optional_skipped);
    }

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

    // Step 3b: Copy packed artifact to project-local bin/Release.
    char project_packed_path[512] = {};
    bool copied_packed = CopyBuiltExeToProjectOutput(editor, "minimal_intro_packed.exe",
                                                     project_packed_path,
                                                     sizeof(project_packed_path));

    // Step 4: Launch — absolute exe path + cues_path as argv[1]
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching packed intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    printf("Step 4: Launching packed intro (%s)...\n", cues_path);
    const char* launch_path = copied_packed
        ? project_packed_path
        : "";
    if (!launch_path[0]) {
        launch_path = nullptr;
    }
    char fallback_launch_path[512] = {};
    if (!launch_path) {
        snprintf(fallback_launch_path, sizeof(fallback_launch_path), "%s\\build\\bin\\Release\\minimal_intro_packed.exe", editor->startup_dir);
        launch_path = fallback_launch_path;
    }
    char run_command[768];
    snprintf(run_command, sizeof(run_command),
             "start \"\" \"%s\" \"%s\"",
             launch_path, cues_path);
    int run_result = system(run_command);
    if (run_result == 0) {
        char msg[128];
        if (pack_result.optional_skipped > 0) {
            snprintf(msg, sizeof(msg), "Launched with %d baked-text warning(s)", pack_result.optional_skipped);
        } else {
            snprintf(msg, sizeof(msg), "Packed intro launched! (%d assets, %d skipped)",
                     pack_result.total, pack_result.skipped);
        }
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

bool BuildScreenSaver(EditorContext* editor) {
    if (!editor) return false;

    if (!PackBuildAndRun(editor)) return false;

    char release_dir[512] = {};
    if (!EnsureProjectOutputReleaseDir(editor, release_dir, sizeof(release_dir))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                  "Could not locate project output folder.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    char packed_path[512] = {};
    char screen_saver_path[512] = {};
    snprintf(packed_path, sizeof(packed_path), "%s\\minimal_intro_packed.exe", release_dir);
    snprintf(screen_saver_path, sizeof(screen_saver_path), "%s\\minimal_intro.scr", release_dir);
    if (!CopyFileA(packed_path, screen_saver_path, FALSE)) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                  "Screen saver copy failed.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    printf("Screen saver created: %s\n", screen_saver_path);
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
              "Screen saver created (.scr).", _TRUNCATE);
    editor->build_status_timer = 8.0f;
    return true;
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

    scene->animated_sprite_cues = nullptr;
    scene->animated_sprite_cue_count = 0;
    scene->animated_sprite_cue_capacity = 0;

    scene->pixel_cues = nullptr;
    scene->pixel_cue_count = 0;
    scene->pixel_cue_capacity = 0;

    scene->pixel_emitter_cues = nullptr;
    scene->pixel_emitter_cue_count = 0;
    scene->pixel_emitter_cue_capacity = 0;
    
    scene->text_cues = nullptr;
    scene->text_cue_count = 0;
    scene->text_cue_capacity = 0;

    scene->scroll_text_cues = nullptr;
    scene->scroll_text_cue_count = 0;
    scene->scroll_text_cue_capacity = 0;
    
    scene->music_cues = nullptr;
    scene->music_cue_count = 0;
    scene->music_cue_capacity = 0;

    scene->mesh_cues = nullptr;
    scene->mesh_cue_count = 0;
    scene->mesh_cue_capacity = 0;

    scene->post_effects = nullptr;
    scene->post_effect_count = 0;
    scene->post_effect_capacity = 0;
    scene->scene_layer_post_effect_count = 0;
    for (int i = 0; i < rev::runtime::kMaxLayerPostEffects; ++i) {
        scene->scene_layer_post_effects[i] = {};
        scene->scene_layer_post_effects[i].type = -1;
        scene->scene_layer_post_effects[i].end_time = -1.0f;
        scene->scene_layer_post_effects[i].curve_intensity = -1;
        scene->scene_layer_post_effects[i].curve_threshold = -1;
        scene->scene_layer_post_effects[i].curve_radius = -1;
        scene->scene_layer_post_effects[i].curve_color_r = -1;
        scene->scene_layer_post_effects[i].curve_color_g = -1;
        scene->scene_layer_post_effects[i].curve_color_b = -1;
        scene->scene_layer_post_effects[i].curve_color_a = -1;
        scene->scene_layer_post_effects[i].curve_amount = -1;
    }

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
    delete[] scene->animated_sprite_cues;
    delete[] scene->pixel_cues;
    delete[] scene->pixel_emitter_cues;
    delete[] scene->text_cues;
    delete[] scene->scroll_text_cues;
    delete[] scene->music_cues;
    delete[] scene->mesh_cues;
    delete[] scene->post_effects;
    
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

int AddAnimatedSpriteCue(SceneBlock* scene, const AnimatedSpriteCue& cue) {
    if (!scene) return -1;

    if (scene->animated_sprite_cue_count >= scene->animated_sprite_cue_capacity) {
        int new_capacity = scene->animated_sprite_cue_capacity == 0 ? 4 : scene->animated_sprite_cue_capacity * 2;
        AnimatedSpriteCue* new_cues = new AnimatedSpriteCue[new_capacity];

        for (int i = 0; i < scene->animated_sprite_cue_count; ++i) {
            new_cues[i] = scene->animated_sprite_cues[i];
        }

        delete[] scene->animated_sprite_cues;
        scene->animated_sprite_cues = new_cues;
        scene->animated_sprite_cue_capacity = new_capacity;
    }

    int index = scene->animated_sprite_cue_count++;
    scene->animated_sprite_cues[index] = cue;
    return index;
}

int AddPixelCue(SceneBlock* scene, const PixelCue& cue) {
    if (!scene) return -1;

    if (scene->pixel_cue_count >= scene->pixel_cue_capacity) {
        int new_capacity = scene->pixel_cue_capacity == 0 ? 4 : scene->pixel_cue_capacity * 2;
        PixelCue* new_cues = new PixelCue[new_capacity];
        for (int i = 0; i < scene->pixel_cue_count; ++i) {
            new_cues[i] = scene->pixel_cues[i];
        }
        delete[] scene->pixel_cues;
        scene->pixel_cues = new_cues;
        scene->pixel_cue_capacity = new_capacity;
    }

    int index = scene->pixel_cue_count++;
    scene->pixel_cues[index] = cue;
    return index;
}

int AddPixelEmitterCue(SceneBlock* scene, const PixelEmitterCue& cue) {
    if (!scene) return -1;

    if (scene->pixel_emitter_cue_count >= scene->pixel_emitter_cue_capacity) {
        int new_capacity = scene->pixel_emitter_cue_capacity == 0 ? 4 : scene->pixel_emitter_cue_capacity * 2;
        PixelEmitterCue* new_cues = new PixelEmitterCue[new_capacity];
        for (int i = 0; i < scene->pixel_emitter_cue_count; ++i) {
            new_cues[i] = scene->pixel_emitter_cues[i];
        }
        delete[] scene->pixel_emitter_cues;
        scene->pixel_emitter_cues = new_cues;
        scene->pixel_emitter_cue_capacity = new_capacity;
    }

    int index = scene->pixel_emitter_cue_count++;
    scene->pixel_emitter_cues[index] = cue;
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

int AddScrollTextCue(SceneBlock* scene, const ScrollTextCue& cue) {
    if (!scene) return -1;

    if (scene->scroll_text_cue_count >= scene->scroll_text_cue_capacity) {
        int new_capacity = scene->scroll_text_cue_capacity == 0 ? 4 : scene->scroll_text_cue_capacity * 2;
        ScrollTextCue* new_cues = new ScrollTextCue[new_capacity];

        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            new_cues[i] = scene->scroll_text_cues[i];
        }

        delete[] scene->scroll_text_cues;
        scene->scroll_text_cues = new_cues;
        scene->scroll_text_cue_capacity = new_capacity;
    }

    int index = scene->scroll_text_cue_count++;
    scene->scroll_text_cues[index] = cue;
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

void DeleteAnimatedSpriteCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->animated_sprite_cue_count) return;

    for (int i = cue_index; i < scene->animated_sprite_cue_count - 1; ++i) {
        scene->animated_sprite_cues[i] = scene->animated_sprite_cues[i + 1];
    }
    scene->animated_sprite_cue_count--;
}

void DeletePixelCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->pixel_cue_count) return;

    for (int i = cue_index; i < scene->pixel_cue_count - 1; ++i) {
        scene->pixel_cues[i] = scene->pixel_cues[i + 1];
    }
    scene->pixel_cue_count--;
}

void DeletePixelEmitterCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->pixel_emitter_cue_count) return;

    for (int i = cue_index; i < scene->pixel_emitter_cue_count - 1; ++i) {
        scene->pixel_emitter_cues[i] = scene->pixel_emitter_cues[i + 1];
    }
    scene->pixel_emitter_cue_count--;
}

void DeleteTextCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->text_cue_count) return;
    
    for (int i = cue_index; i < scene->text_cue_count - 1; ++i) {
        scene->text_cues[i] = scene->text_cues[i + 1];
    }
    scene->text_cue_count--;
}

void DeleteScrollTextCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->scroll_text_cue_count) return;

    for (int i = cue_index; i < scene->scroll_text_cue_count - 1; ++i) {
        scene->scroll_text_cues[i] = scene->scroll_text_cues[i + 1];
    }
    scene->scroll_text_cue_count--;
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

int AddPostEffect(SceneBlock* scene, const PostEffect& effect) {
    if (!scene) return -1;

    if (scene->post_effect_count >= scene->post_effect_capacity) {
        int new_capacity = scene->post_effect_capacity == 0 ? 4 : scene->post_effect_capacity * 2;
        PostEffect* new_effects = new PostEffect[new_capacity];
        for (int i = 0; i < scene->post_effect_count; ++i) {
            new_effects[i] = scene->post_effects[i];
        }
        delete[] scene->post_effects;
        scene->post_effects = new_effects;
        scene->post_effect_capacity = new_capacity;
    }

    int index = scene->post_effect_count++;
    scene->post_effects[index] = effect;
    return index;
}

void DeletePostEffect(SceneBlock* scene, int effect_index) {
    if (!scene || effect_index < 0 || effect_index >= scene->post_effect_count) return;

    for (int i = effect_index; i < scene->post_effect_count - 1; ++i) {
        scene->post_effects[i] = scene->post_effects[i + 1];
    }
    scene->post_effect_count--;
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
uniform float u_warp;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);

    vec2 base_uv = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    vec2 p = base_uv * 2.0 - 1.0;
    float r2 = dot(p, p);
    vec2 swirl = vec2(-p.y, p.x);
    float warp = clamp(u_warp, 0.0, 1.0);

    // Small common UV warp so all shader presets react to warp, even without explicit u_warp usage.
    base_uv += swirl * (0.03 * warp) * (0.2 + 0.8 * max(0.0, 1.0 - r2));
    uv = base_uv;

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

static const char* post_fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D u_scene;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_enabled[13];
uniform float u_intensity[13];
uniform float u_threshold[13];
uniform float u_radius[13];
uniform vec4 u_color[13];

vec3 SampleScene(vec2 coord) {
    return texture(u_scene, clamp(coord, vec2(0.0), vec2(1.0))).rgb;
}

void main() {
    vec2 coord = uv;
    vec2 texel = 1.0 / u_resolution;
    if (u_enabled[10] != 0) {
        float shake = u_intensity[10] * 0.003;
        coord += vec2(sin(u_time * 31.0), cos(u_time * 37.0)) * shake;
    }

    vec3 color = SampleScene(coord);
    if (u_enabled[2] != 0) {
        vec3 bloom = vec3(0.0);
        float radius = max(u_radius[2], 0.5);
        for (int x = -2; x <= 2; ++x) {
            for (int y = -2; y <= 2; ++y) {
                vec3 sample_color = SampleScene(coord + vec2(x, y) * texel * radius);
                bloom += max(sample_color - vec3(u_threshold[2]), vec3(0.0));
            }
        }
        color += bloom * (u_intensity[2] / 25.0);
    }
    if (u_enabled[9] != 0) {
        float shift = u_intensity[9] * 0.004;
        color.r = SampleScene(coord + vec2(shift, 0.0)).r;
        color.b = SampleScene(coord - vec2(shift, 0.0)).b;
    }
    if (u_enabled[8] != 0) {
        vec3 luma = vec3(0.299, 0.587, 0.114);
        float center = dot(color, luma);
        float edge = 0.0;
        edge += abs(center - dot(SampleScene(coord + vec2(texel.x, 0.0)), luma));
        edge += abs(center - dot(SampleScene(coord + vec2(0.0, texel.y)), luma));
        color = mix(color, vec3(center), clamp(edge * u_intensity[8] * 2.0, 0.0, 0.35));
    }
    if (u_enabled[4] != 0) {
        color = mix(color, color * u_color[4].rgb, clamp(u_intensity[4], 0.0, 1.0));
    }
    if (u_enabled[7] != 0) {
        float fog = 1.0 - exp(-max(uv.y, 0.0) * u_intensity[7] * 3.0);
        color = mix(color, u_color[7].rgb, clamp(fog, 0.0, 1.0));
    }
    if (u_enabled[3] != 0) {
        vec2 centered = uv * 2.0 - 1.0;
        float vignette = smoothstep(1.2, 0.2, dot(centered, centered));
        color *= mix(1.0 - u_intensity[3], 1.0, vignette);
    }
    if (u_enabled[5] != 0 || u_enabled[6] != 0) {
        float noise = fract(sin(dot(uv * u_resolution + u_time, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        float amount = u_intensity[5] * 0.08 + u_intensity[6] * 0.025;
        color += noise * amount;
    }
    if (u_enabled[1] != 0) {
        color = color / (color + vec3(1.0));
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    }
    if (u_enabled[0] != 0) {
        color = color / (color + vec3(1.0));
    }
    if (u_enabled[11] != 0) {
        color += u_color[11].rgb * u_intensity[11] * max(0.0, sin(u_time * 31.416));
    }
    if (u_enabled[12] != 0) {
        color *= 1.0 - clamp(u_intensity[12], 0.0, 1.0);
    }
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
)";

// Sprite vertex shader for image/text rendering
static const char* sprite_vertex_shader = R"(
#version 330 core
out vec2 uv;
uniform vec2 u_position;  // -1 to 1
uniform vec2 u_size;      // width, height in normalized coords
uniform float u_rotation; // degrees around the sprite centre
uniform float u_flip_v;
uniform vec4 u_uv_rect;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 1);
    float y = -1.0 + float((gl_VertexID >> 1) << 1);
    float base_v = (y + 1.0) * 0.5;
    vec2 local_uv = vec2((x + 1.0) * 0.5, mix(base_v, 1.0 - base_v, u_flip_v));
    uv = mix(u_uv_rect.xy, u_uv_rect.zw, local_uv);
    // Use Z = 0.999 to ensure sprites render in front of 3D meshes even if depth state isn't fully reset
    float angle = radians(u_rotation);
    float c = cos(angle);
    float s = sin(angle);
    vec2 rotated = vec2(x * c - y * s, x * s + y * c);
    gl_Position = vec4(u_position + rotated * u_size, 0.999, 1.0);
}
)";

// Sprite fragment shader - textured with opacity
static const char* sprite_fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform float u_opacity;
uniform vec3 u_color_tint;
void main() {
    vec4 texColor = texture(u_texture, uv);
    fragColor = vec4(texColor.rgb * u_color_tint, texColor.a * u_opacity);
}
)";

static bool DrawPreviewGlyphRun(rev::shader::Program* program,
                                const rev::runtime::TextGlyphAtlas* atlas,
                                const char* text, float x, float y, float size_scale,
                                float spacing, float opacity, float r, float g, float b,
                                float viewport_width, float viewport_height,
                                float wave_amp, float wave_freq, float wave_length,
                                float jitter_amp, float jitter_freq, float time, float rotation,
                                bool horizontal_scroll,
                                const rev::runtime::TextAnimationConfig* animation = nullptr,
                                float animation_time = 0.0f) {
    if (!program || !atlas || atlas->texture_id == 0 || !text) return false;
    int u_pos = rev::shader::GetUniformLocation(program, "u_position");
    int u_sz = rev::shader::GetUniformLocation(program, "u_size");
    int u_tex = rev::shader::GetUniformLocation(program, "u_texture");
    int u_opa = rev::shader::GetUniformLocation(program, "u_opacity");
    int u_col = rev::shader::GetUniformLocation(program, "u_color_tint");
    int u_rot = rev::shader::GetUniformLocation(program, "u_rotation");
    int u_uv = rev::shader::GetUniformLocation(program, "u_uv_rect");
    if (u_tex >= 0) rev::shader::SetInt(program, u_tex, 0);
    if (u_opa >= 0) rev::shader::SetFloat(program, u_opa, opacity);
    if (u_col >= 0) rev::shader::SetVec3(program, u_col, r, g, b);
    if (u_rot >= 0) rev::shader::SetFloat(program, u_rot, rotation);
    if (spacing < 0.01f) spacing = 0.01f;
    float line_width = 0.0f;
    for (const unsigned char* p = (const unsigned char*)text; *p && *p != '\n'; ++p) {
        const rev::runtime::TextGlyph* glyph = rev::runtime::FindTextGlyph(atlas, *p);
        if (glyph) line_width += glyph->advance * spacing * size_scale;
    }
    float cursor_x = x - line_width / viewport_width;
    float cursor_y = y;
    bool drew_glyph = false;
    int glyph_index = 0;
    unsigned int character_count = 0;
    unsigned int word_count = 0;
    unsigned int line_count = 1;
    bool in_word = false;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        if (*p == '\n') {
            ++line_count;
            in_word = false;
        } else {
            ++character_count;
            if (*p != ' ' && *p != '\t' && *p != '\r') {
                if (!in_word) ++word_count;
                in_word = true;
            } else {
                in_word = false;
            }
        }
    }
    unsigned int word_index = 0;
    unsigned int line_index = 0;
    unsigned int character_index = 0;
    in_word = false;
    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        if (*p == '\n') {
            cursor_x = x - line_width / viewport_width;
            cursor_y += atlas->line_height * size_scale / viewport_height * 2.0f;
            ++line_index;
            in_word = false;
            continue;
        }
        const rev::runtime::TextGlyph* glyph = rev::runtime::FindTextGlyph(atlas, *p);
        bool whitespace = (*p == ' ' || *p == '\t' || *p == '\r');
        unsigned int current_character_index = character_index++;
        if (!whitespace && !in_word) ++word_index;
        in_word = !whitespace;
        if (!glyph) continue;
        float w = glyph->width * size_scale / viewport_width * 2.0f;
        float h = glyph->height * size_scale / viewport_height * 2.0f;
        float safe_wave_length = wave_length < 2.0f ? 2.0f : wave_length;
        float phase = time * wave_freq * 6.2831853f + (float)glyph_index * 6.2831853f / safe_wave_length;
        float jitter_phase = time * jitter_freq * 6.2831853f + (float)glyph_index * 1.19f;
        float wave = sinf(phase) * wave_amp;
        float jitter_x = sinf(jitter_phase * 1.7f) * jitter_amp;
        float jitter_y = cosf(jitter_phase * 1.3f) * jitter_amp;
        float glyph_x = cursor_x + glyph->width * 0.5f * size_scale / viewport_width * 2.0f;
        float glyph_y = cursor_y;
        if (horizontal_scroll) {
            glyph_x += jitter_x;
            glyph_y += wave + jitter_y;
        } else {
            glyph_x += wave + jitter_x;
            glyph_y += jitter_y;
        }
        rev::runtime::GlyphAnimationState animation_state = {};
        if (animation && animation->version > 0) {
            rev::runtime::TextGlyphTimingInfo timing = {};
            timing.character_index = current_character_index;
            timing.word_index = word_index > 0 ? word_index - 1 : 0;
            timing.line_index = line_index;
            timing.character_count = character_count;
            timing.word_count = word_count;
            timing.line_count = line_count;
            timing.whitespace = whitespace ? 1 : 0;
            rev::runtime::EvaluateTextGlyphAnimation(animation, animation_time, &timing, &animation_state);
            if (!animation_state.visible) {
                cursor_x += glyph->advance * spacing * size_scale / viewport_width * 2.0f;
                ++glyph_index;
                continue;
            }
            glyph_x += animation_state.position_offset_x;
            glyph_y += animation_state.position_offset_y;
            w *= animation_state.scale_x;
            h *= animation_state.scale_y;
            if (u_opa >= 0) rev::shader::SetFloat(program, u_opa, opacity * animation_state.opacity);
            if (u_rot >= 0) rev::shader::SetFloat(program, u_rot, rotation + animation_state.rotation);
        } else {
            if (u_opa >= 0) rev::shader::SetFloat(program, u_opa, opacity);
            if (u_rot >= 0) rev::shader::SetFloat(program, u_rot, rotation);
        }
        if (u_pos >= 0) rev::shader::SetVec2(program, u_pos, glyph_x * 2.0f - 1.0f, -((glyph_y * 2.0f) - 1.0f));
        if (u_sz >= 0) rev::shader::SetVec2(program, u_sz, w, h);
        if (u_uv >= 0) rev::shader::SetVec4(program, u_uv, glyph->u0, glyph->v0, glyph->u1, glyph->v1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        cursor_x += glyph->advance * spacing * size_scale / viewport_width * 2.0f;
        ++glyph_index;
        drew_glyph = true;
    }
    return drew_glyph;
}

static rev::runtime::TextGlyphAtlas* EnsurePreviewAtlas(EditorContext* editor,
                                                        bool scroll, const char* font_name,
                                                        float size) {
    rev::runtime::TextGlyphAtlas* atlas = scroll ? &editor->preview_scroll_atlas : &editor->preview_text_atlas;
    char* cached_font = scroll ? editor->preview_scroll_atlas_font : editor->preview_text_atlas_font;
    float* cached_size = scroll ? &editor->preview_scroll_atlas_size : &editor->preview_text_atlas_size;
    if (atlas->texture_id != 0 && strcmp(cached_font, font_name) == 0 && fabsf(*cached_size - size) < 0.01f) {
        return atlas;
    }
    rev::runtime::DestroyTextGlyphAtlas(atlas);
    if (!rev::runtime::CreateTextGlyphAtlas(font_name, size, atlas)) {
        cached_font[0] = '\0';
        *cached_size = 0.0f;
        return nullptr;
    }
    strncpy_s(cached_font, 64, font_name, _TRUNCATE);
    *cached_size = size;
    return atlas;
}

// Mesh (3D Phong) shaders
static const char* mesh_vertex_shader = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
out vec3 v_frag_pos;
out vec3 v_normal;
out vec2 v_uv;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
void main() {
    vec4 world_pos = u_model * vec4(a_pos, 1.0);
    v_frag_pos = world_pos.xyz;
    v_normal   = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv       = a_uv;
    gl_Position = u_projection * u_view * world_pos;
}
)";

static const char* mesh_fragment_shader = R"(
#version 330 core
in vec3 v_frag_pos;
in vec3 v_normal;
in vec2 v_uv;
out vec4 fragColor;
uniform vec3  u_light_pos;
uniform vec3  u_view_pos;
uniform vec4  u_color;
uniform float u_metallic;
uniform float u_roughness;
uniform vec3  u_emissive_color;
uniform float u_emissive_strength;
uniform sampler2D u_base_color_texture;
uniform int u_has_texture;
void main() {
    vec3  base = u_color.rgb;
    float alpha = u_color.a;
    if (u_has_texture != 0) {
        vec4 tex_color = texture(u_base_color_texture, v_uv);
        base *= tex_color.rgb;
        alpha *= tex_color.a;
    }
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
    vec3  emissive    = u_emissive_color * u_emissive_strength;
    vec3  result      = base * (ambient + diff) + spec + emissive;
    fragColor = vec4(result, alpha);
}
)";

// Shader source is now centralized in shader_presets.cpp
// Use GetShaderSourceById() to fetch shader GLSL code

static rev::shader::Program* g_preview_shader_cache[128] = {};
static rev::shader::Program* g_asset_shader_cache[128] = {};

static rev::shader::Program* GetOrCompilePreviewShaderProgram(int shader_id) {
    if (shader_id < 0 || shader_id >= g_shader_preset_count) return nullptr;
    if (shader_id >= (int)(sizeof(g_preview_shader_cache) / sizeof(g_preview_shader_cache[0]))) return nullptr;

    if (g_preview_shader_cache[shader_id]) {
        return g_preview_shader_cache[shader_id];
    }

    const char* shader_source = GetShaderSourceById(shader_id);
    if (!shader_source) {
        return nullptr;
    }

    rev::shader::Program* prog = rev::shader::CompileFromSource(preview_vertex_shader, shader_source);
    if (!prog) {
        return nullptr;
    }

    g_preview_shader_cache[shader_id] = prog;
    return prog;
}

static rev::shader::Program* GetOrCompileAssetShaderProgram(int shader_id) {
    if (shader_id < 0 || shader_id >= g_shader_preset_count || shader_id >= 128) return nullptr;
    if (g_asset_shader_cache[shader_id]) return g_asset_shader_cache[shader_id];
    const char* source = GetShaderSourceById(shader_id);
    if (!source) return nullptr;
    std::string asset_source(source);
    const size_t output_decl = asset_source.find("out vec4 fragColor;");
    const size_t main_end = asset_source.rfind('}');
    if (output_decl == std::string::npos || main_end == std::string::npos || main_end <= output_decl) return nullptr;
    asset_source.insert(output_decl + strlen("out vec4 fragColor;"),
                        "\nuniform sampler2D u_asset_texture;\nuniform float u_asset_opacity;");
    const size_t adjusted_main_end = asset_source.rfind('}');
    asset_source.insert(adjusted_main_end,
                        "\n    float asset_alpha = texture(u_asset_texture, uv).a * u_asset_opacity;\n"
                        "    fragColor.rgb *= asset_alpha;\n"
                        "    fragColor.a *= asset_alpha;");
    rev::shader::Program* program = rev::shader::CompileFromSource(sprite_vertex_shader, asset_source.c_str());
    if (!program) return nullptr;
    g_asset_shader_cache[shader_id] = program;
    return program;
}

static void RenderAssetShaderOverlays(const AssetShader* shaders, int shader_count,
                                      const ProjectData* project,
                                      float x, float y, float width, float height,
                                      float rotation, float layer_time,
                                      int preview_width, int preview_height,
                                      unsigned int asset_texture_id) {
    if (!shaders || shader_count <= 0) return;
    for (int shader_index = 0; shader_index < shader_count && shader_index < rev::runtime::kMaxAssetShaders; ++shader_index) {
        const AssetShader& asset_shader = shaders[shader_index];
        float end_time = asset_shader.end_time < 0.0f ? 1.0e30f : asset_shader.end_time;
        if (!asset_shader.enabled || layer_time < asset_shader.start_time || layer_time > end_time) continue;
        auto evaluate_curve = [&](int curve_index, float fallback) {
            if (!project || curve_index < 0 || curve_index >= project->curve_count) return fallback;
            const rev::curve::Curve& curve = project->curves[curve_index];
            const float curve_time = curve.duration > 0.0f
                ? (layer_time - asset_shader.start_time) / curve.duration : 0.0f;
            return rev::curve::Evaluate(curve, curve_time);
        };
        float shader_speed = evaluate_curve(asset_shader.curve_speed, asset_shader.speed);
        float shader_intensity = evaluate_curve(asset_shader.curve_intensity, asset_shader.intensity);
        float shader_warp = evaluate_curve(asset_shader.curve_warp, asset_shader.warp);
        float shader_exposure = evaluate_curve(asset_shader.curve_exposure, asset_shader.exposure_base);
        float shader_fade = evaluate_curve(asset_shader.curve_fade, asset_shader.fade_base);
        float shader_opacity = evaluate_curve(asset_shader.curve_opacity, asset_shader.opacity);
        float shader_low_r = evaluate_curve(asset_shader.curve_palette_low_r, asset_shader.palette_low[0]);
        float shader_low_g = evaluate_curve(asset_shader.curve_palette_low_g, asset_shader.palette_low[1]);
        float shader_low_b = evaluate_curve(asset_shader.curve_palette_low_b, asset_shader.palette_low[2]);
        float shader_mid_r = evaluate_curve(asset_shader.curve_palette_mid_r, asset_shader.palette_mid[0]);
        float shader_mid_g = evaluate_curve(asset_shader.curve_palette_mid_g, asset_shader.palette_mid[1]);
        float shader_mid_b = evaluate_curve(asset_shader.curve_palette_mid_b, asset_shader.palette_mid[2]);
        float shader_high_r = evaluate_curve(asset_shader.curve_palette_high_r, asset_shader.palette_high[0]);
        float shader_high_g = evaluate_curve(asset_shader.curve_palette_high_g, asset_shader.palette_high[1]);
        float shader_high_b = evaluate_curve(asset_shader.curve_palette_high_b, asset_shader.palette_high[2]);
        rev::shader::Program* program = GetOrCompileAssetShaderProgram(asset_shader.shader_id);
        if (!program) continue;
        rev::shader::Use(program);
        int u_position = rev::shader::GetUniformLocation(program, "u_position");
        int u_size = rev::shader::GetUniformLocation(program, "u_size");
        int u_rotation = rev::shader::GetUniformLocation(program, "u_rotation");
        int u_flip_v = rev::shader::GetUniformLocation(program, "u_flip_v");
        int u_uv_rect = rev::shader::GetUniformLocation(program, "u_uv_rect");
        int u_time = rev::shader::GetUniformLocation(program, "u_time");
        int u_resolution = rev::shader::GetUniformLocation(program, "u_resolution");
        int u_palette_low = rev::shader::GetUniformLocation(program, "u_palette_low");
        int u_palette_mid = rev::shader::GetUniformLocation(program, "u_palette_mid");
        int u_palette_high = rev::shader::GetUniformLocation(program, "u_palette_high");
        int u_speed = rev::shader::GetUniformLocation(program, "u_speed");
        int u_intensity = rev::shader::GetUniformLocation(program, "u_intensity");
        int u_warp = rev::shader::GetUniformLocation(program, "u_warp");
        int u_exposure_base = rev::shader::GetUniformLocation(program, "u_exposure_base");
        int u_fade_base = rev::shader::GetUniformLocation(program, "u_fade_base");
        int u_asset_texture = rev::shader::GetUniformLocation(program, "u_asset_texture");
        int u_asset_opacity = rev::shader::GetUniformLocation(program, "u_asset_opacity");
        if (u_position >= 0) rev::shader::SetVec2(program, u_position, x, y);
        if (u_size >= 0) rev::shader::SetVec2(program, u_size, width, height);
        if (u_rotation >= 0) rev::shader::SetFloat(program, u_rotation, rotation);
        if (u_flip_v >= 0) rev::shader::SetFloat(program, u_flip_v, 1.0f);
        if (u_uv_rect >= 0) rev::shader::SetVec4(program, u_uv_rect, 0.0f, 0.0f, 1.0f, 1.0f);
        if (u_time >= 0) rev::shader::SetFloat(program, u_time, layer_time);
        if (u_resolution >= 0) rev::shader::SetVec2(program, u_resolution, (float)preview_width, (float)preview_height);
        if (u_palette_low >= 0) rev::shader::SetVec3(program, u_palette_low, shader_low_r, shader_low_g, shader_low_b);
        if (u_palette_mid >= 0) rev::shader::SetVec3(program, u_palette_mid, shader_mid_r, shader_mid_g, shader_mid_b);
        if (u_palette_high >= 0) rev::shader::SetVec3(program, u_palette_high, shader_high_r, shader_high_g, shader_high_b);
        if (u_speed >= 0) rev::shader::SetFloat(program, u_speed, shader_speed);
        if (u_intensity >= 0) rev::shader::SetFloat(program, u_intensity, shader_intensity);
        if (u_warp >= 0) rev::shader::SetFloat(program, u_warp, shader_warp);
        if (u_exposure_base >= 0) rev::shader::SetFloat(program, u_exposure_base, shader_exposure);
        if (u_fade_base >= 0) rev::shader::SetFloat(program, u_fade_base, shader_fade);
        if (u_asset_texture >= 0) rev::shader::SetInt(program, u_asset_texture, 0);
        if (u_asset_opacity >= 0) rev::shader::SetFloat(program, u_asset_opacity, shader_opacity);
        glBindTexture(GL_TEXTURE_2D, asset_texture_id);
        glEnable(GL_BLEND);
        ApplyShaderLayerBlendMode(asset_shader.blend_mode, shader_opacity);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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

    // Separate color target for the fullscreen post-production pass.
    glGenFramebuffers(1, &editor->post_fbo);
    glBindFramebuffer(0x8D40, editor->post_fbo);
    glGenTextures(1, &editor->post_texture);
    glBindTexture(GL_TEXTURE_2D, editor->post_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, editor->post_texture, 0);
    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) {
        CleanupPreview(editor);
        return;
    }

    glGenFramebuffers(1, &editor->post_history_fbo);
    glBindFramebuffer(0x8D40, editor->post_history_fbo);
    glGenTextures(1, &editor->post_history_texture);
    glBindTexture(GL_TEXTURE_2D, editor->post_history_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, editor->post_history_texture, 0);
    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) {
        CleanupPreview(editor);
        return;
    }

    glGenFramebuffers(1, &editor->layer_fbo);
    glBindFramebuffer(0x8D40, editor->layer_fbo);
    glGenTextures(1, &editor->layer_texture);
    glBindTexture(GL_TEXTURE_2D, editor->layer_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, editor->layer_texture, 0);
    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) {
        CleanupPreview(editor);
        return;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x00004000); // GL_COLOR_BUFFER_BIT
    
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
    rev::shader::Use((rev::shader::Program*)editor->sprite_shader);
    rev::shader::SetFloat((rev::shader::Program*)editor->sprite_shader,
                          rev::shader::GetUniformLocation((rev::shader::Program*)editor->sprite_shader, "u_flip_v"), 1.0f);
    rev::shader::SetVec4((rev::shader::Program*)editor->sprite_shader,
                         rev::shader::GetUniformLocation((rev::shader::Program*)editor->sprite_shader, "u_uv_rect"),
                         0.0f, 0.0f, 1.0f, 1.0f);

    editor->post_shader = rev::shader::CompileFromSource(preview_vertex_shader, GetPostEffectFragmentSource());
    if (!editor->post_shader) {
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

    rev::runtime::DestroyTextGlyphAtlas(&editor->preview_text_atlas);
    rev::runtime::DestroyTextGlyphAtlas(&editor->preview_scroll_atlas);
    editor->preview_text_atlas_font[0] = '\0';
    editor->preview_scroll_atlas_font[0] = '\0';
    
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
    if (editor->post_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->post_shader);
        editor->post_shader = nullptr;
    }
    editor->preview_current_shader_id = -1;
    
    if (editor->preview_texture) {
        glDeleteTextures(1, &editor->preview_texture);
        editor->preview_texture = 0;
    }

    if (editor->post_texture) {
        glDeleteTextures(1, &editor->post_texture);
        editor->post_texture = 0;
    }
    if (editor->post_history_texture) {
        glDeleteTextures(1, &editor->post_history_texture);
        editor->post_history_texture = 0;
    }
    if (editor->layer_texture) {
        glDeleteTextures(1, &editor->layer_texture);
        editor->layer_texture = 0;
    }
    
    if (editor->preview_depth && glDeleteRenderbuffers) {
        glDeleteRenderbuffers(1, &editor->preview_depth);
        editor->preview_depth = 0;
    }
    
    if (editor->preview_fbo && glDeleteFramebuffers) {
        glDeleteFramebuffers(1, &editor->preview_fbo);
        editor->preview_fbo = 0;
    }
    if (editor->post_fbo && glDeleteFramebuffers) {
        glDeleteFramebuffers(1, &editor->post_fbo);
        editor->post_fbo = 0;
    }
    if (editor->post_history_fbo && glDeleteFramebuffers) {
        glDeleteFramebuffers(1, &editor->post_history_fbo);
        editor->post_history_fbo = 0;
    }
    if (editor->layer_fbo && glDeleteFramebuffers) {
        glDeleteFramebuffers(1, &editor->layer_fbo);
        editor->layer_fbo = 0;
    }

    if (editor->preview_vao) {
        typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(int n, const unsigned int* arrays);
        auto glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
        if (glDeleteVertexArrays) glDeleteVertexArrays(1, &editor->preview_vao);
        editor->preview_vao = 0;
    }

    for (int i = 0; i < (int)(sizeof(g_preview_shader_cache) / sizeof(g_preview_shader_cache[0])); ++i) {
        if (g_preview_shader_cache[i]) {
            rev::shader::DestroyProgram(g_preview_shader_cache[i]);
            g_preview_shader_cache[i] = nullptr;
        }
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
    if (!editor || !editor->preview_initialized || !editor->project ||
        editor->project->scene_count <= 0) return;

    editor->post_frame_rendered = false;

    typedef void (*PFNGLACTIVETEXTUREPROC)(unsigned int texture);
    PFNGLACTIVETEXTUREPROC glActiveTexture_preview =
        (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
    
    typedef void (*PFNGLBINDFRAMEBUFFERPROC)(unsigned int target, unsigned int framebuffer);
    typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
    auto glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    auto glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    
    if (!glBindFramebuffer) return;
    
    // Bind preview framebuffer
    glBindFramebuffer(0x8D40, editor->preview_fbo); // GL_FRAMEBUFFER
    if (glFramebufferTexture2D) {
        glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, editor->preview_texture, 0);
    }

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
    
    // Disable depth test for background shader (fullscreen quad at infinite depth)
    glDisable(0x0B71);  // GL_DEPTH_TEST
    typedef void (*PFNGLDEPTHMASKPROC)(unsigned char flag);
    auto glDepthMask_fn = (PFNGLDEPTHMASKPROC)wglGetProcAddress("glDepthMask");
    if (glDepthMask_fn) glDepthMask_fn(0);  // GL_FALSE
    
    // Render shader layers first (supports stacked shader compositing with blend and order).
    if (editor->project) {
        struct ActiveShaderLayer {
            ShaderCue* cue;
            int layer_order;
            int layer_role;
            float absolute_start;
            float absolute_end;
        };

        ActiveShaderLayer layers[256];
        int layer_count = 0;

        float scene_start_time = 0.0f;
        for (int s = 0; s < editor->project->scene_count && layer_count < 256; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            bool is_last_scene = (s == editor->project->scene_count - 1);

            for (int i = 0; i < scene->shader_cue_count && layer_count < 256; i++) {
                ShaderCue* cue = &scene->shader_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = scene_start_time + cue->cue_start;
                float absolute_end = scene_start_time + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range) {
                    layers[layer_count++] = { cue, cue->layer_order, cue->layer_role, absolute_start, absolute_end };
                }
            }

            scene_start_time += scene->duration;
        }

        for (int i = 0; i < layer_count - 1; i++) {
            for (int j = 0; j < layer_count - i - 1; j++) {
                const ActiveShaderLayer& a = layers[j];
                const ActiveShaderLayer& b = layers[j + 1];
                if ((a.layer_order > b.layer_order) ||
                    (a.layer_order == b.layer_order && a.layer_role > b.layer_role)) {
                    ActiveShaderLayer tmp = layers[j];
                    layers[j] = layers[j + 1];
                    layers[j + 1] = tmp;
                }
            }
        }

        for (int li = 0; li < layer_count; ++li) {
            ShaderCue* cue = layers[li].cue;
            if (!cue) continue;

            rev::shader::Program* prog = GetOrCompilePreviewShaderProgram(cue->shader_scene_id);
            if (!prog) continue;

            float speed = cue->speed;
            float intensity = cue->intensity;
            float warp = cue->warp;
            float exposure_base = cue->exposure_base;
            float exposure_ramp = cue->exposure_ramp;
            float fade_base = cue->fade_base;
            float fade_ramp = cue->fade_ramp;
            float opacity = cue->opacity;
            float position[3] = {cue->position_x, cue->position_y, cue->position_z};
            float rotation[3] = {cue->rotation_x, cue->rotation_y, cue->rotation_z};
            float motion[3] = {cue->motion_x, cue->motion_y, cue->motion_z};
            float palette_low[3] = { cue->palette_low.r, cue->palette_low.g, cue->palette_low.b };
            float palette_mid[3] = { cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b };
            float palette_high[3] = { cue->palette_high.r, cue->palette_high.g, cue->palette_high.b };

            float elapsed_time = editor->current_time - layers[li].absolute_start;
            float cue_duration = layers[li].absolute_end - layers[li].absolute_start;
            float local_time = elapsed_time;
            if (local_time < 0.0f) local_time = 0.0f;
            if (cue_duration > 0.0f && local_time > cue_duration) local_time = cue_duration;
            elapsed_time = local_time;

            if (elapsed_time >= 0.0f) {
                if (cue->curve_palette_low_r >= 0 && cue->curve_palette_low_r < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_low_r].duration;
                    palette_low[0] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_low_r], t);
                }
                if (cue->curve_palette_low_g >= 0 && cue->curve_palette_low_g < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_low_g].duration;
                    palette_low[1] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_low_g], t);
                }
                if (cue->curve_palette_low_b >= 0 && cue->curve_palette_low_b < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_low_b].duration;
                    palette_low[2] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_low_b], t);
                }
                if (cue->curve_palette_mid_r >= 0 && cue->curve_palette_mid_r < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_mid_r].duration;
                    palette_mid[0] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_mid_r], t);
                }
                if (cue->curve_palette_mid_g >= 0 && cue->curve_palette_mid_g < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_mid_g].duration;
                    palette_mid[1] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_mid_g], t);
                }
                if (cue->curve_palette_mid_b >= 0 && cue->curve_palette_mid_b < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_mid_b].duration;
                    palette_mid[2] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_mid_b], t);
                }
                if (cue->curve_palette_high_r >= 0 && cue->curve_palette_high_r < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_high_r].duration;
                    palette_high[0] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_high_r], t);
                }
                if (cue->curve_palette_high_g >= 0 && cue->curve_palette_high_g < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_high_g].duration;
                    palette_high[1] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_high_g], t);
                }
                if (cue->curve_palette_high_b >= 0 && cue->curve_palette_high_b < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_high_b].duration;
                    palette_high[2] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_high_b], t);
                }
                if (cue->curve_speed >= 0 && cue->curve_speed < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_speed].duration;
                    speed = rev::curve::Evaluate(editor->project->curves[cue->curve_speed], t);
                }
                if (cue->curve_intensity >= 0 && cue->curve_intensity < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_intensity].duration;
                    intensity = rev::curve::Evaluate(editor->project->curves[cue->curve_intensity], t);
                }
                if (cue->curve_warp >= 0 && cue->curve_warp < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_warp].duration;
                    warp = rev::curve::Evaluate(editor->project->curves[cue->curve_warp], t);
                }
                if (cue->curve_exposure >= 0 && cue->curve_exposure < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_exposure].duration;
                    exposure_base = rev::curve::Evaluate(editor->project->curves[cue->curve_exposure], t);
                }
                if (cue->curve_fade >= 0 && cue->curve_fade < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_fade].duration;
                    fade_base = rev::curve::Evaluate(editor->project->curves[cue->curve_fade], t);
                }
                if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_opacity].duration;
                    opacity = rev::curve::Evaluate(editor->project->curves[cue->curve_opacity], t);
                }
                if (cue->curve_exposure_ramp >= 0 && cue->curve_exposure_ramp < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_exposure_ramp].duration;
                    exposure_ramp = rev::curve::Evaluate(editor->project->curves[cue->curve_exposure_ramp], t);
                }
                if (cue->curve_fade_ramp >= 0 && cue->curve_fade_ramp < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_fade_ramp].duration;
                    fade_ramp = rev::curve::Evaluate(editor->project->curves[cue->curve_fade_ramp], t);
                }
            }

            float envelope = ComputeShaderCueEnvelope(local_time, cue_duration, cue->fade_in, cue->fade_out);
            float exposure = exposure_base + exposure_ramp * local_time;
            float fade = (fade_base + fade_ramp * local_time) * envelope;
            if (exposure < 0.0f) exposure = 0.0f;
            if (fade < 0.0f) fade = 0.0f;

            if (li == 0) {
                glDisable(GL_BLEND);
            } else {
                glEnable(GL_BLEND);
                ApplyShaderLayerBlendMode(cue->blend_mode, opacity);
            }

            // Fold layer opacity into fade for shaders that use fade as alpha/intensity.
            fade *= opacity;

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
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_exposure_base"), exposure);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_fade_base"), fade);
            rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_position"),
                                 position[0], position[1], position[2]);
            rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_rotation"),
                                 rotation[0], rotation[1], rotation[2]);
            rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_motion"),
                                 motion[0], motion[1], motion[2]);
            rev::shader::SetInt(prog, rev::shader::GetUniformLocation(prog, "u_noise_enabled"), cue->noise.enabled);
            rev::shader::SetInt(prog, rev::shader::GetUniformLocation(prog, "u_noise_type"), cue->noise.type);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_scale"), cue->noise.scale);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_strength"), cue->noise.strength);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_octaves"), cue->noise.octaves);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_lacunarity"), cue->noise.lacunarity);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_gain"), cue->noise.gain);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_warp"), cue->noise.warp);
            rev::shader::SetVec2(prog, rev::shader::GetUniformLocation(prog, "u_noise_speed"), cue->noise.speed_x, cue->noise.speed_y);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_seed"), cue->noise.seed);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_noise_contrast"), cue->noise.contrast);

            if (glActiveTexture_preview) {
                int noise_map_count = 0;
                for (int map_index = 0; map_index < 4; ++map_index) {
                    unsigned int texture_id = GetEditorNoiseTexture(editor, cue->noise_textures.paths[map_index]);
                    if (texture_id != 0) ++noise_map_count;
                    glActiveTexture_preview(0x84C4u + (unsigned int)map_index);
                    glBindTexture(GL_TEXTURE_2D, texture_id);
                    char uniform_name[32] = {};
                    snprintf(uniform_name, sizeof(uniform_name), "u_noise_map_%d", map_index);
                    rev::shader::SetTextureUnit(prog,
                        rev::shader::GetUniformLocation(prog, uniform_name), 4 + map_index);
                }
                rev::shader::SetInt(prog,
                    rev::shader::GetUniformLocation(prog, "u_noise_map_count"), noise_map_count);
                glActiveTexture_preview(0x84C0u);
            }

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glDisable(GL_BLEND);
    }

    // --- Unified layered draw pass ---
    // Collect all active image/text/scroll/mesh cues across all scenes, sort by
    // layer_order (ascending = drawn first = further back), draw in order.
    if (editor->project && (editor->sprite_shader || editor->mesh_shader)) {
        auto* sprite_prog = editor->sprite_shader ? (rev::shader::Program*)editor->sprite_shader : nullptr;
        auto* mesh_prog   = editor->mesh_shader   ? (rev::shader::Program*)editor->mesh_shader   : nullptr;

        // Pre-load GL extension procs
        typedef void (*PFNGLACTIVETEXTUREPROC)(unsigned int texture);
        auto glActiveTexture_fn = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
        auto glUniformMatrix4fv = (void(*)(int,int,unsigned char,const float*))wglGetProcAddress("glUniformMatrix4fv");
        typedef void (*PFNGLDEPTHFUNCPROC)(unsigned int func);
        auto glDepthFunc_fn = (PFNGLDEPTHFUNCPROC)wglGetProcAddress("glDepthFunc");
        typedef void (*PFNGLUNIFORM4FVPROC)(int, int, const float*);
        auto glUniform4fv_fn = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
        typedef void (*PFNGLDEPTHMASKPROC)(unsigned char flag);
        auto glDepthMask_mesh_fn = (PFNGLDEPTHMASKPROC)wglGetProcAddress("glDepthMask");

        // Pre-compute 3D camera (reused for every mesh item)
        float mesh_aspect = (editor->preview_height > 0)
            ? (float)editor->preview_width / (float)editor->preview_height : 1.0f;
        float eye[3]       = {0.0f, 0.0f, 5.0f};
        float center3[3]   = {0.0f, 0.0f, 0.0f};
        float up3[3]       = {0.0f, 1.0f, 0.0f};
        float light_pos[3] = {3.0f, 5.0f, 4.0f};
        float view_mat[16];
        rev::runtime::Mat4LookAt(view_mat, eye, center3, up3);

        // Upload view/proj/lighting once if we have a mesh shader
        int mp_model = -1, mp_view = -1, mp_proj = -1, mp_light = -1,
            mp_vpos  = -1, mp_col  = -1, mp_metal = -1, mp_rough = -1,
            mp_emissive_color = -1, mp_emissive_strength = -1;
        if (mesh_prog) {
            mp_model = rev::shader::GetUniformLocation(mesh_prog, "u_model");
            mp_view  = rev::shader::GetUniformLocation(mesh_prog, "u_view");
            mp_proj  = rev::shader::GetUniformLocation(mesh_prog, "u_projection");
            mp_light = rev::shader::GetUniformLocation(mesh_prog, "u_light_pos");
            mp_vpos  = rev::shader::GetUniformLocation(mesh_prog, "u_view_pos");
            mp_col   = rev::shader::GetUniformLocation(mesh_prog, "u_color");
            mp_metal = rev::shader::GetUniformLocation(mesh_prog, "u_metallic");
            mp_rough = rev::shader::GetUniformLocation(mesh_prog, "u_roughness");
            mp_emissive_color = rev::shader::GetUniformLocation(mesh_prog, "u_emissive_color");
            mp_emissive_strength = rev::shader::GetUniformLocation(mesh_prog, "u_emissive_strength");
            rev::shader::Use(mesh_prog);
            if (glUniformMatrix4fv) {
                glUniformMatrix4fv(mp_view, 1, 0, view_mat);
            }
            rev::shader::SetVec3(mesh_prog, mp_light, light_pos[0], light_pos[1], light_pos[2]);
            rev::shader::SetVec3(mesh_prog, mp_vpos, eye[0], eye[1], eye[2]);
        }

        // Sprite shader uniform locations
        int sp_tex = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_texture")  : -1;
        int sp_pos = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_position") : -1;
        int sp_sz  = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_size")     : -1;
        int sp_rot = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_rotation") : -1;
        int sp_opa = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_opacity")  : -1;
        int sp_col = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_color_tint") : -1;

        // Build unified draw list: type 0=image 1=text 2=mesh 3=scroll text 4=animated sprite 5=pixel 6=emitter
        struct DrawItem { int type; void* cue; int layer_order; float scene_start_time; };
        static const int kMaxItems = 512;
        DrawItem items[kMaxItems];
        int item_count = 0;

        // Calculate scene start times for absolute time comparison
        float item_scene_start = 0.0f;
        for (int s = 0; s < editor->project->scene_count && item_count < kMaxItems; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            bool is_last_scene = (s == editor->project->scene_count - 1);
            
            for (int i = 0; i < scene->image_cue_count && item_count < kMaxItems; i++) {
                ImageCue* cue = &scene->image_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range)
                    items[item_count++] = { 0, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->animated_sprite_cue_count && item_count < kMaxItems; i++) {
                AnimatedSpriteCue* cue = &scene->animated_sprite_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range && cue->frame_keys_csv[0])
                    items[item_count++] = { 4, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->pixel_cue_count && item_count < kMaxItems; i++) {
                PixelCue* cue = &scene->pixel_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range && cue->asset_key[0])
                    items[item_count++] = { 5, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->pixel_emitter_cue_count && item_count < kMaxItems; i++) {
                PixelEmitterCue* cue = &scene->pixel_emitter_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range)
                    items[item_count++] = { 6, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->text_cue_count && item_count < kMaxItems; i++) {
                TextCue* cue = &scene->text_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range && cue->text[0])
                    items[item_count++] = { 1, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->scroll_text_cue_count && item_count < kMaxItems; i++) {
                ScrollTextCue* cue = &scene->scroll_text_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range && cue->text[0])
                    items[item_count++] = { 3, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->mesh_cue_count && item_count < kMaxItems; i++) {
                MeshCue* cue = &scene->mesh_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range)
                    items[item_count++] = { 2, cue, cue->layer_order, item_scene_start };
            }
            
            // Advance scene start time for next scene
            item_scene_start += scene->duration;
        }

        // Stable bubble sort by layer_order ascending
        // Tie-break rule for same layer: mesh behind image/sprite behind text/scroll.
        auto draw_priority = [](int type) {
            // type: 0=image, 1=text, 2=mesh, 3=scroll text, 4=animated sprite, 5=pixel, 6=emitter
            if (type == 2) return 0; // mesh first (back)
            if (type == 0 || type == 4 || type == 5 || type == 6) return 1; // image middle
            return 2;                // text/scroll front
        };
        for (int i = 0; i < item_count - 1; i++)
            for (int j = 0; j < item_count - i - 1; j++)
                if (items[j].layer_order > items[j+1].layer_order ||
                    (items[j].layer_order == items[j+1].layer_order &&
                     draw_priority(items[j].type) > draw_priority(items[j+1].type))) {
                    DrawItem tmp = items[j]; items[j] = items[j+1]; items[j+1] = tmp;
                }

        bool blend_on = false, depth_on = false;

        for (int idx = 0; idx < item_count; idx++) {
            DrawItem& item = items[idx];
            glDisable(GL_CULL_FACE);

            if (item.type == 0 || item.type == 1 || item.type == 3 || item.type == 4 || item.type == 5 || item.type == 6) {
                // Sprite (image, text, scroll text, animated sprite)
                if (!sprite_prog) continue;
                
                // When switching from mesh to sprite, rebind dummy VAO for gl_VertexID rendering
                if (depth_on && editor->preview_vao) {
                    typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
                    auto glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
                    if (glBindVertexArray) glBindVertexArray(editor->preview_vao);
                }
                
                // Don't clear depth here - already cleared once before layer pass
                
                // Disable depth testing for 2D sprites
                glDisable(0x0B71);  // GL_DEPTH_TEST
                if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(0);  // GL_FALSE
                depth_on = false;
                
                // Enable blending for sprites
                glEnable(GL_BLEND);
                blend_on = true;

                int sprite_blend_mode = 0;
                if (item.type == 0) {
                    sprite_blend_mode = ((ImageCue*)item.cue)->blend_mode;
                } else if (item.type == 4) {
                    sprite_blend_mode = ((AnimatedSpriteCue*)item.cue)->blend_mode;
                } else if (item.type == 1) {
                    sprite_blend_mode = ((TextCue*)item.cue)->blend_mode;
                } else if (item.type == 5) {
                    sprite_blend_mode = ((PixelCue*)item.cue)->blend_mode;
                } else if (item.type == 6) {
                    sprite_blend_mode = ((PixelEmitterCue*)item.cue)->blend_mode;
                } else {
                    sprite_blend_mode = ((ScrollTextCue*)item.cue)->blend_mode;
                }
                ApplySpriteBlendMode(sprite_blend_mode);
                
                rev::shader::Use(sprite_prog);
                int sprite_uv = rev::shader::GetUniformLocation(sprite_prog, "u_uv_rect");
                if (sprite_uv >= 0) rev::shader::SetVec4(sprite_prog, sprite_uv, 0.0f, 0.0f, 1.0f, 1.0f);

                if (item.type == 6) {
                    PixelEmitterCue* cue = (PixelEmitterCue*)item.cue;
                    float elapsed_time = editor->current_time - (item.scene_start_time + cue->cue_start);
                    if (elapsed_time < 0.0f) continue;

                    rev::particles::EmitterSettings settings = {};
                    auto EvaluateEmitterCurve = [&](int curve_index, float fallback) {
                        if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                        const rev::curve::Curve& curve = editor->project->curves[curve_index];
                        float curve_time = curve.duration > 0.0f ? elapsed_time / curve.duration : 0.0f;
                        return rev::curve::Evaluate(curve, curve_time);
                    };
                    float emitter_x = EvaluateEmitterCurve(cue->curve_x, cue->x);
                    float emitter_y = EvaluateEmitterCurve(cue->curve_y, cue->y);
                    float emitter_scale = EvaluateEmitterCurve(cue->curve_scale, cue->scale);
                    float emitter_rotation = EvaluateEmitterCurve(cue->curve_rotation, cue->rotation);
                    float emitter_opacity = EvaluateEmitterCurve(cue->curve_opacity, cue->opacity);
                    float emission_rate = EvaluateEmitterCurve(cue->curve_emission_rate, cue->emission_rate);
                    float speed_min = EvaluateEmitterCurve(cue->curve_speed_min, cue->speed_min);
                    float speed_max = EvaluateEmitterCurve(cue->curve_speed_max, cue->speed_max);
                    float lifetime_min = EvaluateEmitterCurve(cue->curve_lifetime_min, cue->lifetime_min);
                    float lifetime_max = EvaluateEmitterCurve(cue->curve_lifetime_max, cue->lifetime_max);
                    float scale_min = EvaluateEmitterCurve(cue->curve_scale_min, cue->scale_min);
                    float scale_max = EvaluateEmitterCurve(cue->curve_scale_max, cue->scale_max);
                    settings.seed = cue->seed;
                    settings.visual_source = cue->visual_source == 0
                        ? rev::particles::VisualSourceAsset : rev::particles::VisualSourcePrimitive;
                    settings.primitive_shape = (rev::particles::PrimitiveShape)cue->primitive_shape;
                    settings.max_particles = cue->max_particles > 0 ? cue->max_particles : 1;
                    settings.emission_rate = emission_rate;
                    settings.burst_count = cue->burst_count;
                    settings.duration = cue->duration;
                    settings.loop = cue->loop != 0;
                    settings.start_delay = cue->start_delay;
                    settings.simulation_space = (rev::particles::SimulationSpace)cue->simulation_space;
                    settings.position = {0.0f, 0.0f, 0.0f};
                    settings.direction = {cue->direction_x, cue->direction_y, 0.0f};
                    settings.cone_angle_degrees = cue->cone_angle_degrees;
                    settings.speed = {speed_min, speed_max};
                    settings.lifetime = {lifetime_min, lifetime_max};
                    settings.scale = {scale_min, scale_max};
                    settings.rotation = {cue->rotation_min, cue->rotation_max};
                    settings.angular_velocity = {cue->angular_velocity_min, cue->angular_velocity_max};
                    settings.animation_speed = {0.0f, 0.0f};
                    settings.acceleration = {cue->acceleration_x, cue->acceleration_y, 0.0f};
                    settings.drag = cue->drag;

                    int capacity = settings.max_particles > 256 ? 256 : settings.max_particles;
                    rev::particles::Particle storage[256] = {};
                    rev::particles::ParticleSystem system = {};
                    if (!rev::particles::Initialize(&system, storage, capacity, settings)) continue;
                    const float step = 1.0f / 120.0f;
                    int steps = (int)floorf(elapsed_time / step);
                    for (int step_index = 0; step_index < steps; ++step_index)
                        rev::particles::Update(&system, step);
                    float remainder = elapsed_time - (float)steps * step;
                    if (remainder > 0.0f) rev::particles::Update(&system, remainder);

                    unsigned int emitter_tex = 0;
                    int emitter_width = 1;
                    int emitter_height = 1;
                    rev::runtime::ImageTexture emitter_image = {};
                    if (cue->visual_source == rev::particles::VisualSourceAsset) {
                        char emitter_path[512] = {};
                        if (ResolveEditorPixelEmitterPath(editor->project, cue,
                                                           emitter_path, sizeof(emitter_path))) {
                            if (rev::runtime::LoadImageTexture(emitter_path, &emitter_image)) {
                                emitter_tex = emitter_image.texture_id;
                                emitter_width = emitter_image.width;
                                emitter_height = emitter_image.height;
                            }
                        }
                    } else {
                        UploadPrimitiveEmitterTexture(cue, &emitter_tex);
                        emitter_width = 16;
                        emitter_height = 16;
                    }
                    if (!emitter_tex) {
                        continue;
                    }
                    if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                    glBindTexture(GL_TEXTURE_2D, emitter_tex);
                    if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                    if (sp_col >= 0) rev::shader::SetVec3(sprite_prog, sp_col,
                        cue->visual_source == rev::particles::VisualSourceAsset ? 1.0f : cue->primitive_color[0],
                        cue->visual_source == rev::particles::VisualSourceAsset ? 1.0f : cue->primitive_color[1],
                        cue->visual_source == rev::particles::VisualSourceAsset ? 1.0f : cue->primitive_color[2]);
                    const rev::particles::Particle* particles = rev::particles::GetParticles(&system);
                    for (int particle_index = 0; particle_index < capacity; ++particle_index) {
                        const rev::particles::Particle& particle = particles[particle_index];
                        if (!particle.active) continue;
                        float particle_x = emitter_x + particle.position.x;
                        float particle_y = emitter_y + particle.position.y;
                        float particle_size = emitter_scale * particle.scale;
                        float particle_opacity = emitter_opacity;
                        if (particle.lifetime > 0.0f) {
                            float life = particle.age / particle.lifetime;
                            if (life > 0.8f) particle_opacity *= (1.0f - life) * 5.0f;
                        }
                        if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos,
                            particle_x * 2.0f - 1.0f, -((particle_y * 2.0f) - 1.0f));
                        if (sp_sz >= 0) rev::shader::SetVec2(sprite_prog, sp_sz,
                            particle_size * emitter_width * 2.0f / editor->preview_width,
                            particle_size * emitter_height * 2.0f / editor->preview_height);
                        if (sp_rot >= 0) rev::shader::SetFloat(sprite_prog, sp_rot,
                            emitter_rotation + particle.rotation);
                        if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, particle_opacity);
                        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    }
                    glDeleteTextures(1, &emitter_tex);
                    if (sp_col >= 0) rev::shader::SetVec3(sprite_prog, sp_col, 1.0f, 1.0f, 1.0f);
                    continue;
                }

                unsigned int tex = 0;
                float norm_w = 0, norm_h = 0, pos_x = 0, pos_y = 0, rotation = 0.0f, opacity = 1.0f;

                if (item.type == 0) {
                    ImageCue* cue = (ImageCue*)item.cue;
                    
                    // Evaluate curves for animation
                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_scale = cue->scale;
                    float anim_rotation = cue->rotation;
                    float anim_opacity = cue->opacity;
                    
                    // Calculate elapsed time from cue start (convert scene-relative to absolute)
                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time >= 0.0f) {
                        if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                            float t = elapsed_time / curve->duration;
                            anim_x = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                            float t = elapsed_time / curve->duration;
                            anim_y = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_scale >= 0 && cue->curve_scale < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale];
                            float t = elapsed_time / curve->duration;
                            anim_scale = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_rotation >= 0 && cue->curve_rotation < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_rotation];
                            float t = elapsed_time / curve->duration;
                            anim_rotation = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_opacity];
                            float t = elapsed_time / curve->duration;
                            anim_opacity = rev::curve::Evaluate(*curve, t);
                        }
                    }
                    
                    char full_path[512];
                    snprintf(full_path, sizeof(full_path), "%s\\%s",
                             editor->project->assets_path, cue->asset_key);
                    rev::runtime::ImageTexture rt_img{};
                    if (!rev::runtime::LoadImageTexture(full_path, &rt_img)) continue;
                    tex    = rt_img.texture_id;
                    norm_w = (rt_img.width  * anim_scale) / editor->preview_width  * 2.0f;
                    norm_h = (rt_img.height * anim_scale) / editor->preview_height * 2.0f;
                    pos_x  =  (anim_x * 2.0f) - 1.0f;
                    pos_y  = -((anim_y * 2.0f) - 1.0f);
                    rotation = anim_rotation;
                    opacity = anim_opacity * rev::runtime::ComputeEffectOpacity(
                        cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end, editor->current_time - item.scene_start_time);
                } else if (item.type == 4) {
                    AnimatedSpriteCue* cue = (AnimatedSpriteCue*)item.cue;

                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_scale = cue->scale;
                    float anim_rotation = cue->rotation;
                    float anim_opacity = cue->opacity;
                    float anim_frame = (float)cue->start_frame;

                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time >= 0.0f) {
                        if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                            float t = elapsed_time / curve->duration;
                            anim_x = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                            float t = elapsed_time / curve->duration;
                            anim_y = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_scale >= 0 && cue->curve_scale < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale];
                            float t = elapsed_time / curve->duration;
                            anim_scale = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_rotation >= 0 && cue->curve_rotation < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_rotation];
                            float t = elapsed_time / curve->duration;
                            anim_rotation = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_opacity];
                            float t = elapsed_time / curve->duration;
                            anim_opacity = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_frame >= 0 && cue->curve_frame < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_frame];
                            float t = elapsed_time / curve->duration;
                            anim_frame = rev::curve::Evaluate(*curve, t);
                        }
                    }

                    int frame_count = 0;
                    for (const char* p = cue->frame_keys_csv; *p; ++p) {
                        if (*p == ';') ++frame_count;
                    }
                    if (cue->frame_keys_csv[0] != '\0') ++frame_count;
                    if (frame_count <= 0) continue;

                    int frame_idx = cue->start_frame;
                    if (cue->fps > 0.0f && elapsed_time > 0.0f) {
                        float frame_f = elapsed_time * cue->fps;
                        if (cue->playback_mode == 1) {
                            int once_idx = (int)frame_f;
                            if (once_idx >= frame_count) once_idx = frame_count - 1;
                            if (once_idx < 0) once_idx = 0;
                            frame_idx = once_idx;
                        } else if (cue->playback_mode == 2 && frame_count > 1) {
                            int period = (frame_count * 2) - 2;
                            int ping_idx = ((int)frame_f) % period;
                            if (ping_idx < 0) ping_idx += period;
                            if (ping_idx >= frame_count) ping_idx = period - ping_idx;
                            frame_idx = ping_idx;
                        } else {
                            int loop_idx = ((int)frame_f) % frame_count;
                            if (loop_idx < 0) loop_idx += frame_count;
                            frame_idx = loop_idx;
                        }
                    }
                    if (cue->curve_frame >= 0 && cue->curve_frame < editor->project->curve_count) {
                        frame_idx = (int)anim_frame;
                    }
                    if (frame_idx < 0) frame_idx = 0;
                    if (frame_idx >= frame_count) frame_idx = frame_count - 1;

                    char selected_key[256] = {};
                    {
                        int current = 0;
                        const char* start = cue->frame_keys_csv;
                        const char* p = cue->frame_keys_csv;
                        while (true) {
                            if (*p == ';' || *p == '\0') {
                                if (current == frame_idx) {
                                    size_t len = (size_t)(p - start);
                                    if (len >= sizeof(selected_key)) len = sizeof(selected_key) - 1;
                                    strncpy_s(selected_key, start, len);
                                    break;
                                }
                                if (*p == '\0') break;
                                start = p + 1;
                                ++current;
                            }
                            if (*p == '\0') break;
                            ++p;
                        }
                    }
                    if (selected_key[0] == '\0') continue;

                    char full_path[512];
                    snprintf(full_path, sizeof(full_path), "%s\\%s", editor->project->assets_path, selected_key);
                    rev::runtime::ImageTexture rt_img{};
                    if (!rev::runtime::LoadImageTexture(full_path, &rt_img)) continue;
                    tex    = rt_img.texture_id;
                    norm_w = (rt_img.width  * anim_scale) / editor->preview_width  * 2.0f;
                    norm_h = (rt_img.height * anim_scale) / editor->preview_height * 2.0f;
                    pos_x  =  (anim_x * 2.0f) - 1.0f;
                    pos_y  = -((anim_y * 2.0f) - 1.0f);
                    rotation = anim_rotation;
                    opacity = anim_opacity * rev::runtime::ComputeEffectOpacity(
                        cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end, editor->current_time - item.scene_start_time);
                } else if (item.type == 5) {
                    PixelCue* cue = (PixelCue*)item.cue;
                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_scale = cue->scale;
                    float anim_rotation = cue->rotation;
                    float anim_opacity = cue->opacity;
                    float anim_frame = (float)cue->start_frame;
                    float palette_offset = (float)cue->palette_offset;
                    float elapsed_time = editor->current_time - (item.scene_start_time + cue->cue_start);
                    if (elapsed_time < 0.0f) elapsed_time = 0.0f;

                    auto evaluate_pixel_curve = [&](int curve_index, float fallback) {
                        if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                        rev::curve::Curve* curve = &editor->project->curves[curve_index];
                        return curve->duration > 0.0f
                            ? rev::curve::Evaluate(*curve, elapsed_time / curve->duration) : fallback;
                    };
                    anim_x = evaluate_pixel_curve(cue->curve_x, anim_x);
                    anim_y = evaluate_pixel_curve(cue->curve_y, anim_y);
                    anim_scale = evaluate_pixel_curve(cue->curve_scale, anim_scale);
                    anim_rotation = evaluate_pixel_curve(cue->curve_rotation, anim_rotation);
                    anim_opacity = evaluate_pixel_curve(cue->curve_opacity, anim_opacity);
                    anim_frame = evaluate_pixel_curve(cue->curve_frame, anim_frame);
                    palette_offset = evaluate_pixel_curve(cue->curve_palette_offset, palette_offset);

                    char pixel_path[512] = {};
                    if (!ResolveEditorPixelPath(editor->project, cue, pixel_path, sizeof(pixel_path))) continue;
                    rev::pixel::PixelAnimation* animation = rev::pixel::LoadAnimation(pixel_path);
                    if (!animation) continue;

                    int frame_idx = cue->start_frame;
                    if (cue->fps > 0.0f && elapsed_time > 0.0f) {
                        int frame_value = (int)(elapsed_time * cue->fps);
                        if (cue->playback_mode == 1) {
                            frame_idx = frame_value < animation->frame_count ? frame_value : animation->frame_count - 1;
                        } else if (cue->playback_mode == 2 && animation->frame_count > 1) {
                            int period = animation->frame_count * 2 - 2;
                            int ping_idx = frame_value % period;
                            if (ping_idx < 0) ping_idx += period;
                            frame_idx = ping_idx < animation->frame_count ? ping_idx : period - ping_idx;
                        } else {
                            frame_idx = frame_value % animation->frame_count;
                            if (frame_idx < 0) frame_idx += animation->frame_count;
                        }
                    }
                    if (cue->curve_frame >= 0) frame_idx = (int)anim_frame;
                    if (frame_idx < 0) frame_idx = 0;
                    if (frame_idx >= animation->frame_count) frame_idx = animation->frame_count - 1;
                    palette_offset += elapsed_time * (float)cue->palette_cycle_speed;

                    if (cue->snap_to_pixels) {
                        anim_x = floorf(anim_x * editor->preview_width + 0.5f) / editor->preview_width;
                        anim_y = floorf(anim_y * editor->preview_height + 0.5f) / editor->preview_height;
                    }
                    int pixel_width = animation->width;
                    int pixel_height = animation->height;
                    if (!UploadEditorPixelFrame(animation, frame_idx, (int)palette_offset, &tex)) {
                        rev::pixel::DestroyAnimation(animation);
                        continue;
                    }
                    rev::pixel::DestroyAnimation(animation);
                    norm_w = (pixel_width * anim_scale) / editor->preview_width * 2.0f;
                    norm_h = (pixel_height * anim_scale) / editor->preview_height * 2.0f;
                    pos_x = (anim_x * 2.0f) - 1.0f;
                    pos_y = -((anim_y * 2.0f) - 1.0f);
                    rotation = anim_rotation;
                    opacity = anim_opacity;
                } else if (item.type == 1) {
                    TextCue* cue = (TextCue*)item.cue;
                    
                    // Evaluate curves for animation
                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_size = cue->size;
                    float anim_rotation = cue->rotation;
                    float anim_color_r = cue->color.r;
                    float anim_color_g = cue->color.g;
                    float anim_color_b = cue->color.b;
                    
                    // Calculate elapsed time from cue start (convert scene-relative to absolute)
                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time >= 0.0f) {
                        if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                            float t = elapsed_time / curve->duration;
                            anim_x = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                            float t = elapsed_time / curve->duration;
                            anim_y = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_size >= 0 && cue->curve_size < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_size];
                            float t = elapsed_time / curve->duration;
                            anim_size = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_rotation >= 0 && cue->curve_rotation < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_rotation];
                            anim_rotation = rev::curve::Evaluate(*curve, elapsed_time / curve->duration);
                        }
                        if (cue->curve_color_r >= 0 && cue->curve_color_r < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_r];
                            float t = elapsed_time / curve->duration;
                            anim_color_r = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_color_g >= 0 && cue->curve_color_g < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_g];
                            float t = elapsed_time / curve->duration;
                            anim_color_g = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_color_b >= 0 && cue->curve_color_b < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_b];
                            float t = elapsed_time / curve->duration;
                            anim_color_b = rev::curve::Evaluate(*curve, t);
                        }
                    }
                    
                        rev::runtime::TextEffectFrame fx = {};
                        float scene_time = editor->current_time - item.scene_start_time;
                        if (!rev::runtime::BuildTextEffectFrame(cue, scene_time, &fx)) continue;

                        bool has_text_animation = cue->animation.version > 0;
                        const char* preview_text = has_text_animation ? cue->text : fx.text;

                        rev::runtime::TextGlyphAtlas* atlas = EnsurePreviewAtlas(editor, false,
                            cue->font_name, cue->size);
                        if (atlas && glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                        if (atlas && DrawPreviewGlyphRun(sprite_prog, atlas, preview_text,
                            anim_x + fx.offset_x, anim_y + fx.offset_y,
                            cue->size > 0.0f ? anim_size / cue->size : 1.0f,
                            1.0f,
                            rev::runtime::ComputeEffectOpacity(
                                cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                                cue->fade_out_start, cue->fade_out_end, scene_time) * fx.opacity_mul,
                            anim_color_r, anim_color_g, anim_color_b,
                            (float)editor->preview_width, (float)editor->preview_height,
                            0.0f, 0.0f, 9.0f, 0.0f, 0.0f, scene_time, anim_rotation, true,
                            has_text_animation ? &cue->animation : nullptr, scene_time)) {
                            continue;
                        }

                        rev::runtime::TextTexture rt_txt{};
                        if (!rev::runtime::RenderTextToTexture(
                            fx.text, cue->font_name, anim_size,
                            anim_color_r, anim_color_g, anim_color_b, &rt_txt)) continue;
                    tex    = rt_txt.texture_id;
                    norm_w = (float)rt_txt.width  / editor->preview_width  * 2.0f;
                    norm_h = (float)rt_txt.height / editor->preview_height * 2.0f;
                        pos_x  =  ((anim_x + fx.offset_x) * 2.0f) - 1.0f;
                        pos_y  = -(((anim_y + fx.offset_y) * 2.0f) - 1.0f);
                        rotation = anim_rotation;
                        opacity = rev::runtime::ComputeEffectOpacity(
                        cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end, scene_time) * fx.opacity_mul;
                } else {
                    ScrollTextCue* cue = (ScrollTextCue*)item.cue;

                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_speed = cue->speed;
                    float anim_size = cue->size;
                    float anim_rotation = cue->rotation;
                    float anim_opacity = cue->opacity;
                    float anim_color_r = cue->color.r;
                    float anim_color_g = cue->color.g;
                    float anim_color_b = cue->color.b;
                    float anim_wave_amp = cue->wave_amp;
                    float anim_wave_freq = cue->wave_freq;
                    float anim_wave_length = cue->wave_length;
                    float anim_jitter_amp = cue->jitter_amp;
                    float anim_jitter_freq = cue->jitter_freq;

                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time < 0.0f) elapsed_time = 0.0f;

                    if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                        float t = elapsed_time / curve->duration;
                        anim_x = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                        float t = elapsed_time / curve->duration;
                        anim_y = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_speed >= 0 && cue->curve_speed < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_speed];
                        float t = elapsed_time / curve->duration;
                        anim_speed = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_size >= 0 && cue->curve_size < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_size];
                        float t = elapsed_time / curve->duration;
                        anim_size = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_rotation >= 0 && cue->curve_rotation < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rotation];
                        anim_rotation = rev::curve::Evaluate(*curve, elapsed_time / curve->duration);
                    }
                    if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_opacity];
                        float t = elapsed_time / curve->duration;
                        anim_opacity = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_r >= 0 && cue->curve_color_r < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_r];
                        float t = elapsed_time / curve->duration;
                        anim_color_r = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_g >= 0 && cue->curve_color_g < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_g];
                        float t = elapsed_time / curve->duration;
                        anim_color_g = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_b >= 0 && cue->curve_color_b < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_b];
                        float t = elapsed_time / curve->duration;
                        anim_color_b = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_wave_amp >= 0 && cue->curve_wave_amp < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_wave_amp];
                        float t = elapsed_time / curve->duration;
                        anim_wave_amp = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_wave_freq >= 0 && cue->curve_wave_freq < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_wave_freq];
                        float t = elapsed_time / curve->duration;
                        anim_wave_freq = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_wave_length >= 0 && cue->curve_wave_length < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_wave_length];
                        float t = elapsed_time / curve->duration;
                        anim_wave_length = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_jitter_amp >= 0 && cue->curve_jitter_amp < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_jitter_amp];
                        float t = elapsed_time / curve->duration;
                        anim_jitter_amp = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_jitter_freq >= 0 && cue->curve_jitter_freq < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_jitter_freq];
                        float t = elapsed_time / curve->duration;
                        anim_jitter_freq = rev::curve::Evaluate(*curve, t);
                    }

                    auto clamp01 = [](float v) {
                        if (v < 0.0f) return 0.0f;
                        if (v > 1.0f) return 1.0f;
                        return v;
                    };

                    float scene_time = editor->current_time - item.scene_start_time;
                    rev::runtime::TextGlyphAtlas* atlas = EnsurePreviewAtlas(editor, true,
                        cue->font_name, cue->size);
                    float size_scale = cue->size > 0.0f ? anim_size / cue->size : 1.0f;
                    float travel = rev::runtime::ComputeScrollTextTravel(
                        atlas, cue->text, cue->direction, size_scale, cue->spacing,
                        cue->wrap_gap, (float)editor->preview_width,
                        (float)editor->preview_height);
                    float wrapped = elapsed_time * anim_speed;
                    if (cue->loop_mode == 0) {
                        float speed_abs = fabsf(anim_speed);
                        if (speed_abs < 0.0001f) speed_abs = 0.0001f;
                        float cycle_duration = travel / speed_abs;
                        float local_time = fmodf(elapsed_time, cycle_duration);
                        if (local_time < 0.0f) local_time += cycle_duration;
                        wrapped = local_time * anim_speed;
                    } else {
                        wrapped = elapsed_time * anim_speed;
                        if (wrapped > travel) wrapped = travel;
                        if (wrapped < -travel) wrapped = -travel;
                    }

                    float dir_x = 0.0f;
                    float dir_y = 0.0f;
                    if (cue->direction == 0) dir_x = -1.0f;
                    else if (cue->direction == 1) dir_x = 1.0f;
                    else if (cue->direction == 2) dir_y = -1.0f;
                    else dir_y = 1.0f;

                    float wave_phase = elapsed_time * anim_wave_freq * 6.2831853f;
                    float jitter_phase = elapsed_time * anim_jitter_freq * 6.2831853f;
                    float wave_offset = sinf(wave_phase) * anim_wave_amp;
                    float jitter_x = sinf(jitter_phase * 1.7f) * anim_jitter_amp;
                    float jitter_y = cosf(jitter_phase * 1.3f) * anim_jitter_amp;
                    float distortion = cue->distortion * sinf(elapsed_time * 17.0f);

                    float glow_boost = cue->glow * 0.28f;
                    float chroma = cue->chroma_shift;
                    float draw_r = clamp01(anim_color_r + glow_boost + chroma * 0.25f);
                    float draw_g = clamp01(anim_color_g + glow_boost * 0.8f);
                    float draw_b = clamp01(anim_color_b + glow_boost + chroma * 0.6f);

                    float effective_size = anim_size * (1.0f + cue->outline * 0.08f + fabsf(distortion) * 0.05f);
                    if (effective_size < 4.0f) effective_size = 4.0f;

                    char scroll_text_buffer[512] = {};
                    if (cue->direction <= 1) {
                        snprintf(scroll_text_buffer, sizeof(scroll_text_buffer), "%s   %s", cue->text, cue->text);
                    } else {
                        strncpy_s(scroll_text_buffer, sizeof(scroll_text_buffer), cue->text, _TRUNCATE);
                    }

                    float scroll_x = anim_x + dir_x * wrapped + jitter_x + distortion;
                    float scroll_y = anim_y + dir_y * wrapped + jitter_y;
                    if (cue->direction <= 1) scroll_y += wave_offset;
                    else scroll_x += wave_offset;
                    float fade_mul = rev::runtime::ComputeEffectOpacity(
                        1, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end, scene_time);
                    float style_mul = 1.0f + cue->shadow * 0.1f;
                    float scroll_opacity = clamp01(anim_opacity * fade_mul * style_mul);
                    if (atlas && glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                    if (atlas && DrawPreviewGlyphRun(sprite_prog, atlas, scroll_text_buffer,
                        scroll_x - jitter_x, scroll_y - jitter_y - wave_offset,
                        cue->size > 0.0f ? effective_size / cue->size : 1.0f,
                        cue->spacing, scroll_opacity,
                        draw_r, draw_g, draw_b,
                        (float)editor->preview_width, (float)editor->preview_height,
                        anim_wave_amp, anim_wave_freq,
                        anim_wave_length,
                        anim_jitter_amp, anim_jitter_freq, elapsed_time, anim_rotation,
                        cue->direction <= 1)) {
                        continue;
                    }

                    rev::runtime::TextTexture rt_txt{};
                    if (!rev::runtime::RenderTextToTexture(scroll_text_buffer, cue->font_name, effective_size,
                        draw_r, draw_g, draw_b, &rt_txt)) {
                        continue;
                    }

                    tex = rt_txt.texture_id;
                    float spacing_mul = (cue->spacing <= 0.01f) ? 0.01f : cue->spacing;
                    norm_w = ((float)rt_txt.width * spacing_mul) / editor->preview_width * 2.0f;
                    norm_h = (float)rt_txt.height / editor->preview_height * 2.0f;

                    pos_x = (scroll_x * 2.0f) - 1.0f;
                    pos_y = -((scroll_y * 2.0f) - 1.0f);

                    opacity = scroll_opacity;
                    rotation = anim_rotation;
                }

                LayerPostEffect* layer_effects = nullptr;
                int layer_effect_count = 0;
                if (item.type == 0) {
                    ImageCue* cue = (ImageCue*)item.cue;
                    layer_effects = cue->post_effects;
                    layer_effect_count = cue->post_effect_count;
                } else if (item.type == 4) {
                    AnimatedSpriteCue* cue = (AnimatedSpriteCue*)item.cue;
                    layer_effects = cue->post_effects;
                    layer_effect_count = cue->post_effect_count;
                } else if (item.type == 5) {
                    PixelCue* cue = (PixelCue*)item.cue;
                    layer_effects = cue->post_effects;
                    layer_effect_count = cue->post_effect_count;
                }

                bool has_layer_post = editor->layer_fbo && editor->layer_texture && editor->post_shader &&
                    layer_effects && layer_effect_count > 0;
                if (has_layer_post) {
#if 0
                    glBindFramebuffer(0x8D40, editor->layer_fbo);
                    glViewport(0, 0, editor->preview_width, editor->preview_height);
                    glDisable(GL_BLEND);
                    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    rev::shader::Use(sprite_prog);
                    if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                    glBindTexture(GL_TEXTURE_2D, tex);
                    if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                    if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos, pos_x, pos_y);
                    if (sp_sz  >= 0) rev::shader::SetVec2(sprite_prog, sp_sz, norm_w, norm_h);
                    if (sp_rot >= 0) rev::shader::SetFloat(sprite_prog, sp_rot, rotation);
                    if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, opacity);
                    if (sp_col >= 0) rev::shader::SetVec3(sprite_prog, sp_col, 1.0f, 1.0f, 1.0f);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                    int enabled[PostEffectCount] = {};
                    float intensity[PostEffectCount] = {};
                    float threshold[PostEffectCount] = {};
                    float radius[PostEffectCount] = {};
                    float color[PostEffectCount][4] = {};
                    int layer_blend_mode = 0;
                    float layer_time = editor->current_time - item.scene_start_time;
                    for (int effect_index = 0; effect_index < layer_effect_count && effect_index < rev::runtime::kMaxLayerPostEffects; ++effect_index) {
                        LayerPostEffect& effect = layer_effects[effect_index];
                        float effect_end = effect.end_time < 0.0f ? 1.0e30f : effect.end_time;
                        if (!effect.enabled || effect.type < 0 || effect.type >= PostEffectCount ||
                            effect.type >= 20 ||
                            layer_time < effect.start_time || layer_time > effect_end) continue;
                        layer_blend_mode = (effect.blend_mode >= 0 && effect.blend_mode <= 3)
                            ? effect.blend_mode : 0;
                        auto evaluate_layer_curve = [&](int curve_index, float fallback) {
                            if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                            const rev::curve::Curve& curve = editor->project->curves[curve_index];
                            float normalized_time = curve.duration > 0.0f
                                ? (layer_time - effect.start_time) / curve.duration : 0.0f;
                            return rev::curve::Evaluate(curve, normalized_time);
                        };
                        enabled[effect.type] = 1;
                        intensity[effect.type] = evaluate_layer_curve(effect.curve_intensity, effect.intensity);
                        threshold[effect.type] = evaluate_layer_curve(effect.curve_threshold, effect.threshold);
                        radius[effect.type] = evaluate_layer_curve(effect.curve_radius, effect.radius);
                        color[effect.type][0] = evaluate_layer_curve(effect.curve_color_r, effect.color[0]);
                        color[effect.type][1] = evaluate_layer_curve(effect.curve_color_g, effect.color[1]);
                        color[effect.type][2] = evaluate_layer_curve(effect.curve_color_b, effect.color[2]);
                        color[effect.type][3] = evaluate_layer_curve(effect.curve_color_a, effect.color[3]);
                    }

                    glBindFramebuffer(0x8D40, editor->post_fbo);
                    glViewport(0, 0, editor->preview_width, editor->preview_height);
                    rev::shader::Program* layer_post = (rev::shader::Program*)editor->post_shader;
                    rev::shader::Use(layer_post);
                    rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, "u_scene"), 0);
                    rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, "u_history"), 1);
                    rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, "u_unpremultiply_scene"), 1);
                    rev::shader::SetVec2(layer_post, rev::shader::GetUniformLocation(layer_post, "u_resolution"),
                                         (float)editor->preview_width, (float)editor->preview_height);
                    rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, "u_time"), layer_time);
                    if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                    glBindTexture(GL_TEXTURE_2D, editor->layer_texture);
                    if (glActiveTexture_fn) glActiveTexture_fn(0x84C1);
                    glBindTexture(GL_TEXTURE_2D, editor->post_history_texture);
                    for (int effect_index = 0; effect_index < PostEffectCount; ++effect_index) {
                        char uniform_name[64];
                        snprintf(uniform_name, sizeof(uniform_name), "u_enabled[%d]", effect_index);
                        rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), enabled[effect_index]);
                        snprintf(uniform_name, sizeof(uniform_name), "u_intensity[%d]", effect_index);
                        rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), intensity[effect_index]);
                        snprintf(uniform_name, sizeof(uniform_name), "u_threshold[%d]", effect_index);
                        rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), threshold[effect_index]);
                        snprintf(uniform_name, sizeof(uniform_name), "u_radius[%d]", effect_index);
                        rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), radius[effect_index]);
                        snprintf(uniform_name, sizeof(uniform_name), "u_color[%d]", effect_index);
                        rev::shader::SetVec4(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name),
                                             color[effect_index][0], color[effect_index][1], color[effect_index][2], color[effect_index][3]);
                    }
                    glDrawArrays(GL_TRIANGLES, 0, 3);

                    glBindFramebuffer(0x8D40, editor->preview_fbo);
                    glEnable(GL_BLEND);
                    ApplySpriteBlendMode(layer_blend_mode);
                    rev::shader::Use(sprite_prog);
                    if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                    glBindTexture(GL_TEXTURE_2D, editor->post_texture);
                    if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                    if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos, 0.0f, 0.0f);
                    if (sp_sz >= 0) rev::shader::SetVec2(sprite_prog, sp_sz, 1.0f, 1.0f);
                    if (sp_rot >= 0) rev::shader::SetFloat(sprite_prog, sp_rot, 0.0f);
                    if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, 1.0f);
                    if (sp_col >= 0) rev::shader::SetVec3(sprite_prog, sp_col, 1.0f, 1.0f, 1.0f);
                    rev::shader::SetFloat(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_flip_v"), 0.0f);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    rev::shader::SetFloat(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_flip_v"), 1.0f);
                }
#endif
                    std::vector<int> active_effects;
                    float layer_time = editor->current_time - item.scene_start_time;
                    for (int effect_index = 0; effect_index < layer_effect_count &&
                         effect_index < rev::runtime::kMaxLayerPostEffects; ++effect_index) {
                        LayerPostEffect& effect = layer_effects[effect_index];
                        float effect_end = effect.end_time < 0.0f ? 1.0e30f : effect.end_time;
                        if (effect.enabled && effect.type >= 0 && effect.type < PostEffectCount &&
                            effect.type < 20 && layer_time >= effect.start_time && layer_time < effect_end) {
                            active_effects.push_back(effect_index);
                        }
                    }
                    if (active_effects.empty()) {
                        if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                        glBindTexture(GL_TEXTURE_2D, tex);
                        if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                        if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos, pos_x, pos_y);
                        if (sp_sz >= 0) rev::shader::SetVec2(sprite_prog, sp_sz, norm_w, norm_h);
                        if (sp_rot >= 0) rev::shader::SetFloat(sprite_prog, sp_rot, rotation);
                        if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, opacity);
                        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    } else {
                        unsigned int source_texture = editor->layer_texture;
                        unsigned int destination_fbo = editor->post_fbo;
                        unsigned int destination_texture = editor->post_texture;
                        int layer_blend_mode = 0;
                        for (int active_index : active_effects) {
                            LayerPostEffect& effect = layer_effects[active_index];
                            layer_blend_mode = (effect.blend_mode >= 0 && effect.blend_mode <= 3)
                                ? effect.blend_mode : 0;
                            float curve_time = layer_time - effect.start_time;
                            auto evaluate_layer_curve = [&](int curve_index, float fallback) {
                                if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                                const rev::curve::Curve& curve = editor->project->curves[curve_index];
                                float normalized_time = curve.duration > 0.0f ? curve_time / curve.duration : 0.0f;
                                return rev::curve::Evaluate(curve, normalized_time);
                            };
                            glBindFramebuffer(0x8D40, destination_fbo);
                            glViewport(0, 0, editor->preview_width, editor->preview_height);
                            glDisable(GL_BLEND);
                            rev::shader::Use((rev::shader::Program*)editor->post_shader);
                            rev::shader::Program* layer_post = (rev::shader::Program*)editor->post_shader;
                            rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, "u_scene"), 0);
                            rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, "u_history"), 1);
                            rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, "u_unpremultiply_scene"), 1);
                            rev::shader::SetVec2(layer_post, rev::shader::GetUniformLocation(layer_post, "u_resolution"),
                                                 (float)editor->preview_width, (float)editor->preview_height);
                            rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, "u_time"), layer_time);
                            if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                            glBindTexture(GL_TEXTURE_2D, source_texture);
                            if (glActiveTexture_fn) glActiveTexture_fn(0x84C1);
                            glBindTexture(GL_TEXTURE_2D, editor->post_history_texture);
                            for (int uniform_index = 0; uniform_index < PostEffectCount; ++uniform_index) {
                                char uniform_name[64];
                                bool selected = uniform_index == effect.type;
                                snprintf(uniform_name, sizeof(uniform_name), "u_enabled[%d]", uniform_index);
                                rev::shader::SetInt(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), selected ? 1 : 0);
                                snprintf(uniform_name, sizeof(uniform_name), "u_intensity[%d]", uniform_index);
                                rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), selected ? evaluate_layer_curve(effect.curve_intensity, effect.intensity) : 0.0f);
                                snprintf(uniform_name, sizeof(uniform_name), "u_threshold[%d]", uniform_index);
                                rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), selected ? evaluate_layer_curve(effect.curve_threshold, effect.threshold) : 0.0f);
                                snprintf(uniform_name, sizeof(uniform_name), "u_radius[%d]", uniform_index);
                                rev::shader::SetFloat(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name), selected ? evaluate_layer_curve(effect.curve_radius, effect.radius) : 0.0f);
                                snprintf(uniform_name, sizeof(uniform_name), "u_color[%d]", uniform_index);
                                rev::shader::SetVec4(layer_post, rev::shader::GetUniformLocation(layer_post, uniform_name),
                                    selected ? evaluate_layer_curve(effect.curve_color_r, effect.color[0]) : 0.0f,
                                    selected ? evaluate_layer_curve(effect.curve_color_g, effect.color[1]) : 0.0f,
                                    selected ? evaluate_layer_curve(effect.curve_color_b, effect.color[2]) : 0.0f,
                                    selected ? evaluate_layer_curve(effect.curve_color_a, effect.color[3]) : 0.0f);
                            }
                            glDrawArrays(GL_TRIANGLES, 0, 3);
                            source_texture = destination_texture;
                            if (destination_fbo == editor->post_fbo) {
                                destination_fbo = editor->layer_fbo;
                                destination_texture = editor->layer_texture;
                            } else {
                                destination_fbo = editor->post_fbo;
                                destination_texture = editor->post_texture;
                            }
                        }
                        glBindFramebuffer(0x8D40, editor->preview_fbo);
                        glEnable(GL_BLEND);
                        ApplySpriteBlendMode(layer_blend_mode);
                        rev::shader::Use(sprite_prog);
                        if (glActiveTexture_fn) glActiveTexture_fn(0x84C0);
                        glBindTexture(GL_TEXTURE_2D, source_texture);
                        if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                        if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos, 0.0f, 0.0f);
                        if (sp_sz >= 0) rev::shader::SetVec2(sprite_prog, sp_sz, 1.0f, 1.0f);
                        if (sp_rot >= 0) rev::shader::SetFloat(sprite_prog, sp_rot, 0.0f);
                        if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, 1.0f);
                        rev::shader::SetFloat(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_flip_v"), 0.0f);
                        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                        rev::shader::SetFloat(sprite_prog, rev::shader::GetUniformLocation(sprite_prog, "u_flip_v"), 1.0f);
                    }
                } else {
                    if (glActiveTexture_fn) glActiveTexture_fn(0x84C0); // GL_TEXTURE0
                    glBindTexture(GL_TEXTURE_2D, tex);
                    if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                    if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos, pos_x, pos_y);
                    if (sp_sz  >= 0) rev::shader::SetVec2(sprite_prog, sp_sz, norm_w, norm_h);
                    if (sp_rot >= 0) rev::shader::SetFloat(sprite_prog, sp_rot, rotation);
                    if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, opacity);
                    if (sp_col >= 0) rev::shader::SetVec3(sprite_prog, sp_col, 1.0f, 1.0f, 1.0f);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                }
                if (item.type == 0) {
                    ImageCue* image = (ImageCue*)item.cue;
                    RenderAssetShaderOverlays(image->shaders, image->shader_count, editor->project,
                                              pos_x, pos_y, norm_w, norm_h, rotation,
                                              editor->current_time - item.scene_start_time,
                                              editor->preview_width, editor->preview_height, tex);
                } else if (item.type == 4) {
                    AnimatedSpriteCue* animated_sprite = (AnimatedSpriteCue*)item.cue;
                    RenderAssetShaderOverlays(animated_sprite->shaders, animated_sprite->shader_count, editor->project,
                                              pos_x, pos_y, norm_w, norm_h, rotation,
                                              editor->current_time - item.scene_start_time,
                                              editor->preview_width, editor->preview_height, tex);
                } else if (item.type == 5) {
                    PixelCue* pixel = (PixelCue*)item.cue;
                    RenderAssetShaderOverlays(pixel->shaders, pixel->shader_count, editor->project,
                                              pos_x, pos_y, norm_w, norm_h, rotation,
                                              editor->current_time - item.scene_start_time,
                                              editor->preview_width, editor->preview_height, tex);
                }
                glDeleteTextures(1, &tex);

            } else {
                // Mesh (type == 2)
                if (!mesh_prog) continue;
                if (glActiveTexture_fn) glActiveTexture_fn(0x84C0); // GL_TEXTURE0 for mesh material samplers
                if (!depth_on) {
                    glEnable(0x0B71);                       // GL_DEPTH_TEST
                    if (glDepthFunc_fn) glDepthFunc_fn(0x0201); // GL_LESS
                    depth_on = true;
                }
                rev::shader::Use(mesh_prog);

                MeshCue* cue = (MeshCue*)item.cue;
                float opacity = rev::runtime::ComputeEffectOpacity(
                    cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                    cue->fade_out_start, cue->fade_out_end, editor->current_time - item.scene_start_time);

                // Animated transform and material properties (start with cue values)
                float anim_pos[3] = {cue->pos[0], cue->pos[1], cue->pos[2]};
                float anim_rot[3] = {cue->rot[0], cue->rot[1], cue->rot[2]};
                float anim_scale[3] = {cue->scale[0], cue->scale[1], cue->scale[2]};
                float anim_color[4] = {cue->color[0], cue->color[1], cue->color[2], cue->color[3]};
                float anim_metallic = cue->metallic;
                float anim_roughness = cue->roughness;
                float anim_mesh_size = cue->mesh_size;
                float anim_fov = (cue->fov_deg > 0.0f) ? cue->fov_deg : 45.0f;
                
                // Calculate elapsed time from cue start for curve evaluation (convert scene-relative to absolute)
                float absolute_cue_start = item.scene_start_time + cue->cue_start;
                float elapsed_time = editor->current_time - absolute_cue_start;
                if (elapsed_time >= 0.0f) {
                    // Position curves
                    if (cue->curve_pos_x >= 0 && cue->curve_pos_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_pos_x];
                        float t = elapsed_time / curve->duration;
                        anim_pos[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_pos_y >= 0 && cue->curve_pos_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_pos_y];
                        float t = elapsed_time / curve->duration;
                        anim_pos[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_pos_z >= 0 && cue->curve_pos_z < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_pos_z];
                        float t = elapsed_time / curve->duration;
                        anim_pos[2] = rev::curve::Evaluate(*curve, t);
                    }
                    // Rotation curves
                    if (cue->curve_rot_x >= 0 && cue->curve_rot_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rot_x];
                        float t = elapsed_time / curve->duration;
                        anim_rot[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_rot_y >= 0 && cue->curve_rot_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rot_y];
                        float t = elapsed_time / curve->duration;
                        anim_rot[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_rot_z >= 0 && cue->curve_rot_z < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rot_z];
                        float t = elapsed_time / curve->duration;
                        anim_rot[2] = rev::curve::Evaluate(*curve, t);
                    }
                    // Scale curves
                    if (cue->curve_scale_x >= 0 && cue->curve_scale_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale_x];
                        float t = elapsed_time / curve->duration;
                        anim_scale[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_scale_y >= 0 && cue->curve_scale_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale_y];
                        float t = elapsed_time / curve->duration;
                        anim_scale[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_scale_z >= 0 && cue->curve_scale_z < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale_z];
                        float t = elapsed_time / curve->duration;
                        anim_scale[2] = rev::curve::Evaluate(*curve, t);
                    }
                    // Color curves
                    if (cue->curve_color_r >= 0 && cue->curve_color_r < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_r];
                        float t = elapsed_time / curve->duration;
                        anim_color[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_g >= 0 && cue->curve_color_g < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_g];
                        float t = elapsed_time / curve->duration;
                        anim_color[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_b >= 0 && cue->curve_color_b < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_b];
                        float t = elapsed_time / curve->duration;
                        anim_color[2] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_a >= 0 && cue->curve_color_a < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_a];
                        float t = elapsed_time / curve->duration;
                        anim_color[3] = rev::curve::Evaluate(*curve, t);
                    }
                    // Material curves
                    if (cue->curve_metallic >= 0 && cue->curve_metallic < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_metallic];
                        float t = elapsed_time / curve->duration;
                        anim_metallic = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_roughness >= 0 && cue->curve_roughness < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_roughness];
                        float t = elapsed_time / curve->duration;
                        anim_roughness = rev::curve::Evaluate(*curve, t);
                    }
                    // Mesh size curve
                    if (cue->curve_mesh_size >= 0 && cue->curve_mesh_size < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_mesh_size];
                        float t = elapsed_time / curve->duration;
                        anim_mesh_size = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_fov >= 0 && cue->curve_fov < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_fov];
                        float t = elapsed_time / curve->duration;
                        anim_fov = rev::curve::Evaluate(*curve, t);
                    }
                }

                float model[16];
                rev::runtime::Mat4Model(model, anim_pos, anim_rot, anim_scale);  // Use animated transform
                if (glUniformMatrix4fv) glUniformMatrix4fv(mp_model, 1, 0, model);
                float proj_mat[16];
                if (anim_fov < 1.0f) anim_fov = 1.0f;
                if (anim_fov > 170.0f) anim_fov = 170.0f;
                rev::runtime::Mat4Perspective(proj_mat, anim_fov * 3.14159265f / 180.0f, mesh_aspect, 0.1f, 100.0f);
                if (glUniformMatrix4fv) glUniformMatrix4fv(mp_proj, 1, 0, proj_mat);
                if (glUniform4fv_fn) {
                    float col[4] = {anim_color[0], anim_color[1], anim_color[2], anim_color[3]*opacity};
                    glUniform4fv_fn(mp_col, 1, col);
                }
                if (mp_metal >= 0) rev::shader::SetFloat(mesh_prog, mp_metal, anim_metallic);
                if (mp_rough >= 0) rev::shader::SetFloat(mesh_prog, mp_rough, anim_roughness);

                float cue_col[4] = {anim_color[0], anim_color[1], anim_color[2], anim_color[3] * opacity};
                if (cue->cull_mode == 1) {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                } else if (cue->cull_mode == 2) {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_FRONT);
                }

                bool mesh_needs_blend = (cue_col[3] < 0.999f);

                float size  = anim_mesh_size > 0.0f ? anim_mesh_size : 1.0f;
                float param = cue->mesh_param > 0.0f ? cue->mesh_param : 16.0f;
                rev::mesh::Mesh* mesh = nullptr;
                switch (cue->mesh_type) {
                    case 0: mesh = rev::mesh::CreateCube(size); break;
                    case 1: mesh = rev::mesh::CreateSphere(size, (int)param); break;
                    case 2: mesh = rev::mesh::CreatePlane(size, param > 0.0f ? param : size); break;
                    case 3: mesh = rev::mesh::CreateTorus(size, param > 0.0f ? param : 0.3f, 32, 16); break;
                    case 4: {
                        if (cue->asset_path[0]) {
                            // Check cache for this mesh
                            uint64_t current_file_time = GetFileModificationTime(cue->asset_path);
                            rev::mesh::Mesh* cached = nullptr;
                            int cached_index = -1;
                            
                            for (int c = 0; c < editor->mesh_cache_count; ++c) {
                                if (strcmp(editor->mesh_cache[c].path, cue->asset_path) == 0) {
                                    // Found in cache - check if file has been modified
                                    if (editor->mesh_cache[c].last_write_time == current_file_time) {
                                        cached = (rev::mesh::Mesh*)editor->mesh_cache[c].mesh;
                                        cached_index = c;
                                    } else {
                                        // File modified - invalidate cache entry
                                        printf("[Cache] File modified, reloading: %s\n", cue->asset_path);
                                        if (editor->mesh_cache[c].mesh) {
                                            rev::mesh::DestroyMesh((rev::mesh::Mesh*)editor->mesh_cache[c].mesh);
                                            editor->mesh_cache[c].mesh = nullptr;
                                        }
                                    }
                                    break;
                                }
                            }
                            
                            if (cached) {
                                // Evaluate and apply animation transform based on scene time.
                                // Imported multi-node meshes use per-node delta matrices later during slot draws.
                                float* node_delta_mats = nullptr;
                                if (cached->current_animation >= 0) {
                                    rev::gltf::Animation* anims = (rev::gltf::Animation*)cached->animation_data;
                                    if (anims && (!cached->imported_nodes || cached->imported_node_count == 0)) {
                                        // Calculate animation time relative to cue start time
                                        float absolute_anim_start = absolute_cue_start;
                                        float absolute_fade_in_start = item.scene_start_time + cue->fade_in_start;
                                        if (absolute_fade_in_start > absolute_anim_start) {
                                            absolute_anim_start = absolute_fade_in_start;
                                        }
                                        float anim_time = editor->current_time - absolute_anim_start;
                                        
                                        // Handle looping
                                        if (cached->animation_loop && anims[cached->current_animation].duration > 0.0f) {
                                            float duration = anims[cached->current_animation].duration;
                                            if (anim_time < 0.0f) {
                                                anim_time = 0.0f;
                                            } else if (anim_time > duration) {
                                                anim_time = (float)std::fmod(anim_time, duration);
                                            }
                                        } else {
                                            // Clamp to animation duration if not looping
                                            if (anim_time < 0.0f) anim_time = 0.0f;
                                            if (anim_time > anims[cached->current_animation].duration) {
                                                anim_time = anims[cached->current_animation].duration;
                                            }
                                        }
                                        
                                        float translation[3], rotation[4], scale[3];
                                        rev::gltf::EvaluateAnimation(&anims[cached->current_animation],
                                                                    anim_time,
                                                                    translation, rotation, scale);
                                        rev::gltf::ApplyAnimationTransform(anim_pos, anim_rot, anim_scale,
                                                                          translation, rotation, scale);
                                        // Rebuild model matrix with animated transform
                                        rev::runtime::Mat4Model(model, anim_pos, anim_rot, anim_scale);
                                        if (glUniformMatrix4fv) glUniformMatrix4fv(mp_model, 1, 0, model);
                                    }
                                    if (anims && cached->animation_count > 0 && cached->imported_nodes && cached->imported_node_count > 0) {
                                        float absolute_anim_start = absolute_cue_start;
                                        float absolute_fade_in_start = item.scene_start_time + cue->fade_in_start;
                                        if (absolute_fade_in_start > absolute_anim_start) {
                                            absolute_anim_start = absolute_fade_in_start;
                                        }
                                        float anim_time = editor->current_time - absolute_anim_start;
                                        if (anim_time < 0.0f) anim_time = 0.0f;
                                        node_delta_mats = new float[cached->imported_node_count * 16];
                                        if (!rev::gltf::BuildAnimatedNodeDeltaMatricesAll(cached,
                                                                                          anims,
                                                                                          cached->animation_count,
                                                                                          anim_time,
                                                                                          cached->animation_loop,
                                                                                          node_delta_mats,
                                                                                          (int)cached->imported_node_count)) {
                                            delete[] node_delta_mats;
                                            node_delta_mats = nullptr;
                                        }
                                    }
                                }

                                float draw_light[3] = {3.0f, 5.0f, 4.0f};
                                if (cue->use_imported_light && cached->has_imported_light) {
                                    const float* light_delta = nullptr;
                                    if (node_delta_mats && cached->imported_light_node_index >= 0 &&
                                        cached->imported_light_node_index < (int)cached->imported_node_count) {
                                        light_delta = &node_delta_mats[cached->imported_light_node_index * 16];
                                    }
                                    if (light_delta) {
                                        const float* base_light = cached->imported_light_pos;
                                        draw_light[0] = light_delta[0] * base_light[0] + light_delta[4] * base_light[1] + light_delta[8]  * base_light[2] + light_delta[12];
                                        draw_light[1] = light_delta[1] * base_light[0] + light_delta[5] * base_light[1] + light_delta[9]  * base_light[2] + light_delta[13];
                                        draw_light[2] = light_delta[2] * base_light[0] + light_delta[6] * base_light[1] + light_delta[10] * base_light[2] + light_delta[14];
                                    } else {
                                        draw_light[0] = cached->imported_light_pos[0];
                                        draw_light[1] = cached->imported_light_pos[1];
                                        draw_light[2] = cached->imported_light_pos[2];
                                    }
                                }
                                if (mp_light >= 0) rev::shader::SetVec3(mesh_prog, mp_light, draw_light[0], draw_light[1], draw_light[2]);

                                float camera_eye[3] = {0.0f, 0.0f, 5.0f};
                                float camera_center[3] = {0.0f, 0.0f, 0.0f};
                                float camera_up[3] = {0.0f, 1.0f, 0.0f};
                                float camera_fov_deg = anim_fov;
                                if (cue->use_imported_camera && cached->has_imported_camera) {
                                    if (cached->imported_camera_node_index >= 0 &&
                                        cached->imported_camera_node_index < (int)cached->imported_node_count) {
                                        float camera_world[16] = {};
                                        const float* base_world = cached->imported_nodes[cached->imported_camera_node_index].base_world;
                                        if (node_delta_mats) {
                                            const float* camera_delta = &node_delta_mats[cached->imported_camera_node_index * 16];
                                            rev::runtime::Mat4Multiply(camera_world, camera_delta, base_world);
                                        } else {
                                            memcpy(camera_world, base_world, sizeof(camera_world));
                                        }

                                        camera_eye[0] = camera_world[12];
                                        camera_eye[1] = camera_world[13];
                                        camera_eye[2] = camera_world[14];

                                        float forward[3] = {-camera_world[8], -camera_world[9], -camera_world[10]};
                                        float forward_len = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
                                        if (forward_len > 0.000001f) {
                                            forward[0] /= forward_len;
                                            forward[1] /= forward_len;
                                            forward[2] /= forward_len;
                                        }

                                        camera_up[0] = camera_world[4];
                                        camera_up[1] = camera_world[5];
                                        camera_up[2] = camera_world[6];
                                        float up_len = sqrtf(camera_up[0] * camera_up[0] + camera_up[1] * camera_up[1] + camera_up[2] * camera_up[2]);
                                        if (up_len > 0.000001f) {
                                            camera_up[0] /= up_len;
                                            camera_up[1] /= up_len;
                                            camera_up[2] /= up_len;
                                        }

                                        camera_center[0] = camera_eye[0] + forward[0];
                                        camera_center[1] = camera_eye[1] + forward[1];
                                        camera_center[2] = camera_eye[2] + forward[2];
                                    } else {
                                        camera_eye[0] = cached->imported_camera_pos[0];
                                        camera_eye[1] = cached->imported_camera_pos[1];
                                        camera_eye[2] = cached->imported_camera_pos[2];
                                        camera_center[0] = cached->imported_camera_target[0];
                                        camera_center[1] = cached->imported_camera_target[1];
                                        camera_center[2] = cached->imported_camera_target[2];
                                    }
                                    camera_fov_deg = cached->imported_camera_fov_deg;
                                }
                                if (camera_fov_deg < 1.0f) camera_fov_deg = 1.0f;
                                if (camera_fov_deg > 170.0f) camera_fov_deg = 170.0f;
                                rev::runtime::Mat4LookAt(view_mat, camera_eye, camera_center, camera_up);
                                rev::runtime::Mat4Perspective(proj_mat, camera_fov_deg * 3.14159265f / 180.0f, mesh_aspect, 0.1f, 100.0f);
                                if (glUniformMatrix4fv) {
                                    glUniformMatrix4fv(mp_view, 1, 0, view_mat);
                                    glUniformMatrix4fv(mp_proj, 1, 0, proj_mat);
                                }
                                if (mp_vpos >= 0) rev::shader::SetVec3(mesh_prog, mp_vpos, camera_eye[0], camera_eye[1], camera_eye[2]);

                                int loc_has_tex = rev::shader::GetUniformLocation(mesh_prog, "u_has_texture");
                                int loc_tex = rev::shader::GetUniformLocation(mesh_prog, "u_base_color_texture");
                                for (uint32_t si = 0; si < cached->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = cached->material_slots[si];
                                    if (((slot.diffuse_color >> 24) & 0xFF) < 255) {
                                        mesh_needs_blend = true;
                                        break;
                                    }
                                }
                                if (mesh_needs_blend) {
                                    if (!blend_on) {
                                        glEnable(GL_BLEND);
                                        blend_on = true;
                                    }
                                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                    if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(0); // GL_FALSE
                                } else {
                                    if (blend_on) { glDisable(GL_BLEND); blend_on = false; }
                                    if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(1); // GL_TRUE
                                }

                                bool rendered_slot = false;
                                for (uint32_t si = 0; si < cached->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = cached->material_slots[si];
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        float slot_r = (float)((slot.diffuse_color >> 0)  & 0xFF) / 255.0f;
                                        float slot_g = (float)((slot.diffuse_color >> 8)  & 0xFF) / 255.0f;
                                        float slot_b = (float)((slot.diffuse_color >> 16) & 0xFF) / 255.0f;
                                        float slot_a = (float)((slot.diffuse_color >> 24) & 0xFF) / 255.0f;
                                        float col[4] = {
                                            cue_col[0] * slot_r,
                                            cue_col[1] * slot_g,
                                            cue_col[2] * slot_b,
                                            cue_col[3] * slot_a
                                        };
                                        glUniform4fv_fn(mp_col, 1, col);
                                    }
                                    if (mp_emissive_color >= 0) {
                                        rev::shader::SetVec3(mesh_prog, mp_emissive_color,
                                            cue->emissive_color[0] * slot.emissive_color[0],
                                            cue->emissive_color[1] * slot.emissive_color[1],
                                            cue->emissive_color[2] * slot.emissive_color[2]);
                                    }
                                    if (mp_emissive_strength >= 0) {
                                        rev::shader::SetFloat(mesh_prog, mp_emissive_strength,
                                            cue->emissive_strength * slot.emissive_strength);
                                    }

                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        if (node_delta_mats && slot.source_node_index >= 0 &&
                                            slot.source_node_index < (int)cached->imported_node_count) {
                                            float slot_model[16] = {};
                                            rev::runtime::Mat4Multiply(slot_model, model, &node_delta_mats[slot.source_node_index * 16]);
                                            glUniformMatrix4fv(mp_model, 1, 0, slot_model);
                                        } else {
                                            glUniformMatrix4fv(mp_model, 1, 0, model);
                                        }
                                    }

                                    unsigned int tex_id = slot.base_color_texture;
                                    if (tex_id == 0) tex_id = cached->base_color_texture;
                                    if (tex_id != 0) {
                                        glBindTexture(0x0DE1, tex_id); // GL_TEXTURE_2D
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(cached, (int)si);
                                    rendered_slot = true;
                                }
                                if (!rendered_slot) {
                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        glUniformMatrix4fv(mp_model, 1, 0, model);
                                    }
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        glUniform4fv_fn(mp_col, 1, cue_col);
                                    }
                                    if (mp_emissive_color >= 0) {
                                        rev::shader::SetVec3(mesh_prog, mp_emissive_color,
                                            cue->emissive_color[0] * cached->emissive_color[0],
                                            cue->emissive_color[1] * cached->emissive_color[1],
                                            cue->emissive_color[2] * cached->emissive_color[2]);
                                    }
                                    if (mp_emissive_strength >= 0) {
                                        rev::shader::SetFloat(mesh_prog, mp_emissive_strength,
                                            cue->emissive_strength * cached->emissive_strength);
                                    }
                                    if (cached->base_color_texture != 0) {
                                        glBindTexture(0x0DE1, cached->base_color_texture);
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(cached, -1);
                                }
                                delete[] node_delta_mats;
                                continue;
                            }
                            // Extract textures to assets folder
                            rev::gltf::ImportResult* ir = rev::gltf::LoadMesh(cue->asset_path, editor->project->assets_path);
                            if (ir && ir->ok) {
                                mesh = ir->mesh;
                                ir->mesh = nullptr;

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
                                
                                // Load base color texture if present
                                if (ir->material.base_color_texture[0] != '\0') {
                                    rev::runtime::ImageTexture tex{};
                                    if (rev::runtime::LoadImageTexture(ir->material.base_color_texture, &tex)) {
                                        mesh->base_color_texture = tex.texture_id;
                                    }
                                }

                                // Load per-material textures and map to material slots.
                                if (mesh && ir->material_count > 0 && ir->materials) {
                                    unsigned int* material_textures = new unsigned int[ir->material_count];
                                    memset(material_textures, 0, sizeof(unsigned int) * ir->material_count);
                                    for (int mat_i = 0; mat_i < ir->material_count; ++mat_i) {
                                        const char* tex_path = ir->materials[mat_i].base_color_texture;
                                        if (tex_path[0] == '\0') continue;
                                        rev::runtime::ImageTexture tex{};
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
                                
                                // Transfer animations to mesh
                                if (ir->animation_count > 0) {
                                    mesh->animation_data = ir->animations;
                                    mesh->animation_count = ir->animation_count;
                                    mesh->current_animation = 0;  // Start first animation
                                    mesh->animation_time = 0.0f;
                                    mesh->animation_speed = 1.0f;
                                    mesh->animation_loop = true;
                                    ir->animations = nullptr;  // Transfer ownership
                                }
                            }
                            if (ir) rev::gltf::FreeImportResult(ir);
                            if (mesh) {
                                rev::mesh::UploadToGPU(mesh);
                                
                                // Add to cache (or update existing slot if we just invalidated one)
                                int cache_slot = -1;
                                for (int c = 0; c < editor->mesh_cache_count; ++c) {
                                    if (editor->mesh_cache[c].mesh == nullptr) {
                                        cache_slot = c; // Reuse invalidated slot
                                        break;
                                    }
                                }
                                if (cache_slot < 0 && editor->mesh_cache_count < EditorContext::kMeshCacheSize) {
                                    cache_slot = editor->mesh_cache_count++;
                                }
                                
                                if (cache_slot >= 0) {
                                    auto& entry = editor->mesh_cache[cache_slot];
                                    strncpy_s(entry.path, cue->asset_path, _TRUNCATE);
                                    entry.mesh = mesh;
                                    entry.last_write_time = current_file_time;
                                }

                                float* node_delta_mats = nullptr;
                                if (mesh->animation_data && mesh->animation_count > 0 && mesh->imported_nodes && mesh->imported_node_count > 0) {
                                    rev::gltf::Animation* anims = (rev::gltf::Animation*)mesh->animation_data;
                                    float absolute_anim_start = absolute_cue_start;
                                    float absolute_fade_in_start = item.scene_start_time + cue->fade_in_start;
                                    if (absolute_fade_in_start > absolute_anim_start) {
                                        absolute_anim_start = absolute_fade_in_start;
                                    }
                                    float anim_time = editor->current_time - absolute_anim_start;
                                    if (anim_time < 0.0f) anim_time = 0.0f;
                                    node_delta_mats = new float[mesh->imported_node_count * 16];
                                    if (!rev::gltf::BuildAnimatedNodeDeltaMatricesAll(mesh,
                                                                                      anims,
                                                                                      mesh->animation_count,
                                                                                      anim_time,
                                                                                      mesh->animation_loop,
                                                                                      node_delta_mats,
                                                                                      (int)mesh->imported_node_count)) {
                                        delete[] node_delta_mats;
                                        node_delta_mats = nullptr;
                                    }
                                }
                                
                                float draw_light[3] = {3.0f, 5.0f, 4.0f};
                                if (cue->use_imported_light && mesh->has_imported_light) {
                                    const float* light_delta = nullptr;
                                    if (node_delta_mats && mesh->imported_light_node_index >= 0 &&
                                        mesh->imported_light_node_index < (int)mesh->imported_node_count) {
                                        light_delta = &node_delta_mats[mesh->imported_light_node_index * 16];
                                    }
                                    if (light_delta) {
                                        const float* base_light = mesh->imported_light_pos;
                                        draw_light[0] = light_delta[0] * base_light[0] + light_delta[4] * base_light[1] + light_delta[8]  * base_light[2] + light_delta[12];
                                        draw_light[1] = light_delta[1] * base_light[0] + light_delta[5] * base_light[1] + light_delta[9]  * base_light[2] + light_delta[13];
                                        draw_light[2] = light_delta[2] * base_light[0] + light_delta[6] * base_light[1] + light_delta[10] * base_light[2] + light_delta[14];
                                    } else {
                                        draw_light[0] = mesh->imported_light_pos[0];
                                        draw_light[1] = mesh->imported_light_pos[1];
                                        draw_light[2] = mesh->imported_light_pos[2];
                                    }
                                }
                                if (cue->use_imported_camera && mesh->has_imported_camera) {
                                    if (mesh->imported_camera_node_index >= 0 &&
                                        mesh->imported_camera_node_index < (int)mesh->imported_node_count) {
                                        float camera_world[16] = {};
                                        const float* base_world = mesh->imported_nodes[mesh->imported_camera_node_index].base_world;
                                        if (node_delta_mats) {
                                            const float* camera_delta = &node_delta_mats[mesh->imported_camera_node_index * 16];
                                            rev::runtime::Mat4Multiply(camera_world, camera_delta, base_world);
                                        } else {
                                            memcpy(camera_world, base_world, sizeof(camera_world));
                                        }

                                        eye[0] = camera_world[12];
                                        eye[1] = camera_world[13];
                                        eye[2] = camera_world[14];

                                        float forward[3] = {-camera_world[8], -camera_world[9], -camera_world[10]};
                                        float forward_len = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
                                        if (forward_len > 0.000001f) {
                                            forward[0] /= forward_len;
                                            forward[1] /= forward_len;
                                            forward[2] /= forward_len;
                                        }

                                        up3[0] = camera_world[4];
                                        up3[1] = camera_world[5];
                                        up3[2] = camera_world[6];
                                        float up_len = sqrtf(up3[0] * up3[0] + up3[1] * up3[1] + up3[2] * up3[2]);
                                        if (up_len > 0.000001f) {
                                            up3[0] /= up_len;
                                            up3[1] /= up_len;
                                            up3[2] /= up_len;
                                        }

                                        center3[0] = eye[0] + forward[0];
                                        center3[1] = eye[1] + forward[1];
                                        center3[2] = eye[2] + forward[2];
                                    } else {
                                        eye[0] = mesh->imported_camera_pos[0];
                                        eye[1] = mesh->imported_camera_pos[1];
                                        eye[2] = mesh->imported_camera_pos[2];
                                        center3[0] = mesh->imported_camera_target[0];
                                        center3[1] = mesh->imported_camera_target[1];
                                        center3[2] = mesh->imported_camera_target[2];
                                    }
                                    anim_fov = mesh->imported_camera_fov_deg;
                                }
                                if (anim_fov < 1.0f) anim_fov = 1.0f;
                                if (anim_fov > 170.0f) anim_fov = 170.0f;
                                rev::runtime::Mat4LookAt(view_mat, eye, center3, up3);
                                float animated_proj_mat[16];
                                rev::runtime::Mat4Perspective(animated_proj_mat, anim_fov * 3.14159265f / 180.0f, mesh_aspect, 0.1f, 100.0f);
                                if (glUniformMatrix4fv) {
                                    glUniformMatrix4fv(mp_view, 1, 0, view_mat);
                                    glUniformMatrix4fv(mp_proj, 1, 0, animated_proj_mat);
                                }
                                if (mp_light >= 0) rev::shader::SetVec3(mesh_prog, mp_light, draw_light[0], draw_light[1], draw_light[2]);

                                int loc_has_tex = rev::shader::GetUniformLocation(mesh_prog, "u_has_texture");
                                int loc_tex = rev::shader::GetUniformLocation(mesh_prog, "u_base_color_texture");
                                for (uint32_t si = 0; si < mesh->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = mesh->material_slots[si];
                                    if (((slot.diffuse_color >> 24) & 0xFF) < 255) {
                                        mesh_needs_blend = true;
                                        break;
                                    }
                                }
                                if (mesh_needs_blend) {
                                    if (!blend_on) {
                                        glEnable(GL_BLEND);
                                        blend_on = true;
                                    }
                                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                    if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(0); // GL_FALSE
                                } else {
                                    if (blend_on) { glDisable(GL_BLEND); blend_on = false; }
                                    if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(1); // GL_TRUE
                                }

                                bool rendered_slot = false;
                                for (uint32_t si = 0; si < mesh->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = mesh->material_slots[si];
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        float slot_r = (float)((slot.diffuse_color >> 0)  & 0xFF) / 255.0f;
                                        float slot_g = (float)((slot.diffuse_color >> 8)  & 0xFF) / 255.0f;
                                        float slot_b = (float)((slot.diffuse_color >> 16) & 0xFF) / 255.0f;
                                        float slot_a = (float)((slot.diffuse_color >> 24) & 0xFF) / 255.0f;
                                        float col[4] = {
                                            cue_col[0] * slot_r,
                                            cue_col[1] * slot_g,
                                            cue_col[2] * slot_b,
                                            cue_col[3] * slot_a
                                        };
                                        glUniform4fv_fn(mp_col, 1, col);
                                    }
                                    if (mp_emissive_color >= 0) {
                                        rev::shader::SetVec3(mesh_prog, mp_emissive_color,
                                            cue->emissive_color[0] * slot.emissive_color[0],
                                            cue->emissive_color[1] * slot.emissive_color[1],
                                            cue->emissive_color[2] * slot.emissive_color[2]);
                                    }
                                    if (mp_emissive_strength >= 0) {
                                        rev::shader::SetFloat(mesh_prog, mp_emissive_strength,
                                            cue->emissive_strength * slot.emissive_strength);
                                    }

                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        if (node_delta_mats && slot.source_node_index >= 0 &&
                                            slot.source_node_index < (int)mesh->imported_node_count) {
                                            float slot_model[16] = {};
                                            rev::runtime::Mat4Multiply(slot_model, model, &node_delta_mats[slot.source_node_index * 16]);
                                            glUniformMatrix4fv(mp_model, 1, 0, slot_model);
                                        } else {
                                            glUniformMatrix4fv(mp_model, 1, 0, model);
                                        }
                                    }

                                    unsigned int tex_id = slot.base_color_texture;
                                    if (tex_id == 0) tex_id = mesh->base_color_texture;
                                    if (tex_id != 0) {
                                        glBindTexture(0x0DE1, tex_id); // GL_TEXTURE_2D
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(mesh, (int)si);
                                    rendered_slot = true;
                                }
                                if (!rendered_slot) {
                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        glUniformMatrix4fv(mp_model, 1, 0, model);
                                    }
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        glUniform4fv_fn(mp_col, 1, cue_col);
                                    }
                                    if (mp_emissive_color >= 0) {
                                        rev::shader::SetVec3(mesh_prog, mp_emissive_color,
                                            cue->emissive_color[0] * mesh->emissive_color[0],
                                            cue->emissive_color[1] * mesh->emissive_color[1],
                                            cue->emissive_color[2] * mesh->emissive_color[2]);
                                    }
                                    if (mp_emissive_strength >= 0) {
                                        rev::shader::SetFloat(mesh_prog, mp_emissive_strength,
                                            cue->emissive_strength * mesh->emissive_strength);
                                    }
                                    if (mesh->base_color_texture != 0) {
                                        glBindTexture(0x0DE1, mesh->base_color_texture);
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(mesh, -1);
                                }
                                delete[] node_delta_mats;
                                continue;
                            }
                        }
                        mesh = rev::mesh::CreateCube(1.0f);
                        break;
                    }
                    default: mesh = rev::mesh::CreateCube(1.0f); break;
                }
                if (mesh) {
                    rev::mesh::UploadToGPU(mesh);
                    // Procedural meshes don't have textures
                    if (mp_light >= 0) rev::shader::SetVec3(mesh_prog, mp_light, 3.0f, 5.0f, 4.0f);
                    int loc_has_tex = rev::shader::GetUniformLocation(mesh_prog, "u_has_texture");
                    if (blend_on) { glDisable(GL_BLEND); blend_on = false; }
                    if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(1); // GL_TRUE
                    if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                    rev::mesh::Render(mesh, -1);
                    rev::mesh::DestroyMesh(mesh);
                }
            }
        }

        if (blend_on) glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        if (depth_on) {
            if (glDepthMask_mesh_fn) glDepthMask_mesh_fn(1); // GL_TRUE
            glDisable(0x0B71); // GL_DEPTH_TEST
        }
    }

    // Apply scene-level post-production after the complete layered scene draw.
    if (editor->post_shader && editor->project) {
        SceneBlock* active_scene = nullptr;
        float scene_start = 0.0f;
        for (int scene_index = 0; scene_index < editor->project->scene_count; ++scene_index) {
            SceneBlock* scene = &editor->project->scenes[scene_index];
            float scene_end = scene_start + scene->duration;
            if (editor->current_time >= scene_start && editor->current_time < scene_end) {
                active_scene = scene;
                break;
            }
            scene_start = scene_end;
        }

        int enabled[PostEffectCount] = {};
        float intensity[PostEffectCount] = {};
        float threshold[PostEffectCount] = {};
        float radius[PostEffectCount] = {};
        float color[PostEffectCount][4] = {};
        bool has_effect = false;
        if (active_scene) {
            float local_time = editor->current_time - scene_start;
            for (int i = 0; i < active_scene->post_effect_count; ++i) {
                PostEffect* effect = &active_scene->post_effects[i];
                if (!effect->enabled || effect->type < 0 || effect->type >= PostEffectCount) continue;
                if (local_time < effect->start_time ||
                    (effect->end_time >= 0.0f && local_time >= effect->end_time)) continue;
                float curve_time = local_time - effect->start_time;
                auto evaluate_effect_curve = [&](int curve_index, float fallback) {
                    if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                    rev::curve::Curve& curve = editor->project->curves[curve_index];
                    float normalized_time = curve.duration > 0.0f ? curve_time / curve.duration : 0.0f;
                    return rev::curve::Evaluate(curve, normalized_time);
                };
                enabled[effect->type] = 1;
                intensity[effect->type] = evaluate_effect_curve(effect->curve_intensity, effect->intensity);
                threshold[effect->type] = evaluate_effect_curve(effect->curve_threshold, effect->threshold);
                radius[effect->type] = evaluate_effect_curve(effect->curve_radius, effect->radius);
                color[effect->type][0] = evaluate_effect_curve(effect->curve_color_r, effect->color[0]);
                color[effect->type][1] = evaluate_effect_curve(effect->curve_color_g, effect->color[1]);
                color[effect->type][2] = evaluate_effect_curve(effect->curve_color_b, effect->color[2]);
                color[effect->type][3] = evaluate_effect_curve(effect->curve_color_a, effect->color[3]);
                has_effect = true;
            }
            for (int i = 0; i < active_scene->scene_layer_post_effect_count; ++i) {
                LayerPostEffect* effect = &active_scene->scene_layer_post_effects[i];
                if (!effect->enabled || effect->type < 0 || effect->type >= PostEffectCount) continue;
                if (local_time < effect->start_time ||
                    (effect->end_time >= 0.0f && local_time >= effect->end_time)) continue;
                if (enabled[effect->type]) continue;
                float curve_time = local_time - effect->start_time;
                auto evaluate_layer_curve = [&](int curve_index, float fallback) {
                    if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                    rev::curve::Curve& curve = editor->project->curves[curve_index];
                    float normalized_time = curve.duration > 0.0f ? curve_time / curve.duration : 0.0f;
                    return rev::curve::Evaluate(curve, normalized_time);
                };
                enabled[effect->type] = 1;
                intensity[effect->type] = evaluate_layer_curve(effect->curve_intensity, effect->intensity);
                threshold[effect->type] = evaluate_layer_curve(effect->curve_threshold, effect->threshold);
                radius[effect->type] = evaluate_layer_curve(effect->curve_radius, effect->radius);
                color[effect->type][0] = evaluate_layer_curve(effect->curve_color_r, effect->color[0]);
                color[effect->type][1] = evaluate_layer_curve(effect->curve_color_g, effect->color[1]);
                color[effect->type][2] = evaluate_layer_curve(effect->curve_color_b, effect->color[2]);
                color[effect->type][3] = evaluate_layer_curve(effect->curve_color_a, effect->color[3]);
                has_effect = true;
            }
        }

    #if 0
        if (has_effect && glFramebufferTexture2D) {
            glBindFramebuffer(0x8D40, editor->post_fbo);
            glViewport(0, 0, editor->preview_width, editor->preview_height);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            typedef void (*PFNGLCOLORMASKPROC)(unsigned char, unsigned char, unsigned char, unsigned char);
            auto glColorMask_fn = (PFNGLCOLORMASKPROC)wglGetProcAddress("glColorMask");
            if (glColorMask_fn) glColorMask_fn(1, 1, 1, 1);
            typedef void (*PFNGLDEPTHMASKPROC)(unsigned char);
            auto glDepthMask_post = (PFNGLDEPTHMASKPROC)wglGetProcAddress("glDepthMask");
            if (glDepthMask_post) glDepthMask_post(1);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            if (editor->preview_vao) {
                typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
                auto glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
                if (glBindVertexArray) glBindVertexArray(editor->preview_vao);
            }
            rev::shader::Program* post_prog = (rev::shader::Program*)editor->post_shader;
            rev::shader::Use(post_prog);
            rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, "u_scene"), 0);
            rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, "u_history"), 1);
            rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, "u_unpremultiply_scene"), 2);
            rev::shader::SetVec2(post_prog, rev::shader::GetUniformLocation(post_prog, "u_resolution"),
                                 (float)editor->preview_width, (float)editor->preview_height);
            rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, "u_time"), editor->current_time);
            typedef void (*PFNGLACTIVETEXTUREPROC)(unsigned int texture);
            auto glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
            if (glActiveTexture) glActiveTexture(0x84C0); // GL_TEXTURE0
            glBindTexture(GL_TEXTURE_2D, editor->preview_texture);
            if (glActiveTexture) glActiveTexture(0x84C1); // GL_TEXTURE1
            glBindTexture(GL_TEXTURE_2D, editor->post_history_texture);
            for (int i = 0; i < PostEffectCount; ++i) {
                char uniform_name[64];
                snprintf(uniform_name, sizeof(uniform_name), "u_enabled[%d]", i);
                rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), enabled[i]);
                snprintf(uniform_name, sizeof(uniform_name), "u_intensity[%d]", i);
                rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), intensity[i]);
                snprintf(uniform_name, sizeof(uniform_name), "u_threshold[%d]", i);
                rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), threshold[i]);
                snprintf(uniform_name, sizeof(uniform_name), "u_radius[%d]", i);
                rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), radius[i]);
                snprintf(uniform_name, sizeof(uniform_name), "u_color[%d]", i);
                rev::shader::SetVec4(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name),
                                     color[i][0], color[i][1], color[i][2], color[i][3]);
            }
            glDrawArrays(GL_TRIANGLES, 0, 3);
            editor->post_frame_rendered = true;

            typedef void (*PFNGLBLITFRAMEBUFFERPROC)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
            auto glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
            if (glBlitFramebuffer && editor->post_history_fbo) {
                glBindFramebuffer(0x8CA8, editor->post_fbo); // GL_READ_FRAMEBUFFER
                glBindFramebuffer(0x8CA9, editor->post_history_fbo); // GL_DRAW_FRAMEBUFFER
                glBlitFramebuffer(0, 0, editor->preview_width, editor->preview_height,
                                  0, 0, editor->preview_width, editor->preview_height,
                                  0x00004000, 0x2600); // COLOR_BUFFER_BIT, GL_NEAREST
                glBindFramebuffer(0x8D40, editor->post_fbo);
            }

        } else if (editor->post_history_fbo) {
            rev::shader::Program* post_prog = (rev::shader::Program*)editor->post_shader;
            if (post_prog) {
                rev::shader::Use(post_prog);
                for (int i = 0; i < PostEffectCount; ++i) {
                    char uniform_name[64];
                    snprintf(uniform_name, sizeof(uniform_name), "u_enabled[%d]", i);
                    rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), 0);
                    snprintf(uniform_name, sizeof(uniform_name), "u_intensity[%d]", i);
                    rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), 0.0f);
                    snprintf(uniform_name, sizeof(uniform_name), "u_threshold[%d]", i);
                    rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), 0.0f);
                    snprintf(uniform_name, sizeof(uniform_name), "u_radius[%d]", i);
                    rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), 0.0f);
                    snprintf(uniform_name, sizeof(uniform_name), "u_color[%d]", i);
                    rev::shader::SetVec4(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name),
                                         0.0f, 0.0f, 0.0f, 0.0f);
                }
            }
            typedef void (*PFNGLBLITFRAMEBUFFERPROC)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
            auto glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
            if (glBlitFramebuffer) {
                glBindFramebuffer(0x8CA8, editor->preview_fbo); // GL_READ_FRAMEBUFFER
                glBindFramebuffer(0x8CA9, editor->post_history_fbo); // GL_DRAW_FRAMEBUFFER
                glBlitFramebuffer(0, 0, editor->preview_width, editor->preview_height,
                                  0, 0, editor->preview_width, editor->preview_height,
                                  0x00004000, 0x2600); // COLOR_BUFFER_BIT, GL_NEAREST
                glBindFramebuffer(0x8D40, 0);
            }
            editor->post_frame_rendered = false;
        }
#endif
        if (glFramebufferTexture2D && active_scene && editor->post_shader &&
            editor->preview_fbo && editor->preview_texture &&
            editor->post_fbo && editor->post_texture &&
            editor->post_history_fbo && editor->post_history_texture) {
            struct ActivePostEffect {
                int type;
                float intensity;
                float threshold;
                float radius;
                float color[4];
                float time;
            };
            std::vector<ActivePostEffect> active_effects;
            float local_time = editor->current_time - scene_start;
            auto evaluate_curve = [&](int curve_index, float fallback, float curve_time) {
                if (curve_index < 0 || curve_index >= editor->project->curve_count) return fallback;
                const rev::curve::Curve& curve = editor->project->curves[curve_index];
                float normalized_time = curve.duration > 0.0f ? curve_time / curve.duration : 0.0f;
                return rev::curve::Evaluate(curve, normalized_time);
            };
            auto evaluate_trigger = [&](int track_index, float pulse_beats) {
                if (track_index < 0 || track_index >= editor->project->trigger_track_count || pulse_beats <= 0.0f) {
                    return 1.0f;
                }
                return rev::runtime::EvaluateTriggerPulse(
                    &editor->project->trigger_tracks[track_index], editor->current_time, pulse_beats);
            };
            auto add_effect = [&](int type, float intensity_value, float threshold_value,
                                  float radius_value, const float* color_value, float effect_time) {
                active_effects.push_back({type, intensity_value, threshold_value, radius_value,
                                          {color_value[0], color_value[1], color_value[2], color_value[3]}, effect_time});
            };
            for (int i = 0; i < active_scene->scene_layer_post_effect_count; ++i) {
                LayerPostEffect& effect = active_scene->scene_layer_post_effects[i];
                float effect_end = effect.end_time < 0.0f ? active_scene->duration : effect.end_time;
                if (!effect.enabled || effect.type < 0 || effect.type >= PostEffectCount ||
                    local_time < effect.start_time || local_time >= effect_end) continue;
                float curve_time = local_time - effect.start_time;
                float effect_color[4] = {
                    evaluate_curve(effect.curve_color_r, effect.color[0], curve_time),
                    evaluate_curve(effect.curve_color_g, effect.color[1], curve_time),
                    evaluate_curve(effect.curve_color_b, effect.color[2], curve_time),
                    evaluate_curve(effect.curve_color_a, effect.color[3], curve_time)};
                add_effect(effect.type,
                           evaluate_curve(effect.curve_intensity, effect.intensity, curve_time) *
                               evaluate_trigger(effect.trigger_track, effect.trigger_pulse_beats),
                           evaluate_curve(effect.curve_threshold, effect.threshold, curve_time),
                           evaluate_curve(effect.curve_radius, effect.radius, curve_time),
                           effect_color, local_time);
            }
            for (int i = 0; i < active_scene->post_effect_count; ++i) {
                PostEffect& effect = active_scene->post_effects[i];
                float effect_end = effect.end_time < 0.0f ? editor->project->total_duration : effect.end_time;
                if (!effect.enabled || effect.type < 0 || effect.type >= PostEffectCount ||
                    editor->current_time < effect.start_time || editor->current_time >= effect_end) continue;
                float curve_time = editor->current_time - effect.start_time;
                float effect_color[4] = {
                    evaluate_curve(effect.curve_color_r, effect.color[0], curve_time),
                    evaluate_curve(effect.curve_color_g, effect.color[1], curve_time),
                    evaluate_curve(effect.curve_color_b, effect.color[2], curve_time),
                    evaluate_curve(effect.curve_color_a, effect.color[3], curve_time)};
                add_effect(effect.type,
                           evaluate_curve(effect.curve_intensity, effect.intensity, curve_time) *
                               evaluate_trigger(effect.trigger_track, effect.trigger_pulse_beats),
                           evaluate_curve(effect.curve_threshold, effect.threshold, curve_time),
                           evaluate_curve(effect.curve_radius, effect.radius, curve_time),
                           effect_color, editor->current_time);
            }
            unsigned int source_texture = editor->preview_texture;
            unsigned int destination_fbo = editor->post_fbo;
            unsigned int destination_texture = editor->post_texture;
            rev::shader::Program* post_prog = (rev::shader::Program*)editor->post_shader;
            typedef void (*PFNGLACTIVETEXTUREPROC)(unsigned int texture);
            auto glActiveTexture_scene = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
            for (const ActivePostEffect& effect : active_effects) {
                glBindFramebuffer(0x8D40, destination_fbo);
                glViewport(0, 0, editor->preview_width, editor->preview_height);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                rev::shader::Use(post_prog);
                rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, "u_scene"), 0);
                rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, "u_history"), 1);
                rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, "u_unpremultiply_scene"), 0);
                rev::shader::SetVec2(post_prog, rev::shader::GetUniformLocation(post_prog, "u_resolution"),
                                     (float)editor->preview_width, (float)editor->preview_height);
                rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, "u_time"), effect.time);
                if (glActiveTexture_scene) glActiveTexture_scene(0x84C0);
                glBindTexture(GL_TEXTURE_2D, source_texture);
                if (glActiveTexture_scene) glActiveTexture_scene(0x84C1);
                glBindTexture(GL_TEXTURE_2D, editor->post_history_texture);
                for (int i = 0; i < PostEffectCount; ++i) {
                    char uniform_name[64];
                    bool selected = i == effect.type;
                    snprintf(uniform_name, sizeof(uniform_name), "u_enabled[%d]", i);
                    rev::shader::SetInt(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), selected ? 1 : 0);
                    snprintf(uniform_name, sizeof(uniform_name), "u_intensity[%d]", i);
                    rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), selected ? effect.intensity : 0.0f);
                    snprintf(uniform_name, sizeof(uniform_name), "u_threshold[%d]", i);
                    rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), selected ? effect.threshold : 0.0f);
                    snprintf(uniform_name, sizeof(uniform_name), "u_radius[%d]", i);
                    rev::shader::SetFloat(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name), selected ? effect.radius : 0.0f);
                    snprintf(uniform_name, sizeof(uniform_name), "u_color[%d]", i);
                    rev::shader::SetVec4(post_prog, rev::shader::GetUniformLocation(post_prog, uniform_name),
                                         selected ? effect.color[0] : 0.0f, selected ? effect.color[1] : 0.0f,
                                         selected ? effect.color[2] : 0.0f, selected ? effect.color[3] : 0.0f);
                }
                glDrawArrays(GL_TRIANGLES, 0, 3);
                source_texture = destination_texture;
                if (destination_fbo == editor->post_fbo) {
                    destination_fbo = editor->preview_fbo;
                    destination_texture = editor->preview_texture;
                } else {
                    destination_fbo = editor->post_fbo;
                    destination_texture = editor->post_texture;
                }
            }
            if (!active_effects.empty() && source_texture == editor->preview_texture) {
                typedef void (*PFNGLBLITFRAMEBUFFERPROC)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
                auto glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
                if (glBlitFramebuffer) {
                    glBindFramebuffer(0x8CA8, editor->preview_fbo);
                    glBindFramebuffer(0x8CA9, editor->post_fbo);
                    glBlitFramebuffer(0, 0, editor->preview_width, editor->preview_height,
                                      0, 0, editor->preview_width, editor->preview_height,
                                      0x00004000, 0x2600);
                }
                source_texture = editor->post_texture;
            }
            if (!active_effects.empty()) {
                editor->post_frame_rendered = true;
                typedef void (*PFNGLBLITFRAMEBUFFERPROC)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
                auto glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
                if (glBlitFramebuffer && editor->post_history_fbo) {
                    glBindFramebuffer(0x8CA8, editor->post_fbo);
                    glBindFramebuffer(0x8CA9, editor->post_history_fbo);
                    glBlitFramebuffer(0, 0, editor->preview_width, editor->preview_height,
                                      0, 0, editor->preview_width, editor->preview_height,
                                      0x00004000, 0x2600);
                }
            }
        }

    }

    // Unbind framebuffer (restore default)
    glBindFramebuffer(0x8D40, 0);
}

void UpdatePlayback(EditorContext* editor, float delta_time) {
    if (!editor) return;

    if (editor->playing) {
        editor->current_time += delta_time;
    }
    
    // Clamp to project duration (use 10s default if duration is 0)
    if (editor->project && editor->playing) {
        float max_duration = editor->project->total_duration;
        if (max_duration <= 0.0f) max_duration = 10.0f; // Default playback duration
        
        if (editor->current_time >= max_duration) {
            editor->current_time = max_duration;
            editor->playing = false; // Stop at end
        }
    }

    SyncEditorAudio(editor, delta_time);
}

void ReloadEditorAssets(EditorContext* editor) {
    if (!editor) return;

    // Images and text are loaded from disk during preview draws, but mesh imports are cached.
    // Clearing this cache forces the next preview frame to reload meshes/textures from files.
    for (int i = 0; i < editor->mesh_cache_count; ++i) {
        if (editor->mesh_cache[i].mesh) {
            rev::mesh::DestroyMesh((rev::mesh::Mesh*)editor->mesh_cache[i].mesh);
            editor->mesh_cache[i].mesh = nullptr;
        }
        editor->mesh_cache[i].path[0] = '\0';
        editor->mesh_cache[i].last_write_time = 0;
    }
    editor->mesh_cache_count = 0;

    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
              "Preview assets reloaded.", _TRUNCATE);
    editor->build_status_timer = 2.0f;
    printf("[Preview] Reloaded asset cache\n");
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
        // Mesh animations now driven by scene time, no need to reset them individually
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Assets")) {
        ReloadEditorAssets(editor);
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

    // Show playback mode and which music cue would be active at current preview time.
    const char* active_music_key = "(none)";
    if (editor->project->scene_count > 0) {
        float scene_start_time = 0.0f;
        float t = editor->current_time;
        for (int s = 0; s < editor->project->scene_count; ++s) {
            SceneBlock* scene = &editor->project->scenes[s];
            float scene_end_time = scene_start_time + scene->duration;
            bool in_scene = (s == editor->project->scene_count - 1)
                ? (t >= scene_start_time && t <= scene_end_time)
                : (t >= scene_start_time && t < scene_end_time);
            if (in_scene) {
                float local_t = t - scene_start_time;
                float latest_start = -1.0f;
                for (int i = 0; i < scene->music_cue_count; ++i) {
                    MusicCue* cue = &scene->music_cues[i];
                    float cue_end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                    if (local_t >= cue->cue_start && local_t <= cue_end) {
                        if (cue->cue_start >= latest_start) {
                            latest_start = cue->cue_start;
                            active_music_key = (cue->asset_key[0] != '\0') ? cue->asset_key : "(no key)";
                        }
                    }
                }
                break;
            }
            scene_start_time = scene_end_time;
        }
    }

    ImGui::Text("Playback Mode: Intro %s | Music %s",
        editor->project->loop_intro ? "Loop" : "One-shot",
        editor->project->loop_music ? "Loop" : "One-shot");
    ImGui::Text("Music Transition: %s",
        editor->project->music_persist_across_scenes ? "Carry across scenes (unless track changes)" : "Scene-based restart");
    ImGui::Text("Active Music Cue @ %.2fs: %s", editor->current_time, active_music_key);
    
    ImGui::Separator();
    
    // Display preview texture
    unsigned int preview_display_texture = editor->post_frame_rendered
        ? editor->post_texture
        : editor->preview_texture;
    if (preview_display_texture) {
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
        
        // FBO stays at 1920x1080 — ImGui scales the texture to fit the panel.
        // All size formulas (tex_pixels / preview_width * 2) produce the same
        // proportional result as the final product at every panel size.
        ImGui::Image((ImTextureID)(intptr_t)preview_display_texture, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
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
    
    // Default palette - visible colors (shader ID 0 can be set to black for fades)
    cue->palette_low = {0.1f, 0.3f, 0.8f};    // Blue
    cue->palette_mid = {0.8f, 0.4f, 0.2f};    // Orange
    cue->palette_high = {1.0f, 0.9f, 0.7f};   // Warm white
    
    // Default parameters
    cue->speed = 1.0f;
    cue->intensity = 1.0f;
    cue->warp = 0.5f;
    cue->exposure_base = 0.76f;
    cue->exposure_ramp = 0.02f;
    cue->fade_base = 1.0f;  // Full opacity (shaders use this as alpha channel)
    cue->fade_ramp = -0.04f;
    cue->position_x = 0.0f;
    cue->position_y = 0.0f;
    cue->position_z = 0.0f;
    cue->rotation_x = 0.0f;
    cue->rotation_y = 0.0f;
    cue->rotation_z = 0.0f;
    cue->motion_x = 0.0f;
    cue->motion_y = 0.0f;
    cue->motion_z = 0.0f;
    cue->noise.enabled = 0;
    cue->noise.type = 0;
    cue->noise.scale = 3.0f;
    cue->noise.strength = 1.0f;
    cue->noise.octaves = 4.0f;
    cue->noise.lacunarity = 2.0f;
    cue->noise.gain = 0.5f;
    cue->noise.warp = 0.0f;
    cue->noise.speed_x = 0.0f;
    cue->noise.speed_y = 0.0f;
    cue->noise.seed = 0.0f;
    cue->noise.contrast = 1.0f;
    
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
    cue->curve_palette_low_r = -1;
    cue->curve_palette_low_g = -1;
    cue->curve_palette_low_b = -1;
    cue->curve_palette_mid_r = -1;
    cue->curve_palette_mid_g = -1;
    cue->curve_palette_mid_b = -1;
    cue->curve_palette_high_r = -1;
    cue->curve_palette_high_g = -1;
    cue->curve_palette_high_b = -1;
    cue->curve_opacity = -1;
    cue->curve_exposure_ramp = -1;
    cue->curve_fade_ramp = -1;
}

}  // namespace editor
}  // namespace rev
