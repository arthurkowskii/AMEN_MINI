#include "screen_ui.h"

#include <algorithm>
#include <cstdio>

namespace {
// Gap in pixels between the end of a scrolling text and its wrapped copy.
constexpr int kBrowserScrollGapPixels = 16;

// Horizontal offset in pixels of the browser-style marquee once the idle
// delay has passed, looping every cycleWidth pixels. Pure integer math so the
// timeline stays identical across platforms; deterministic for tests.
int browserScrollOffset(std::uint64_t idleMs, int cycleWidth) {
    if (idleMs <= ScreenUi::kBrowserScrollDelayMs || cycleWidth <= 0) {
        return 0;
    }
    const std::uint64_t elapsed = idleMs - ScreenUi::kBrowserScrollDelayMs;
    const std::uint64_t wholeSeconds = elapsed / 1000U;
    const std::uint64_t remainderMs = elapsed % 1000U;
    const std::uint64_t cycle = static_cast<std::uint64_t>(cycleWidth);
    return static_cast<int>(
        ((wholeSeconds % cycle) * ScreenUi::kBrowserScrollPixelsPerSecond +
         remainderMs * ScreenUi::kBrowserScrollPixelsPerSecond / 1000U) %
        cycle);
}

struct Glyph {
    char character;
    std::uint8_t rows[5];
};

// Compact 3x5 font: narrow enough to preserve a 32x32 artwork area.
constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0}},       {'%', {5, 1, 2, 4, 5}},
    {'-', {0, 0, 7, 0, 0}},
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

template <std::size_t Size>
void copyBrowserLabel(std::array<char, Size>& destination, const char* source,
                      bool directory) {
    destination.fill('\0');
    if (source == nullptr || source[0] == '\0') {
        source = "UNNAMED";
    }

    std::size_t sourceLength = 0;
    while (source[sourceLength] != '\0') {
        ++sourceLength;
    }

    const std::size_t suffixLength = directory ? 1U : 0U;
    const std::size_t available = Size - 1U - suffixLength;
    const bool truncated = sourceLength > available;
    const std::size_t textLength = truncated && available >= 2U
        ? available - 2U
        : std::min(sourceLength, available);

    for (std::size_t index = 0; index < textLength; ++index) {
        char character = source[index];
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        destination[index] = character;
    }

    std::size_t end = textLength;
    if (truncated && available >= 2U) {
        destination[end++] = '.';
        destination[end++] = '.';
    }
    if (directory) {
        destination[end] = '/';
    }
}

template <std::size_t Size>
void makeBrowserPreview(std::array<char, Size>& destination, const char* source) {
    destination.fill('\0');
    if (source == nullptr) return;

    std::size_t sourceLength = 0;
    while (source[sourceLength] != '\0') ++sourceLength;
    if (sourceLength < Size) {
        std::copy_n(source, sourceLength, destination.data());
        return;
    }

    const bool directory = sourceLength > 0U && source[sourceLength - 1U] == '/';
    const std::size_t suffixLength = directory ? 1U : 0U;
    const std::size_t textLength = Size - 1U - 2U - suffixLength;
    std::copy_n(source, textLength, destination.data());
    destination[textLength] = '.';
    destination[textLength + 1U] = '.';
    if (directory) destination[textLength + 2U] = '/';
}

std::size_t textLength(const char* text) {
    std::size_t length = 0;
    if (text != nullptr) {
        while (text[length] != '\0') ++length;
    }
    return length;
}

