#pragma once
struct MockEntropy {
    void Initialize() {}
    int available() { return 16; }
    uint32_t random() { return 42; }
};
inline MockEntropy Entropy;
