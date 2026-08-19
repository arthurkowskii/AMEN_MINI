// Tests natifs du PadRecorder (J15, Shift + pad = enregistrement direct).
// Compilation stricte + ASan/UBSan, cf live_repeat_test.cpp.
#include "pad_recorder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
constexpr std::size_t kPads = 12;
constexpr std::size_t kCap = 1024;

struct RecorderFixture {
    std::array<std::int16_t, PadRecorder::requiredSamples(kPads, kCap)> storage{};
    PadRecorder rec{kPads, storage.data(), kCap};
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// 1. Repos : aucun pad arme -> record() est un no-op sans ecriture.
void testStartsIdleAndRecordIsNoOp() {
    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    require(!rec.recording(), "recorder must start idle");
    require(rec.activePad() == -1, "no pad must be active at start");

    std::array<float, 64> left{};
    std::array<float, 64> right{};
    for (std::size_t i = 0; i < left.size(); ++i) {
        left[i] = 0.3f;
        right[i] = -0.2f;
    }
    rec.record(left.data(), right.data(), 64);
    require(std::all_of(fixture.storage.begin(), fixture.storage.end(),
                        [](std::int16_t v) { return v == 0; }),
            "idle record must not write any sample");
}

// 2. Enregistrement mono : (L+R)/2, clamp [-1,1], arrondi au plus proche
// symetrique (0.5 -> 16384, -0.5 -> -16384, +2 -> 32767, -2 -> -32767).
void testRecordAppendsMonoClampedAndRounded() {
    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    rec.arm(3, 48000);

    const std::array<float, 8> left = {1.0f, -1.0f, 0.5f, -0.5f, 2.0f,
                                       -2.0f, 0.25f, 0.75f};
    const std::array<float, 8> right = {0.0f, 0.0f, 0.5f, -0.5f, 0.0f,
                                        0.0f, -0.25f, 0.25f};
    rec.record(left.data(), right.data(), 8);

    require(rec.framesRecorded(3) == 8, "frames must be counted per pad");
    const std::int16_t* block = fixture.storage.data() + 3 * kCap;
    const std::array<std::int16_t, 8> expected = {
        16384,   // (1.0+0.0)/2 = 0.5
        -16384,  // (-1.0+0.0)/2 = -0.5
        16384,   // (0.5+0.5)/2 = 0.5
        -16384,  // (-0.5-0.5)/2 = -0.5
        32767,   // (2.0+0.0)/2 = 1.0 (clamp)
        -32767,  // (-2.0+0.0)/2 = -1.0 (clamp)
        0,       // (0.25-0.25)/2 = 0
        16384};  // (0.75+0.25)/2 = 0.5
    require(std::memcmp(block, expected.data(), 8 * sizeof(std::int16_t)) == 0,
            "mono conversion must clamp and round symmetrically");
}

// 3. PcmView : mono, channels=1, sampleRate de l'arm, sampleCount = frames.
void testPcmViewReflectsRecording() {
    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    rec.arm(7, 44100);
    std::array<float, 16> left{};
    std::array<float, 16> right{};
    rec.record(left.data(), right.data(), 16);
    rec.stop();

    const PcmView view = rec.pcm(7);
    require(view.valid(), "pcm view must be valid after a recording");
    require(view.sampleRate == 44100, "pcm view must carry the arm rate");
    require(view.channels == 1, "recorded material is mono");
    require(view.sampleCount == 16 && view.frameCount() == 16,
            "pcm view must cover exactly the recorded frames");
    require(view.samples == fixture.storage.data() + 7 * kCap,
            "pcm view must point into the pad block");
}

// 4. Capacite : l'enregistrement s'arrete seul a la capacite, sans
// deborder dans le bloc du pad suivant.
void testCapacityStopsRecording() {
    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    rec.arm(1, 48000);
    std::array<float, 512> left{};
    std::array<float, 512> right{};
    left.fill(1.0f);
    right.fill(1.0f);
    rec.record(left.data(), right.data(), 512);
    rec.record(left.data(), right.data(), 512);
    rec.record(left.data(), right.data(), 512);

    require(rec.framesRecorded(1) == kCap,
            "frames must be capped at capacity");
    require(!rec.recording(), "recording must auto-stop at capacity");
    require(rec.activePad() == -1, "no pad must remain active after cap");
    const auto blockBegin =
        fixture.storage.begin() + static_cast<long>(1 * kCap);
    const auto blockEnd =
        fixture.storage.begin() + static_cast<long>(2 * kCap);
    require(std::all_of(blockBegin, blockEnd,
                        [](std::int16_t v) { return v == 32767; }),
            "the recorded block must be fully written");
    const auto nextBegin =
        fixture.storage.begin() + static_cast<long>(2 * kCap);
    const auto nextEnd =
        fixture.storage.begin() + static_cast<long>(3 * kCap);
    require(std::all_of(nextBegin, nextEnd,
                        [](std::int16_t v) { return v == 0; }),
            "the next pad block must stay untouched");
}

// 5. Un seul pad a la fois : armer un autre pad arrete le precedent,
// ses frames restent intactes.
void testArmingAnotherPadStopsPrevious() {
    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    rec.arm(2, 48000);
    std::array<float, 64> left{};
    std::array<float, 64> right{};
    left.fill(0.5f);
    right.fill(0.5f);
    rec.record(left.data(), right.data(), 64);
    require(rec.framesRecorded(2) == 64, "pad 2 must hold its frames");

    rec.arm(5, 48000);
    require(rec.recording(), "new pad must be recording");
    require(rec.activePad() == 5, "the new pad must be active");
    require(rec.framesRecorded(2) == 64, "pad 2 frames must survive");
    require(rec.framesRecorded(5) == 0, "pad 5 must start from zero");
}

// 6. Re-arm du meme pad : le compteur repart de 0 et le bloc est reecrit.
void testRearmResetsCounter() {
    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    rec.arm(4, 48000);
    std::array<float, 32> left{};
    std::array<float, 32> right{};
    left.fill(0.25f);
    right.fill(0.25f);
    rec.record(left.data(), right.data(), 32);
    rec.stop();
    require(rec.framesRecorded(4) == 32, "first pass must hold 32 frames");

    rec.arm(4, 48000);
    rec.record(left.data(), right.data(), 8);
    rec.stop();
    require(rec.framesRecorded(4) == 8, "re-arm must restart the counter");
    const PcmView view = rec.pcm(4);
    require(view.sampleCount == 8, "view must shrink after re-arm");
}

// 7. Defensif : stockage nul, pad hors bornes, capacite nulle, numFrames 0,
// pointeurs nuls — jamais de crash ni d'ecriture.
void testDefensive() {
    PadRecorder null{4, nullptr, 100};
    std::array<float, 16> left{};
    std::array<float, 16> right{};
    null.arm(0, 48000);
    null.record(left.data(), right.data(), 16);
    require(null.padCount() == 0, "null storage must yield zero pads");
    require(!null.recording(), "null recorder must never record");

    RecorderFixture fixture;
    PadRecorder& rec = fixture.rec;
    rec.arm(kPads, 48000);  // hors bornes
    require(!rec.recording(), "out-of-range arm must be a no-op");
    rec.arm(1, 48000);
    rec.record(nullptr, right.data(), 16);
    rec.record(left.data(), nullptr, 16);
    require(rec.framesRecorded(1) == 0, "null pointers must write nothing");
    rec.record(left.data(), right.data(), 0);
    require(rec.framesRecorded(1) == 0, "zero frames must write nothing");
    rec.stop();

    std::array<std::int16_t, PadRecorder::requiredSamples(2, 0)> empty{};
    PadRecorder noCap{2, empty.data(), 0};
    noCap.arm(0, 48000);
    require(!noCap.recording(), "zero capacity must refuse arm");
}

// 8. Capacite 1 frame : un seul sample ecrit, puis auto-stop.
void testCapacityOneFrame() {
    std::array<std::int16_t, 2> storage{};
    PadRecorder rec{2, storage.data(), 1};
    rec.arm(1, 48000);
    std::array<float, 4> left = {1.0f, 0.5f, 0.25f, 0.125f};
    std::array<float, 4> right = {1.0f, 0.5f, 0.25f, 0.125f};
    rec.record(left.data(), right.data(), 4);
    require(rec.framesRecorded(1) == 1, "capacity 1 must keep one frame");
    require(storage[1] == 32767, "the single frame must be written");
    require(storage[0] == 0, "the other pad block must stay untouched");
    require(!rec.recording(), "capacity 1 must auto-stop immediately");
}
}  // namespace

int main() {
    testStartsIdleAndRecordIsNoOp();
    testRecordAppendsMonoClampedAndRounded();
    testPcmViewReflectsRecording();
    testCapacityStopsRecording();
    testArmingAnotherPadStopsPrevious();
    testRearmResetsCounter();
    testDefensive();
    testCapacityOneFrame();
    std::cout << "All Pad Recorder tests passed\n";
    return 0;
}
