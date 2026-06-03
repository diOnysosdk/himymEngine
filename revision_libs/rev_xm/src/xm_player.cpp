#include "rev_xm.h"
#include <cstring>
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
    
    return player;
#else
    // Stub implementation
    (void)xm_data; (void)xm_size;  // Suppress unused warnings
    
    Player* player = new Player();
    player->context = nullptr;
    player->sample_rate = sample_rate;
    player->buffer_size = 2048;
    player->buffer = new float[player->buffer_size];
    
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
        return;
    }
#endif
    
    // Stub: fill with silence
    memset(output, 0, frame_count * 2 * sizeof(float));  // Stereo
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
