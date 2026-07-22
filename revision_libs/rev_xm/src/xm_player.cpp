#include "rev_xm.h"
#include <cstring>
#include <cmath>
#include <mutex>

#ifdef _MSC_VER
#include <malloc.h>  // For alloca on MSVC
#else
#include <alloca.h>
#endif

#ifdef REV_XM_ENABLED
extern "C" {
#include <xm.h>
}
#endif

namespace rev {
namespace xm {

static float Clamp(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

static float DbToGain(float db) {
    return powf(10.0f, db * 0.05f);
}

static void ProcessEffects(Player* player, float* output, int frame_count) {
    const rev::runtime::AudioEffects& fx = player->effects;
    if ((!fx.gain_enabled && !fx.compressor_enabled && !fx.widener_enabled && !fx.eq_enabled) ||
        frame_count <= 0) return;

    const float gain = fx.gain_enabled ? DbToGain(Clamp(fx.gain_db, -24.0f, 24.0f)) : 1.0f;
    const float threshold = Clamp(fx.compressor_threshold, 0.01f, 1.0f);
    const float ratio = Clamp(fx.compressor_ratio, 1.0f, 20.0f);
    const float attack = expf(-1.0f / (Clamp(fx.compressor_attack, 0.001f, 1.0f) * player->sample_rate));
    const float release = expf(-1.0f / (Clamp(fx.compressor_release, 0.005f, 2.0f) * player->sample_rate));
    const float low_alpha = 1.0f - expf(-6.2831853f * 180.0f / player->sample_rate);
    const float high_alpha = 1.0f - expf(-6.2831853f * 3500.0f / player->sample_rate);
    const float low_gain = fx.eq_enabled ? DbToGain(Clamp(fx.eq_low_db, -18.0f, 18.0f)) : 1.0f;
    const float mid_gain = fx.eq_enabled ? DbToGain(Clamp(fx.eq_mid_db, -18.0f, 18.0f)) : 1.0f;
    const float high_gain = fx.eq_enabled ? DbToGain(Clamp(fx.eq_high_db, -18.0f, 18.0f)) : 1.0f;
    const float width = fx.widener_enabled ? Clamp(fx.widener_amount, 0.0f, 2.0f) : 1.0f;

    for (int frame = 0; frame < frame_count; ++frame) {
        float left = output[frame * 2] * gain;
        float right = output[frame * 2 + 1] * gain;
        if (fx.compressor_enabled) {
            const float level = fmaxf(fabsf(left), fabsf(right));
            const float detector = level > player->compressor_gain ? attack : release;
            player->compressor_gain = detector * player->compressor_gain + (1.0f - detector) * level;
            if (player->compressor_gain > threshold) {
                const float compressed = powf(threshold / player->compressor_gain, 1.0f - 1.0f / ratio);
                left *= compressed;
                right *= compressed;
            }
        }
        if (fx.eq_enabled) {
            player->eq_low[0] += low_alpha * (left - player->eq_low[0]);
            player->eq_low[1] += low_alpha * (right - player->eq_low[1]);
            player->eq_high[0] += high_alpha * (left - player->eq_high[0]);
            player->eq_high[1] += high_alpha * (right - player->eq_high[1]);
            left = player->eq_low[0] * low_gain + (left - player->eq_low[0] - (player->eq_high[0] - player->eq_low[0])) * mid_gain +
                   (player->eq_high[0] - player->eq_low[0]) * high_gain;
            right = player->eq_low[1] * low_gain + (right - player->eq_low[1] - (player->eq_high[1] - player->eq_low[1])) * mid_gain +
                    (player->eq_high[1] - player->eq_low[1]) * high_gain;
        }
        if (fx.widener_enabled) {
            const float mid = (left + right) * 0.5f;
            const float side = (left - right) * 0.5f * width;
            left = mid + side;
            right = mid - side;
        }
        output[frame * 2] = Clamp(left, -1.0f, 1.0f);
        output[frame * 2 + 1] = Clamp(right, -1.0f, 1.0f);
    }
}

Player* CreatePlayer(const void* xm_data, size_t xm_size, int sample_rate) {
#ifdef REV_XM_ENABLED
    // Prescan module to determine memory requirements
    xm_prescan_data_t* prescan = (xm_prescan_data_t*)alloca(XM_PRESCAN_DATA_SIZE);
    if (!xm_prescan_module((const char*)xm_data, (uint32_t)xm_size, prescan)) {
        return nullptr;  // Failed to prescan
    }
    
    // Allocate memory for context
    uint32_t ctx_size = xm_size_for_context(prescan);
    char* ctx_memory = new char[ctx_size];
    
    // Create XM context
    xm_context_t* ctx = xm_create_context(ctx_memory, prescan, 
                                          (const char*)xm_data, (uint32_t)xm_size);
    if (!ctx) {
        delete[] ctx_memory;
        return nullptr;
    }
    
    // Set sample rate
    xm_set_sample_rate(ctx, (uint16_t)sample_rate);
    
    // Create player
    Player* player = new Player();
    player->context = ctx;
    player->sample_rate = sample_rate;
    player->buffer_size = 2048;
    player->buffer = new float[player->buffer_size];
    player->compressor_gain = 0.0f;
    
    return player;
#else
    // Stub implementation
    (void)xm_data; (void)xm_size;  // Suppress unused warnings
    
    Player* player = new Player();
    player->context = nullptr;
    player->sample_rate = sample_rate;
    player->buffer_size = 2048;
    player->buffer = new float[player->buffer_size];
    player->compressor_gain = 0.0f;
    
    return player;
#endif
}

void DestroyPlayer(Player* player) {
    if (!player) return;

    {
        std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
        if (player->context) {
            delete[] (char*)player->context;  // Free the context memory
            player->context = nullptr;
        }
#endif

        delete[] player->buffer;
        player->buffer = nullptr;
    }

    delete player;
}

void Update(Player* player, float* output, int frame_count) {
    if (!player || !output) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        xm_generate_samples((xm_context_t*)player->context, output, (uint16_t)frame_count);
        ProcessEffects(player, output, frame_count);
        return;
    }
#endif
    
    // Stub: fill with silence
    memset(output, 0, frame_count * 2 * sizeof(float));  // Stereo
}

void SetAudioEffects(Player* player, const rev::runtime::AudioEffects* effects) {
    if (!player || !effects) return;
    std::lock_guard<std::mutex> lock(player->mutex);
    player->effects = *effects;
    player->compressor_gain = 0.0f;
    player->eq_low[0] = player->eq_low[1] = 0.0f;
    player->eq_high[0] = player->eq_high[1] = 0.0f;
}

void SetPosition(Player* player, int pattern, int row) {
    if (!player) return;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        xm_seek((xm_context_t*)player->context, (uint8_t)pattern, (uint8_t)row, 0);
        return;
    }
#endif
    
    (void)pattern; (void)row;  // Suppress unused warnings
}

int GetPosition(Player* player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        return (int)xm_get_loop_count((const xm_context_t*)player->context);
    }
#endif
    
    return 0;
}

bool IsFinished(Player* player) {
    if (!player) return true;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        return xm_get_loop_count((const xm_context_t*)player->context) > 0;
    }
#endif
    
    return false;
}

int GetPatternCount(Player* player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        return (int)xm_get_number_of_patterns((const xm_context_t*)player->context);
    }
#endif
    
    return 0;
}

int GetChannelCount(Player* player) {
    if (!player) return 0;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        return (int)xm_get_number_of_channels((const xm_context_t*)player->context);
    }
#endif
    
    return 0;
}

float GetDuration(Player* player) {
    if (!player) return 0.0f;

    std::lock_guard<std::mutex> lock(player->mutex);
    
#ifdef REV_XM_ENABLED
    if (player->context) {
        return xm_get_module_length((const xm_context_t*)player->context);
    }
#endif
    
    return 0.0f;
}

}  // namespace xm
}  // namespace rev
