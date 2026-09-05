#pragma once
#include <vector>

struct MockWire {
    int status = 0;
    uint8_t control = 0;
    std::vector<uint8_t> pixels;
    void begin() {}
    void setClock(unsigned) {}
    void beginTransmission(int) {}
    size_t write(uint8_t value) { control = value; return 1; }
    size_t write(const uint8_t *data, size_t size) {
        if (control == 0x40) pixels.insert(pixels.end(), data, data + size);
        return size;
    }
    uint8_t endTransmission() { return status; }
};
inline MockWire Wire;
