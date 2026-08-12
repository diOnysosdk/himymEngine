#include "rev_runtime.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static int Fail(const char* message) {
    std::fprintf(stderr, "[shader_pipeline_tests] %s\n", message);
    return 1;
}

int main() {
    using namespace rev::runtime;
    ShaderPipeline pipeline;
    InitializeShaderPipeline(&pipeline);
    pipeline.passes[ShaderPassBufferA].enabled = true;
    strcpy_s(pipeline.passes[ShaderPassBufferA].source_path, "buffer_a.glsl");
    pipeline.passes[ShaderPassImage].enabled = true;
    strcpy_s(pipeline.passes[ShaderPassImage].source_path, "image.glsl");
    pipeline.passes[ShaderPassImage].channels[0].kind = ShaderChannelBufferA;

    int order[kMaxShaderPasses] = {};
    char error[256] = {};
    int count = BuildShaderPassOrder(&pipeline, order, error, sizeof(error));
    if (count != 2 || order[0] != ShaderPassBufferA || order[1] != ShaderPassImage)
        return Fail("Buffer dependency was not ordered before Image");

    pipeline.passes[ShaderPassBufferA].channels[0].kind = ShaderChannelBufferB;
    pipeline.passes[ShaderPassBufferB].enabled = true;
    strcpy_s(pipeline.passes[ShaderPassBufferB].source_path, "buffer_b.glsl");
    pipeline.passes[ShaderPassBufferB].channels[0].kind = ShaderChannelBufferA;
    if (BuildShaderPassOrder(&pipeline, order, error, sizeof(error)) >= 0)
        return Fail("Dependency cycle was accepted");

    InitializeShaderPipeline(&pipeline);
    pipeline.passes[ShaderPassBufferA].enabled = true;
    strcpy_s(pipeline.passes[ShaderPassBufferA].source_path, "feedback.glsl");
    pipeline.passes[ShaderPassBufferA].channels[0].kind = ShaderChannelSelfPreviousFrame;
    if (BuildShaderPassOrder(&pipeline, order, error, sizeof(error)) != 1)
        return Fail("Explicit previous-frame self-feedback was rejected");

    float samples[kShaderAudioSampleFrames * 2] = {};
    constexpr int kExpectedBin = 16;
    constexpr float kTwoPi = 6.2831853071795864769f;
    for (int i = 0; i < kShaderAudioSampleFrames; ++i) {
        const float value = sinf(kTwoPi * kExpectedBin * (float)i / kShaderAudioSampleFrames);
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
    unsigned char audio_texture[kShaderAudioTextureWidth * 2] = {};
    BuildShaderAudioTexture(samples, kShaderAudioSampleFrames, audio_texture);
    int strongest_bin = 0;
    for (int bin = 1; bin < kShaderAudioTextureWidth; ++bin)
        if (audio_texture[bin] > audio_texture[strongest_bin]) strongest_bin = bin;
    if (strongest_bin < kExpectedBin - 1 || strongest_bin > kExpectedBin + 1)
        return Fail("Audio texture spectrum peak is in the wrong bin");
    if (audio_texture[kShaderAudioTextureWidth] < 126 ||
        audio_texture[kShaderAudioTextureWidth] > 129)
        return Fail("Audio texture waveform row is not normalized around 0.5");

    std::printf("[shader_pipeline_tests] PASS\n");
    return 0;
}
