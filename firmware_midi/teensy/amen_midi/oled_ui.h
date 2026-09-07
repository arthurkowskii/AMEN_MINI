#pragma once

#include "simple_midi_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace amen {

enum class E1Page : uint8_t {
    Octave,
    Tempo,
    Frequency
};

enum class E2Page : uint8_t {
    Root,
    Preset
};

class MonoFramebuffer {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 32;
    static constexpr std::size_t kSize = kWidth * kHeight / 8;

    void clear() noexcept { pixels_.fill(0); }

    void setPixel(int x, int y) noexcept {
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
        pixels_[static_cast<std::size_t>(y / 8) * kWidth + x] |= static_cast<uint8_t>(1U << (y % 8));
    }

    void fillRect(int x, int y, int width, int height) noexcept {
        for (int py = y; py < y + height; ++py)
            for (int px = x; px < x + width; ++px) setPixel(px, py);
    }

    void drawText(int x, int y, const char* text, int scale = 1) noexcept {
        while (*text != '\0') {
            drawChar(x, y, *text++, scale);
            x += 4 * scale;
        }
    }

    const std::array<uint8_t, kSize>& pixels() const noexcept { return pixels_; }

private:
    static std::array<uint8_t, 5> glyph(char c) noexcept {
        switch (c) {
            case '(': return {1, 2, 2, 2, 1};
            case ')': return {4, 2, 2, 2, 4};
            case '#': return {5, 7, 5, 7, 5};
            case '+': return {0, 2, 7, 2, 0};
            case '*': return {0, 5, 2, 5, 0};
            case '-': return {0, 0, 7, 0, 0};
            case '.': return {0, 0, 0, 0, 2};
            case '/': return {4, 4, 2, 2, 1};
            case '0': return {7, 5, 5, 5, 7};
            case '1': return {2, 6, 2, 2, 7};
            case '2': return {7, 1, 7, 4, 7};
            case '3': return {7, 1, 7, 1, 7};
            case '4': return {5, 5, 7, 1, 1};
            case '5': return {7, 4, 7, 1, 7};
            case '6': return {7, 4, 7, 5, 7};
            case '7': return {7, 1, 2, 2, 2};
            case '8': return {7, 5, 7, 5, 7};
            case '9': return {7, 5, 7, 1, 7};
            case 'A': return {2, 5, 7, 5, 5};
            case 'B': return {6, 5, 6, 5, 6};
            case 'C': return {3, 4, 4, 4, 3};
            case 'D': return {6, 5, 5, 5, 6};
            case 'E': return {7, 4, 6, 4, 7};
            case 'F': return {7, 4, 6, 4, 4};
            case 'G': return {3, 4, 5, 5, 3};
            case 'H': return {5, 5, 7, 5, 5};
            case 'I': return {7, 2, 2, 2, 7};
            case 'J': return {1, 1, 1, 5, 2};
            case 'K': return {5, 5, 6, 5, 5};
            case 'L': return {4, 4, 4, 4, 7};
            case 'M': return {5, 7, 7, 5, 5};
            case 'N': return {5, 7, 7, 7, 5};
            case 'O': return {2, 5, 5, 5, 2};
            case 'P': return {6, 5, 6, 4, 4};
            case 'Q': return {2, 5, 5, 7, 3};
            case 'R': return {6, 5, 6, 5, 5};
            case 'S': return {3, 4, 2, 1, 6};
            case 'T': return {7, 2, 2, 2, 2};
            case 'U': return {5, 5, 5, 5, 7};
            case 'V': return {5, 5, 5, 5, 2};
            case 'W': return {5, 5, 7, 7, 5};
            case 'X': return {5, 5, 2, 5, 5};
            case 'Y': return {5, 5, 2, 2, 2};
            case 'Z': return {7, 1, 2, 4, 7};
            case 'b': return {4, 4, 6, 5, 6};
            default: return {0, 0, 0, 0, 0};
        }
    }

