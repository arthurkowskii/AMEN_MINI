#pragma once

#include <cstddef>
#include <cstdint>

#include "ramp_gain.h"

enum class RepeatDivision : std::uint8_t {
    Quarter,
    Eighth,
    EighthTriplet,     // 1/12 — croche tripletée (0,5 x 2/3)
    Sixteenth,
    SixteenthTriplet,  // 1/24 — double-croche tripletée (0,25 x 2/3)
    ThirtySecond,
};

// LOOP : lecture normale de la boucle capturée. SHEPARD : la position de
// lecture accelere lineairement sur kShepardCyclesPerRise boucles puis
// retombe — chaque passage est plus aigu que le precedent, l'effet
// "montee infinie" (riser par rampe de temps de lecture, pas de pitch
// shifter). La profondeur borne la pente : taux = 1 + depth * phase.
enum class RepeatMode : std::uint8_t {
    Loop,
    Shepard,
};

class LiveRepeat {
public:
    static constexpr std::uint32_t kRampFrames = 128;
    static constexpr std::uint32_t kSeamFrames = 64;
    static constexpr std::uint32_t kMinBpm = 20;
    static constexpr float kMaxBpm = 300.0f;
    // Nombre de boucles par cycle de rampe SHEPARD : 4 passages montants
    // avant la retombee (le wrap est masque par le lissage de couture).
    static constexpr std::uint32_t kShepardCyclesPerRise = 4;
    // Pente maximale du taux de lecture en SHEPARD : taux max = 2.0
    // (une octave d'acceleration par cycle).
    static constexpr float kMaxShepardDepth = 1.0f;

    static constexpr std::size_t requiredBufferFrames(std::uint32_t sampleRate) {
        return static_cast<std::size_t>(sampleRate) * 60U / kMinBpm;
    }

    LiveRepeat(std::uint32_t sampleRate, float* historyLeft,
               float* historyRight, float* frozenLeft, float* frozenRight,
               std::size_t bufferCapacity);

    void setActive(bool active);
    void setAmount(float amount);
    void setBpm(float bpm);
    void setDivision(RepeatDivision division);
    void setMode(RepeatMode mode);
    void setShepardDepth(float depth);
    void process(float* left, float* right, int numFrames);
    // Duree de la courbe de transition d'activation (slew, en frames).
    void setSlewFrames(std::uint32_t frames) { mixRamp_.setSlewFrames(frames); }
    std::uint32_t slewFrames() const { return mixRamp_.slewFrames(); }

    bool isActive() const { return requestedActive_; }
    float amount() const { return amount_; }
    float bpm() const { return bpm_; }
    RepeatDivision division() const { return division_; }
    RepeatMode mode() const { return mode_; }
    float shepardDepth() const { return shepardDepth_; }
    std::size_t loopFrames() const { return loopFrames_; }
    // Observable pour les tests : position de lecture SHEPARD (fractionnaire)
    // dans la boucle courante.
    float loopPositionF() const { return loopPositionF_; }

private:
    std::size_t requestedLoopFrames() const;
    std::size_t availableLoopFrames() const;
    void beginLoopChange();
    float historySample(const float* history, const float* frozen,
                        std::size_t loopFrames, std::size_t position) const;
    float smoothedLoopSample(const float* history, const float* frozen,
                             std::size_t loopFrames, std::size_t position) const;
    float smoothedLoopSampleF(const float* history, const float* frozen,
                              std::size_t loopFrames, float position) const;

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
    float bpm_ = 120.0f;
    RepeatDivision division_ = RepeatDivision::Quarter;
    RepeatMode mode_ = RepeatMode::Loop;
    float shepardDepth_ = 0.5f;

    RampGain mixRamp_;

    std::size_t loopFrames_ = 0;
    std::size_t loopPosition_ = 0;
    std::size_t oldLoopFrames_ = 0;
    std::size_t oldLoopPosition_ = 0;
    std::uint32_t loopCrossfadeRemaining_ = 0;

    // Etat SHEPARD : position fractionnaire et phase de rampe [0,1).
    float loopPositionF_ = 0.0f;
    float shepardPhase_ = 0.0f;
};
