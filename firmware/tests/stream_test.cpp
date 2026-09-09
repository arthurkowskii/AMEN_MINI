#include "pcm_wav.h"
#include "stream_mixer.h"
#include "pad_browser.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

static void check(bool condition) { if (!condition) { std::cerr << "FAILED\n"; std::exit(1); } }
class MemoryReader : public amen::WavReader {
public:
    std::vector<uint8_t> bytes;
    uint64_t size() const override { return bytes.size(); }
    bool readAt(uint32_t offset, void* out, size_t count) override {
        if (offset > bytes.size() || count > bytes.size() - offset) return false;
        std::memcpy(out, bytes.data() + offset, count); return true;
    }
};
static void put32(std::vector<uint8_t>& b, size_t p, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) b[p + i] = static_cast<uint8_t>(value >> (i * 8));
}
static MemoryReader wav(uint8_t channels) {
    MemoryReader r;
    r.bytes.resize(44 + 70 * channels * 2);
    auto& b = r.bytes;
    std::memcpy(b.data(), "RIFF", 4); put32(b, 4, static_cast<uint32_t>(b.size() - 8));
    std::memcpy(b.data() + 8, "WAVEfmt ", 8); put32(b, 16, 16);
    b[20] = 1; b[22] = channels; put32(b, 24, 44100); put32(b, 28, 44100 * channels * 2);
    b[32] = channels * 2; b[34] = 16;
    std::memcpy(b.data() + 36, "data", 4); put32(b, 40, 70 * channels * 2);
    return r;
}
static void parserTests() {
    amen::PcmWav parsed;
    auto mono = wav(1), stereo = wav(2);
    check(amen::parseWav(mono, parsed) && parsed.frames == 70 && parsed.channels == 1 && parsed.offset == 44);
    check(amen::parseWav(stereo, parsed) && parsed.channels == 2);
    for (size_t size = 0; size < mono.bytes.size(); ++size) {
        auto shortFile = mono; shortFile.bytes.resize(size); check(!amen::parseWav(shortFile, parsed));
    }
    for (size_t p : {size_t(0), size_t(8), size_t(20), size_t(22), size_t(24), size_t(28), size_t(32), size_t(34)}) {
        auto bad = mono; bad.bytes[p] ^= 0x80; check(!amen::parseWav(bad, parsed));
    }
    auto padded = mono;
    const uint8_t junk[] = {'J','U','N','K',1,0,0,0,42,0};
    padded.bytes.insert(padded.bytes.begin() + 12, std::begin(junk), std::end(junk));
    put32(padded.bytes, 4, static_cast<uint32_t>(padded.bytes.size() - 8));
    check(amen::parseWav(padded, parsed) && parsed.offset == 54);
    put32(padded.bytes, 16, UINT32_MAX); check(!amen::parseWav(padded, parsed));
    auto odd = mono; put32(odd.bytes, 40, 139); check(!amen::parseWav(odd, parsed));
    auto duplicate = mono;
    duplicate.bytes.insert(duplicate.bytes.end(), mono.bytes.begin() + 12, mono.bytes.begin() + 36);
    put32(duplicate.bytes, 4, static_cast<uint32_t>(duplicate.bytes.size() - 8));
    check(!amen::parseWav(duplicate, parsed));
    auto reversed = mono;
    std::rotate(reversed.bytes.begin() + 12, reversed.bytes.begin() + 36, reversed.bytes.end());
    check(amen::parseWav(reversed, parsed) && parsed.offset == 20);
    const auto saved = parsed;
    check(!amen::parseWav(odd, parsed) && parsed.offset == saved.offset && parsed.frames == saved.frames);
}
static void mixerTests() {
    amen::StreamMixer mixer;
    amen::StereoFrame input[256];
    std::fill_n(input, 256, amen::StereoFrame{30000, -30000});
    int16_t left[256], right[256];
    for (size_t v = 0; v < 4; ++v) { mixer.prepare(v, 200); check(mixer.write(v, input, 200) == 200); mixer.start(v); }
    mixer.render(left, right, 256);
    check(left[63] == 30000 && right[63] == -30000 && left[135] == 30000);
    for (size_t i = 136; i < 200; ++i) check(left[i] <= left[i - 1]);
    for (size_t i = 199; i < 256; ++i) check(left[i] == 0 && right[i] == 0);
    for (size_t v = 0; v < 4; ++v) check(mixer.idle(v));
    check(mixer.underruns() == 0);
    mixer.prepare(0, 200); mixer.write(0, input, 100); mixer.start(0);
    mixer.render(left, right, 128); check(!mixer.idle(0) && mixer.underruns() == 1 && left[100] == 0);
    mixer.write(0, input, 100); mixer.render(left, right, 128);
    check(mixer.idle(0) && left[98] > 0 && left[99] == 0 && left[100] == 0);
    mixer.prepare(0, 256); mixer.write(0, input, 256); mixer.start(0); mixer.render(left, right, 128);
    mixer.stop(0); mixer.render(left, right, 128);
    check(mixer.idle(0) && left[0] < 7500 && left[0] > 7000 && left[63] == 0);
    for (size_t i = 1; i < 64; ++i) check(left[i] <= left[i - 1]);
    mixer.prepare(0, 70); mixer.write(0, input, 70); mixer.start(0); mixer.render(left, right, 128);
    check(left[0] > 0 && left[0] < 200 && left[68] > 0 && left[69] == 0 && mixer.idle(0));
    mixer.prepare(0, 20000);
    std::vector<amen::StereoFrame> full(9000, {12340, -12340});
    check(mixer.write(0, full.data(), full.size()) == amen::StreamMixer::kCapacity && mixer.writable(0) == 0);
    mixer.start(0);
    for (size_t i = 0; i < 70; ++i) { mixer.render(left, right, 128); check(mixer.write(0, full.data(), 128) == 128); }
    check(left[127] == 3085 && right[127] == -3085 && mixer.underruns() == 1);
}
static void browserTests() {
    amen::PadBrowser browser;
    browser.open(19); browser.resetEntries(); browser.add("BREAKS", true); browser.turn(1);
    check(browser.enter() && !strcmp(browser.directory, "/BREAKS"));
    browser.resetEntries(); browser.add("amen.wav", false); browser.turn(1);
    char path[amen::PadBrowser::kPath]; check(browser.selectedPath(path) && !strcmp(path, "/BREAKS/amen.wav"));
    check(browser.assign(path, {44,70,2}) && !browser.active && browser.pads[19].wav.frames == 70);
    browser.open(19); check(!browser.assign("bad", {}) && !strcmp(browser.pads[19].path, path));
    browser.turn(-500); check(browser.enter() && !strcmp(browser.directory, "/"));
    browser.resetEntries();
    check(browser.add("notes.txt", false) && browser.count == 1);
    check(browser.add("LOUD.WaV", false) && browser.count == 2);
    browser.resetEntries();
    auto scan = [&] {
        for (size_t i = 0; i < 150; ++i) {
            char name[32]; snprintf(name, sizeof(name), "sample%03zu.wav", i);
            if (!browser.add(name, false)) break;
        }
    };
    scan();
    check(browser.count == 63);
    browser.turn(INT32_MAX);
    check(browser.entries[browser.selected].kind == amen::PadBrowser::Kind::NextPage && browser.enter());
    browser.resetEntries(); scan();
    check(browser.count == 64 && !strcmp(browser.entries[2].name, "sample061.wav"));
    browser.turn(INT32_MAX); check(browser.enter());
    browser.resetEntries(); scan();
    check(browser.count == 30 && !strcmp(browser.entries[2].name, "sample122.wav"));
    check(!strcmp(browser.entries[29].name, "sample149.wav"));
    browser.turn(1); check(browser.enter() && browser.pageStart == 61);
    browser.resetEntries(); scan();
    check(!strcmp(browser.entries[2].name, "sample061.wav"));
    browser.turn(-INT32_MAX); check(browser.enter() && browser.pageStart == 0);
    browser.resetEntries(); check(browser.count == 1);
    check(browser.enter() && !strcmp(browser.directory, "/"));
    browser.cancel(); check(!browser.active && !browser.scanning);
}
int main() { parserTests(); mixerTests(); browserTests(); std::cout << "PCM parser, stream mixer and browser tests passed\n"; }
