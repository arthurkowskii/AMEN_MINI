#include "screen_ui.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    ScreenUi screen;
    screen.setPerformance("amen.wav", 145, PlaybackMode::Granular);
    screen.render(0);
    const ScreenUi::Buffer performance = screen.buffer();
    assert(std::any_of(performance.begin(), performance.end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    screen.showParameter("repeat", 7, 0, 10, 100);
    screen.render(1099);
    assert(screen.buffer() != performance);

    screen.render(1100);
    assert(screen.buffer() == performance);

    char folder[] = "breaks";
    char firstName[] = "amen.wav";
    const ScreenUi::BrowserLine lines[] = {
        {firstName, false},
        {"drums", true},
        {"long_sample_name_that_must_be_truncated_cleanly.wav", false},
        {"voice.wav", false},
    };
    screen.showBrowser(folder, lines, 4, 0);
    screen.render(1200);
    const ScreenUi::Buffer browserFirst = screen.buffer();
    assert(browserFirst != performance);

    folder[0] = 'X';
    firstName[0] = 'X';
    screen.showParameter("amount", 5, 0, 10, 1200);
    screen.render(1200);
    assert(screen.buffer() == browserFirst);
    screen.render(5000);
    assert(screen.buffer() == browserFirst);

    screen.showBrowser("breaks", lines, 4, 1);
    screen.render(5000);
    const ScreenUi::Buffer browserDirectory = screen.buffer();
    assert(browserDirectory != browserFirst);

    const ScreenUi::BrowserLine fileLine[] = {{"drums", false}};
    const ScreenUi::BrowserLine directoryLine[] = {{"drums", true}};
    screen.showBrowser("breaks", fileLine, 1, 0);
    screen.render(5000);
    const ScreenUi::Buffer fileBrowser = screen.buffer();
    screen.showBrowser("breaks", directoryLine, 1, 0);
    screen.render(5000);
    assert(screen.buffer() != fileBrowser);

    screen.showBrowser(nullptr, nullptr, 99, 99);
    screen.render(5000);
    assert(std::any_of(screen.buffer().begin(), screen.buffer().end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    screen.showBrowser("breaks", lines, 4, 999);
    screen.render(5000);
    assert(screen.buffer() != browserFirst);
    const ScreenUi::Buffer browserLast = screen.buffer();
    screen.showParameter("amount", 5, 0, 10, 5000);
    screen.render(5000);
    assert(screen.buffer() == browserLast);

    screen.showPerformance();
    screen.render(5000);
    assert(screen.buffer() == performance);

    screen.showParameter("amount", 99, 0, 10, 2000);
    screen.render(2000);
    const ScreenUi::Buffer clampedHigh = screen.buffer();
    screen.showParameter("amount", 10, 0, 10, 2000);
    screen.render(2000);
    assert(screen.buffer() == clampedHigh);

    screen.showParameter("wide", 0, std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::max(), 3000);
    screen.render(3000);
    assert(std::any_of(screen.buffer().begin(), screen.buffer().end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    screen.showPerformance();
    screen.showParameter("speed", 100, 25, 400, 6000, "%");
    screen.render(6000);
    const ScreenUi::Buffer percentValue = screen.buffer();
    screen.showParameter("speed", 100, 25, 400, 6000);
    screen.render(6000);
    assert(screen.buffer() != percentValue);

    screen.showParameter("speed", 400, 25, 400, 6000, "%");
    screen.render(6000);
    assert(std::any_of(screen.buffer().begin(), screen.buffer().end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    screen.showPerformance();
    screen.showFxPad(7, "BLANK", 1);
    screen.render(7000);
    const ScreenUi::Buffer fxBlank = screen.buffer();
    assert(fxBlank != performance);
    assert(std::any_of(fxBlank.begin(), fxBlank.end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    screen.showFxPad(8, "BLANK", 1);
    screen.render(7000);
    assert(screen.buffer() != fxBlank);

    screen.showFxPad(7, "REVERSE", 1);
    screen.render(7000);
    assert(screen.buffer() != fxBlank);

    screen.showFxPad(7, "BLANK", 2);
    screen.render(7000);
    assert(screen.buffer() != fxBlank);

    screen.showFxPad(7, "BLANK", 1);
    screen.render(7000);
    assert(screen.buffer() == fxBlank);

    screen.showParameter("amount", 5, 0, 10, 7000);
    screen.render(7000);
    assert(screen.buffer() == fxBlank);

    screen.showBrowser("breaks", lines, 4, 0);
    screen.render(7000);
    assert(screen.buffer() != fxBlank);

    screen.showFxPad(7, "BLANK", 1);
    screen.render(7000);
    assert(screen.buffer() == fxBlank);

    screen.showPerformance();
    screen.render(7000);
    assert(screen.buffer() == performance);

    assert(!screen.pixel(-1, 0));
    assert(!screen.pixel(ScreenUi::kWidth, 0));
    std::printf("screen_ui: ok\n");
    return 0;
}
