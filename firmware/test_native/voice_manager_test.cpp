#include "sample_player.h"
#include "pad_trigger_logic.h"
#include "voice_manager.h"
#include "wav_loader.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

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

constexpr std::size_t kCrossfadeSegmentFrames = 128;

WavData makeCrossfadeWav() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    for (int value = 1000; value <= 9000; value += 1000) {
        wav.samples.insert(wav.samples.end(), kCrossfadeSegmentFrames, value);
    }
    return wav;
}

void triggerCrossfadeSegment(VoiceManager& manager, PcmView pcm,
                             VoiceManager::PadId padId, std::size_t segment) {
    const std::size_t start = segment * kCrossfadeSegmentFrames;
    require(manager.trigger(padId, pcm, start, start + kCrossfadeSegmentFrames, 1.0f),
            "crossfade test segment must trigger");
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

void testOneShotEndsWithoutWrapping() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    wav.samples = {1000, 2000, 3000};

    SamplePlayer player;
    player.setSample(wav.view(), 0, wav.view().frameCount(), PlaybackMode::OneShot);
    player.setSpeed(1.0f);
    player.trigger();
    float left[5]{};
    float right[5]{};
    require(!player.render(left, right, 5), "one-shot must end naturally");
    require(near(left[0], 1000.0f / 32768.0f) &&
                near(left[2], 3000.0f / 32768.0f),
            "one-shot must render its range once");
    require(left[3] == 0.0f && left[4] == 0.0f,
            "one-shot must not wrap after its natural end");
}

void testLoopWrapsWithFractionalAndLargeSteps() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    wav.samples = {0, 10000, 20000};

    SamplePlayer boundary;
    boundary.setSample(wav.view(), 0, wav.view().frameCount(), PlaybackMode::Loop);
    boundary.setSpeed(2.5f);
    boundary.trigger();
    float boundaryL[3]{};
    float boundaryR[3]{};
    require(boundary.render(boundaryL, boundaryR, 3), "loop must remain active");
    require(near(boundaryL[1], 10000.0f / 32768.0f),
            "loop interpolation must cross from end to start");
    require(near(boundaryL[2], 20000.0f / 32768.0f),
            "fractional loop wrap must preserve overshoot");

    SamplePlayer largeStep;
    largeStep.setSample(wav.view(), 0, wav.view().frameCount(), PlaybackMode::Loop);
    largeStep.setSpeed(7.25f);
    largeStep.trigger();
    float largeL[3]{};
    float largeR[3]{};
    require(largeStep.render(largeL, largeR, 3),
            "loop must handle source steps larger than its range");
    require(near(largeL[1], 12500.0f / 32768.0f) &&
                near(largeL[2], 10000.0f / 32768.0f),
            "large loop steps must wrap with modulo and retain fractions");
}

void testVoicePlaybackModeAndPlayingQuery() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    wav.samples = {1000, 2000, 3000};
    VoiceManager manager(48000);

    require(manager.trigger(1, wav.view(), 0, 3, 1.0f, PlaybackMode::Loop),
            "looping voice must trigger");
    float left[8]{};
    float right[8]{};
    manager.render(left, right, 8);
    require(manager.isPadPlaying(1), "looping voice must continue beyond sample end");
    require(near(left[6], 1000.0f / 32768.0f),
            "looping voice must restart at its range start");

    require(manager.trigger(2, wav.view(), 0, 3, 1.0f),
            "default one-shot voice must trigger");
    require(manager.isPadPlaying(2), "new one-shot must report playing");
    float finishL[3]{};
    float finishR[3]{};
    manager.render(finishL, finishR, 3);
    require(!manager.isPadPlaying(2),
            "one-shot query must clear after natural completion");
    require(manager.isPadPlaying(1), "one pad's mode must not affect another pad");
}

void testStopPadCancelsOnlyAssociatedAudio() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    triggerCrossfadeSegment(manager, wav.view(), 4, 4);
    triggerCrossfadeSegment(manager, wav.view(), 5, 5);

    manager.stopPad(4);
    require(!manager.isPadPlaying(4), "stopPad must stop its active main voice");
    require(manager.isPadPlaying(2) && manager.isPadPlaying(3) &&
                manager.isPadPlaying(5),
            "stopPad must preserve unrelated active voices");
    float left = 0.0f;
    float right = 0.0f;
    manager.render(&left, &right, 1);
    require(near(left, 9000.0f / 32768.0f),
            "stopPad must cancel associated tails and preserve unrelated tails");
}

