#pragma once

#include <cstddef>
#include <cstdint>

enum class RepeatDivision : std::uint8_t {
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
};

class LiveRepeat {
public:
    static constexpr std::uint32_t kRampFrames = 128;
    static constexpr std::uint32_t kSeamFrames = 64;
    static constexpr std::uint32_t kMinBpm = 20;
    static constexpr float kMaxBpm = 300.0f;

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
    void process(float* left, float* right, int numFrames);

    bool isActive() const { return requestedActive_; }
    float amount() const { return amount_; }
    float bpm() const { return bpm_; }
    RepeatDivision division() const { return division_; }
    std::size_t loopFrames() const { return loopFrames_; }

private:
    std::size_t requestedLoopFrames() const;
    std::size_t availableLoopFrames() const;
    void beginLoopChange();
    float historySample(const float* history, const float* frozen,
                        std::size_t loopFrames, std::size_t position) const;
    float smoothedLoopSample(const float* history, const float* frozen,
                             std::size_t loopFrames, std::size_t position) const;

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

    float mix_ = 0.0f;
    float targetMix_ = 0.0f;
    float mixStep_ = 0.0f;
    std::uint32_t mixRampRemaining_ = 0;

    std::size_t loopFrames_ = 0;
    std::size_t loopPosition_ = 0;
    std::size_t oldLoopFrames_ = 0;
    std::size_t oldLoopPosition_ = 0;
    std::uint32_t loopCrossfadeRemaining_ = 0;
};
