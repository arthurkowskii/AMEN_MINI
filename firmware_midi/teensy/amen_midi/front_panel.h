#pragma once

#include "simple_midi_controller.h"

#include <array>
#include <cstdint>

namespace amen {

enum class E1Page : uint8_t {
    Octave,
    Tempo,
    Frequency,
    Root,
    Preset
};

enum class PanelFeedback : uint8_t {
    None,
    Parameter,
    Shift,
    Mode,
    Bank,
    Velocity,
    Modulation,
    PatternEdit,
    SmartVoicing,
    MidiCc
};

struct PanelResult {
    uint8_t count{};
    PanelFeedback feedback{PanelFeedback::None};
};

// Physical layout: E1-E4 down the left edge, E5-E7 across the top.
// Keep routing portable so tests exercise the same dispatch as the hardware.
class FrontPanel {
public:
    // Preserve E5-E7 mappings; E4 uses the appended CC23 slot.
    static constexpr std::array<uint8_t, 4> kMidiCcNumbers{{20, 21, 22, 23}};
    static constexpr uint8_t midiCcSlot(uint8_t encoder) noexcept {
        return encoder == 3 ? 3 : (encoder >= 4 && encoder < 7 ? static_cast<uint8_t>(encoder - 4) : 4);
    }

    E1Page parameterPage() const noexcept { return parameterPage_; }
    bool modulationSelected() const noexcept { return modulationSelected_; }
    uint8_t midiCcValue(uint8_t slot) const noexcept {
        return slot < midiCcValues_.size() ? midiCcValues_[slot] : 0;
    }

    uint8_t midiCcMultiplier(uint8_t slot) const noexcept {
        return slot < midiCcMultiplierIndices_.size()
            ? static_cast<uint8_t>(1U << midiCcMultiplierIndices_[slot]) : 0;
    }

    PanelResult click(SimpleMidiController& controller, uint8_t encoder,
                      MidiCommand* out, uint8_t capacity) noexcept {
        if (encoder >= 7 || out == nullptr || capacity == 0) return {};
        switch (encoder) {
            case 0:
                // A Shift gesture must not accidentally change the selected parameter.
                if (controller.shiftHeld()) return {};
                if (editingPattern(controller)) return {0, PanelFeedback::PatternEdit};
                parameterPage_ = static_cast<E1Page>((static_cast<uint8_t>(parameterPage_) + 1U) % 5U);
                return {0, PanelFeedback::Parameter};
            case 1:
                return {0, controller.nextPatternBank() ? PanelFeedback::Bank : PanelFeedback::None};
            case 2:
                modulationSelected_ = !modulationSelected_;
                return {0, modulationSelected_ ? PanelFeedback::Modulation : PanelFeedback::Velocity};
            case 3:
                if (controller.shiftHeld()) {
                    controller.toggleSmartVoicing();
                    return {0, PanelFeedback::SmartVoicing};
                }
                [[fallthrough]];
            default: {
                const uint8_t slot = midiCcSlot(encoder);
                midiCcMultiplierIndices_[slot] = static_cast<uint8_t>((midiCcMultiplierIndices_[slot] + 1U) % 4U);
                return {0, PanelFeedback::MidiCc};
            }
        }
    }

    PanelResult turn(SimpleMidiController& controller, uint8_t encoder, int delta,
                     uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        if (encoder >= 7 || delta == 0 || out == nullptr || capacity == 0) return {};
        switch (encoder) {
            case 0:
                return turnParameter(controller, delta, now, out, capacity);
            case 1:
                return {controller.turnPage(delta, out, capacity), PanelFeedback::Mode};
            case 2:
                if (modulationSelected_) {
                    const uint8_t count = controller.turnModulation(scaledDelta(delta, 5), now, out, capacity);
                    return {count, PanelFeedback::Modulation};
                }
                controller.turnVelocity(scaledDelta(delta, 3));
                return {0, PanelFeedback::Velocity};
            default: {
                const uint8_t slot = midiCcSlot(encoder);
                const int64_t next = static_cast<int64_t>(midiCcValues_[slot]) +
                    static_cast<int64_t>(delta) * midiCcMultiplier(slot);
                const uint8_t value = next < 0 ? 0 : (next > 127 ? 127 : static_cast<uint8_t>(next));
                const bool changed = value != midiCcValues_[slot];
                midiCcValues_[slot] = value;
                return changed ? emitCc(slot, out) : PanelResult{0, PanelFeedback::MidiCc};
            }
        }
    }

private:
    static bool editingPattern(const SimpleMidiController& controller) noexcept {
        return controller.page() == PerformancePage::Pattern && controller.patternHeld();
    }

    static int scaledDelta(int delta, int scale) noexcept {
        // Match the historical coarse steps without overflowing on a batched delta.
        const int64_t scaled = static_cast<int64_t>(delta) * scale;
        return scaled < -127 ? -127 : (scaled > 127 ? 127 : static_cast<int>(scaled));
    }

    PanelResult emitCc(uint8_t slot, MidiCommand* out) const noexcept {
        out[0] = {MidiCommandType::ControlChange, kMidiCcNumbers[slot], midiCcValues_[slot]};
        return {1, PanelFeedback::MidiCc};
    }

    PanelResult turnParameter(SimpleMidiController& controller, int delta, uint32_t now,
                              MidiCommand* out, uint8_t capacity) noexcept {
        if (controller.shiftHeld()) {
            return {controller.turnShiftMode(delta, now, out, capacity), PanelFeedback::Shift};
        }
        if (editingPattern(controller)) {
            controller.turnPattern(delta);
            return {0, PanelFeedback::PatternEdit};
        }
        switch (parameterPage_) {
            case E1Page::Octave: controller.turnOctave(delta); break;
            case E1Page::Tempo: controller.turnTempo(delta, now); break;
            case E1Page::Frequency: controller.turnFrequency(delta, now); break;
            case E1Page::Root: controller.turnRoot(delta); break;
            case E1Page::Preset: controller.turnPreset(delta); break;
        }
        return {0, PanelFeedback::Parameter};
    }

    E1Page parameterPage_{E1Page::Octave};
    bool modulationSelected_{};
    std::array<uint8_t, kMidiCcNumbers.size()> midiCcValues_{};
    std::array<uint8_t, kMidiCcNumbers.size()> midiCcMultiplierIndices_{};
};

}
