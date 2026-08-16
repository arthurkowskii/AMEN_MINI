#include "sample_player.h"
#include "voice_manager.h"
#include "wav_loader.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
constexpr float kTolerance = 0.00001f;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < kTolerance;
}

WavData makeSegmentedWav() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    for (int value = 1000; value <= 5000; value += 1000) {
        for (int frame = 0; frame < 4; ++frame) wav.samples.push_back(value);
    }
    return wav;
}

void testViewAndRanges() {
    WavData wav = makeSegmentedWav();
    const PcmView first = wav.view();
    const PcmView copy = first;
    require(first.samples == wav.samples.data(), "WavData::view must reference owned PCM");
    require(copy.samples == first.samples, "copying PcmView must not copy PCM samples");
    require(copy.sampleCount == wav.samples.size(), "PcmView must preserve sample count");

    SamplePlayer a;
    SamplePlayer b;
    a.setSample(first, 0, 4);
    b.setSample(copy, 4, 8);
    a.trigger();
    b.trigger();

    float aL[1] = {-1.0f};
    float aR[1] = {-1.0f};
    float bL[1] = {-1.0f};
    float bR[1] = {-1.0f};
    a.render(aL, aR, 1);
    b.render(bL, bR, 1);
    require(near(aL[0], 1000.0f / 32768.0f), "first range must start at its first frame");
    require(near(bL[0], 2000.0f / 32768.0f), "second range must share PCM at another offset");
}

void testFourVoicesAndOldestSteal() {
    WavData wav = makeSegmentedWav();
    const PcmView pcm = wav.view();
    VoiceManager manager;

    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        require(manager.trigger(pcm, voice * 4, voice * 4 + 4, 1.0f),
                "four voices must trigger");
    }

    float left[1] = {-1.0f};
    float right[1] = {-1.0f};
    manager.render(left, right, 1);
    require(near(left[0], 10000.0f / 32768.0f), "four active voices must be mixed");

    manager.stopAll();
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        manager.trigger(pcm, voice * 4, voice * 4 + 4, 1.0f);
    }
    require(manager.trigger(pcm, 16, 20, 1.0f), "fifth voice must trigger by stealing");
    manager.render(left, right, 1);
    require(near(left[0], 14000.0f / 32768.0f), "fifth voice must steal the oldest voice");
}

void testClampAndZeroFill() {
    WavData loud;
    loud.sampleRate = 48000;
    loud.channels = 1;
    loud.samples = {32767, 32767, -32768, -32768};

    VoiceManager manager;
    for (std::size_t i = 0; i < VoiceManager::kVoiceCount; ++i) {
        manager.trigger(loud.view(), 0, 2, 1.0f);
    }
    float left[1] = {9.0f};
    float right[1] = {9.0f};
    manager.render(left, right, 1);
    require(left[0] == 1.0f && right[0] == 1.0f, "positive mix must clamp to one");

    manager.stopAll();
    for (std::size_t i = 0; i < VoiceManager::kVoiceCount; ++i) {
        manager.trigger(loud.view(), 2, 4, 1.0f);
    }
    manager.render(left, right, 1);
    require(left[0] == -1.0f && right[0] == -1.0f, "negative mix must clamp to minus one");

    SamplePlayer player;
    player.setSample(loud.view(), 0, 2);
    player.trigger();
    float partialL[5] = {9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    float partialR[5] = {9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    player.render(partialL, partialR, 5);
    require(partialL[2] == 0.0f && partialL[4] == 0.0f &&
                partialR[2] == 0.0f && partialR[4] == 0.0f,
            "SamplePlayer must zero the buffer tail after sample end");

    manager.stopAll();
    left[0] = right[0] = 9.0f;
    manager.render(left, right, 1);
    require(left[0] == 0.0f && right[0] == 0.0f, "stopped manager must output silence");
}
}  // namespace

int main() {
    testViewAndRanges();
    testFourVoicesAndOldestSteal();
    testClampAndZeroFill();
    std::cout << "All portable audio engine tests passed\n";
    return 0;
}
