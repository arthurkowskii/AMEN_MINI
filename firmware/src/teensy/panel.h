#pragma once
#include <Arduino.h>
#include <IntervalTimer.h>
#include <util/atomic.h>

namespace amen {
class Panel {
public:
    struct Snapshot { uint32_t keys; uint32_t encoder; bool click; uint32_t scans; uint32_t volumeEncoder; };
    void begin(void (*callback)()) {
        for (uint8_t pin : rows_) { pinMode(pin, INPUT); digitalWrite(pin, LOW); }
        for (uint8_t pin : cols_) pinMode(pin, INPUT_PULLUP);
        pinMode(35, INPUT_PULLUP);
        for (uint8_t i = 0; i < 2; ++i) {
            pinMode(encoderA_[i], INPUT_PULLUP);
            pinMode(encoderB_[i], INPUT_PULLUP);
            ab_[i] = (digitalRead(encoderA_[i]) << 1) | digitalRead(encoderB_[i]);
        }
        timer_.begin(callback, 500);
        timer_.priority(64);
    }
    void scan() {
        const uint32_t now = micros();
        bool sample[22]{};
        for (uint8_t row = 0; row < 5; ++row) {
            digitalWrite(rows_[row], LOW); pinMode(rows_[row], OUTPUT);
            delayMicroseconds(3);
            for (uint8_t col = 0; col < 4; ++col) sample[(4 - row) * 4 + col] = !digitalRead(cols_[col]);
            if (row == 4) sample[20] = !digitalRead(cols_[4]);
            pinMode(rows_[row], INPUT);
        }
        sample[21] = !digitalRead(35);
        for (uint8_t i = 0; i < 22; ++i) {
            if (!scans_ || sample[i] != raw_[i]) { raw_[i] = sample[i]; changed_[i] = now; }
            if (now - changed_[i] >= 5000) {
                if (raw_[i]) keys_ |= 1UL << i; else keys_ &= ~(1UL << i);
            }
        }
        for (uint8_t i = 0; i < 2; ++i) {
            const uint8_t ab = (digitalRead(encoderA_[i]) << 1) | digitalRead(encoderB_[i]);
            if ((ab ^ ab_[i]) == 3) partial_[i] = 0;
            else {
                partial_[i] += quadrature_[ab_[i] * 4 + ab];
                if (partial_[i] == 4 || partial_[i] == -4) {
                    encoders_[i] += partial_[i] > 0 ? 1U : UINT32_MAX;
                    partial_[i] = 0;
                }
            }
            ab_[i] = ab;
        }
        ++scans_;
    }
    Snapshot snapshot() const {
        const uint32_t saved = __get_primask();
        __disable_irq();
        Snapshot result{keys_ & 0x1fffffU, encoders_[0], (keys_ & (1UL << 21)) != 0, scans_, encoders_[1]};
        if (!saved) __enable_irq();
        return result;
    }
private:
    static constexpr uint8_t rows_[5] = {5, 6, 9, 14, 15};
    static constexpr uint8_t cols_[5] = {0, 1, 2, 3, 4};
    static constexpr uint8_t encoderA_[2] = {16, 33};
    static constexpr uint8_t encoderB_[2] = {17, 34};
    static constexpr int8_t quadrature_[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
    IntervalTimer timer_;
    volatile uint32_t keys_ = 0, encoders_[2]{}, scans_ = 0;
    bool raw_[22]{};
    uint32_t changed_[22]{};
    uint8_t ab_[2]{};
    int8_t partial_[2]{};
};
}
