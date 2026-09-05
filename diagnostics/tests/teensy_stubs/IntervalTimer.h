#pragma once
struct IntervalTimer {
    bool begin(void (*)(), unsigned) { return true; }
    void priority(int) {}
};
