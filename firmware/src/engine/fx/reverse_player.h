#pragma once

#include <cstddef>
#include <cstdint>

#include "ramp_gain.h"

// REVERSE (pad FX) : boucle inversee du materiau qui precedait l'appui.
//
// Le buffer d'historique tourne en permanence ; a l'activation, une fenetre
// fixe de kWindowSeconds (plafonnee a ce qui a ete capture) est lue A
// L'ENVERS, en boucle, tant que le pad FX est tenu. Le lissage de couture
// et la rampe de slew (RampGain) garantissent qu'aucun etat ne commute sec :
// l'entree en reverse glisse comme un filtre (courbe de transition).
//
// Memoire fixe fournie par l'appelant, aucune allocation, aucun appel
// bloquant dans process(). Non reentrant : serialise par l'appelant.
class ReversePlayer {
public:
    static constexpr float kWindowSeconds = 2.0f;
    static constexpr std::uint32_t kSeamFrames = 64;

    static constexpr std::size_t requiredBufferFrames(std::uint32_t sampleRate) {
        return static_cast<std::size_t>(
            static_cast<float>(sampleRate) * kWindowSeconds);
    }

    ReversePlayer(std::uint32_t sampleRate, float* historyLeft,
                  float* historyRight, float* frozenLeft, float* frozenRight,
                  std::size_t bufferCapacity);

    void setActive(bool active);
    void setAmount(float amount);
    void setSlewFrames(std::uint32_t frames) { mixRamp_.setSlewFrames(frames); }
    void process(float* left, float* right, int numFrames);

    bool isActive() const { return requestedActive_; }
    float amount() const { return amount_; }
    std::uint32_t slewFrames() const { return mixRamp_.slewFrames(); }
    std::size_t loopFrames() const { return loopFrames_; }

private:
    float historySample(const float* history, const float* frozen,
                        std::size_t positionFromEnd) const;
    float smoothedReverseSample(const float* history, const float* frozen,
                                std::size_t positionFromEnd) const;

    std::uint32_t sampleRate_;
    float* historyL_;
    float* historyR_;
    float* frozenL_;
    float* frozenR_;
    std::size_t bufferCapacity_;
    std::uint64_t framesWritten_ = 0;
    std::uint64_t captureEnd_ = 0;

    bool requestedActive_ = false;
    float amount_ = 1.0f;
    RampGain mixRamp_;

    std::size_t loopFrames_ = 0;
    std::size_t loopPosition_ = 0;  // 0 = frame la plus recente capturee
};