void testPadTriggerBehaviorMatrix() {
    using Action = PadTriggerAction;
    require(padDownAction(PlaybackMode::OneShot, TriggerBehavior::Gate, false) ==
                Action::Trigger &&
                padUpAction(TriggerBehavior::Gate) == Action::Stop,
            "one-shot gate must trigger on down and stop on release");
    require(padDownAction(PlaybackMode::OneShot, TriggerBehavior::Latch, true) ==
                Action::Trigger &&
                padUpAction(TriggerBehavior::Latch) == Action::None,
            "one-shot latch must retrigger on down and ignore release");
    require(padDownAction(PlaybackMode::Loop, TriggerBehavior::Gate, true) ==
                Action::Trigger &&
                padUpAction(TriggerBehavior::Gate) == Action::Stop,
            "loop gate must retrigger on down and stop on release");
    require(padDownAction(PlaybackMode::Loop, TriggerBehavior::Latch, false) ==
                Action::Trigger &&
                padDownAction(PlaybackMode::Loop, TriggerBehavior::Latch, true) ==
                    Action::Stop &&
                padUpAction(TriggerBehavior::Latch) == Action::None,
            "loop latch must toggle on down and ignore release");
}

void applyPadDown(VoiceManager& manager, VoiceManager::PadId padId, PcmView pcm,
                  PlaybackMode mode, TriggerBehavior behavior) {
    const PadTriggerAction action =
        padDownAction(mode, behavior, manager.isPadPlaying(padId));
    if (action == PadTriggerAction::Stop) {
        manager.stopPad(padId);
    } else {
        require(manager.trigger(padId, pcm, 0, pcm.frameCount(), 1.0f, mode),
                "matrix pad-down trigger must succeed");
    }
}

void applyPadUp(VoiceManager& manager, VoiceManager::PadId padId,
                TriggerBehavior behavior) {
    if (padUpAction(behavior) == PadTriggerAction::Stop) manager.stopPad(padId);
}

void testPadTriggerMatrixDrivesEngine() {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    wav.samples = {1000, 2000, 3000};
    const PcmView pcm = wav.view();
    float left[4]{};
    float right[4]{};

    VoiceManager oneShotGate(48000);
    applyPadDown(oneShotGate, 1, pcm, PlaybackMode::OneShot,
                 TriggerBehavior::Gate);
    require(oneShotGate.isPadPlaying(1), "one-shot gate down must start audio");
    applyPadUp(oneShotGate, 1, TriggerBehavior::Gate);
    require(!oneShotGate.isPadPlaying(1), "one-shot gate release must stop audio");
    applyPadDown(oneShotGate, 1, pcm, PlaybackMode::OneShot,
                 TriggerBehavior::Gate);
    oneShotGate.render(left, right, 4);
    require(!oneShotGate.isPadPlaying(1), "one-shot gate must also end naturally");

    VoiceManager oneShotLatch(48000);
    applyPadDown(oneShotLatch, 2, pcm, PlaybackMode::OneShot,
                 TriggerBehavior::Latch);
    applyPadUp(oneShotLatch, 2, TriggerBehavior::Latch);
    require(oneShotLatch.isPadPlaying(2), "one-shot latch release must be a no-op");
    oneShotLatch.render(left, right, 1);
    applyPadDown(oneShotLatch, 2, pcm, PlaybackMode::OneShot,
                 TriggerBehavior::Latch);
    left[0] = right[0] = 0.0f;
    oneShotLatch.render(left, right, 1);
    require(near(left[0], 1000.0f / 32768.0f),
            "one-shot latch second down must retrigger from the start");
    oneShotLatch.render(left, right, 3);
    require(!oneShotLatch.isPadPlaying(2), "one-shot latch must end naturally");

    VoiceManager loopGate(48000);
    applyPadDown(loopGate, 3, pcm, PlaybackMode::Loop, TriggerBehavior::Gate);
    loopGate.render(left, right, 4);
    require(loopGate.isPadPlaying(3), "loop gate must wrap while held");
    applyPadUp(loopGate, 3, TriggerBehavior::Gate);
    require(!loopGate.isPadPlaying(3), "loop gate release must stop audio");

    VoiceManager loopLatch(48000);
    applyPadDown(loopLatch, 4, pcm, PlaybackMode::Loop, TriggerBehavior::Latch);
    applyPadUp(loopLatch, 4, TriggerBehavior::Latch);
    require(loopLatch.isPadPlaying(4), "loop latch release must be a no-op");
    applyPadDown(loopLatch, 4, pcm, PlaybackMode::Loop, TriggerBehavior::Latch);
    require(!loopLatch.isPadPlaying(4), "loop latch second down must stop audio");
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
    require(near(left[0], 10000.0f / 32768.0f),
            "a stolen voice must start with its outgoing contribution unchanged");
}

