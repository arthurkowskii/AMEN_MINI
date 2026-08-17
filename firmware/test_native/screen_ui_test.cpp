#include "screen_ui.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using Buffer = ScreenUi::Buffer;

bool regionEqual(const Buffer& left, const Buffer& right, int yBegin, int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = 0; x < ScreenUi::kWidth; ++x) {
            const int index = x + (y / 8) * ScreenUi::kWidth;
            const std::uint8_t mask = static_cast<std::uint8_t>(1U << (y % 8));
            if ((left[index] & mask) != (right[index] & mask)) return false;
        }
    }
    return true;
}

bool regionShiftedLeftBy(const Buffer& earlier, const Buffer& later, int pixels,
                         int xBegin, int xEnd, int yBegin, int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            const int earlierIndex = x + pixels + (y / 8) * ScreenUi::kWidth;
            const int laterIndex = x + (y / 8) * ScreenUi::kWidth;
            const std::uint8_t mask = static_cast<std::uint8_t>(1U << (y % 8));
            if ((earlier[earlierIndex] & mask) != (later[laterIndex] & mask)) {
                return false;
            }
        }
    }
    return true;
}

Buffer renderAt(ScreenUi& screen, std::uint64_t nowMs) {
    screen.render(nowMs);
    return screen.buffer();
}

bool anyPixels(const Buffer& buffer) {
    return std::any_of(buffer.begin(), buffer.end(),
                       [](std::uint8_t byte) { return byte != 0; });
}
}  // namespace

