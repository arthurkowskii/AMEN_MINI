#pragma once
#include <cstdint>

constexpr unsigned AUDIO_BLOCK_SAMPLES = 128;
struct audio_block_t { int16_t data[AUDIO_BLOCK_SAMPLES]{}; };
class AudioStream {
public:
    AudioStream(unsigned, void*) {}
    virtual ~AudioStream() = default;
    virtual void update() = 0;
protected:
    audio_block_t* allocate() { return nullptr; }
    void release(audio_block_t*) {}
    void transmit(audio_block_t*, unsigned) {}
};
