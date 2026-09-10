#include "../src/teensy/stream_io.h"
#include <cstdlib>
#include <iostream>

static void check(bool condition, const char* label) {
    if (!condition) { std::cerr << label << '\n'; std::exit(1); }
}
static std::shared_ptr<TestFile> makeFile(const char* name, int16_t value, uint32_t frames, unsigned channels = 1) {
    auto file = std::make_shared<TestFile>();
    file->bytes.resize(size_t(frames) * channels * 2);
    for (size_t i = 0; i < file->bytes.size(); i += 2) {
        file->bytes[i] = static_cast<uint8_t>(value);
        file->bytes[i + 1] = static_cast<uint8_t>(uint16_t(value) >> 8);
    }
    SD.files[name] = file;
    return file;
}
static void polyphony() {
    SD = {};
    amen::StreamMixer mixer;
    amen::SdStreams streams(mixer);
    int16_t left[128], right[128];
    const amen::PcmWav wav{0, 44100, 1};
    for (uint8_t pad = 0; pad < 5; ++pad) {
        const std::string name = "/" + std::to_string(pad) + ".wav";
        makeFile(name.c_str(), int16_t(4000 * (pad + 1)), wav.frames);
        if (pad < 4) check(streams.trigger(pad, name.c_str(), wav), "initial trigger");
    }
    for (unsigned tick = 0; tick < 100; ++tick) {
        streams.service();
        mixer.render(left, right, 128);
    }
    check(SD.opened.size() == 4 && left[127] == 10000 && right[127] == 10000, "four independent streams mix");
    check(mixer.underruns() == 0 && !streams.takeError(), "four stream health");
    check(streams.trigger(1, "/1.wav", wav), "retrigger");
    for (unsigned tick = 0; tick < 20; ++tick) { streams.service(); mixer.render(left, right, 128); }
    check(SD.opened.size() == 5 && left[127] == 10000, "retrigger replaces rather than adds");
    check(streams.trigger(4, "/4.wav", wav), "fifth trigger");
    for (unsigned tick = 0; tick < 20; ++tick) { streams.service(); mixer.render(left, right, 128); }
    check(left[127] == 14000 && right[127] == 14000, "fifth steals oldest source");
    for (unsigned tick = 0; tick < 400; ++tick) { streams.service(); mixer.render(left, right, 128); }
    for (size_t voice = 0; voice < 4; ++voice) check(mixer.idle(voice), "EOF retires all voices");
    check(left[127] == 0 && mixer.underruns() == 0, "long streams and EOF remain clean");
}
static void errorsAndRapidTriggers() {
    SD = {};
    amen::StreamMixer mixer;
    amen::SdStreams streams(mixer);
    int16_t left[128], right[128];
    const amen::PcmWav wav{0, 12000, 2};
    auto file = makeFile("/valid.wav", 12000, wav.frames, 2);
    check(streams.trigger(0, "/valid.wav", wav), "prime trigger");
    streams.service();
    check(mixer.idle(0), "priming precedes publication");
    check(streams.trigger(0, "/valid.wav", wav), "replace during priming");
    for (unsigned i = 0; i < 30; ++i) { streams.service(); mixer.render(left, right, 128); }
    check(left[127] == 3000 && right[127] == 3000, "stereo decode after priming replacement");
    file->failAt = 0;
    for (unsigned i = 0; i < 100; ++i) { streams.service(); mixer.render(left, right, 128); }
    check(streams.takeError() && !streams.takeError() && mixer.idle(0), "read failure fades out and reports once");
    check(streams.trigger(0, "/missing.wav", wav), "missing request queues");
    streams.service();
    check(streams.takeError() && mixer.idle(0), "open failure does not activate stale buffer");
    file->failAt = SIZE_MAX;
    check(streams.trigger(0, "/valid.wav", wav), "recover after read error");
    for (unsigned i = 0; i < 30; ++i) { streams.service(); mixer.render(left, right, 128); }
    check(left[127] == 3000 && !streams.takeError(), "recovery reads new source");
}
int main() {
    polyphony();
    errorsAndRapidTriggers();
    std::cout << "SD producer: four sources, retrigger, stealing, priming, EOF and read errors passed\n";
}
