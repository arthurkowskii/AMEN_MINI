#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include "pcm_wav.h"
#include "stream_mixer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstdio>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {
class FileSource final : public amen::WavReader {
public:
    explicit FileSource(const std::string& path) : file_(std::fopen(path.c_str(), "rb")) {
        if (file_ && _fseeki64(file_, 0, SEEK_END) == 0) {
            const auto length = _ftelli64(file_);
            if (length >= 0) size_ = static_cast<uint64_t>(length);
        }
    }
    ~FileSource() override { if (file_) std::fclose(file_); }
    uint64_t size() const override { return size_; }
    bool readAt(uint32_t offset, void* destination, size_t count) override {
        return file_ && uint64_t(offset) + count <= size_ &&
            _fseeki64(file_, offset, SEEK_SET) == 0 &&
            std::fread(destination, 1, count, file_) == count;
    }
    bool load(size_t count) {
        std::array<unsigned char, 4096> bytes{};
        count = std::min(count, size_t(wav.frames - cursor));
        if (!readAt(wav.offset + cursor * wav.channels * 2, bytes.data(), count * wav.channels * 2)) return false;
        auto decode = [&](size_t i) {
            const int value = int(bytes[i]) | (int(bytes[i + 1]) << 8);
            return static_cast<int16_t>(value >= 32768 ? value - 65536 : value);
        };
        for (size_t i = 0; i < count; ++i) {
            const size_t offset = i * wav.channels * 2;
            buffer[i].left = decode(offset);
            buffer[i].right = wav.channels == 1 ? buffer[i].left : decode(offset + 2);
        }
        buffered = count;
        cursor += static_cast<uint32_t>(count);
        return true;
    }
    amen::PcmWav wav{};
    std::array<amen::StereoFrame, 1024> buffer{};
    uint32_t cursor = 0;
    size_t buffered = 0;
private:
    std::FILE* file_ = nullptr;
    uint64_t size_ = 0;
};

std::unique_ptr<FileSource> openWav(const std::string& path) {
    auto source = std::make_unique<FileSource>(path);
    if (!amen::parseWav(*source, source->wav) || !source->load(1024)) {
        std::fprintf(stderr, "Cannot read PCM16 44100 Hz mono/stereo WAV: %s\n", path.c_str());
        return nullptr;
    }
    return source;
}

struct Audio {
    amen::StreamMixer mixer;
    std::atomic<uint32_t> frames{0}, nonzero{0};
};

void callback(ma_device* device, void* output, const void*, ma_uint32 count) {
    auto& audio = *static_cast<Audio*>(device->pUserData);
    auto* stereo = static_cast<int16_t*>(output);
    std::array<int16_t, 128> left{}, right{};
    uint32_t nonzero = 0;
    for (size_t offset = 0; offset < count; offset += 128) {
        const size_t chunk = std::min(size_t(128), size_t(count) - offset);
        audio.mixer.render(left.data(), right.data(), chunk);
        for (size_t i = 0; i < chunk; ++i) {
            stereo[2 * (offset + i)] = left[i];
            stereo[2 * (offset + i) + 1] = right[i];
            if (left[i] || right[i]) ++nonzero;
        }
    }
    audio.frames.fetch_add(count, std::memory_order_relaxed);
    audio.nonzero.fetch_add(nonzero, std::memory_order_relaxed);
}

class Harness {
public:
    Audio audio;
    std::array<std::string, 20> pads{};
    uint32_t started = 0, eof = 0, errors = 0;

    bool assign(int pad, const std::string& path) {
        if (!validPad(pad) || !openWav(path)) return false;
        pads[size_t(pad - 1)] = path;
        std::printf("Assigned pad %d: %s\n", pad, path.c_str());
        return true;
    }
    bool play(int pad) {
        if (!validPad(pad)) return false;
        auto next = openWav(pads[size_t(pad - 1)]);
        if (!next) return false;
        size_t selected = slots_.size();
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].pad == pad) { selected = i; break; }
        if (selected == slots_.size())
            for (size_t i = 0; i < slots_.size(); ++i)
                if (!slots_[i].pending && audio.mixer.idle(i)) { selected = i; break; }
        if (selected == slots_.size()) {
            selected = 0;
            for (size_t i = 1; i < slots_.size(); ++i)
                if (slots_[i].age < slots_[selected].age) selected = i;
        }
        auto& slot = slots_[selected];
        slot.pending = std::move(next);
        slot.pad = pad;
        slot.age = ++age_;
        if (!audio.mixer.idle(selected)) {
            slot.stopping = true;
            audio.mixer.stop(selected);
        }
        service();
        return true;
    }
    void stop() {
        for (size_t i = 0; i < slots_.size(); ++i) {
            slots_[i].pending.reset();
            slots_[i].stopping = true;
            audio.mixer.stop(i);
        }
    }
    void service() {
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (audio.mixer.idle(i)) {
                if (slot.source) {
                    if (!slot.stopping && slot.source->cursor == slot.source->wav.frames) ++eof;
                    slot.source.reset();
                }
                if (!slot.pending) { slot.pad = 0; continue; }
                slot.source = std::move(slot.pending);
                slot.stopping = false;
                audio.mixer.prepare(i, slot.source->wav.frames);
                if (refill(i)) { audio.mixer.start(i); ++started; }
                else slot.source.reset();
            } else if (!slot.stopping) {
                if (!refill(i)) { slot.stopping = true; audio.mixer.stop(i); }
            }
        }
    }