    void drawChar(int x, int y, char c, int scale) noexcept {
        const auto rows = glyph(c);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((rows[static_cast<std::size_t>(row)] & (1U << (2 - column))) == 0) continue;
                fillRect(x + column * scale, y + row * scale, scale, scale);
            }
        }
    }

    std::array<uint8_t, kSize> pixels_{};
};

class OledUi {
public:
    void showE1(E1Page page, uint32_t now) noexcept {
        overlay_ = page == E1Page::Octave ? Overlay::Octave
            : (page == E1Page::Tempo ? Overlay::Tempo : Overlay::Frequency);
        overlayChangedAt_ = now;
    }

    void showE2(E2Page page, uint32_t now) noexcept {
        overlay_ = page == E2Page::Root ? Overlay::Root : Overlay::Preset;
        overlayChangedAt_ = now;
    }

    void showHarmony(uint32_t now) noexcept {
        overlay_ = Overlay::Harmony;
        overlayChangedAt_ = now;
    }

    void showPage(uint32_t now) noexcept {
        overlay_ = Overlay::Page;
        overlayChangedAt_ = now;
    }

    void showPattern(uint32_t now) noexcept {
        overlay_ = Overlay::Pattern;
        overlayChangedAt_ = now;
    }

    void showPatternEdit(uint32_t now) noexcept {
        overlay_ = Overlay::PatternEdit;
        overlayChangedAt_ = now;
    }

    const MonoFramebuffer& render(const SimpleMidiController& controller, E2Page page, uint32_t now) noexcept {
        framebuffer_.clear();
        if (overlay_ != Overlay::None && now - overlayChangedAt_ < 800U) renderOverlay(controller);
        else {
            overlay_ = Overlay::None;
            renderHome(controller, page);
        }
        framebuffer_.drawText(96, 0,
            controller.page() == PerformancePage::Harmony ? "HARM"
            : controller.page() == PerformancePage::Pattern ? "PATT" : "NONE", 2);
        return framebuffer_;
    }

    bool overlayVisible() const noexcept { return overlay_ != Overlay::None; }

private:
    enum class Overlay : uint8_t {
        None,
        Octave,
        Tempo,
        Frequency,
        Root,
        Preset,
        Harmony,
        Page,
        Pattern,
        PatternEdit
    };

    void renderHome(const SimpleMidiController& controller, E2Page) noexcept {
        char line[36];
        std::snprintf(line, sizeof(line), "O%u %s", controller.octaveNumber(),
                      pitchClassName(controller.rootPitchClass()));
        framebuffer_.drawText(0, 0, line, 2);
        const char* currentNote = controller.currentNoteName();
        std::snprintf(line, sizeof(line), "%s%s", controller.presetName(),
                      controller.hasHeldPresetMismatch() ? "*" : "");
        framebuffer_.drawText(0, 11, line, 2);
        if (currentNote[0] != '\0') {
            framebuffer_.drawText(static_cast<int>(std::strlen(line)) * 8 + 4, 11,
                                  currentNote, controller.drums() ? 1 : 2);
        }
        if (controller.page() == PerformancePage::Pattern) {
            char state[24];
            std::snprintf(state, sizeof(state), "%s %s", controller.runShapeName(),
                          controller.runActive() ? "PLAY" : (controller.patternHeld() ? "READY" : "IDLE"));
            drawCenteredText(22, state, 2);
        } else if (controller.harmonyActive()) drawCenteredText(22, controller.harmonyName(), 2);
        else drawCenteredText(22, modeDescription(controller.scale()), 2);
    }