int main() {
    ScreenUi screen;
    screen.setPerformance("amen.wav", 145, PlaybackMode::Granular);
    const Buffer performance = renderAt(screen, 0);
    assert(std::any_of(performance.begin(), performance.end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    // Every interaction renews the overlay from that interaction's timestamp.
    screen.showParameter("repeat", 7, 0, 10, 100);
    screen.showParameter("repeat", 8, 0, 10, 900);
    assert(renderAt(screen, 1899) != performance);
    assert(renderAt(screen, 1900) == performance);

    char folder[] = "breaks";
    char firstName[] = "amen.wav";
    const ScreenUi::BrowserLine lines[] = {
        {firstName, false},
        {"drums", true},
        {"long_sample_name_that_must_be_truncated_cleanly.wav", false},
        {"voice.wav", false},
    };

    // Browser remains above a parameter overlay before and after its expiry.
    screen.showBrowser(folder, lines, 4, 0, 2000);
    const Buffer browserFirst = renderAt(screen, 2000);
    screen.showParameter("amount", 5, 0, 10, 2000);
    assert(renderAt(screen, 2000) == browserFirst);
    assert(renderAt(screen, 3000) == browserFirst);

    // Caller-owned browser strings are copied by showBrowser.
    folder[0] = 'X';
    firstName[0] = 'X';
    assert(renderAt(screen, 3000) == browserFirst);

    screen.showPerformance();
    screen.showFxPad(7, "BLANK", 1);
    const Buffer fxBlank = renderAt(screen, 4000);
    screen.showParameter("amount", 5, 0, 10, 4000);
    assert(renderAt(screen, 4000) == fxBlank);
    assert(renderAt(screen, 5000) == fxBlank);

    // A selected long line keeps its '..' preview through exactly 500 ms.
    constexpr const char* kLongA =
        "selected_long_sample_name_that_keeps_scrolling_for_the_player.wav";
    constexpr const char* kLongB =
        "another_selected_long_sample_name_used_for_reset_testing.wav";
    const ScreenUi::BrowserLine longLines[] = {
        {kLongA, false},
        {kLongB, false},
        {"STATIC_SHORT.WAV", false},
    };
    screen.showBrowser("LONG", longLines, 3, 0, 10000);
    const Buffer preview0 = renderAt(screen, 10000);
    const Buffer preview499 = renderAt(screen, 10499);
    const Buffer preview500 = renderAt(screen, 10500);
    assert(preview0 == preview499);
    assert(preview0 == preview500);
    const ScreenUi::BrowserLine previewLines[] = {
        {"selected_long_sample_name_th..", false},
        {kLongB, false},
        {"STATIC_SHORT.WAV", false},
    };
    ScreenUi previewReference;
    previewReference.showBrowser("LONG", previewLines, 3, 0, 10000);
    assert(preview0 == renderAt(previewReference, 10000));
    const Buffer scroll600 = renderAt(screen, 10600);
    const Buffer scroll700 = renderAt(screen, 10700);
    assert(scroll600 != preview500);

    // At 30 px/s, elapsed scroll times 100 ms and 200 ms produce offsets 3 and
    // 6: the later selected text is the earlier text shifted left exactly 3 px.
    // The interior range excludes the x=1..3 marker and both clipping edges.
    assert(regionShiftedLeftBy(scroll600, scroll700, 3, 12, 112, 10, 15));

    // Only the selected row moves; a non-selected long row stays fixed.
    assert(!regionEqual(scroll600, scroll700, 8, 16));
    assert(regionEqual(scroll600, scroll700, 16, 24));

    // Reconstructing the display with the retained event timestamp preserves
    // the idle timeline instead of restarting the 500 ms delay.
    screen.showBrowser("LONG", longLines, 3, 0, 10000);
    assert(renderAt(screen, 10700) == scroll700);

    // A selection event resets the new selected row to a fresh preview baseline.
    screen.showBrowser("LONG", longLines, 3, 1, 10700);
    const Buffer changedSelection = renderAt(screen, 10700);
    ScreenUi freshSelection;
    freshSelection.showBrowser("LONG", longLines, 3, 1, 10700);
    assert(changedSelection == renderAt(freshSelection, 10700));
    assert(renderAt(screen, 11199) == changedSelection);

    // A folder event also resets scrolling to the event timestamp.
    screen.showBrowser("OTHER", longLines, 3, 1, 12000);
    const Buffer changedFolder = renderAt(screen, 12000);
    ScreenUi freshFolder;
    freshFolder.showBrowser("OTHER", longLines, 3, 1, 12000);
    assert(changedFolder == renderAt(freshFolder, 12000));

    // Selected short labels and non-selected labels never scroll after the delay.
    screen.showBrowser("SHORT", longLines, 3, 2, 13000);
    const Buffer shortStart = renderAt(screen, 13000);
    assert(renderAt(screen, 20000) == shortStart);

    // Full selected text is retained after showBrowser; scrolling never rereads pointers.
    char mutableLong[96] =
        "pointer_owned_selected_long_sample_name_continues_scrolling.wav";
    const std::array<char, 96> originalLong = [&mutableLong] {
        std::array<char, 96> copy{};
        std::copy(std::begin(mutableLong), std::end(mutableLong), copy.begin());
        return copy;
    }();
    const ScreenUi::BrowserLine mutableLine[] = {{mutableLong, false}};
    screen.showBrowser("COPY", mutableLine, 1, 0, 21000);
    mutableLong[0] = 'X';
    const Buffer copiedScroll = renderAt(screen, 21700);
    ScreenUi copiedBaseline;
    const ScreenUi::BrowserLine originalLine[] = {{originalLong.data(), false}};
    copiedBaseline.showBrowser("COPY", originalLine, 1, 0, 21000);
    assert(copiedScroll == renderAt(copiedBaseline, 21700));

    // Existing browser formatting and defensive bounds remain intact.
    const ScreenUi::BrowserLine fileLine[] = {{"drums", false}};
    const ScreenUi::BrowserLine directoryLine[] = {{"drums", true}};
    screen.showBrowser("breaks", fileLine, 1, 0, 22000);
    const Buffer fileBrowser = renderAt(screen, 22000);
    screen.showBrowser("breaks", directoryLine, 1, 0, 22000);
    assert(renderAt(screen, 22000) != fileBrowser);
    screen.showBrowser(nullptr, nullptr, 99, 99, 22000);
    assert(anyPixels(renderAt(screen, 22000)));

    screen.showPerformance();
    screen.showParameter("amount", 99, 0, 10, 23000);
    const Buffer clampedHigh = renderAt(screen, 23000);
    screen.showParameter("amount", 10, 0, 10, 23000);
    assert(renderAt(screen, 23000) == clampedHigh);

    screen.showParameter("wide", 0, std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::max(), 24000);
    assert(anyPixels(renderAt(screen, 24000)));

    screen.showPerformance();
    screen.showParameter("speed", 100, 25, 400, 25000, "%");
    const Buffer percentValue = renderAt(screen, 25000);
    screen.showParameter("speed", 100, 25, 400, 25000);
    assert(renderAt(screen, 25000) != percentValue);

    assert(!screen.pixel(-1, 0));
    assert(!screen.pixel(ScreenUi::kWidth, 0));
    std::printf("screen_ui: ok\n");
    return 0;
}
