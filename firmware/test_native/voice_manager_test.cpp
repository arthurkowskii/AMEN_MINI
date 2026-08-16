#include "sample_player.h"
#include "voice_manager.h"
#include "wav_loader.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
        require(manager.trigger(static_cast<VoiceManager::PadId>(voice), pcm,
                                voice * 4, voice * 4 + 4, 1.0f),
                "four voices must trigger");
    }

    float left[1] = {-1.0f};
    float right[1] = {-1.0f};
    manager.render(left, right, 1);
    require(near(left[0], 10000.0f / 32768.0f), "four active voices must be mixed");

    manager.stopAll();
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        manager.trigger(static_cast<VoiceManager::PadId>(voice), pcm,
                        voice * 4, voice * 4 + 4, 1.0f);
    }
    require(manager.trigger(4, pcm, 16, 20, 1.0f),
            "fifth voice must trigger by stealing");
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
        manager.trigger(static_cast<VoiceManager::PadId>(i), loud.view(), 0, 2, 1.0f);
    }
    float left[1] = {9.0f};
    float right[1] = {9.0f};
    manager.render(left, right, 1);
    require(left[0] == 1.0f && right[0] == 1.0f, "positive mix must clamp to one");

    manager.stopAll();
    for (std::size_t i = 0; i < VoiceManager::kVoiceCount; ++i) {
        manager.trigger(static_cast<VoiceManager::PadId>(i), loud.view(), 2, 4, 1.0f);
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

void testPadRetriggerAndDistinctPadMix() {
    WavData wav = makeSegmentedWav();
    const PcmView pcm = wav.view();
    VoiceManager manager(48000);

    require(manager.trigger(7, pcm, 0, 4, 1.0f), "first pad range must trigger");
    require(manager.trigger(7, pcm, 4, 8, 1.0f), "same pad must retrigger");

    float left = 0.0f;
    float right = 0.0f;
    manager.render(&left, &right, 1);
    require(near(left, 2000.0f / 32768.0f),
            "same pad retrigger must replace the previous range");

    manager.stopAll();
    require(manager.trigger(7, pcm, 0, 4, 1.0f), "first distinct pad must trigger");
    require(manager.trigger(8, pcm, 4, 8, 1.0f), "second distinct pad must trigger");
    manager.render(&left, &right, 1);
    require(near(left, 3000.0f / 32768.0f), "distinct pad IDs must mix together");
}

std::size_t renderedFrames(uint32_t sourceSampleRate, std::size_t sourceFrames,
                           float userSpeed, uint32_t outputSampleRate = 44100) {
    WavData wav;
    wav.sampleRate = sourceSampleRate;
    wav.channels = 1;
    wav.samples.assign(sourceFrames, 1000);

    VoiceManager manager(outputSampleRate);
    const PcmView pcm = wav.view();
    require(manager.trigger(0, pcm, 0, pcm.frameCount(), userSpeed),
            "valid sample-rate conversion must trigger");

    std::size_t audibleFrames = 0;
    for (;;) {
        float left = 0.0f;
        float right = 0.0f;
        manager.render(&left, &right, 1);
        if (left == 0.0f && right == 0.0f) break;
        ++audibleFrames;
        require(audibleFrames <= sourceFrames * 4, "converted playback must terminate");
    }
    return audibleFrames;
}

void testNativeSampleRatesAndUserSpeed() {
    require(renderedFrames(48000, 48000, 1.0f) == 44100,
            "48000 frames at 48 kHz must last 44100 output frames");
    require(renderedFrames(44100, 44100, 1.0f) == 44100,
            "44.1 kHz PCM must keep one source frame per output frame");
    require(renderedFrames(48000, 48000, 2.0f) == 22050,
            "user speed must multiply the sample-rate source step");
}

void testInvalidRatesAndSpeeds() {
    WavData wav = makeSegmentedWav();
    VoiceManager invalidOutput(0);
    const PcmView pcm = wav.view();
    require(!invalidOutput.trigger(0, pcm, 0, pcm.frameCount(), 1.0f),
            "zero output sample rate must be rejected");

    VoiceManager manager;
    wav.sampleRate = 0;
    require(!manager.trigger(0, wav.view(), 0, 20, 1.0f),
            "zero source sample rate must be rejected");
    wav.sampleRate = 48000;
    require(!manager.trigger(0, wav.view(), 0, 20, 0.0f),
            "zero user speed must be rejected");
    require(!manager.trigger(0, wav.view(), 0, 20,
                             std::numeric_limits<float>::infinity()),
            "non-finite user speed must be rejected");
}
}  // namespace

int main() {
    testViewAndRanges();
    testFourVoicesAndOldestSteal();
    testClampAndZeroFill();
    testPadRetriggerAndDistinctPadMix();
    testNativeSampleRatesAndUserSpeed();
    testInvalidRatesAndSpeeds();
    std::cout << "All portable audio engine tests passed\n";
    return 0;
}
