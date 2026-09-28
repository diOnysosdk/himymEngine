#include "rev_audio_file.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace rev {
namespace audio_file {

static float Clamp(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

static float DbToGain(float db) { return powf(10.0f, db * 0.05f); }

static void ProcessEffects(Player* player, float* output, int frame_count) {
    const rev::runtime::AudioEffects& fx = player->effects;
    if ((!fx.gain_enabled && !fx.compressor_enabled && !fx.widener_enabled && !fx.eq_enabled) || frame_count <= 0) return;
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
                left *= compressed; right *= compressed;
            }
        }
        if (fx.eq_enabled) {
            player->eq_low[0] += low_alpha * (left - player->eq_low[0]);
            player->eq_low[1] += low_alpha * (right - player->eq_low[1]);
            player->eq_high[0] += high_alpha * (left - player->eq_high[0]);
            player->eq_high[1] += high_alpha * (right - player->eq_high[1]);
            left = player->eq_low[0] * low_gain + (left - player->eq_high[0]) * mid_gain + (player->eq_high[0] - player->eq_low[0]) * high_gain;
            right = player->eq_low[1] * low_gain + (right - player->eq_high[1]) * mid_gain + (player->eq_high[1] - player->eq_low[1]) * high_gain;
        }
        if (fx.widener_enabled) {
            const float mid = (left + right) * 0.5f;
            const float side = (left - right) * 0.5f * width;
            left = mid + side; right = mid - side;
        }
        output[frame * 2] = Clamp(left, -1.0f, 1.0f);
        output[frame * 2 + 1] = Clamp(right, -1.0f, 1.0f);
    }
}

template <typename T> static void Release(T*& value) { if (value) { value->Release(); value = nullptr; } }

static bool DecodeWithMediaFoundation(const void* data, size_t size, int sample_rate, std::vector<float>* samples) {
    if (!data || size == 0 || size > ULONG_MAX || !samples) return false;
    HRESULT co_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize_com = SUCCEEDED(co_result);
    if (FAILED(co_result) && co_result != RPC_E_CHANGED_MODE) return false;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { if (uninitialize_com) CoUninitialize(); return false; }

    IStream* stream = SHCreateMemStream((const BYTE*)data, (UINT)size);
    IMFByteStream* byte_stream = nullptr;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* type = nullptr;
    bool ok = stream && SUCCEEDED(MFCreateMFByteStreamOnStream(stream, &byte_stream)) &&
              SUCCEEDED(MFCreateSourceReaderFromByteStream(byte_stream, nullptr, &reader)) &&
              SUCCEEDED(MFCreateMediaType(&type));
    if (ok) {
        ok = SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
             SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float)) &&
             SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)) &&
             SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)sample_rate)) &&
             SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type));
    }
    while (ok) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) { ok = false; Release(sample); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { Release(sample); break; }
        if (sample) {
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* bytes = nullptr; DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &length))) {
                    size_t old_size = samples->size();
                    samples->resize(old_size + length / sizeof(float));
                    memcpy(samples->data() + old_size, bytes, length);
                    buffer->Unlock();
                }
            }
            Release(buffer); Release(sample);
        }
    }
    Release(type); Release(reader); Release(byte_stream); Release(stream);
    MFShutdown();
    if (uninitialize_com) CoUninitialize();
    return ok && !samples->empty();
}

Player* CreatePlayer(const void* data, size_t size, int sample_rate) {
    std::vector<float> decoded;
    if (!DecodeWithMediaFoundation(data, size, sample_rate, &decoded) || decoded.size() < 2) return nullptr;
    Player* player = new Player{};
    player->samples = new float[decoded.size()];
    memcpy(player->samples, decoded.data(), decoded.size() * sizeof(float));
    player->frame_count = decoded.size() / 2;
    player->sample_rate = sample_rate;
    return player;
}

void DestroyPlayer(Player* player) { if (!player) return; delete[] player->samples; delete player; }

void Update(Player* player, float* output, int frame_count) {
    if (!player || !output || frame_count <= 0) return;
    std::lock_guard<std::mutex> lock(player->mutex);
    size_t available = player->frame_position < player->frame_count ? player->frame_count - player->frame_position : 0;
    size_t copy_frames = (std::min)(available, (size_t)frame_count);
    if (copy_frames) memcpy(output, player->samples + player->frame_position * 2, copy_frames * 2 * sizeof(float));
    if (copy_frames < (size_t)frame_count) memset(output + copy_frames * 2, 0, ((size_t)frame_count - copy_frames) * 2 * sizeof(float));
    player->frame_position += copy_frames;
    ProcessEffects(player, output, frame_count);
}

void SetAudioEffects(Player* player, const rev::runtime::AudioEffects* effects) {
    if (!player || !effects) return;
    std::lock_guard<std::mutex> lock(player->mutex);
    player->effects = *effects;
}

void SetPositionSeconds(Player* player, float seconds) {
    if (!player) return;
    std::lock_guard<std::mutex> lock(player->mutex);
    size_t frame = seconds > 0.0f ? (size_t)(seconds * player->sample_rate) : 0;
    player->frame_position = (std::min)(frame, player->frame_count);
    player->compressor_gain = player->eq_low[0] = player->eq_low[1] = player->eq_high[0] = player->eq_high[1] = 0.0f;
}

bool IsFinished(Player* player) { if (!player) return true; std::lock_guard<std::mutex> lock(player->mutex); return player->frame_position >= player->frame_count; }
float GetDuration(Player* player) { if (!player || player->sample_rate <= 0) return 0.0f; return (float)player->frame_count / player->sample_rate; }

} // namespace audio_file
} // namespace rev
