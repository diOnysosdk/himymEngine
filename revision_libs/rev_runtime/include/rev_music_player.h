#pragma once

#include <cstddef>
#include <cstring>
#include "rev_runtime.h"

#ifndef REV_MUSIC_USE_XM
#define REV_MUSIC_USE_XM 1
#endif
#ifndef REV_MUSIC_USE_AUDIO_FILE
#define REV_MUSIC_USE_AUDIO_FILE 1
#endif

#if REV_MUSIC_USE_XM
#include "rev_xm.h"
#endif
#if REV_MUSIC_USE_AUDIO_FILE
#include "rev_audio_file.h"
#endif

namespace rev {
namespace music {

enum PlayerKind { PlayerNone, PlayerXm, PlayerAudioFile };
struct Player { PlayerKind kind; void* implementation; };

inline bool EndsWithInsensitive(const char* value, const char* suffix) {
    if (!value || !suffix) return false;
    size_t value_length = strlen(value), suffix_length = strlen(suffix);
    if (suffix_length > value_length) return false;
    const char* tail = value + value_length - suffix_length;
    for (size_t i = 0; i < suffix_length; ++i) {
        char a = tail[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

inline bool IsXmName(const char* name) { return EndsWithInsensitive(name, ".xm"); }
inline bool IsAudioFileName(const char* name) {
    return EndsWithInsensitive(name, ".wav") || EndsWithInsensitive(name, ".wave") ||
           EndsWithInsensitive(name, ".mp3");
}

inline Player* CreatePlayer(const void* data, size_t size, const char* asset_name, int sample_rate = 48000) {
    if (!data || !size) return nullptr;
    PlayerKind preferred = IsXmName(asset_name) ? PlayerXm : PlayerAudioFile;
    if (size >= 17 && memcmp(data, "Extended Module: ", 17) == 0) preferred = PlayerXm;
    void* implementation = nullptr;
#if REV_MUSIC_USE_XM
    if (preferred == PlayerXm) implementation = rev::xm::CreatePlayer(data, size, sample_rate);
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (preferred == PlayerAudioFile) implementation = rev::audio_file::CreatePlayer(data, size, sample_rate);
#endif
    if (!implementation) return nullptr;
    Player* player = new Player{preferred, implementation};
    return player;
}

inline void DestroyPlayer(Player* player) {
    if (!player) return;
#if REV_MUSIC_USE_XM
    if (player->kind == PlayerXm) rev::xm::DestroyPlayer((rev::xm::Player*)player->implementation);
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (player->kind == PlayerAudioFile) rev::audio_file::DestroyPlayer((rev::audio_file::Player*)player->implementation);
#endif
    delete player;
}

inline void Update(Player* player, float* output, int frame_count) {
    if (!player || !output) return;
#if REV_MUSIC_USE_XM
    if (player->kind == PlayerXm) { rev::xm::Update((rev::xm::Player*)player->implementation, output, frame_count); return; }
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (player->kind == PlayerAudioFile) { rev::audio_file::Update((rev::audio_file::Player*)player->implementation, output, frame_count); return; }
#endif
    memset(output, 0, (size_t)frame_count * 2 * sizeof(float));
}

inline void SetAudioEffects(Player* player, const rev::runtime::AudioEffects* effects) {
    if (!player || !effects) return;
#if REV_MUSIC_USE_XM
    if (player->kind == PlayerXm) rev::xm::SetAudioEffects((rev::xm::Player*)player->implementation, effects);
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (player->kind == PlayerAudioFile) rev::audio_file::SetAudioEffects((rev::audio_file::Player*)player->implementation, effects);
#endif
}

inline void SetPositionSeconds(Player* player, float seconds) {
    if (!player) return;
#if REV_MUSIC_USE_XM
    if (player->kind == PlayerXm && seconds <= 0.0f) rev::xm::SetPosition((rev::xm::Player*)player->implementation, 0, 0);
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (player->kind == PlayerAudioFile) rev::audio_file::SetPositionSeconds((rev::audio_file::Player*)player->implementation, seconds);
#endif
}

inline bool IsFinished(Player* player) {
    if (!player) return true;
#if REV_MUSIC_USE_XM
    if (player->kind == PlayerXm) return rev::xm::IsFinished((rev::xm::Player*)player->implementation);
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (player->kind == PlayerAudioFile) return rev::audio_file::IsFinished((rev::audio_file::Player*)player->implementation);
#endif
    return true;
}

inline float GetDuration(Player* player) {
    if (!player) return 0.0f;
#if REV_MUSIC_USE_XM
    if (player->kind == PlayerXm) return rev::xm::GetDuration((rev::xm::Player*)player->implementation);
#endif
#if REV_MUSIC_USE_AUDIO_FILE
    if (player->kind == PlayerAudioFile) return rev::audio_file::GetDuration((rev::audio_file::Player*)player->implementation);
#endif
    return 0.0f;
}

} // namespace music
} // namespace rev
