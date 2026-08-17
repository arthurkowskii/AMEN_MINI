// Tests natifs du tampon de capture retrospective (plan 8, COMMIT).
// Compilation stricte (-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
// -Werror -fno-exceptions -fno-rtti) + ASan/UBSan.
#include "capture_buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr std::uint32_t kSampleRate = 44100;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// 1. Round-trip avant saturation : l'extraction restitue exactement les
// dernieres windowFrames frames, en ordre chronologique.
void testRoundTripBeforeFull() {
    constexpr std::size_t kCapacity = 4096;
    std::vector<float> storageL(kCapacity, 0.0f);
    std::vector<float> storageR(kCapacity, 0.0f);
    CaptureBuffer buffer(kSampleRate, storageL.data(), storageR.data(),
                         kCapacity);

    std::vector<float> inputL(1000);
    std::vector<float> inputR(1000);
    for (std::size_t i = 0; i < inputL.size(); ++i) {
        inputL[i] = static_cast<float>(i);
        inputR[i] = -static_cast<float>(i);
    }
    buffer.record(inputL.data(), inputR.data(), 1000);

    std::vector<float> outL(1000, -1.0f);
    std::vector<float> outR(1000, -1.0f);
    const std::size_t count = buffer.extractWindow(outL.data(), outR.data(), 300);
    require(count == 300, "extract must return the requested window");
    for (std::size_t i = 0; i < count; ++i) {
        require(outL[i] == static_cast<float>(700 + i),
                "left window must be chronological (700..999)");
        require(outR[i] == -static_cast<float>(700 + i),
                "right window must mirror the input");
    }
}

// 2. Saturation : apres plus de capacityFrames frames, l'extraction donne
// exactement les dernieres windowFrames (lecture circulaire correcte).
void testRoundTripAfterWrap() {
    constexpr std::size_t kCapacity = 512;
    std::vector<float> storageL(kCapacity, 0.0f);
    std::vector<float> storageR(kCapacity, 0.0f);
    CaptureBuffer buffer(kSampleRate, storageL.data(), storageR.data(),
                         kCapacity);

    std::vector<float> inputL(2000);
    std::vector<float> inputR(2000);
    for (std::size_t i = 0; i < inputL.size(); ++i) {
        inputL[i] = static_cast<float>(i);
        inputR[i] = static_cast<float>(i) * 2.0f;
    }
    for (std::size_t start = 0; start < 2000; start += 137) {
        const int chunk = static_cast<int>(std::min<std::size_t>(137, 2000 - start));
        buffer.record(inputL.data() + start, inputR.data() + start, chunk);
    }
    require(buffer.recordedFrames() == kCapacity,
            "recorded frames must saturate at capacity");

    std::vector<float> outL(512, -1.0f);
    std::vector<float> outR(512, -1.0f);
    const std::size_t count = buffer.extractWindow(outL.data(), outR.data(), 512);
    require(count == 512, "full window must be returned after saturation");
    for (std::size_t i = 0; i < count; ++i) {
        require(outL[i] == static_cast<float>(1488 + i),
                "left wrap window must be the last 512 frames (1488..1999)");
        require(outR[i] == static_cast<float>(1488 + i) * 2.0f,
                "right wrap window must match");
    }
}

// 3. Extraction plafonnee a l'historique reel : rien d'enregistre -> 0 ;
// moins de windowFrames -> tout l'historique, en ordre.
void testExtractClampedToRecorded() {
    constexpr std::size_t kCapacity = 1024;
    std::vector<float> storageL(kCapacity, 0.0f);
    std::vector<float> storageR(kCapacity, 0.0f);
    CaptureBuffer buffer(kSampleRate, storageL.data(), storageR.data(),
                         kCapacity);

    std::vector<float> outL(1024, -1.0f);
    std::vector<float> outR(1024, -1.0f);
    require(buffer.extractWindow(outL.data(), outR.data(), 100) == 0,
            "empty history must extract 0 frames");
    require(outL[0] == -1.0f, "empty extract must not touch the output");

    std::vector<float> inputL(50);
    std::vector<float> inputR(50);
    for (std::size_t i = 0; i < 50; ++i) inputL[i] = static_cast<float>(i + 1);
    buffer.record(inputL.data(), inputR.data(), 50);
    const std::size_t count = buffer.extractWindow(outL.data(), outR.data(), 100);
    require(count == 50, "extract must clamp to the real history");
    require(outL[0] == 1.0f && outL[49] == 50.0f,
            "short history must be chronological from frame 0");
}

