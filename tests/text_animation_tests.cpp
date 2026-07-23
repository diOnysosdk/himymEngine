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
