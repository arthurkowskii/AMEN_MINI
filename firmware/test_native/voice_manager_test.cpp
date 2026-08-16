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

WavData makeLongSegmentedWav() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    wav.samples.insert(wav.samples.end(), 400, 1000);
    wav.samples.insert(wav.samples.end(), 400, 2000);
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

void testLiveSpeedChangeKeepsPlaybackPosition() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    for (int frame = 0; frame < 300; ++frame) {
        wav.samples.push_back(static_cast<int16_t>(frame * 100));
    }

    VoiceManager manager(48000);
    require(manager.trigger(9, wav.view(), 0, wav.view().frameCount(), 1.0f),
            "ramp sample must trigger");

    float left[10]{};
    float right[10]{};
    manager.render(left, right, 10);
    require(manager.setPadSpeed(9, 2.0f), "active pad speed must update");

    float nextLeft = 0.0f;
    float nextRight = 0.0f;
    manager.render(&nextLeft, &nextRight, 1);
    require(near(nextLeft, 1000.0f / 32768.0f),
            "live speed change must continue from the current playback position");
}

void testLiveSpeedRampConvergesAfter128Frames() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    for (int frame = 0; frame < 400; ++frame) {
        wav.samples.push_back(static_cast<int16_t>(frame * 50));
    }

    SamplePlayer player;
    player.setSample(wav.view(), 0, wav.view().frameCount());
    player.setSpeed(1.0f);
    player.trigger();

    float initialLeft = 0.0f;
    float initialRight = 0.0f;
    player.render(&initialLeft, &initialRight, 1);
    player.setSpeed(2.0f);

    float left[130]{};
    float right[130]{};
    player.render(left, right, 130);
    require(near(left[0], 50.0f / 32768.0f),
            "speed ramp must start from the current speed");
    require(near(left[64], 4037.5f / 32768.0f),
            "speed ramp must progress linearly at its midpoint");
    require(near(left[128], 9625.0f / 32768.0f),
            "speed ramp must reach its target after 128 frames");
    require(near(left[129], 9725.0f / 32768.0f),
            "speed must remain at the target after the ramp");
}

void testLiveSpeedChangeOnlyAffectsSelectedPad() {
    WavData wav = makeLongSegmentedWav();
    VoiceManager manager(48000);
    require(manager.trigger(7, wav.view(), 0, 400, 1.0f), "first pad must trigger");
    require(manager.trigger(8, wav.view(), 400, 800, 1.0f), "second pad must trigger");
    require(manager.setPadSpeed(8, 4.0f), "selected pad speed must update");

    float left[160]{};
    float right[160]{};
    manager.render(left, right, 160);
    require(near(left[159], 1000.0f / 32768.0f),
            "unselected pad must keep playing at its previous speed");
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
    require(!manager.trigger(0, wav.view(), 0, 20, 0.20f),
            "speed below the performance range must be rejected");
    require(!manager.trigger(0, wav.view(), 0, 20, 4.05f),
            "speed above the performance range must be rejected");

    require(manager.trigger(3, wav.view(), 0, 20, 1.0f),
            "valid speed must still trigger");
    require(!manager.setPadSpeed(3, 0.20f), "low live speed must be rejected");
    require(!manager.setPadSpeed(3, std::numeric_limits<float>::infinity()),
            "non-finite live speed must be rejected");
    require(!manager.setPadSpeed(99, 1.0f), "inactive pad must not report an update");

    manager.stopAll();
    require(manager.trigger(4, wav.view(), 0, 20, VoiceManager::kMinUserSpeed),
            "minimum performance speed must be accepted");
    require(manager.setPadSpeed(4, VoiceManager::kMaxUserSpeed),
            "maximum live speed must be accepted");
    manager.stopAll();
    require(manager.trigger(5, wav.view(), 0, 20, VoiceManager::kMaxUserSpeed),
            "maximum performance speed must be accepted");
    require(manager.setPadSpeed(5, VoiceManager::kMinUserSpeed),
            "minimum live speed must be accepted");
}
}  // namespace

int main() {
    testViewAndRanges();
    testFourVoicesAndOldestSteal();
    testClampAndZeroFill();
    testPadRetriggerAndDistinctPadMix();
    testNativeSampleRatesAndUserSpeed();
    testLiveSpeedChangeKeepsPlaybackPosition();
    testLiveSpeedRampConvergesAfter128Frames();
    testLiveSpeedChangeOnlyAffectsSelectedPad();
    testInvalidRatesAndSpeeds();
    std::cout << "All portable audio engine tests passed\n";
    return 0;
}