// 4. Determinisme : la meme sequence enregistree puis extraite deux fois
// donne des octets identiques.
void testDeterminism() {
    constexpr std::size_t kCapacity = 2048;
    std::vector<float> storageL(kCapacity, 0.0f);
    std::vector<float> storageR(kCapacity, 0.0f);
    CaptureBuffer buffer(kSampleRate, storageL.data(), storageR.data(),
                         kCapacity);

    std::vector<float> inputL(3000);
    std::vector<float> inputR(3000);
    std::uint32_t state = 4242U;
    for (std::size_t i = 0; i < inputL.size(); ++i) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        inputL[i] = static_cast<float>(state) / 4294967296.0f - 0.5f;
        inputR[i] = -inputL[i];
    }
    for (std::size_t start = 0; start < 3000; start += 251) {
        const int chunk = static_cast<int>(std::min<std::size_t>(251, 3000 - start));
        buffer.record(inputL.data() + start, inputR.data() + start, chunk);
    }
    std::vector<float> a(2048), b(2048), ar(2048), br(2048);
    const std::size_t n1 = buffer.extractWindow(a.data(), ar.data(), 2048);
    const std::size_t n2 = buffer.extractWindow(b.data(), br.data(), 2048);
    require(n1 == n2, "repeat extraction must return the same size");
    require(std::memcmp(a.data(), b.data(), n1 * sizeof(float)) == 0,
            "repeat extraction must be byte-identical");
    require(std::memcmp(ar.data(), br.data(), n1 * sizeof(float)) == 0,
            "repeat extraction right must be byte-identical");
}

// 5. Defensif : pointeurs nuls et tailles non positives ignores ; capacite
// nulle ne lit ni n'ecrit rien.
void testDefensive() {
    std::vector<float> storageL(64, 0.0f);
    std::vector<float> storageR(64, 0.0f);
    CaptureBuffer buffer(kSampleRate, storageL.data(), storageR.data(), 64);
    std::vector<float> inputL(16, 0.5f);
    std::vector<float> inputR(16, 0.5f);
    const std::vector<float> copyL = inputL;
    buffer.record(nullptr, inputR.data(), 16);
    buffer.record(inputL.data(), nullptr, 16);
    buffer.record(inputL.data(), inputR.data(), 0);
    buffer.record(inputL.data(), inputR.data(), -4);
    require(buffer.recordedFrames() == 0, "invalid record calls must be no-ops");
    require(std::memcmp(inputL.data(), copyL.data(), 16 * sizeof(float)) == 0,
            "record must not touch the input buffers");

    std::vector<float> outL(16, -1.0f);
    require(buffer.extractWindow(nullptr, outL.data(), 16) == 0,
            "null output must extract 0");

    // Capacite nulle : aucun crash, aucune ecriture.
    float dummyL = 0.0f;
    float dummyR = 0.0f;
    CaptureBuffer empty(kSampleRate, &dummyL, &dummyR, 0);
    empty.record(inputL.data(), inputR.data(), 16);
    require(empty.recordedFrames() == 0, "zero capacity must record nothing");
}

// 6. Longueur COMMIT par defaut : requiredBufferFrames(44100, 15) = 15 s
// exactes de stereo.
void testDefaultWindowSizing() {
    const std::size_t frames = CaptureBuffer::requiredBufferFrames(44100, 15);
    require(frames == 44100U * 15U,
            "15 s at 44.1 kHz must size 661500 frames");
    const std::size_t tenSeconds = CaptureBuffer::requiredBufferFrames(48000, 10);
    require(tenSeconds == 48000U * 10U,
            "10 s at 48 kHz must size 480000 frames");
}
}  // namespace

int main() {
    testRoundTripBeforeFull();
    testRoundTripAfterWrap();
    testExtractClampedToRecorded();
    testDeterminism();
    testDefensive();
    testDefaultWindowSizing();
    std::cout << "All Capture Buffer tests passed\n";
    return 0;
}
