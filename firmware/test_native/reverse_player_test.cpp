// Tests natifs du FX REVERSE (boucle inversee de la fenetre precedant
// l'appui). Compilation stricte + ASan/UBSan, cf live_repeat_test.cpp.
#include "fx/reverse_player.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr float kTolerance = 0.0001f;

struct ReverseFixture {
    static constexpr std::uint32_t kSampleRate = 100;
    static constexpr std::size_t kBufferFrames =
        ReversePlayer::requiredBufferFrames(kSampleRate);

    std::array<float, kBufferFrames> historyL{};
    std::array<float, kBufferFrames> historyR{};
    std::array<float, kBufferFrames> frozenL{};
    std::array<float, kBufferFrames> frozenR{};
    ReversePlayer reverse{kSampleRate, historyL.data(), historyR.data(),
                          frozenL.data(), frozenR.data(), kBufferFrames};
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < kTolerance;
}

// Remplit l'historique d'une rampe croissante 0 -> 0.199 (200 frames).
void fillRamp(ReversePlayer& reverse) {
    float left[200]{};
    float right[200]{};
    for (int i = 0; i < 200; ++i) {
        left[i] = static_cast<float>(i) / 1000.0f;
        right[i] = -left[i];
    }
    reverse.process(left, right, 200);
}

// 1. Inactif : passthrough bit-exact (l'historique est enregistre, la
// sortie ne bouge pas).
void testInactiveIsBitExactPassthrough() {
    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    float left[64]{};
    float right[64]{};
    for (int i = 0; i < 64; ++i) {
        left[i] = 0.3f;
        right[i] = -0.2f;
    }
    std::array<float, 64> copyL{};
    std::array<float, 64> copyR{};
    std::copy(left, left + 64, copyL.begin());
    std::copy(right, right + 64, copyR.begin());
    reverse.process(left, right, 64);
    require(std::memcmp(left, copyL.data(), 64 * sizeof(float)) == 0,
            "inactive reverse must pass through exactly (L)");
    require(std::memcmp(right, copyR.data(), 64 * sizeof(float)) == 0,
            "inactive reverse must pass through exactly (R)");
}

// 2. Capture avant l'appui : la premiere frame active lit la frame la plus
// recente capturee (0.199), puis descend — la fenetre est lue a l'envers.
// Slew de 1 frame : la sortie reflete le wet pur (la rampe est testee a part).
void testPlaysCapturedWindowBackwards() {
    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    reverse.setSlewFrames(1U);
    fillRamp(reverse);
    reverse.setAmount(1.0f);
    reverse.setActive(true);

    float left[300]{};
    float right[300]{};
    reverse.process(left, right, 300);

    // Fenetre = 2 s = 200 frames = toute la rampe. La couture (position 0)
    // force la moyenne des extremites (0.0995) ; la lecture pure se mesure
    // hors de la zone de lissage (distance >= 64 frames) : position 64 ->
    // frame absolue 199-64 = 135 -> 0.135, et la valeur DECROIT ensuite
    // (lecture a l'envers). Boucle : 200 frames plus loin, identique.
    require(reverse.loopFrames() == 200,
            "reverse window must cover the captured pre-roll");
    require(near(left[64], 0.135f),
            "reverse must read the window backwards (pure zone)");
    require(left[65] < left[64],
            "reverse samples must decrease as the read position advances");
    require(near(left[0], 0.0995f),
            "the wrap seam must crossfade the window extremes");
    require(near(left[264], left[64]),
            "reverse window must loop after one pass");
    require(near(right[64], -left[64]), "reverse must preserve stereo history");
}

// 3. Fenetre courte : moins d'historique que la fenetre -> la boucle se
// limite a ce qui existe. Slew de 1 frame pour comparer des positions pures.
void testShortHistoryClampsWindow() {
    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    reverse.setSlewFrames(1U);

    float left[50]{};
    float right[50]{};
    for (int i = 0; i < 50; ++i) left[i] = static_cast<float>(i) / 100.0f;
    reverse.process(left, right, 50);
    reverse.setActive(true);

    float outL[190]{};
    float outR[190]{};
    reverse.process(outL, outR, 190);
    require(reverse.loopFrames() == 50,
            "window must clamp to the available history");
    require(near(outL[49], outL[99]),
            "short reverse window must still produce a stable period");
}