    void renderOverlay(const SimpleMidiController& controller) noexcept {
        char value[36];
        if (overlay_ == Overlay::Page || overlay_ == Overlay::Pattern || overlay_ == Overlay::PatternEdit) {
            const char* label = overlay_ == Overlay::Page ? "PAGE"
                : (overlay_ == Overlay::PatternEdit ? "SLOT" : "PATTERN");
            framebuffer_.drawText(0, 0, label, 2);
            if (overlay_ == Overlay::Page) {
                drawCenteredText(18,
                    controller.page() == PerformancePage::Harmony ? "HARMONY"
                    : controller.page() == PerformancePage::Pattern ? "PATTERN" : "NONE", 2);
            } else if (overlay_ == Overlay::PatternEdit) {
                std::snprintf(value, sizeof(value), "%u %s", controller.patternSlot(),
                              controller.runShapeName());
                drawCenteredText(18, value, 2);
            } else {
                std::snprintf(value, sizeof(value), "%s %s", controller.runShapeName(),
                              controller.runActive() ? "PLAY" : (controller.patternHeld() ? "READY" : "IDLE"));
                drawCenteredText(18, value, 2);
            }
            return;
        }
        if (overlay_ == Overlay::Octave || overlay_ == Overlay::Tempo || overlay_ == Overlay::Frequency) {
            const bool tempoPage = overlay_ == Overlay::Tempo;
            const bool frequencyPage = overlay_ == Overlay::Frequency;
            framebuffer_.drawText(0, 0, frequencyPage ? "FREQUENCY" : (tempoPage ? "TEMPO" : "OCTAVE"), 2);
            if (tempoPage) std::snprintf(value, sizeof(value), "%u", controller.tempo());
            else if (frequencyPage) {
                const uint16_t tenths = controller.frequencyTenths();
                std::snprintf(value, sizeof(value), "%u.%u HZ", tenths / 10U, tenths % 10U);
            }
            else std::snprintf(value, sizeof(value), "O%u", controller.octaveNumber());
            drawCenteredText(frequencyPage ? 16 : 12, value, frequencyPage ? 2 : 4);
            if ((tempoPage && controller.clockMode() == ClockMode::Tempo) ||
                (frequencyPage && controller.clockMode() == ClockMode::Frequency))
                framebuffer_.drawText(84, 0, "*", 2);
            drawPageDots(frequencyPage ? 2 : (tempoPage ? 1 : 0), 3);
            return;
        }

        const bool rootPage = overlay_ == Overlay::Root;
        if (overlay_ == Overlay::Harmony) {
            framebuffer_.drawText(0, 0, "HARMONY", 2);
            const char* name = controller.harmonyName();
            int length = 0;
            while (name[length] != '\0') ++length;
            drawCenteredText(12, name, length * 16 - 4 <= MonoFramebuffer::kWidth ? 4 : 2);
            return;
        }
        framebuffer_.drawText(0, 0, rootPage ? "ROOT" : "PRESET", 2);
        if (rootPage) drawCenteredText(12, pitchClassName(controller.rootPitchClass()), 4);
        else drawCenteredText(14, controller.presetName(), 2);
        drawPageDots(rootPage ? 0 : 1, 2);
    }

    void drawCenteredText(int y, const char* text, int scale) noexcept {
        int characters = 0;
        while (text[characters] != '\0') ++characters;
        const int width = characters == 0 ? 0 : characters * 4 * scale - scale;
        framebuffer_.drawText((MonoFramebuffer::kWidth - width) / 2, y, text, scale);
    }

    void drawPageDots(uint8_t page, uint8_t count) noexcept {
        const int startX = count == 3 ? 103 : 112;
        for (uint8_t index = 0; index < count; ++index) {
            const int x = startX + index * 9;
            const bool active = index == page;
            if (active) framebuffer_.fillRect(x, 27, 5, 5);
            else {
                framebuffer_.fillRect(x, 27, 5, 1);
                framebuffer_.fillRect(x, 31, 5, 1);
                framebuffer_.fillRect(x, 28, 1, 3);
                framebuffer_.fillRect(x + 4, 28, 1, 3);
            }
        }
    }

    MonoFramebuffer framebuffer_{};
    uint32_t overlayChangedAt_{};
    Overlay overlay_{Overlay::None};
};

}