const char* modeName(PlaybackMode mode) {
    switch (mode) {
        case PlaybackMode::OneShot:
            return "ONE SHOT";
        case PlaybackMode::Loop:
            return "LOOP";
        case PlaybackMode::Granular:
            return "CLOUD";
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
                              std::uint64_t nowMs, const char* suffix) {
    copyLabel(parameterName_, name);
    copyLabel(parameterSuffix_, suffix);
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    parameterMinimum_ = minimum;
    parameterMaximum_ = maximum;
    parameterValue_ = std::clamp(value, minimum, maximum);
    overlayUntilMs_ = nowMs + kOverlayDurationMs;
}

void ScreenUi::showBrowser(const char* folderName, const BrowserLine* lines,
                           std::size_t count, std::size_t selectedIndex,
                           std::uint64_t eventTimeMs) {
    copyBrowserLabel(browserFolder_,
                     folderName == nullptr || folderName[0] == '\0'
                         ? "ROOT"
                         : folderName,
                     false);
    browserLineCount_ = 0;
    browserSelectedLine_ = 0;
    browserScrollStartMs_ = eventTimeMs;
    browserActive_ = true;
    fxPadActive_ = false;
    assignmentMenuActive_ = false;

    if (lines == nullptr || count == 0U) {
        return;
    }

    const std::size_t selected = std::min(selectedIndex, count - 1U);
    const std::size_t first = count <= browserLines_.size()
        ? 0U
        : std::min(selected == 0U ? 0U : selected - 1U,
                   count - browserLines_.size());
    browserLineCount_ = std::min(count, browserLines_.size());
    browserSelectedLine_ = selected - first;

    for (std::size_t index = 0; index < browserLineCount_; ++index) {
        const BrowserLine& source = lines[first + index];
        browserLines_[index].directory = source.directory;
        copyBrowserLabel(browserLines_[index].name, source.name, source.directory);
    }
}

void ScreenUi::showAssignmentMenu(const char* fileName, int selectedIndex,
                                  std::uint64_t eventTimeMs) {
    copyBrowserLabel(assignmentFileName_,
                     fileName == nullptr || fileName[0] == '\0' ? "UNNAMED"
                                                               : fileName,
                     false);
    assignmentSelectedOption_ =
        std::clamp(selectedIndex, 0, kAssignmentOptionCount - 1);
    assignmentMenuEventMs_ = eventTimeMs;
    assignmentMenuActive_ = true;
    browserActive_ = false;
    fxPadActive_ = false;
    overlayUntilMs_ = 0;
}

void ScreenUi::showFxPad(int padNumber, const char* fxName, int selectedEncoder) {
    copyLabel(fxPadName_, fxName);
    fxPadNumber_ = std::clamp(padNumber, 0, 99);
    fxPadEncoder_ = std::clamp(selectedEncoder, 1, 7);
    fxPadActive_ = true;
    browserActive_ = false;
    assignmentMenuActive_ = false;
    overlayUntilMs_ = 0;
}

void ScreenUi::showPerformance() {
    browserActive_ = false;
    fxPadActive_ = false;
    assignmentMenuActive_ = false;
    overlayUntilMs_ = 0;
}

void ScreenUi::render(std::uint64_t nowMs) {
    clear();
    if (assignmentMenuActive_) {
        drawAssignmentMenu(nowMs);
    } else if (browserActive_) {
        drawBrowser(nowMs);
    } else if (fxPadActive_) {
        drawFxPad();
    } else if (nowMs < overlayUntilMs_) {
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

void ScreenUi::drawBrowserText(int x, int y, const char* text,
                               std::size_t length, int leftEdge,
                               int rightEdge) {
    constexpr int kGlyphWidth = 3;
    constexpr int kAdvance = 4;
    if (text == nullptr || length == 0U) {
        return;
    }
    if (leftEdge < 0) {
        leftEdge = 0;
    }
    if (rightEdge > kWidth) {
        rightEdge = kWidth;
    }
    if (rightEdge <= leftEdge) {
        return;
    }

    // Jump directly to the first glyph that can intersect the viewport. This
    // keeps work bounded by the visible glyphs, even for 255-char names.
    std::size_t first = 0;
    if (x + kGlyphWidth <= leftEdge) {
        first = std::min(length, static_cast<std::size_t>(
            (leftEdge - (x + kGlyphWidth)) / kAdvance + 1));
    }

    for (std::size_t index = first; index < length; ++index) {
        const int characterX = x + static_cast<int>(index) * kAdvance;
        if (characterX >= rightEdge) {
            break;
        }
        const std::uint8_t* rows = glyphRows(text[index]);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < kGlyphWidth; ++column) {
                const int pixelX = characterX + column;
                if (pixelX >= leftEdge && pixelX < rightEdge &&
                    (rows[row] & (1U << (2 - column))) != 0) {
                    setPixel(pixelX, y + row);
                }
            }
        }
    }
}

void ScreenUi::drawNumberRight(int right, int y, int value, int scale,
                               const char* suffix) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%d%s", value, suffix == nullptr ? "" : suffix);
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

    const std::int64_t range = static_cast<std::int64_t>(parameterMaximum_) -
                               parameterMinimum_;
    const std::int64_t offset = static_cast<std::int64_t>(parameterValue_) -
                                parameterMinimum_;
    const int progress = range == 0 ? 0 : static_cast<int>(offset * 58 / range);
    const int visualState = range == 0 ? 0 :
        static_cast<int>(offset * 3 / (range + 1));
    for (int state = 0; state < 3; ++state) {
        if (state <= visualState) {
            fillRect(8 + state * 6, 23, 4, 4);
        } else {
            drawRect(8 + state * 6, 23, 4, 4);
        }
    }

    drawVerticalLine(33, 0, kHeight);
    drawText(38, 2, parameterName_.data());
    drawNumberRight(126, 9, parameterValue_, 3, parameterSuffix_.data());
    drawRect(38, 24, 60, 6);
    if (progress > 0) {
        fillRect(39, 25, progress, 4);
    }
}

