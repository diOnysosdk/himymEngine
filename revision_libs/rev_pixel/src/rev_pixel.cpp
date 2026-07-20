#include "rev_pixel.h"

#include <cstdio>
#include <cstring>

namespace rev {
namespace pixel {

namespace {

struct FileHeader {
    char magic[4];
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint8_t palette_count;
    uint8_t reserved;
    float fps;
};

static size_t PixelCount(const PixelAnimation* animation) {
    if (!animation) return 0;
    return static_cast<size_t>(animation->width) * animation->height * animation->frame_count;
}

static uint32_t NextRandom(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

} // namespace

PixelAnimation* CreateAnimation(uint16_t width, uint16_t height, uint16_t frame_count) {
    if (width == 0 || height == 0 || frame_count == 0) return nullptr;

    PixelAnimation* animation = new PixelAnimation{};
    animation->width = width;
    animation->height = height;
    animation->frame_count = frame_count;
    animation->palette_count = kMaxPaletteColors;
    animation->fps = 12.0f;
    animation->pixels = new uint8_t[PixelCount(animation)]{};

    animation->palette[0] = {0, 0, 0, 0};
    for (int i = 1; i < kMaxPaletteColors; ++i) {
        animation->palette[i] = {255, 255, 255, 255};
    }
    return animation;
}

void DestroyAnimation(PixelAnimation* animation) {
    if (!animation) return;
    delete[] animation->pixels;
    delete animation;
}

bool SetPixel(PixelAnimation* animation, int frame, int x, int y, uint8_t palette_index) {
    if (!animation || !animation->pixels || frame < 0 || frame >= animation->frame_count ||
        x < 0 || x >= animation->width || y < 0 || y >= animation->height ||
        palette_index >= kMaxPaletteColors) return false;

    size_t frame_size = static_cast<size_t>(animation->width) * animation->height;
    animation->pixels[static_cast<size_t>(frame) * frame_size + static_cast<size_t>(y) * animation->width + x] = palette_index;
    return true;
}

uint8_t GetPixel(const PixelAnimation* animation, int frame, int x, int y) {
    if (!animation || !animation->pixels || frame < 0 || frame >= animation->frame_count ||
        x < 0 || x >= animation->width || y < 0 || y >= animation->height) return 0;

    size_t frame_size = static_cast<size_t>(animation->width) * animation->height;
    return animation->pixels[static_cast<size_t>(frame) * frame_size + static_cast<size_t>(y) * animation->width + x];
}

bool SaveAnimation(const PixelAnimation* animation, const char* path) {
    if (!animation || !animation->pixels || !path || animation->width == 0 ||
        animation->height == 0 || animation->frame_count == 0 ||
        animation->palette_count == 0 || animation->palette_count > kMaxPaletteColors) return false;

    FILE* file = nullptr;
    fopen_s(&file, path, "wb");
    if (!file) return false;

    FileHeader header = {{'H', 'P', 'I', 'X'}, kPixelFormatVersion,
                         animation->width, animation->height,
                         animation->frame_count, animation->palette_count, 0,
                         animation->fps};
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
              fwrite(animation->palette, sizeof(PixelColor), animation->palette_count, file) == animation->palette_count &&
              fwrite(animation->pixels, 1, PixelCount(animation), file) == PixelCount(animation);
    fclose(file);
    return ok;
}

PixelAnimation* LoadAnimation(const char* path) {
    if (!path) return nullptr;

    FILE* file = nullptr;
    fopen_s(&file, path, "rb");
    if (!file) return nullptr;

    FileHeader header = {};
    bool ok = fread(&header, sizeof(header), 1, file) == 1 &&
              memcmp(header.magic, "HPIX", 4) == 0 &&
              header.version == kPixelFormatVersion &&
              header.width > 0 && header.height > 0 && header.frame_count > 0 &&
              header.palette_count > 0 && header.palette_count <= kMaxPaletteColors;
    if (!ok) {
        fclose(file);
        return nullptr;
    }
    PixelAnimation* animation = CreateAnimation(header.width, header.height, header.frame_count);
    if (!animation) {
        fclose(file);
        return nullptr;
    }
    animation->palette_count = header.palette_count;
    animation->fps = header.fps;
    ok = fread(animation->palette, sizeof(PixelColor), animation->palette_count, file) == animation->palette_count &&
         fread(animation->pixels, 1, PixelCount(animation), file) == PixelCount(animation);
    fclose(file);
    if (!ok) {
        DestroyAnimation(animation);
        return nullptr;
    }
    return animation;
}

PixelAnimation* LoadAnimationFromMemory(const unsigned char* data, size_t size) {
    if (!data || size < sizeof(FileHeader)) return nullptr;

    FileHeader header = {};
    memcpy(&header, data, sizeof(header));
    if (memcmp(header.magic, "HPIX", 4) != 0 ||
        header.version != kPixelFormatVersion || header.width == 0 ||
        header.height == 0 || header.frame_count == 0 || header.palette_count == 0 ||
        header.palette_count > kMaxPaletteColors) return nullptr;

    size_t frame_bytes = static_cast<size_t>(header.width) * header.height * header.frame_count;
    size_t required = sizeof(FileHeader) +
                      static_cast<size_t>(header.palette_count) * sizeof(PixelColor) + frame_bytes;
    if (required > size) return nullptr;

    PixelAnimation* animation = CreateAnimation(header.width, header.height, header.frame_count);
    if (!animation) return nullptr;
    animation->palette_count = header.palette_count;
    animation->fps = header.fps;
    const unsigned char* palette_data = data + sizeof(FileHeader);
    memcpy(animation->palette, palette_data,
           static_cast<size_t>(animation->palette_count) * sizeof(PixelColor));
    memcpy(animation->pixels,
           palette_data + static_cast<size_t>(animation->palette_count) * sizeof(PixelColor),
           frame_bytes);
    return animation;
}

bool GenerateFire(PixelAnimation* animation, const FireParameters& parameters) {
    if (!animation || !animation->pixels || animation->width == 0 || animation->height < 2 ||
        animation->frame_count == 0 || animation->palette_count < 2) return false;

    const int width = animation->width;
    const int height = animation->height;
    const size_t plane_size = static_cast<size_t>(width) * height;
    uint8_t* heat = new uint8_t[plane_size]{};
    uint32_t random_state = parameters.random_seed ? parameters.random_seed : 1u;

    for (uint16_t frame = 0; frame < animation->frame_count; ++frame) {
        for (int x = 0; x < width; ++x) {
            int variation = parameters.turbulence > 0
                ? static_cast<int>(NextRandom(&random_state) % static_cast<uint32_t>(parameters.turbulence + 1))
                : 0;
            int seeded = parameters.seed_intensity - variation;
            heat[static_cast<size_t>(height - 1) * width + x] = static_cast<uint8_t>(seeded < 0 ? 0 : seeded > 255 ? 255 : seeded);
        }

        for (int y = height - 2; y >= 0; --y) {
            for (int x = 0; x < width; ++x) {
                int source_x = x + parameters.wind;
                if (source_x < 0) source_x = 0;
                if (source_x >= width) source_x = width - 1;
                int left_x = source_x > 0 ? source_x - 1 : source_x;
                int right_x = source_x + 1 < width ? source_x + 1 : source_x;
                int below = heat[static_cast<size_t>(y + 1) * width + source_x];
                int below_left = heat[static_cast<size_t>(y + 1) * width + left_x];
                int below_right = heat[static_cast<size_t>(y + 1) * width + right_x];
                int two_below = y + 2 < height ? heat[static_cast<size_t>(y + 2) * width + source_x] : below;
                int value = (below_left + below + below_right + two_below) / 4 - parameters.cooling;
                if (value < 0) value = 0;
                heat[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(value);
            }
        }

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t value = heat[static_cast<size_t>(y) * width + x];
                int palette_index = 1 + (static_cast<int>(value) * (animation->palette_count - 1)) / 255;
                SetPixel(animation, frame, x, y, static_cast<uint8_t>(palette_index));
            }
        }
    }

    delete[] heat;
    return true;
}

} // namespace pixel
} // namespace rev
