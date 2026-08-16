#include "screen_ui.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

int main() {
    ScreenUi screen;
    screen.setPerformance("amen.wav", 145, PlaybackMode::Granular);
    screen.render(0);
    const ScreenUi::Buffer performance = screen.buffer();
    assert(std::any_of(performance.begin(), performance.end(),
                       [](std::uint8_t byte) { return byte != 0; }));

    screen.showParameter("disperser", 7, 0, 10, 100);
    screen.render(1099);
    assert(screen.buffer() != performance);

    screen.render(1100);
    assert(screen.buffer() == performance);

    screen.showParameter("amount", 99, 0, 10, 2000);
    screen.render(2000);
    const ScreenUi::Buffer clampedHigh = screen.buffer();
    screen.showParameter("amount", 10, 0, 10, 2000);
    screen.render(2000);
    assert(screen.buffer() == clampedHigh);

    assert(!screen.pixel(-1, 0));
    assert(!screen.pixel(ScreenUi::kWidth, 0));
    std::printf("screen_ui: ok\n");
    return 0;
}
