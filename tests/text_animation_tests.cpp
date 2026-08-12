#include "rev_runtime.h"

#include <cmath>
#include <cstdio>

using namespace rev::runtime;

static bool NearlyEqual(float left, float right, float epsilon = 0.0001f)
{
    return fabsf(left - right) <= epsilon;
}

static bool Check(bool condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "[text_animation_tests] FAILED: %s\n", message);
        return false;
    }
    return true;
}

int main()
{
    bool passed = true;

    passed &= Check(NearlyEqual(ComputePostFadeIntensity(1.0f, 0.0f, 0.0f, 10.0f, 2.0f, 2.0f), 1.0f),
                    "post fade starts black");
    passed &= Check(NearlyEqual(ComputePostFadeIntensity(1.0f, 1.0f, 0.0f, 10.0f, 2.0f, 2.0f), 0.5f),
                    "post fade-in interpolates");
    passed &= Check(NearlyEqual(ComputePostFadeIntensity(1.0f, 5.0f, 0.0f, 10.0f, 2.0f, 2.0f), 0.0f),
                    "post fade leaves scene visible between fades");
    passed &= Check(NearlyEqual(ComputePostFadeIntensity(1.0f, 9.0f, 0.0f, 10.0f, 2.0f, 2.0f), 0.5f),
                    "post fade-out interpolates");

    TextGlyphAtlas scroll_atlas = {};
    scroll_atlas.line_height = 50.0f;
    scroll_atlas.glyphs['A'].codepoint = 'A';
    scroll_atlas.glyphs['A'].advance = 100.0f;
    passed &= Check(NearlyEqual(ComputeScrollTextTravel(
                        &scroll_atlas, "AA", 0, 1.0f, 1.0f, 0.2f,
                        1000.0f, 500.0f, 2.0f, 0.0f, 1), 3.4f),
                    "clamped left scroll clears the viewport from authored start");
    passed &= Check(NearlyEqual(ComputeScrollTextTravel(
                        &scroll_atlas, "AA", 1, 1.0f, 1.0f, 0.2f,
                        1000.0f, 500.0f, -2.0f, 0.0f, 1), 3.4f),
                    "clamped right scroll clears the viewport from authored start");
    passed &= Check(NearlyEqual(ComputeScrollTextTravel(
                        &scroll_atlas, "AA", 0, 1.0f, 1.0f, 0.2f,
                        1000.0f, 500.0f, 2.0f, 0.0f, 0), 0.6f),
                    "looping scroll travel remains one text extent plus gap");

    AudioEffects authored_audio = {};
    InitializeAudioEffects(&authored_audio);
    passed &= Check(authored_audio.curve_gain_db == -1 &&
                        authored_audio.curve_eq_high_db == -1,
                    "audio curve defaults are unassigned");
    rev::curve::Curve audio_curve = rev::curve::CreateCurve(2);
    audio_curve.duration = 10.0f;
    rev::curve::AddPoint(audio_curve, 0.0f, -12.0f);
    rev::curve::AddPoint(audio_curve, 1.0f, 0.0f);
    authored_audio.gain_enabled = 1;
    authored_audio.gain_db = -6.0f;
    authored_audio.curve_gain_db = 0;
    AudioEffects evaluated_audio = {};
    EvaluateAudioEffects(&authored_audio, &audio_curve, 1, 5.0f, &evaluated_audio);
    passed &= Check(NearlyEqual(evaluated_audio.gain_db, -6.0f),
                    "audio gain curve uses project timeline");
    rev::curve::DestroyCurve(audio_curve);

    passed &= Check(NearlyEqual(ApplyTextEasing(0.5f, TextEasingEaseInQuad), 0.25f),
                    "ease-in quadratic midpoint");
    passed &= Check(NearlyEqual(GetTextElementOrder(1, 4, TextStaggerOrderForward, 7), 1.0f / 3.0f),
                    "forward stagger order");
    passed &= Check(NearlyEqual(GetTextElementOrder(1, 4, TextStaggerOrderReverse, 7), 2.0f / 3.0f),
                    "reverse stagger order");
    passed &= Check(GetTextElementOrder(2, 8, TextStaggerOrderRandom, 42) ==
                        GetTextElementOrder(2, 8, TextStaggerOrderRandom, 42),
                    "random stagger order is deterministic");
    passed &= Check(NearlyEqual(CalculateTextStaggeredProgress(0.5f, 0.5f, 0.5f), 0.5f),
                    "staggered progress");

    TriggerTiming timing = {120.0f, 0.25f};
    passed &= Check(NearlyEqual(GetBeatDurationSeconds(timing.bpm), 0.5f),
                    "120 BPM beat duration");
    passed &= Check(NearlyEqual(GetTriggerTimeSeconds(&timing, 2.0f), 1.25f),
                    "beat time includes offset");
    passed &= Check(NearlyEqual(QuantizeTriggerBeat(0.74f, 0.5f), 0.5f),
                    "eighth-note quantization");
    passed &= Check(NearlyEqual(QuantizeTriggerBeat(7.9f, 8.0f), 8.0f),
                    "eight-beat quantization");

    TriggerTrack track = {};
    track.timing = timing;
    passed &= Check(AddTriggerEvent(&track, 1.0f, 1) && track.event_count == 1,
                    "trigger event insertion");
    passed &= Check(!AddTriggerEvent(&track, -1.0f, 1),
                    "negative trigger beat rejected");
    passed &= Check(NearlyEqual(EvaluateTriggerPulse(&track, 0.75f, 0.5f), 1.0f),
                    "trigger pulse is active after event");
    passed &= Check(NearlyEqual(EvaluateTriggerPulse(&track, 1.01f, 0.5f), 0.0f),
                    "trigger pulse expires");

    TextAnimationConfig config = {};
    InitializeTextAnimationConfig(&config);
    passed &= Check(config.version == 1 && config.modifier_count == 0,
                    "animation defaults");

    TextGlyphTimingInfo glyph = {};
    glyph.character_index = 1;
    glyph.character_count = 3;

    config.reveal.type = TextRevealFade;
    config.reveal.duration = 1.0f;
    config.reveal.stagger.delay = 0.25f;

    GlyphAnimationState state = {};
    EvaluateTextGlyphAnimation(&config, 0.5f, &glyph, &state);
    passed &= Check(NearlyEqual(state.opacity, 0.5f),
                    "character stagger reveal opacity");

    glyph.whitespace = 1;
    config.reveal.stagger.ignore_whitespace = 1;
    EvaluateTextGlyphAnimation(&config, 0.0f, &glyph, &state);
    passed &= Check(NearlyEqual(state.opacity, 1.0f),
                    "ignored whitespace bypasses reveal");

    glyph.whitespace = 0;
    config.reveal.stagger.ignore_whitespace = 0;
    config.reveal.duration = 0.0f;
    EvaluateTextGlyphAnimation(&config, 0.0f, &glyph, &state);
    passed &= Check(NearlyEqual(state.opacity, 1.0f),
                    "zero-duration reveal completes at its start");

    config.reveal.duration = 1.0f;
    config.reveal.stagger.delay = 0.0f;
    config.exit.type = TextExitFadeOut;
    config.exit.duration = 1.0f;
    EvaluateTextGlyphAnimation(&config, 0.5f, &glyph, &state);
    passed &= Check(NearlyEqual(state.opacity, 0.25f),
                    "reveal and exit opacity compose");

    config.exit.type = TextExitNone;
    config.reveal.type = TextRevealCharacterByCharacter;
    config.reveal.start_offset = 2.0f;
    glyph.character_index = 1;
    glyph.character_count = 3;
    const float asset_start_time = 5.0f;
    const float scene_time_before_reveal = 6.9f;
    const float scene_time_at_reveal = 7.0f;
    EvaluateTextGlyphAnimation(&config, scene_time_before_reveal - asset_start_time, &glyph, &state);
    passed &= Check(state.visible == 0 && NearlyEqual(state.opacity, 1.0f),
                    "character reveal offset is relative to asset start");
    EvaluateTextGlyphAnimation(&config, scene_time_at_reveal - asset_start_time, &glyph, &state);
    passed &= Check(state.visible == 0 && NearlyEqual(state.opacity, 1.0f),
                    "character reveal keeps later characters hidden at offset");
    EvaluateTextGlyphAnimation(&config, 2.5f, &glyph, &state);
    passed &= Check(state.visible != 0 && NearlyEqual(state.opacity, 1.0f),
                    "character reveal advances without fading");

    config.reveal.type = TextRevealNone;
    config.reveal.start_offset = 0.0f;
    config.exit.type = TextExitReverseTypewriter;
    EvaluateTextGlyphAnimation(&config, 0.5f, &glyph, &state);
    passed &= Check(state.visible != 0 && NearlyEqual(state.opacity, 1.0f),
                    "reverse typewriter stays opaque while visible");
    EvaluateTextGlyphAnimation(&config, 1.0f, &glyph, &state);
    passed &= Check(state.visible == 0 && NearlyEqual(state.opacity, 1.0f),
                    "reverse typewriter hides discretely");

    config.exit.type = TextExitNone;
    config.modifier_count = 1;
    config.modifiers[0].type = TextModifierJitter;
    config.modifiers[0].enabled = 1;
    config.modifiers[0].amount = 0.25f;
    config.modifiers[0].frequency = 4.0f;
    config.modifiers[0].seed = 99;
    EvaluateTextGlyphAnimation(&config, 0.375f, &glyph, &state);
    GlyphAnimationState repeat = {};
    EvaluateTextGlyphAnimation(&config, 0.375f, &glyph, &repeat);
    passed &= Check(NearlyEqual(state.position_offset_x, repeat.position_offset_x) &&
                        NearlyEqual(state.position_offset_y, repeat.position_offset_y),
                    "jitter is deterministic for the same timeline and glyph");

    if (!passed) return 1;
    printf("[text_animation_tests] PASS\n");
    return 0;
}