void testStolenVoiceCrossfadeIsExactly64Frames() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    triggerCrossfadeSegment(manager, wav.view(), 4, 4);

    float left[65]{};
    float right[65]{};
    manager.render(left, right, 65);

    require(near(left[0], 10000.0f / 32768.0f),
            "crossfade frame one must contain full outgoing and zero incoming");
    require(near(left[31], (10000.0f + 4000.0f * 31.0f / 63.0f) / 32768.0f),
            "crossfade midpoint must follow the 64-frame linear envelope");
    require(near(left[63], 14000.0f / 32768.0f),
            "crossfade frame 64 must contain zero outgoing and full incoming");
    require(near(left[64], 14000.0f / 32768.0f),
            "only the incoming source may contribute after the transition");
}

void testSamePadRetriggerDoesNotLeaveATail() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    triggerCrossfadeSegment(manager, wav.view(), 7, 0);
    triggerCrossfadeSegment(manager, wav.view(), 7, 5);

    float left[65]{};
    float right[65]{};
    manager.render(left, right, 65);
    for (float sample : left) {
        require(near(sample, 6000.0f / 32768.0f),
                "same-pad retrigger must replace rather than retire the old source");
    }

    manager.stopAll();
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    triggerCrossfadeSegment(manager, wav.view(), 4, 4);
    triggerCrossfadeSegment(manager, wav.view(), 4, 5);
    float sampleL = 0.0f;
    float sampleR = 0.0f;
    manager.render(&sampleL, &sampleR, 1);
    require(near(sampleL, 15000.0f / 32768.0f),
            "incoming-pad retrigger must cancel the tail from its active crossfade");
}

void testRetirementOnlyPadRetriggerCancelsItsOldTail() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    triggerCrossfadeSegment(manager, wav.view(), 4, 4);
    triggerCrossfadeSegment(manager, wav.view(), 0, 5);

    float sampleL = 0.0f;
    float sampleR = 0.0f;
    manager.render(&sampleL, &sampleR, 1);
    require(near(sampleL, 9000.0f / 32768.0f),
            "retirement-only pad retrigger must cancel its old tail before stealing");
}

void testPadRetriggerPreservesUnrelatedRetirementTails() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    triggerCrossfadeSegment(manager, wav.view(), 4, 4);
    triggerCrossfadeSegment(manager, wav.view(), 5, 5);
    triggerCrossfadeSegment(manager, wav.view(), 4, 6);

    float sampleL = 0.0f;
    float sampleR = 0.0f;
    manager.render(&sampleL, &sampleR, 1);
    require(near(sampleL, 16000.0f / 32768.0f),
            "pad retrigger must preserve retirement tails from unrelated steals");
}

void testStopAllCancelsCrossfadeTail() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    triggerCrossfadeSegment(manager, wav.view(), 4, 4);

    float sampleL = 0.0f;
    float sampleR = 0.0f;
    manager.render(&sampleL, &sampleR, 1);
    manager.stopAll();
    sampleL = sampleR = 9.0f;
    manager.render(&sampleL, &sampleR, 1);
    require(sampleL == 0.0f && sampleR == 0.0f,
            "stopAll during a crossfade must silence active and retired voices");
}

