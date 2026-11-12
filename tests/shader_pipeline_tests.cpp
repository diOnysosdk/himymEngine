#include "rev_runtime.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static int Fail(const char* message) {
    std::fprintf(stderr, "[shader_pipeline_tests] %s\n", message);
    return 1;
}

static float CompositeNormalShaderChannel(float source, float source_alpha,
                                          float authored_opacity, float destination) {
    const float combined_alpha = source_alpha * authored_opacity;
    return source * combined_alpha + destination * (1.0f - combined_alpha);
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

    const float transparent_png_over_opaque =
        CompositeNormalShaderChannel(1.0f, 0.25f, 0.5f, 0.2f);
    if (fabsf(transparent_png_over_opaque - 0.3f) > 0.0001f)
        return Fail("Shader layer alpha did not combine per-pixel alpha and authored opacity once");

    struct QueueItem { int kind; int layer; int authored_index; } queue[] = {
        {0, 10, 1},  // banner pole image
        {1, 30, 3},  // foreground shader
        {1, 0, 0},   // background shader
        {1, 20, 2},  // animated banner shader
        {0, 10, 4},  // equal-layer item must retain authored order
    };
    auto priority = [](int kind) { return kind == 1 ? 1 : 2; };
    const int queue_count = (int)(sizeof(queue) / sizeof(queue[0]));
    for (int i = 0; i < queue_count - 1; ++i)
        for (int j = 0; j < queue_count - i - 1; ++j)
            if (RenderQueueComesAfter(queue[j].layer, priority(queue[j].kind),
                                      queue[j + 1].layer, priority(queue[j + 1].kind))) {
                QueueItem temporary = queue[j];
                queue[j] = queue[j + 1];
                queue[j + 1] = temporary;
            }
    const int expected_order[] = {0, 1, 4, 2, 3};
    for (int i = 0; i < queue_count; ++i)
        if (queue[i].authored_index != expected_order[i])
            return Fail("Mixed shader/image queue did not preserve global layer order and stable ties");

    std::printf("[shader_pipeline_tests] PASS\n");
    return 0;
}
