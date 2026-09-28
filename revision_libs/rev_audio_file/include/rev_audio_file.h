#pragma once

#include <cstddef>
#include <mutex>
#include "rev_runtime.h"

namespace rev {
namespace audio_file {

struct Player {
    float* samples;
    size_t frame_count;
    size_t frame_position;
    int sample_rate;
    rev::runtime::AudioEffects effects;
    float compressor_gain;
    float eq_low[2];
    float eq_high[2];
    std::mutex mutex;
};

Player* CreatePlayer(const void* data, size_t size, int sample_rate = 48000);
void DestroyPlayer(Player* player);
void Update(Player* player, float* output, int frame_count);
void SetAudioEffects(Player* player, const rev::runtime::AudioEffects* effects);
void SetPositionSeconds(Player* player, float seconds);
bool IsFinished(Player* player);
float GetDuration(Player* player);

} // namespace audio_file
} // namespace rev