private:
    struct Slot {
        std::unique_ptr<FileSource> source, pending;
        uint64_t age = 0;
        int pad = 0;
        bool stopping = false;
    };
    std::array<Slot, amen::StreamMixer::kVoices> slots_{};
    uint64_t age_ = 0;
    static bool validPad(int pad) {
        if (pad >= 1 && pad <= 20) return true;
        std::fprintf(stderr, "Pad must be 1..20\n");
        return false;
    }
    bool refill(size_t i) {
        auto& source = *slots_[i].source;
        while (audio.mixer.writable(i) > 0) {
            if (source.buffered == 0) {
                if (source.cursor == source.wav.frames) break;
                if (!source.load(std::min(size_t(1024), size_t(audio.mixer.writable(i))))) {
                    ++errors;
                    std::fprintf(stderr, "Read failure on voice %zu\n", i + 1);
                    return false;
                }
            }
            if (audio.mixer.writable(i) < source.buffered) break;
            audio.mixer.write(i, source.buffer.data(), source.buffered);
            source.buffered = 0;
        }
        return true;
    }
};

bool command(Harness& harness, const std::string& line) {
    std::istringstream input(line);
    std::string action;
    input >> action;
    if (action == "quit") return false;
    if (action == "stop") { harness.stop(); return true; }
    int pad = 0;
    if (action == "play" && input >> pad) harness.play(pad);
    else if (action == "assign" && input >> pad) {
        std::string path;
        input >> std::ws;
        if (input.peek() == '"') input >> std::quoted(path);
        else std::getline(input, path);
        harness.assign(pad, path);
    } else std::puts("Commands: assign <1..20> <path> | play <1..20> | stop | quit");
    return true;
}
}

int main(int argc, char** argv) {
    bool smoke = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--smoke") smoke = true;
        else if (path.empty()) path = argv[i];
        else { std::fprintf(stderr, "Usage: amen_rt [--smoke] [file.wav]\n"); return 1; }
    }
    if (smoke && path.empty()) { std::fprintf(stderr, "--smoke requires a WAV\n"); return 1; }
    bool expectEof = false;
    if (smoke) {
        auto source = openWav(path);
        if (!source) return 1;
        expectEof = source->wav.frames <= 44100;
    }
    Harness harness;
    const int voices = smoke ? 4 : 1;
    if (!path.empty())
        for (int pad = 1; pad <= voices; ++pad)
            if (!harness.assign(pad, path) || !harness.play(pad)) return 1;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = 44100;
    config.periodSizeInFrames = 128;
    config.dataCallback = callback;
    config.pUserData = &harness.audio;
    ma_device device{};
    if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS) {
        std::fprintf(stderr, "Audio device initialization failed\n"); return 1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "Audio device start failed\n"); ma_device_uninit(&device); return 1;
    }
    std::puts("Native streaming diagnostics: 4 voices, 20 pads, PCM16/44100.");
    std::puts("Type then Enter: assign <pad> <path> | play <pad> | stop | quit");
    const auto begin = std::chrono::steady_clock::now();
    std::string line;
    bool running = true;
    while (running) {
        harness.service();
        if (smoke) {
            running = std::chrono::steady_clock::now() - begin < std::chrono::seconds(2);
        } else {
            while (_kbhit()) {
                const int key = _getch();
                if (key == 0 || key == 224) { _getch(); continue; }
                if (key == '\r') { std::putchar('\n'); running = command(harness, line); line.clear(); break; }
                if (key == '\b' && !line.empty()) { line.pop_back(); std::printf("\b \b"); }
                else if (key >= 32 && key < 127 && line.size() < 4096) { line += char(key); std::putchar(key); }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    harness.service();
    harness.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ma_device_uninit(&device);
    const auto frames = harness.audio.frames.load();
    std::printf("started=%u eof=%u frames=%u nonzero=%u underruns=%u read_errors=%u\n",
        harness.started, harness.eof, frames, harness.audio.nonzero.load(),
        harness.audio.mixer.underruns(), harness.errors);
    return harness.errors || (smoke && (harness.started != 4 || frames == 0 ||
        (expectEof && harness.eof != 4) || harness.audio.mixer.underruns() != 0)) ? 1 : 0;
}