void ScreenUi::drawFxPad() {
    // Reserved artwork tile. Final religious sprites will replace this placeholder.
    drawRect(0, 0, 32, 32);
    drawText(10, 9, "ART");

    drawVerticalLine(33, 0, kHeight);
    char padLabel[8]{};
    std::snprintf(padLabel, sizeof(padLabel), "PAD %d", fxPadNumber_);
    drawText(38, 1, padLabel);
    drawText(38, 9, fxPadName_.data(), 2);
    drawText(38, 24, fxPadEncoder_ == 1 ? "E1 NAV CLIC ASSIGN" : "F1 POUR E1");
}

void ScreenUi::drawBrowser(std::uint64_t nowMs) {
    drawText(1, 1, browserFolder_.data());
    drawHorizontalLine(0, 7, kWidth);

    if (browserLineCount_ == 0U) {
        drawBrowserText(7, 14, "EMPTY", 5U);
        return;
    }

    constexpr int kLineY[] = {10, 18, 26};
    constexpr int kTextX = 7;
    constexpr int kAvailableWidth = kWidth - kTextX;
    constexpr int kScrollGap = 16;
    for (std::size_t index = 0; index < browserLineCount_; ++index) {
        const char* const name = browserLines_[index].name.data();
        std::array<char, 31> preview{};
        makeBrowserPreview(preview, name);
        if (index == browserSelectedLine_) {
            fillRect(1, kLineY[index] - 1, 3, 7);
        }

        const std::size_t length = textLength(name);
        const int textWidth = length == 0U ? 0 : static_cast<int>(length * 4U - 1U);
        const bool selectedLong = index == browserSelectedLine_ &&
                                  textWidth > kAvailableWidth;
        const std::uint64_t idleMs = nowMs >= browserScrollStartMs_
            ? nowMs - browserScrollStartMs_
            : 0U;
        if (!selectedLong || idleMs <= kBrowserScrollDelayMs) {
            drawBrowserText(kTextX, kLineY[index], preview.data(),
                            textLength(preview.data()));
            continue;
        }

        const int cycleWidth = textWidth + kScrollGap;
        const std::uint64_t elapsed = idleMs - kBrowserScrollDelayMs;
        const int offset = browserScrollOffset(elapsed + kBrowserScrollDelayMs,
                                               cycleWidth);
        const int x = kTextX - static_cast<int>(offset);
        drawBrowserText(x, kLineY[index], name, length);
        drawBrowserText(x + cycleWidth, kLineY[index], name, length);
    }
}

void ScreenUi::drawAssignmentMenu(std::uint64_t nowMs) {
    static constexpr int kOptionY[] = {10, 18, 26};
    static constexpr const char* kOptionLabels[] = {"ALL PADS", "TRANSIENT",
                                                    "CANCEL"};

    // Header: scrolling file name (30 px/s after 500 ms, browser semantics)
    // confined to x < 106 so the static E1 OK hint never overlaps it.
    constexpr int kNameRightEdge = 106;
    constexpr int kScrollGap = 16;
    const std::size_t length = textLength(assignmentFileName_.data());
    const int textWidth = length == 0U ? 0 : static_cast<int>(length * 4U - 1U);
    const std::uint64_t idleMs = nowMs >= assignmentMenuEventMs_
        ? nowMs - assignmentMenuEventMs_
        : 0U;
    if (textWidth <= kNameRightEdge) {
        drawBrowserText(0, 1, assignmentFileName_.data(), length, 0,
                        kNameRightEdge);
    } else {
        const int cycleWidth = textWidth + kScrollGap;
        const int offset = browserScrollOffset(idleMs, cycleWidth);
        const int x = 0 - offset;
        drawBrowserText(x, 1, assignmentFileName_.data(), length, 0,
                        kNameRightEdge);
        drawBrowserText(x + cycleWidth, 1, assignmentFileName_.data(), length,
                        0, kNameRightEdge);
    }
    drawText(108, 1, "E1 OK");
    drawHorizontalLine(0, 7, kWidth);

    for (int option = 0; option < kAssignmentOptionCount; ++option) {
        if (option == assignmentSelectedOption_) {
            fillRect(1, kOptionY[option] - 1, 3, 7);
        }
        drawBrowserText(7, kOptionY[option], kOptionLabels[option],
                        textLength(kOptionLabels[option]));
    }
}
