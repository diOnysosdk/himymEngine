#pragma once

#include <cstddef>
#ifdef HIMYM_CRINKLER_BUILD
#include <windows.h>
#else
#include <mutex>
#endif
#include "rev_runtime.h"

namespace rev {
namespace xm {

#ifdef HIMYM_CRINKLER_BUILD
struct PlayerMutex {
    CRITICAL_SECTION section;
    PlayerMutex() { InitializeCriticalSection(&section); }
    ~PlayerMutex() { DeleteCriticalSection(&section); }
    void lock() { EnterCriticalSection(&section); }
    void unlock() { LeaveCriticalSection(&section); }
    PlayerMutex(const PlayerMutex&) = delete;
    PlayerMutex& operator=(const PlayerMutex&) = delete;
};
#else
using PlayerMutex = std::mutex;
#endif

// Opaque XM player handle
struct Player {
    void* context;
    float* buffer;
    int sample_rate;
    int buffer_size;
    rev::runtime::AudioEffects effects;
    float compressor_gain;
    float eq_low[2];
    float eq_high[2];
    PlayerMutex mutex;
};

// Lifecycle
Player* CreatePlayer(const void* xm_data, size_t xm_size, int sample_rate = 48000);
void DestroyPlayer(Player* player);

// Playback
void Update(Player* player, float* output, int frame_count);
void SetAudioEffects(Player* player, const rev::runtime::AudioEffects* effects);
void SetPosition(Player* player, int pattern, int row);
int GetPosition(Player* player);
bool IsFinished(Player* player);

// Info
int GetPatternCount(Player* player);
int GetChannelCount(Player* player);
float GetDuration(Player* player);

}  // namespace xm
}  // namespace rev
