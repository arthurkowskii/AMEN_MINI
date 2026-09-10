#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

constexpr int LOW = 0, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2;
inline uint32_t clock_us = 0;
inline int levels[64];
inline int modes[64];
inline bool switches[5][5] = {};
inline void digitalWrite(int, int level) { assert(level == LOW); }
inline void pinMode(int pin, int mode) {
    modes[pin] = mode;
    int outputs = 0;
    for (int value : modes) outputs += value == OUTPUT;
    assert(outputs <= 1);
}
inline int digitalRead(int pin) {
    const int rows[] = {5, 6, 9, 14, 15};
    if (pin < 5) {
        for (int row = 0; row < 5; ++row) {
            if (modes[rows[row]] == OUTPUT && switches[row][pin]) return 0;
        }
        return 1;
    }
    return levels[pin];
}
inline uint32_t micros() { return clock_us; }
inline uint32_t millis() { return clock_us / 1000; }
inline void delayMicroseconds(uint32_t time) { clock_us += time; }
inline void noInterrupts() {}
inline void interrupts() {}
inline void yield() { clock_us += 100; }

struct MockSerial {
    std::string input, output;
    int capacity = 64;
    bool connected = true;
    void begin(int) {}
    operator bool() { return connected; }
    int available() { return static_cast<int>(input.size()); }
    int availableForWrite() { return capacity; }
    int read() {
        if (input.empty()) return -1;
        char value = input[0];
        input.erase(0, 1);
        return value;
    }
    size_t write(const uint8_t *data, size_t size) {
        output.append(reinterpret_cast<const char *>(data), size);
        return size;
    }
};
inline MockSerial Serial;
