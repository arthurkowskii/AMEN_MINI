#include "screen_ui.h"

#include <algorithm>
#include <cstdio>

namespace {
struct Glyph {
    char character;
    std::uint8_t rows[5];
};

// Compact 3x5 font: narrow enough to preserve a 32x32 artwork area.
constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0}},       {'-', {0, 0, 7, 0, 0}},
    {'.', {0, 0, 0, 0, 2}},       {'/', {1, 1, 2, 4, 4}},
    {'_', {0, 0, 0, 0, 7}},       {'+', {0, 2, 7, 2, 0}},
    {'0', {7, 5, 5, 5, 7}},       {'1', {2, 6, 2, 2, 7}},
    {'2', {7, 1, 7, 4, 7}},       {'3', {7, 1, 7, 1, 7}},
    {'4', {5, 5, 7, 1, 1}},       {'5', {7, 4, 7, 1, 7}},
    {'6', {7, 4, 7, 5, 7}},       {'7', {7, 1, 1, 1, 1}},
    {'8', {7, 5, 7, 5, 7}},       {'9', {7, 5, 7, 1, 7}},
    {'A', {2, 5, 7, 5, 5}},       {'B', {6, 5, 6, 5, 6}},
    {'C', {3, 4, 4, 4, 3}},       {'D', {6, 5, 5, 5, 6}},
    {'E', {7, 4, 6, 4, 7}},       {'F', {7, 4, 6, 4, 4}},
    {'G', {3, 4, 5, 5, 3}},       {'H', {5, 5, 7, 5, 5}},
    {'I', {7, 2, 2, 2, 7}},       {'J', {1, 1, 1, 5, 2}},
    {'K', {5, 5, 6, 5, 5}},       {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 7, 5, 5}},       {'N', {5, 7, 7, 7, 5}},
    {'O', {2, 5, 5, 5, 2}},       {'P', {6, 5, 6, 4, 4}},
    {'Q', {2, 5, 5, 3, 1}},       {'R', {6, 5, 6, 5, 5}},
    {'S', {3, 4, 2, 1, 6}},       {'T', {7, 2, 2, 2, 2}},
    {'U', {5, 5, 5, 5, 7}},       {'V', {5, 5, 5, 5, 2}},
    {'W', {5, 5, 7, 7, 5}},       {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}},       {'Z', {7, 1, 2, 4, 7}},
};

const std::uint8_t* glyphRows(char character) {
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.character == character) {
            return glyph.rows;
        }
    }
    return kGlyphs[0].rows;
}

template <std::size_t Size>
void copyLabel(std::array<char, Size>& destination, const char* source) {
    destination.fill('\0');
    if (source == nullptr) {
        return;
    }
    std::size_t index = 0;
    while (source[index] != '\0' && index + 1 < Size) {
        char character = source[index];
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        destination[index] = character;
        ++index;
    }
}

const char* modeName(PlaybackMode mode) {
    switch (mode) {
        case PlaybackMode::OneShot:
            return "ONE SHOT";
        case PlaybackMode::Loop:
            return "LOOP";
        case PlaybackMode::Granular:
            return "GRAIN";
        case PlaybackMode::SliceSync:
            return "SYNC";
    }
    return "ONE SHOT";
}
}  // namespace

void ScreenUi::setPerformance(const char* breakName, int bpm, PlaybackMode mode) {
    copyLabel(breakName_, breakName);
    bpm_ = std::clamp(bpm, 20, 300);
    mode_ = mode;
}

void ScreenUi::showParameter(const char* name, int value, int minimum, int maximum,
                             std::uint64_t nowMs) {
    copyLabel(parameterName_, name);
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    parameterMinimum_ = minimum;
    parameterMaximum_ = maximum;
    parameterValue_ = std::clamp(value, minimum, maximum);
    overlayUntilMs_ = nowMs + kOverlayDurationMs;
}

void ScreenUi::render(std::uint64_t nowMs) {
    clear();
    if (nowMs < overlayUntilMs_) {
        drawParameter();
    } else {
        drawPerformance();
    }
}

const ScreenUi::Buffer& ScreenUi::buffer() const {
    return buffer_;
}

bool ScreenUi::pixel(int x, int y) const {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return false;
    }
    const int index = x + (y / 8) * kWidth;
    return (buffer_[index] & (1U << (y % 8))) != 0;
}

void ScreenUi::clear() {
    buffer_.fill(0);
}

void ScreenUi::setPixel(int x, int y, bool on) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return;
    }
    const int index = x + (y / 8) * kWidth;
    const std::uint8_t mask = static_cast<std::uint8_t>(1U << (y % 8));
    if (on) {
        buffer_[index] |= mask;
    } else {
        buffer_[index] &= static_cast<std::uint8_t>(~mask);
    }
}

void ScreenUi::drawHorizontalLine(int x, int y, int width) {
    for (int offset = 0; offset < width; ++offset) {
        setPixel(x + offset, y);
    }
}

void ScreenUi::drawVerticalLine(int x, int y, int height) {
    for (int offset = 0; offset < height; ++offset) {
        setPixel(x, y + offset);
    }
}

void ScreenUi::drawRect(int x, int y, int width, int height) {
    drawHorizontalLine(x, y, width);
    drawHorizontalLine(x, y + height - 1, width);
    drawVerticalLine(x, y, height);
    drawVerticalLine(x + width - 1, y, height);
}

void ScreenUi::fillRect(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
        drawHorizontalLine(x, y + row, width);
    }
}

void ScreenUi::drawChar(int x, int y, char character, int scale) {
    const std::uint8_t* rows = glyphRows(character);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 3; ++column) {
            if ((rows[row] & (1U << (2 - column))) != 0) {
                fillRect(x + column * scale, y + row * scale, scale, scale);
            }
        }
    }
}

void ScreenUi::drawText(int x, int y, const char* text, int scale) {
    if (text == nullptr) {
        return;
    }
    const int advance = 4 * scale;
    for (int index = 0; text[index] != '\0'; ++index) {
        drawChar(x + index * advance, y, text[index], scale);
    }
}

void ScreenUi::drawNumberRight(int right, int y, int value, int scale) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%d", value);
    int length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    const int width = length > 0 ? length * 4 * scale - scale : 0;
    drawText(right - width + 1, y, text, scale);
}

void ScreenUi::drawPerformance() {
    drawText(1, 2, breakName_.data());
    drawNumberRight(126, 1, bpm_, 2);
    drawHorizontalLine(0, 10, kWidth);
    drawText(1, 15, "MODE");
    drawText(1, 22, modeName(mode_));
    drawText(108, 23, "BPM");
}

void ScreenUi::drawParameter() {
    // Reserved artwork tile. Final religious sprites will replace this placeholder.
    drawRect(0, 0, 32, 32);
    drawText(10, 9, "ART");

    const int range = parameterMaximum_ - parameterMinimum_;
    const int progress = range == 0 ? 0 :
        (parameterValue_ - parameterMinimum_) * 58 / range;
    const int visualState = range == 0 ? 0 :
        (parameterValue_ - parameterMinimum_) * 3 / (range + 1);
    for (int state = 0; state < 3; ++state) {
        if (state <= visualState) {
            fillRect(8 + state * 6, 23, 4, 4);
        } else {
            drawRect(8 + state * 6, 23, 4, 4);
        }
    }

    drawVerticalLine(33, 0, kHeight);
    drawText(38, 2, parameterName_.data());
    drawNumberRight(126, 9, parameterValue_, 3);
    drawRect(38, 24, 60, 6);
    if (progress > 0) {
        fillRect(39, 25, progress, 4);
    }
}
