#pragma once

#include <cstddef>
#include <cstdint>

namespace rev {
namespace pixel {

constexpr int kMaxPaletteColors = 16;
constexpr uint16_t kPixelFormatVersion = 1;

struct PixelColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct PixelAnimation {
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint8_t palette_count;
    float fps;
    PixelColor palette[kMaxPaletteColors];
    uint8_t* pixels;
};

struct FireParameters {
    int cooling;
    int turbulence;
    int wind;
    int seed_intensity;
    uint32_t random_seed;
};

PixelAnimation* CreateAnimation(uint16_t width, uint16_t height, uint16_t frame_count);
void DestroyAnimation(PixelAnimation* animation);

bool SetPixel(PixelAnimation* animation, int frame, int x, int y, uint8_t palette_index);
uint8_t GetPixel(const PixelAnimation* animation, int frame, int x, int y);

bool SaveAnimation(const PixelAnimation* animation, const char* path);
PixelAnimation* LoadAnimation(const char* path);
PixelAnimation* LoadAnimationFromMemory(const unsigned char* data, size_t size);

bool GenerateFire(PixelAnimation* animation, const FireParameters& parameters);

} // namespace pixel
} // namespace rev
