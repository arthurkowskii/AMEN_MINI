#pragma once
#include <Audio.h>
#include <SD.h>
#include <algorithm>
#include <cstring>
#include "../engine/pcm_wav.h"
#include "../engine/pad_browser.h"
#include "../engine/stream_mixer.h"

namespace amen {
class SdReader : public WavReader {
public:
    explicit SdReader(File& file, void (*service)() = nullptr) : file_(file), service_(service) {}
    uint64_t size() const override { return file_.size(); }
    bool readAt(uint32_t offset, void* destination, size_t count) override {
        if (service_) service_();
        return file_.seek(offset) && file_.read(destination, count) == count;
    }
private:
    File& file_;
    void (*service_)();
};
class AudioStreams : public AudioStream {
public:
    AudioStreams() : AudioStream(0, nullptr) {}
    StreamMixer mixer;
    void update() override {
        audio_block_t* left = allocate();
        audio_block_t* right = allocate();
        if (!left || !right) {
            if (left) release(left);
            if (right) release(right);
            mixer.render(scratchLeft_, scratchRight_, AUDIO_BLOCK_SAMPLES);
            return;
        }
        mixer.render(left->data, right->data, AUDIO_BLOCK_SAMPLES);
        transmit(left, 0);
        transmit(right, 1);
        release(left);
        release(right);
    }
private:
    int16_t scratchLeft_[AUDIO_BLOCK_SAMPLES]{}, scratchRight_[AUDIO_BLOCK_SAMPLES]{};
};
class SdStreams {
public:
    explicit SdStreams(StreamMixer& mixer) : mixer_(mixer) {}
    bool trigger(uint8_t pad, const char* path, const PcmWav& wav) {
        size_t chosen = 0;
        bool found = false;
        for (size_t i = 0; i < 4; ++i) {
            if (slots_[i].pad == pad && (!mixer_.idle(i) || slots_[i].pending || slots_[i].priming)) { chosen = i; found = true; break; }
        }
        if (!found) {
            for (size_t i = 0; i < 4; ++i) {
                if (mixer_.idle(i) && !slots_[i].pending && !slots_[i].priming) { chosen = i; found = true; break; }
            }
        }
        if (!found) for (size_t i = 1; i < 4; ++i) if (slots_[i].order < slots_[chosen].order) chosen = i;
        if (strlen(path) >= sizeof(slots_[chosen].path)) return false;
        auto& slot = slots_[chosen];
        strcpy(slot.path, path);
        slot.wav = wav;
        slot.pad = pad;
        slot.order = ++order_;
        slot.pending = true;
        mixer_.stop(chosen);
        return true;
    }
    void service() {
        for (size_t i = 0; i < 4; ++i) {
            auto& slot = slots_[i];
            if (mixer_.idle(i) && (!slot.priming || slot.pending)) {
                if (!slot.pending) { if (slot.file) slot.file.close(); continue; }
                slot.file.close();
                slot.file = SD.open(slot.path, FILE_READ);
                slot.pending = false;
                slot.remaining = slot.wav.frames;
                if (!slot.file || !slot.file.seek(slot.wav.offset)) { fail(slot); continue; }
                mixer_.prepare(i, slot.wav.frames);
                slot.priming = true;
            }
            if (slot.pending || !slot.file) continue;
            const uint32_t count = std::min(uint32_t(512), std::min(slot.remaining, mixer_.writable(i)));
            if (count) {
                const uint32_t bytes = count * slot.wav.channels * 2;
                if (slot.file.read(bytes_, bytes) != bytes) {
                    fail(slot);
                    if (!mixer_.idle(i)) mixer_.stop(i);
                    continue;
                }
                for (uint32_t f = 0; f < count; ++f) {
                    const uint32_t p = f * slot.wav.channels * 2;
                    frames_[f].left = decode(bytes_ + p);
                    frames_[f].right = slot.wav.channels == 1 ? frames_[f].left : decode(bytes_ + p + 2);
                }
                mixer_.write(i, frames_, count);
                slot.remaining -= count;
            }
            if (slot.priming) {
                if (slot.remaining && mixer_.writable(i) > StreamMixer::kCapacity - 4096) continue;
                slot.priming = false;
                mixer_.start(i);
            }
        }
    }
    bool takeError() { const bool result = error_; error_ = false; return result; }
private:
    struct Slot {
        File file;
        char path[PadBrowser::kPath]{};
        PcmWav wav;
        uint64_t order = 0;
        uint32_t remaining = 0;
        uint8_t pad = 255;
        bool pending = false, priming = false;
    } slots_[4];
    static int16_t decode(const uint8_t* p) { return static_cast<int16_t>(uint16_t(p[0]) | uint16_t(p[1]) << 8); }
    void fail(Slot& slot) { slot.file.close(); slot.priming = false; error_ = true; }
    StreamMixer& mixer_;
    uint64_t order_ = 0;
    bool error_ = false;
    uint8_t bytes_[2048]{};
    StereoFrame frames_[512]{};
};
}
