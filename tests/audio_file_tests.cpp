#include "rev_audio_file.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static void Put16(std::vector<unsigned char>& bytes, size_t offset, unsigned value) {
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1] = (unsigned char)(value >> 8);
}

static void Put32(std::vector<unsigned char>& bytes, size_t offset, unsigned value) {
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1] = (unsigned char)(value >> 8);
    bytes[offset + 2] = (unsigned char)(value >> 16);
    bytes[offset + 3] = (unsigned char)(value >> 24);
}

int main(int argc, char** argv) {
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
        rev::audio_file::Player* player = rev::audio_file::CreatePlayer(bytes.data(), bytes.size());
        if (!player) { std::fprintf(stderr, "Audio-file decode failed\n"); return 1; }
        float output[2048 * 2] = {};
        rev::audio_file::Update(player, output, 2048);
        bool audible = false;
        for (float value : output) if (fabsf(value) > 0.01f) { audible = true; break; }
        rev::audio_file::DestroyPlayer(player);
        if (!audible) { std::fprintf(stderr, "Decoded audio is silent\n"); return 1; }
        std::printf("[audio_file_tests] external decode PASS\n");
        return 0;
    }
    const unsigned sample_rate = 48000;
    const unsigned frame_count = 480;
    const unsigned data_size = frame_count * 2 * sizeof(short);
    std::vector<unsigned char> wav(44 + data_size, 0);
    memcpy(wav.data(), "RIFF", 4); Put32(wav, 4, 36 + data_size);
    memcpy(wav.data() + 8, "WAVEfmt ", 8); Put32(wav, 16, 16);
    Put16(wav, 20, 1); Put16(wav, 22, 2); Put32(wav, 24, sample_rate);
    Put32(wav, 28, sample_rate * 4); Put16(wav, 32, 4); Put16(wav, 34, 16);
    memcpy(wav.data() + 36, "data", 4); Put32(wav, 40, data_size);
    short* pcm = (short*)(wav.data() + 44);
    for (unsigned i = 0; i < frame_count; ++i) {
        short value = (short)(sinf((float)i * 0.1f) * 12000.0f);
        pcm[i * 2] = value; pcm[i * 2 + 1] = value;
    }

    rev::audio_file::Player* player = rev::audio_file::CreatePlayer(wav.data(), wav.size());
    if (!player) { std::fprintf(stderr, "WAV decode failed\n"); return 1; }
    float output[frame_count * 2] = {};
    rev::audio_file::Update(player, output, frame_count);
    bool audible = false;
    for (float value : output) if (fabsf(value) > 0.01f) { audible = true; break; }
    const float duration = rev::audio_file::GetDuration(player);
    const bool finished = rev::audio_file::IsFinished(player);
    rev::audio_file::DestroyPlayer(player);
    if (!audible || duration < 0.009f || duration > 0.011f || !finished) {
        std::fprintf(stderr, "WAV playback contract failed\n"); return 1;
    }
    std::printf("[audio_file_tests] PASS\n");
    return 0;
}
