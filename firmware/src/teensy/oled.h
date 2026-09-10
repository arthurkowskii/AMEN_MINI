#pragma once
#include <Wire.h>
#include <cstring>

namespace amen {
class Oled {
public:
    bool begin() {
        for (uint8_t address = 0x3c; address <= 0x3d; ++address) {
            Wire.beginTransmission(address);
            if (!Wire.endTransmission()) { address_ = address; break; }
        }
        if (!address_) return false;
        const uint8_t init[] = {0xae,0xd5,0x80,0xa8,0x1f,0xd3,0,0x40,0x8d,0x14,0x20,0,0xa1,0xc8,0xda,2,0x81,0x8f,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0xaf};
        ready_ = write(0, init, sizeof(init));
        return ready_;
    }
    bool busy() const { return pending_; }
    void clear() { memset(frame_, 0, sizeof(frame_)); }
    void text(uint8_t row, const char* text) {
        if (row > 3) return;
        for (size_t x = 0; *text && x < 128; x += 4, ++text) {
            char c = *text;
            if (c >= 'a' && c <= 'z') c -= 32;
            const uint16_t bits = glyph(c);
            for (uint8_t y = 0; y < 5; ++y) for (uint8_t col = 0; col < 3; ++col)
                if (x + col < 128 && (bits & (1U << (14 - y * 3 - col)))) frame_[row * 128 + x + col] |= 1U << y;
        }
    }
    void queue() { if (ready_) { offset_ = 0; pending_ = true; } }
    bool service() {
        if (!ready_ || !pending_) return ready_;
        if (!offset_) {
            const uint8_t window[] = {0x21,0,127,0x22,0,3};
            if (!write(0, window, sizeof(window))) return fail();
        }
        if (!write(0x40, frame_ + offset_, 16)) return fail();
        offset_ += 16;
        if (offset_ == sizeof(frame_)) pending_ = false;
        return true;
    }
private:
    static uint16_t glyph(char c) {
        static constexpr uint16_t letters[] = {
            0b010101111101101,0b110101110101110,0b011100100100011,0b110101101101110,
            0b111100110100111,0b111100110100100,0b011100101101011,0b101101111101101,
            0b111010010010111,0b001001001101010,0b101101110101101,0b100100100100111,
            0b101111111101101,0b101111111111101,0b010101101101010,0b110101110100100,
            0b010101101111011,0b110101110101101,0b011100010001110,0b111010010010010,
            0b101101101101111,0b101101101101010,0b101101111111101,0b101101010101101,
            0b101101010010010,0b111001010100111};
        static constexpr uint16_t digits[] = {0b111101101101111,0b010110010010111,0b111001111100111,
            0b111001111001111,0b101101111001001,0b111100111001111,0b111100111101111,
            0b111001010010010,0b111101111101111,0b111101111001111};
        if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
        if (c >= '0' && c <= '9') return digits[c - '0'];
        if (c == '.') return 0b000000000000010;
        if (c == '/') return 0b001001010100100;
        if (c == '>') return 0b100010001010100;
        if (c == '-') return 0b000000111000000;
        if (c == '+') return 0b000010111010000;
        if (c == '_') return 7;
        return c == ' ' ? 0 : 0b111001010000010;
    }
    bool write(uint8_t control, const uint8_t* data, size_t count) {
        Wire.beginTransmission(address_); Wire.write(control);
        const bool complete = Wire.write(data, count) == count;
        const uint8_t status = Wire.endTransmission();
        return complete && status == 0;
    }
    bool fail() { ready_ = pending_ = false; return false; }
    uint8_t frame_[512]{}, address_ = 0;
    size_t offset_ = 0;
    bool pending_ = false, ready_ = false;
};
}