void testRapidStealRetirementOverflowDropsOldestTail() {
    WavData wav = makeCrossfadeWav();
    VoiceManager manager(48000);
    for (std::size_t voice = 0; voice < VoiceManager::kVoiceCount; ++voice) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(voice), voice);
    }
    for (std::size_t segment = 4; segment < 9; ++segment) {
        triggerCrossfadeSegment(manager, wav.view(),
                                static_cast<VoiceManager::PadId>(segment), segment);
    }

    float left = 0.0f;
    float right = 0.0f;
    manager.render(&left, &right, 1);
    require(near(left, 14000.0f / 32768.0f),
            "a fifth rapid steal must deterministically drop the oldest of four tails");
}

void testCrossfadeMixRemainsClamped() {
    WavData loud;
    loud.sampleRate = 48000;
    loud.channels = 1;
    loud.samples.assign(128, 32767);

    VoiceManager manager(48000);
    for (std::size_t pad = 0; pad < VoiceManager::kVoiceCount + 1; ++pad) {
        require(manager.trigger(static_cast<VoiceManager::PadId>(pad), loud.view(),
                                0, loud.view().frameCount(), 1.0f),
                "loud crossfade voice must trigger");
    }
    float left[64]{};
    float right[64]{};
    manager.render(left, right, 64);
    for (std::size_t frame = 0; frame < 64; ++frame) {
        require(left[frame] >= -1.0f && left[frame] <= 1.0f &&
                    right[frame] >= -1.0f && right[frame] <= 1.0f,
                "crossfade output must remain clamped to the public range");
    }
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

// T4 (J15) : la matiere enregistree d'un pad (mono int16, channels=1, taux
// natif) se joue comme n'importe quel WAV : le rendu stereo duplique le
// mono sur les deux canaux, sans artefact ni silence.
void testRecordedMonoMaterialPlaysOnBothChannels() {
    // Forme exacte d'un PcmView sorti de PadRecorder::pcm() : mono, 48 kHz.
    constexpr std::size_t kFrames = 1024;
    std::vector<std::int16_t> recorded(kFrames, 8192);  // DC 0.25
    PcmView pcm;
    pcm.sampleRate = 48000;
    pcm.channels = 1;
    pcm.samples = recorded.data();
    pcm.sampleCount = recorded.size();

    VoiceManager manager;
    require(manager.trigger(1, pcm, 0, pcm.frameCount(), 1.0f),
            "recorded mono material must trigger like a WAV");
    float left[4] = {9.0f, 9.0f, 9.0f, 9.0f};
    float right[4] = {9.0f, 9.0f, 9.0f, 9.0f};
    manager.render(left, right, 4);
    for (std::size_t i = 0; i < 4; ++i) {
        require(near(left[i], 8192.0f / 32768.0f),
                "recorded material must render at the right level (L)");
        require(near(right[i], left[i]),
                "mono material must be duplicated on both channels");
    }
    manager.stopAll();
}
}  // namespace

int main() {
    testViewAndRanges();
    testOneShotEndsWithoutWrapping();
    testLoopWrapsWithFractionalAndLargeSteps();
    testVoicePlaybackModeAndPlayingQuery();
    testStopPadCancelsOnlyAssociatedAudio();
    testPadTriggerBehaviorMatrix();
    testPadTriggerMatrixDrivesEngine();
    testFourVoicesAndOldestSteal();
    testStolenVoiceCrossfadeIsExactly64Frames();
    testSamePadRetriggerDoesNotLeaveATail();
    testRetirementOnlyPadRetriggerCancelsItsOldTail();
    testPadRetriggerPreservesUnrelatedRetirementTails();
    testStopAllCancelsCrossfadeTail();
    testRapidStealRetirementOverflowDropsOldestTail();
    testCrossfadeMixRemainsClamped();
    testClampAndZeroFill();
    testPadRetriggerAndDistinctPadMix();
    testNativeSampleRatesAndUserSpeed();
    testLiveSpeedChangeKeepsPlaybackPosition();
    testLiveSpeedRampConvergesAfter128Frames();
    testLiveSpeedChangeOnlyAffectsSelectedPad();
    testInvalidRatesAndSpeeds();
    testRecordedMonoMaterialPlaysOnBothChannels();
    std::cout << "All portable audio engine tests passed\n";
    return 0;
}
