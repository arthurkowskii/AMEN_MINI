#pragma once

#include "simple_midi_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace amen {

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
            case '#': return {5, 7, 5, 7, 5};
            case '+': return {0, 2, 7, 2, 0};
            case '-': return {0, 0, 7, 0, 0};
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
    void showOctave(uint32_t now) noexcept {
        octaveOverlay_ = true;
        octaveChangedAt_ = now;
    }

    const MonoFramebuffer& render(const SimpleMidiController& controller, uint32_t now) noexcept {
        framebuffer_.clear();
        if (octaveOverlay_ && now - octaveChangedAt_ < 800U) renderOctave(controller);
        else {
            octaveOverlay_ = false;
            renderHome(controller);
        }
        return framebuffer_;
    }

    bool overlayVisible() const noexcept { return octaveOverlay_; }

private:
    static const char* noteName(uint8_t note) noexcept {
        static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return names[note % 12];
    }

    void renderHome(const SimpleMidiController& controller) noexcept {
        char line[32];
        std::snprintf(line, sizeof(line), "OCT %+d M%u-%u", controller.octave(), controller.baseNote(), controller.baseNote() + 19);
        framebuffer_.drawText(0, 0, line);

        if (controller.lastNote() >= 0) {
            const uint8_t note = static_cast<uint8_t>(controller.lastNote());
            std::snprintf(line, sizeof(line), "LAST %s M%u", noteName(note), note);
            framebuffer_.drawText(0, 7, line);
        } else {
            framebuffer_.drawText(0, 7, "PLAY A PAD");
        }

        std::snprintf(line, sizeof(line), "HELD %u", controller.heldCount());
        framebuffer_.drawText(0, 14, line);

        for (uint8_t key = 0; key < SimpleMidiController::kKeyCount; ++key) {
            const int x = 4 + key * 6;
            if (controller.activeNote(key) >= 0) framebuffer_.fillRect(x, 24, 4, 7);
            else {
                framebuffer_.fillRect(x, 30, 4, 1);
                framebuffer_.setPixel(x, 29);
                framebuffer_.setPixel(x + 3, 29);
            }
        }
    }

    void renderOctave(const SimpleMidiController& controller) noexcept {
        char value[8];
        char range[24];
        framebuffer_.drawText(0, 1, "OCTAVE");
        std::snprintf(value, sizeof(value), "%+d", controller.octave());
        framebuffer_.drawText(48, 8, value, 3);
        std::snprintf(range, sizeof(range), "M%u-%u", controller.baseNote(), controller.baseNote() + 19);
        framebuffer_.drawText(0, 26, range);
    }

    MonoFramebuffer framebuffer_{};
    uint32_t octaveChangedAt_{};
    bool octaveOverlay_{};
};

}
