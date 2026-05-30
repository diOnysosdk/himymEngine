#include "rev_xm.h"
#include <cstring>

// NOTE: This is a stub implementation
// libxm requires GCC/Clang (C23) and does not compile with MSVC
// To enable real XM playback:
// 1. Use MinGW-w64 or Clang-cl compiler
// 2. Uncomment add_subdirectory in CMakeLists.txt
// 3. Update this implementation to use libxm API

namespace rev {
namespace xm {

Player* CreatePlayer(const void* xm_data, size_t xm_size, int sample_rate) {
    // Stub implementation
    (void)xm_data; (void)xm_size;  // Suppress unused warnings
    
    Player* player = new Player();
    player->context = nullptr;
    player->sample_rate = sample_rate;
    player->buffer_size = 2048;
    player->buffer = new float[player->buffer_size];
    
    return player;
}

void DestroyPlayer(Player* player) {
    if (!player) return;
    
    delete[] player->buffer;
    delete player;
}

void Update(Player* player, float* output, int frame_count) {
    if (!player || !output) return;
    
    // Stub: fill with silence
    memset(output, 0, frame_count * 2 * sizeof(float));  // Stereo
}

void SetPosition(Player* player, int pattern, int row) {
    if (!player) return;
    (void)pattern; (void)row;  // Suppress unused warnings
    // Stub implementation
}

int GetPosition(Player* player) {
    if (!player) return 0;
    return 0;  // Stub
}

bool IsFinished(Player* player) {
    if (!player) return true;
    return false;  // Stub
}

int GetPatternCount(Player* player) {
    if (!player) return 0;
    return 0;  // Stub
}

int GetChannelCount(Player* player) {
    if (!player) return 0;
    return 0;  // Stub
}

float GetDuration(Player* player) {
    if (!player) return 0.0f;
    return 0.0f;  // Stub
}

}  // namespace xm
}  // namespace rev