// 4. Slew : l'activation glisse (premiere frame entre 0 et la cible), le
// relachement retourne au dry sans coupure franche.
void testActivationAndReleaseSlew() {
    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    fillRamp(reverse);
    reverse.setAmount(0.5f);
    reverse.setActive(true);

    float left[128]{};
    float right[128]{};
    reverse.process(left, right, 128);
    require(left[0] > 0.0f && left[0] < 0.1f,
            "activation must begin with a short slew ramp");
    // Frame 127 : mix installe, position 127 -> frame absolue 72 -> 0.072
    // (hors zone de couture). Wet pur 0.072 * amount 0.5.
    require(near(left[127], 0.072f * 0.5f),
            "amount must set the settled dry/wet target");

    reverse.setActive(false);
    float releaseL[129]{};
    float releaseR[129]{};
    reverse.process(releaseL, releaseR, 129);
    require(releaseL[0] > 0.0f,
            "release must not mute the reversed signal in one sample");
    require(near(releaseL[128], 0.0f),
            "release ramp must return fully to dry input");
}

// 5. Couture : contenu en dents de scie, le saut au wrap de la boucle
// inverse doit rester borne (lissage identique a LiveRepeat).
void testReverseWrapIsSmoothed() {
    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    reverse.setSlewFrames(1U);

    float historyL[100]{};
    float historyR[100]{};
    for (int i = 0; i < 100; ++i) {
        historyL[i] = -0.9f + 1.8f * static_cast<float>(i) / 99.0f;
        historyR[i] = historyL[i];
    }
    reverse.process(historyL, historyR, 100);
    reverse.setActive(true);

    float outputL[330]{};
    float outputR[330]{};
    reverse.process(outputL, outputR, 330);
    const float firstWrapJump = std::fabs(outputL[99] - outputL[100]);
    const float secondWrapJump = std::fabs(outputL[199] - outputL[200]);
    require(firstWrapJump < 0.05f && secondWrapJump < 0.05f,
            "every reverse wrap must have a bounded sample jump");
    require(near(outputL[70], outputL[170]),
            "seam smoothing must preserve the exact window period");
}

// 6. Tenue longue : la fenetre fige l'audio capture A l'appui ; la suite
// n'ecrase pas la boucle (protection par gel).
void testLongHoldDoesNotOverwriteCapturedWindow() {
    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    reverse.setSlewFrames(1U);
    fillRamp(reverse);
    reverse.setActive(true);

    std::vector<float> left(700, 0.0f);
    std::vector<float> right(700, 0.0f);
    reverse.process(left.data(), right.data(), static_cast<int>(left.size()));

    require(near(left[664], 0.135f),
            "a long hold must keep the originally captured reverse window");
}

// 7. Defensif : buffers nuls = passthrough sans crash ; depth/amount
// clamps ; activation avant tout audio = silence.
void testDefensive() {
    ReversePlayer null{100, nullptr, nullptr, nullptr, nullptr, 100};
    float left[16]{};
    float right[16]{};
    for (int i = 0; i < 16; ++i) {
        left[i] = 0.5f;
        right[i] = 0.5f;
    }
    null.setActive(true);
    null.process(left, right, 16);
    require(std::all_of(left, left + 16, [](float v) { return v == 0.5f; }),
            "null buffers must pass through untouched");

    ReverseFixture fixture;
    ReversePlayer& reverse = fixture.reverse;
    reverse.setAmount(5.0f);
    require(near(reverse.amount(), 1.0f), "amount must clamp to 1");
    reverse.setAmount(-1.0f);
    require(near(reverse.amount(), 0.0f), "amount must clamp to 0");
    reverse.setSlewFrames(0U);
    require(reverse.slewFrames() >= 1U, "slew frames must clamp to at least 1");

    reverse.setActive(true);  // aucune frame capturee : fenetre vide
    float silentL[32]{};
    float silentR[32]{};
    reverse.process(silentL, silentR, 32);
    require(reverse.loopFrames() == 0,
            "activation with no history must produce an empty window");
    require(std::all_of(silentL, silentL + 32,
                        [](float v) { return v == 0.0f; }),
            "empty window must stay silent");
}
}  // namespace

int main() {
    testInactiveIsBitExactPassthrough();
    testPlaysCapturedWindowBackwards();
    testShortHistoryClampsWindow();
    testActivationAndReleaseSlew();
    testReverseWrapIsSmoothed();
    testLongHoldDoesNotOverwriteCapturedWindow();
    testDefensive();
    std::cout << "All Reverse Player tests passed\n";
    return 0;
}
