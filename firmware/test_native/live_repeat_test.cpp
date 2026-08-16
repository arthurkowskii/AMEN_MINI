#include "fx/live_repeat.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
constexpr float kTolerance = 0.0001f;

struct RepeatFixture {
    static constexpr std::uint32_t kSampleRate = 100;
    static constexpr std::size_t kBufferFrames =
        LiveRepeat::requiredBufferFrames(kSampleRate);

    std::array<float, kBufferFrames> historyL{};
    std::array<float, kBufferFrames> historyR{};
    std::array<float, kBufferFrames> frozenL{};
    std::array<float, kBufferFrames> frozenR{};
    LiveRepeat repeat{kSampleRate, historyL.data(), historyR.data(),
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

void fillHistory(LiveRepeat& repeat) {
    float left[200]{};
    float right[200]{};
    for (int i = 0; i < 200; ++i) {
        left[i] = static_cast<float>(i) / 1000.0f;
        right[i] = -left[i];
    }
    repeat.process(left, right, 200);
}

void testCapturesAudioBeforePress() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);
    fillHistory(repeat);
    repeat.setAmount(1.0f);
    repeat.setActive(true);

    float left[300]{};
    float right[300]{};
    repeat.process(left, right, 300);

    require(repeat.loopFrames() == 100, "quarter note must use one beat of history");
    require(near(left[128], 0.128f), "repeat must read the audio immediately before activation");
    require(near(left[228], left[128]), "captured quarter note must loop periodically");
    require(near(right[228], -left[228]), "repeat must preserve stereo history");
}

void testDivisionAndBpmChangeLive() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);
    fillHistory(repeat);
    repeat.setActive(true);
    float settleL[160]{};
    float settleR[160]{};
    repeat.process(settleL, settleR, 160);

    repeat.setDivision(RepeatDivision::Eighth);
    float eighthL[260]{};
    float eighthR[260]{};
    repeat.process(eighthL, eighthR, 260);
    require(repeat.loopFrames() == 50, "E3 division change must select an eighth note");
    require(near(eighthL[159], eighthL[209]), "division change must settle to the new period");

    repeat.setBpm(120.0f);
    float bpmL[220]{};
    float bpmR[220]{};
    repeat.process(bpmL, bpmR, 220);
    require(repeat.loopFrames() == 25, "live BPM must recalculate repeat length");
    require(near(bpmL[159], bpmL[184]), "BPM change must settle to the recalculated period");
}

void testTripletDivisions() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);
    fillHistory(repeat);
    repeat.setAmount(1.0f);
    repeat.setActive(true);
    float settleL[160]{};
    float settleR[160]{};
    repeat.process(settleL, settleR, 160);

    repeat.setDivision(RepeatDivision::EighthTriplet);
    float triplet8L[200]{};
    float triplet8R[200]{};
    repeat.process(triplet8L, triplet8R, 200);
    require(repeat.loopFrames() == 33,
            "1/12 (eighth triplet) must loop a third of a beat");
    require(near(triplet8L[159], triplet8L[192]),
            "1/12 must settle to its exact triplet period");

    repeat.setDivision(RepeatDivision::SixteenthTriplet);
    float triplet16L[200]{};
    float triplet16R[200]{};
    repeat.process(triplet16L, triplet16R, 200);
    require(repeat.loopFrames() == 17,
            "1/24 (sixteenth triplet) must loop a sixth of a beat");
    require(near(triplet16L[159], triplet16L[176]),
            "1/24 must settle to its exact triplet period");
}

void testActivationAmountAndReleaseRamps() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);
    fillHistory(repeat);
    repeat.setAmount(0.5f);
    repeat.setActive(true);

    float left[128]{};
    float right[128]{};
    repeat.process(left, right, 128);
    require(left[0] > 0.0f && left[0] < 0.1f,
            "activation must begin with a short dry/wet ramp");
    require(near(left[127], 0.0635f), "E2 amount must set the settled dry/wet target");

    repeat.setActive(false);
    float releaseL[129]{};
    float releaseR[129]{};
    repeat.process(releaseL, releaseR, 129);
    require(releaseL[0] > 0.0f, "release must not mute the repeated signal in one sample");
    require(near(releaseL[128], 0.0f), "release ramp must return fully to dry input");
}

void testLongHoldDoesNotOverwriteCapturedLoop() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);
    fillHistory(repeat);
    repeat.setActive(true);

    std::vector<float> left(700, 0.0f);
    std::vector<float> right(700, 0.0f);
    repeat.process(left.data(), right.data(), static_cast<int>(left.size()));

    require(near(left[628], 0.128f),
            "a long pad hold must keep the originally captured audio intact");
}

void testInsufficientHistoryUsesAvailableAudio() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);

    float historyL[30]{};
    float historyR[30]{};
    for (int i = 0; i < 30; ++i) historyL[i] = static_cast<float>(i) / 100.0f;
    repeat.process(historyL, historyR, 30);
    repeat.setActive(true);

    float outputL[190]{};
    float outputR[190]{};
    repeat.process(outputL, outputR, 190);
    require(repeat.loopFrames() == 30,
            "activation must clamp the loop to the available history");
    require(near(outputL[129], outputL[159]),
            "short history must still produce a stable period");
}

void testEveryLoopWrapIsSmoothed() {
    RepeatFixture fixture;
    LiveRepeat& repeat = fixture.repeat;
    repeat.setBpm(60.0f);

    float historyL[100]{};
    float historyR[100]{};
    for (int i = 0; i < 100; ++i) {
        historyL[i] = -0.9f + 1.8f * static_cast<float>(i) / 99.0f;
        historyR[i] = historyL[i];
    }
    repeat.process(historyL, historyR, 100);
    repeat.setActive(true);

    float outputL[330]{};
    float outputR[330]{};
    repeat.process(outputL, outputR, 330);
    const float firstWrapJump = std::fabs(outputL[199] - outputL[200]);
    const float secondWrapJump = std::fabs(outputL[299] - outputL[300]);
    require(firstWrapJump < 0.05f && secondWrapJump < 0.05f,
            "every periodic loop seam must have a bounded sample jump");
    require(near(outputL[170], outputL[270]),
            "seam smoothing must preserve the exact BPM period");
}
}  // namespace

int main() {
    testCapturesAudioBeforePress();
    testDivisionAndBpmChangeLive();
    testTripletDivisions();
    testActivationAmountAndReleaseRamps();
    testLongHoldDoesNotOverwriteCapturedLoop();
    testInsufficientHistoryUsesAvailableAudio();
    testEveryLoopWrapIsSmoothed();
    std::cout << "All Live Repeat tests passed\n";
    return 0;
}
