#include "diatonic_scales.h"
#include "harmony_recipes.h"
#include "simple_midi_controller.h"
#include "oled_ui.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>

namespace {

using amen::MidiCommand;
using amen::MidiCommandType;
using amen::SimpleMidiController;

const char* g_block = "";

constexpr uint8_t kCap = SimpleMidiController::kMaxEventsPerAction;

constexpr uint32_t us(uint32_t milliseconds) {
    return milliseconds * 1000U;
}

struct EventList {
    MidiCommand items[kCap];
    uint8_t count{};
};

EventList press(SimpleMidiController& controller, uint8_t key) {
    EventList list;
    list.count = controller.press(key, list.items, kCap);
    return list;
}

EventList release(SimpleMidiController& controller, uint8_t key) {
    EventList list;
    list.count = controller.release(key, list.items, kCap);
    return list;
}

EventList press(SimpleMidiController& controller, uint8_t key, uint32_t now) {
    EventList list;
    list.count = controller.press(key, us(now), list.items, kCap);
    return list;
}

EventList tick(SimpleMidiController& controller, uint32_t now) {
    EventList list;
    list.count = controller.tick(us(now), list.items, kCap);
    return list;
}

EventList pressUs(SimpleMidiController& controller, uint8_t key, uint32_t now) {
    EventList list;
    list.count = controller.press(key, now, list.items, kCap);
    return list;
}

EventList tickUs(SimpleMidiController& controller, uint32_t now) {
    EventList list;
    list.count = controller.tick(now, list.items, kCap);
    return list;
}

EventList togglePage(SimpleMidiController& controller) {
    EventList list;
    list.count = controller.togglePage(list.items, kCap);
    return list;
}

EventList cancelRun(SimpleMidiController& controller) {
    EventList list;
    list.count = controller.cancelRun(list.items, kCap);
    return list;
}

void assertEvents(const EventList& list, std::initializer_list<MidiCommand> expected) {
    if (static_cast<std::size_t>(list.count) != expected.size() ||
        !std::equal(expected.begin(), expected.end(), list.items,
                    [](const MidiCommand& a, const MidiCommand& b) {
                        return a.type == b.type && a.note == b.note && a.velocity == b.velocity;
                    })) {
        std::fprintf(stderr, "assertEvents FAIL in block: %s (count %u)\nexpected:", g_block, list.count);
        for (const MidiCommand& command : expected)
            std::fprintf(stderr, " %s%u", command.type == MidiCommandType::NoteOn ? "on" : "off", command.note);
        std::fprintf(stderr, "\nreceived:");
        for (uint8_t i = 0; i < list.count; ++i)
            std::fprintf(stderr, " %s%u", list.items[i].type == MidiCommandType::NoteOn ? "on" : "off",
                         list.items[i].note);
        std::fprintf(stderr, "\n");
        std::abort();
    }
}

void assertNoteOns(const EventList& list, std::initializer_list<uint8_t> notes) {
    if (static_cast<std::size_t>(list.count) != notes.size() ||
        !std::equal(notes.begin(), notes.end(), list.items,
                    [](uint8_t note, const MidiCommand& command) {
                        return command.type == MidiCommandType::NoteOn && command.note == note && command.velocity == 100;
                    })) {
        std::fprintf(stderr, "assertNoteOns FAIL in block: %s (count %u)\n", g_block, list.count);
        std::abort();
    }
}

constexpr MidiCommand on(uint8_t note) {
    return {MidiCommandType::NoteOn, note, 100};
}

constexpr MidiCommand off(uint8_t note) {
    return {MidiCommandType::NoteOff, note, 0};
}

void walkRun(SimpleMidiController& controller, std::initializer_list<uint8_t> notes,
             uint32_t stepDuration, uint32_t start, int = -1) {
    const size_t count = notes.size();
    for (size_t i = 1; i < count; ++i) {
        const uint32_t t = start + static_cast<uint32_t>(i) * stepDuration;
        assertEvents(tick(controller, t - 1), {});
        assertEvents(tick(controller, t), {off(notes.begin()[i - 1]), on(notes.begin()[i])});
    }
    const uint32_t end = start + static_cast<uint32_t>(count) * stepDuration;
    assertEvents(tick(controller, end - 1), {});
    const uint8_t first = notes.begin()[0];
    const uint8_t last = notes.begin()[count - 1];
    if (first == last) assertEvents(tick(controller, end), {});
    else assertEvents(tick(controller, end), {off(last), on(first)});
    assert(controller.runActive());
}

void testPatterns() {
    {
        g_block = "pattern-slots-defaults";
        static constexpr std::array<const char*, 8> names{
            "RUN UP", "RUN DOWN", "UP DOWN", "DOWN UP", "THIRDS UP", "THIRDS DN", "ARP UP", "ARP DOWN"};
        SimpleMidiController controller;
        for (uint8_t slot = 0; slot < 8; ++slot)
            assert(std::string(amen::runShapeName(controller.slotAssignment(slot))) == names[slot]);
        assert(amen::kRunShapeCount == 12);
        assert(std::string(amen::runShapeName(amen::RunShape::Repeat)) == "REPEAT");
        assert(amen::runShapeName(static_cast<amen::RunShape>(255))[0] == '\0');
    }

    {
        g_block = "run-contours-all-shapes";
        for (uint8_t shape = 0; shape < amen::kRunShapeCount; ++shape) {
            for (uint8_t mode = 0; mode < amen::kDiatonicModeCount; ++mode) {
                amen::RunPattern run;
                run.start(60, static_cast<amen::DiatonicMode>(mode), 0,
                          static_cast<amen::RunShape>(shape), 80, 0);
                const amen::RunPatternDefinition& def = amen::kRunShapes[shape];
                for (uint8_t step = 0; step < def.degreeCount; ++step) {
                    run.tick(step * 80U);
                    assert(run.note() == 60 + amen::signedScaleDegreeOffset(
                        static_cast<amen::DiatonicMode>(mode), def.degrees[step]));
                }
                run.tick(def.degreeCount * 80U);
                assert(run.active());
                assert(run.note() == 60);
            }
        }
    }

    {
        g_block = "run-up-timeline";
        SimpleMidiController controller;
        assert(controller.page() == amen::PerformancePage::Harmony && controller.tempo() == 120);
        assertEvents(togglePage(controller), {});
        assert(controller.page() == amen::PerformancePage::Pattern);
        assertEvents(press(controller, 12), {});
        assert(controller.patternHeld());
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.runActive());
        walkRun(controller, {60, 62, 64, 65, 67, 69, 71, 72}, 125, 0, 60);
        release(controller, 0);
        release(controller, 12);
    }

    {
        g_block = "run-down-timeline";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 13);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 59, 57, 55, 53, 52, 50, 48}, 125, 0, 60);
        release(controller, 0);
        release(controller, 13);
    }

    {
        g_block = "pingpong-updown";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 14);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62}, 125, 0, 60);
        release(controller, 0);
        release(controller, 14);
    }

    {
        g_block = "pingpong-downup";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 15);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 59, 57, 55, 53, 52, 50, 48, 50, 52, 53, 55, 57, 59}, 125, 0, 60);
        release(controller, 0);
        release(controller, 15);
    }

    {
        g_block = "thirds-up";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 16);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 64, 62, 65, 64, 67, 65, 69, 67, 71, 69, 72}, 125, 0, 60);
        release(controller, 0);
        release(controller, 16);
    }

    {
        g_block = "thirds-down";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 17);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 57, 59, 55, 57, 53, 55, 52, 53, 50, 52, 48}, 125, 0, 60);
        release(controller, 0);
        release(controller, 17);
    }

    {
        g_block = "arp-up";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 18);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 64, 67, 72}, 125, 0, 60);
        release(controller, 0);
        release(controller, 18);
    }

    {
        g_block = "arp-down";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 19);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 55, 52, 48}, 125, 0, 60);
        release(controller, 0);
        release(controller, 19);
    }

    {
        g_block = "repeat-gate-and-release-pattern";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 12);
        assert(controller.turnPattern(static_cast<int>(amen::RunShape::Repeat)));
        assert(controller.currentPattern() == amen::RunShape::Repeat);
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.runActive());
        assertEvents(tick(controller, 93), {});
        assertEvents(tick(controller, 94), {off(60)});
        assertEvents(tick(controller, 124), {});
        assertEvents(tick(controller, 125), {on(60)});
        assertEvents(tick(controller, 219), {off(60)});
        assertEvents(tick(controller, 250), {on(60)});
        assertEvents(release(controller, 12), {});
        assert(!controller.runActive());
        assertEvents(release(controller, 0), {off(60)});
    }

    {
        g_block = "repeat-release-source-and-live-tempo";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 12);
        controller.turnPattern(static_cast<int>(amen::RunShape::Repeat));
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.turnTempo(180, 0) && controller.tempo() == 300);
        assertEvents(tick(controller, 38), {off(60)});
        assertEvents(tick(controller, 50), {on(60)});
        assertEvents(release(controller, 0), {off(60)});
        assert(!controller.runActive());
        release(controller, 12);
    }

    {
        g_block = "repeat-lower-first-restores-from-gap";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 12);
        controller.turnPattern(static_cast<int>(amen::RunShape::Repeat));
        release(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60)});
        assertEvents(press(controller, 12, 0), {});
        assert(controller.runShape() == amen::RunShape::Repeat);
        assertEvents(tick(controller, 94), {off(60)});
        assertEvents(release(controller, 12), {on(60)});
        assert(!controller.runActive());
        assertEvents(release(controller, 0), {off(60)});
    }

    {
        g_block = "run-up-truncates-at-127";
        SimpleMidiController controller;
        controller.turnOctave(100);
        controller.turnRoot(-1);
        controller.turnPreset(1);
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 6, 0), {on(117)});
        walkRun(controller, {117, 119, 121, 122, 124, 126, 127}, 125, 0, 117);
        release(controller, 6);
        release(controller, 12);
    }

    {
        g_block = "run-down-truncates-at-0";
        SimpleMidiController controller;
        controller.turnOctave(-100);
        togglePage(controller);
        press(controller, 13);
        assertEvents(press(controller, 0, 0), {on(0)});
        assert(controller.runActive());
        assertEvents(tick(controller, 125), {});
        assert(controller.runActive());
        assertEvents(release(controller, 0), {off(0)});
        assert(!controller.runActive());
        release(controller, 13);
    }

    {
        g_block = "run-down-nonnegative-all-presets";
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            if (amen::SimpleMidiController::isDrumPreset(static_cast<amen::MusicalPreset>(preset))) continue;
            SimpleMidiController controller;
            controller.turnPreset(preset);
            controller.turnOctave(-100);
            togglePage(controller);
            press(controller, 13);
            assertEvents(press(controller, 0, 0), {on(0)});
            assert(controller.runActive());
            assertEvents(tick(controller, 125), {});
            assert(controller.runActive());
            assertEvents(release(controller, 0), {off(0)});
            assert(!controller.runActive());
            release(controller, 13);
        }
    }

    {
        g_block = "order-lower-then-upper-restores-manual";
        SimpleMidiController controller;
        togglePage(controller);
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.heldCount() == 1 && !controller.runActive());
        assertEvents(press(controller, 12, 0), {});
        assert(controller.runActive() && controller.runSourceKey() == 0);
        assertEvents(tick(controller, 125), {off(60), on(62)});
        assertEvents(tick(controller, 250), {off(62), on(64)});
        assertEvents(tick(controller, 375), {off(64), on(65)});
        assertEvents(tick(controller, 500), {off(65), on(67)});
        assertEvents(tick(controller, 625), {off(67), on(69)});
        assertEvents(tick(controller, 750), {off(69), on(71)});
        assertEvents(tick(controller, 875), {off(71), on(72)});
        assertEvents(tick(controller, 1000), {off(72), on(60)});
        assert(controller.runActive() && controller.heldCount() == 1);
        assertEvents(release(controller, 12), {});
        assert(!controller.runActive());
        assertEvents(release(controller, 0), {off(60)});
    }

    {
        g_block = "order-upper-then-lower-restores-manual";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 13);
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.runSourceKey() == 0 && controller.runShape() == amen::RunShape::RunDown);
        assertEvents(tick(controller, 125), {off(60), on(59)});
        assertEvents(tick(controller, 250), {off(59), on(57)});
        assertEvents(tick(controller, 375), {off(57), on(55)});
        assertEvents(tick(controller, 500), {off(55), on(53)});
        assertEvents(tick(controller, 625), {off(53), on(52)});
        assertEvents(tick(controller, 750), {off(52), on(50)});
        assertEvents(tick(controller, 875), {off(50), on(48)});
        assertEvents(tick(controller, 1000), {off(48), on(60)});
        assert(controller.runActive() && controller.heldCount() == 1);
        assertEvents(release(controller, 13), {});
        assert(!controller.runActive());
        assertEvents(release(controller, 0), {off(60)});
    }

    {
        g_block = "hold-lower-then-upper-restores-chord";
        SimpleMidiController controller;
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60), on(64), on(67)});
        togglePage(controller);
        assertEvents(press(controller, 13, 0), {off(64), off(67)});
        assert(controller.runActive());
        assertEvents(tick(controller, 125), {off(60), on(59)});
        assertEvents(tick(controller, 250), {off(59), on(57)});
        assertEvents(tick(controller, 375), {off(57), on(55)});
        assertEvents(tick(controller, 500), {off(55), on(53)});
        assertEvents(tick(controller, 625), {off(53), on(52)});
        assertEvents(tick(controller, 750), {off(52), on(50)});
        assertEvents(tick(controller, 875), {off(50), on(48)});
        assertEvents(tick(controller, 1000), {off(48), on(60)});
        assertEvents(release(controller, 13), {on(64), on(67)});
        assertEvents(release(controller, 0), {off(60), off(64), off(67)});
        release(controller, 12);
    }

    {
        g_block = "cancel-restores-source";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 0, 0);
        press(controller, 12, 0);
        assertEvents(tick(controller, 125), {off(60), on(62)});
        assertEvents(cancelRun(controller), {off(62), on(60)});
        assert(!controller.runActive() && controller.runSourceKey() == amen::SimpleMidiController::kNoRunSource);
        release(controller, 0);
        release(controller, 12);
    }

    {
        g_block = "release-source-prevents-restoration";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 0, 0);
        press(controller, 12, 0);
        assertEvents(release(controller, 0), {off(60)});
        assert(!controller.runActive() && controller.heldCount() == 0);
        release(controller, 12);
    }

    {
        g_block = "retrigger-transfers-restoration";
        SimpleMidiController controller;
        togglePage(controller);
        assertEvents(press(controller, 0, 0), {on(60)});
        assertEvents(press(controller, 12, 0), {});
        assertEvents(tick(controller, 125), {off(60), on(62)});
        assertEvents(press(controller, 4, 150), {off(62), on(60), on(67)});
        assert(controller.runSourceKey() == 4);
        assertEvents(tick(controller, 275), {off(67), on(69)});
        assertEvents(tick(controller, 400), {off(69), on(71)});
        assertEvents(tick(controller, 525), {off(71), on(72)});
        assertEvents(tick(controller, 650), {off(72), on(74)});
        assertEvents(tick(controller, 775), {off(74), on(76)});
        assertEvents(tick(controller, 900), {off(76), on(77)});
        assertEvents(tick(controller, 1025), {off(77), on(79)});
        assertEvents(tick(controller, 1150), {off(79), on(67)});
        release(controller, 0);
        release(controller, 4);
        release(controller, 12);
    }

    {
        g_block = "reference-lifo-most-recent-lower";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 0, 0);
        press(controller, 4, 0);
        assertEvents(press(controller, 19, 0), {});
        assert(controller.runSourceKey() == 4 && controller.runNote() == 67);
        release(controller, 0);
        release(controller, 4);
        release(controller, 19);
    }

    {
        g_block = "pattern-lifo-release-restores-previous";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 12);
        assert(controller.patternSlot() == 0);
        press(controller, 13);
        assert(controller.patternSlot() == 1);
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.runShape() == amen::RunShape::RunDown);
        release(controller, 0);
        tick(controller, 1000);
        assert(!controller.runActive());
        release(controller, 13);
        assert(controller.patternSlot() == 0);
        assertEvents(press(controller, 0, 1100), {on(60)});
        assert(controller.runShape() == amen::RunShape::RunUp);
        release(controller, 0);
        tick(controller, 2100);
        release(controller, 12);
    }

    {
        g_block = "edit-under-held-wraps-no-mutate-active";
        SimpleMidiController controller;
        togglePage(controller);
        assert(!controller.turnPattern(1));
        press(controller, 12);
        assert(controller.patternSlot() == 0);
        assert(controller.slotAssignment(0) == amen::RunShape::RunUp);
        assertEvents(press(controller, 0, 0), {on(60)});
        assert(controller.turnPattern(1));
        assert(controller.slotAssignment(0) == amen::RunShape::RunDown);
        assert(controller.runShape() == amen::RunShape::RunUp);
        assertEvents(tick(controller, 125), {off(60), on(62)});
        for (int i = 0; i < amen::kRunShapeCount - 1; ++i) assert(controller.turnPattern(1));
        assert(controller.slotAssignment(0) == amen::RunShape::RunUp);
        assert(!controller.turnPattern(0));
        tick(controller, 1000);
        release(controller, 0);
        release(controller, 12);
    }

    {
        g_block = "run-shared-owner-pitch";
        SimpleMidiController controller;
        assertEvents(press(controller, 7), {on(72)});
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60)});
        assertEvents(tick(controller, 875), {off(60)});
        assert(controller.runNote() == 72);
        assertEvents(tick(controller, 1000), {on(60)});
        assertEvents(release(controller, 7), {off(72)});
        release(controller, 0);
        release(controller, 12);
    }

    {
        g_block = "run-live-tempo";
        SimpleMidiController controller;
        assert(controller.turnTempo(1, 0) && controller.tempo() == 121);
        assert(controller.turnTempo(-1, 0) && controller.tempo() == 120);
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60)});
        controller.turnRoot(9);
        controller.turnPreset(2);
        controller.turnOctave(1);
        controller.turnTempo(30, 0);
        assert(controller.tempo() == 150);
        assertEvents(tick(controller, 200), {off(60), on(64)});
        assertEvents(tick(controller, 700), {off(64), on(72)});
        assertEvents(release(controller, 0), {off(72)});
        assertEvents(tick(controller, 800), {});
        assertEvents(press(controller, 0, 900), {on(81)});
        assertEvents(tick(controller, 999), {});
        assertEvents(tick(controller, 1000), {off(81), on(83)});
        assertEvents(release(controller, 0), {off(83)});
        assertEvents(tick(controller, 1500), {});
        assertEvents(tick(controller, 1700), {});
        assert(controller.turnTempo(std::numeric_limits<int>::max(), us(1700)) && controller.tempo() == 300);
        assert(!controller.turnTempo(1, us(1700)));
        assert(controller.turnTempo(std::numeric_limits<int>::min(), us(1700)) && controller.tempo() == 20);
        assert(!controller.turnTempo(-1, us(1700)) && !controller.turnTempo(0, us(1700)));
        release(controller, 12);
    }

    {
        g_block = "run-live-tempo-preserves-phase";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60)});
        assertEvents(tick(controller, 62), {});
        assert(controller.turnTempo(180, us(62)));
        assertEvents(tick(controller, 87), {});
        assertEvents(tick(controller, 88), {off(60), on(62)});
        release(controller, 0);
        release(controller, 12);
    }

    {
        g_block = "frequency-clock-range-and-switching";
        SimpleMidiController controller;
        assert(controller.clockMode() == amen::ClockMode::Tempo);
        assert(controller.frequencyTenths() == 81);
        assert(controller.turnFrequency(std::numeric_limits<int>::min(), 0));
        assert(controller.clockMode() == amen::ClockMode::Frequency && controller.frequencyTenths() == 5);
        assert(!controller.turnFrequency(-1, 0));
        assert(controller.turnFrequency(std::numeric_limits<int>::max(), 0));
        assert(controller.frequencyTenths() == 500);
        assert(!controller.turnFrequency(1, 0));
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60)});
        assertEvents(tick(controller, 19), {});
        assertEvents(tick(controller, 20), {off(60), on(62)});
        assert(controller.turnTempo(0, us(20)));
        assert(controller.clockMode() == amen::ClockMode::Tempo);
        assertEvents(tick(controller, 144), {});
        assertEvents(tick(controller, 145), {off(62), on(64)});
        assert(controller.turnFrequency(std::numeric_limits<int>::min(), us(145)));
        assertEvents(tick(controller, 2144), {});
        assertEvents(tick(controller, 2145), {off(64), on(65)});
        assertEvents(release(controller, 0), {off(65)});
        release(controller, 12);
    }

    {
        g_block = "run-wrap-and-delay-skip";
        SimpleMidiController controller;
        togglePage(controller);
        press(controller, 12);
        const uint32_t start = std::numeric_limits<uint32_t>::max() - 39999U;
        assertEvents(pressUs(controller, 0, start), {on(60)});
        assertEvents(tickUs(controller, 84999), {});
        assertEvents(tickUs(controller, 85000), {off(60), on(62)});
        assertEvents(tickUs(controller, 584999), {off(62), on(67)});
        assertEvents(tickUs(controller, 585000), {off(67), on(69)});
        assertEvents(tickUs(controller, 709999), {});
        assertEvents(tickUs(controller, 710000), {off(69), on(71)});
        assertEvents(release(controller, 0), {off(71)});
        release(controller, 12);
    }

    {
        g_block = "toggle-page-cancels-restores";
        SimpleMidiController controller;
        togglePage(controller);
        assertEvents(press(controller, 0, 0), {on(60)});
        press(controller, 12, 0);
        assertEvents(tick(controller, 125), {off(60), on(62)});
        assertEvents(togglePage(controller), {off(62), on(60)});
        assert(controller.page() == amen::PerformancePage::None && !controller.runActive());
        assert(controller.runSourceKey() == amen::SimpleMidiController::kNoRunSource);
        assertEvents(togglePage(controller), {});
        assert(controller.page() == amen::PerformancePage::Harmony && !controller.runActive());
        assertEvents(press(controller, 1), {on(62)});
        assert(!controller.runActive());
        release(controller, 0);
        release(controller, 1);
        release(controller, 12);
    }

    {
        g_block = "pattern-transactional-actions";
        const auto check = [](SimpleMidiController initial, const auto& action) {
            SimpleMidiController expected = initial;
            EventList accepted;
            accepted.count = action(expected, accepted.items, kCap);
            for (uint8_t capacity = 0; capacity < std::max<uint8_t>(1, accepted.count); ++capacity) {
                SimpleMidiController actual = initial;
                std::array<MidiCommand, kCap> buffer;
                buffer.fill(on(127));
                assert(action(actual, buffer.data(), capacity) == 0);
                for (const auto& event : buffer) assert(event.type == MidiCommandType::NoteOn && event.note == 127 && event.velocity == 100);
                assert(actual.page() == initial.page());
                assert(actual.runActive() == initial.runActive());
                assert(actual.runShape() == initial.runShape());
                assert(actual.patternHeld() == initial.patternHeld());
                assert(actual.heldCount() == initial.heldCount());
                assert(actual.harmonyStackSize() == initial.harmonyStackSize());
                assert(actual.runSourceKey() == initial.runSourceKey());
                assert(action(actual, buffer.data(), kCap) == accepted.count);
                for (uint8_t i = 0; i < accepted.count; ++i) {
                    assert(buffer[i].type == accepted.items[i].type && buffer[i].note == accepted.items[i].note);
                }
                for (uint8_t key = 0; key < 20; ++key) {
                    SimpleMidiController reference = expected;
                    SimpleMidiController released = actual;
                    const auto a = release(released, key);
                    const auto b = release(reference, key);
                    assert(a.count == b.count);
                    for (uint8_t i = 0; i < a.count; ++i) assert(a.items[i].type == b.items[i].type && a.items[i].note == b.items[i].note);
                }
                const auto a = tick(actual, 10000);
                SimpleMidiController reference = expected;
                const auto b = tick(reference, 10000);
                assert(a.count == b.count);
                for (uint8_t i = 0; i < a.count; ++i) assert(a.items[i].type == b.items[i].type && a.items[i].note == b.items[i].note);
            }
        };
        const auto page = [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.togglePage(out, cap); };
        const auto trigger = [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.press(0, 0, out, cap); };
        const auto advance = [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.tick(us(125), out, cap); };
        SimpleMidiController controller;
        check(controller, page);
        togglePage(controller);
        check(controller, [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.press(12, 0, out, cap); });
        press(controller, 12);
        check(controller, trigger);
        press(controller, 0, 0);
        check(controller, advance);
        check(controller, page);
        check(controller, [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.tick(us(1000), out, cap); });
        check(controller, [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.release(0, out, cap); });
        check(controller, [](SimpleMidiController& c, MidiCommand* out, uint8_t cap) { return c.release(12, out, cap); });
    }

    {
        g_block = "ui-pattern-label-persists";
        amen::SimpleMidiController controller;
        amen::OledUi ui;
        togglePage(controller);
        press(controller, 12);
        ui.showPattern(10);
        amen::MonoFramebuffer expected;
        expected.drawText(0, 0, "PATTERN", 2);
        expected.drawText(96, 0, "PATT", 2);
        expected.drawText(17, 18, "RUN UP READY", 2);
        assert(ui.render(controller, amen::E2Page::Root, 10).pixels() == expected.pixels());
        assert(ui.overlayVisible());
        controller.turnPattern(1);
        ui.showPatternEdit(20);
        const auto edit = ui.render(controller, amen::E2Page::Root, 20).pixels();
        assert(ui.overlayVisible() && edit != expected.pixels());
        assert(ui.render(controller, amen::E2Page::Root, 820).pixels() ==
               ui.render(controller, amen::E2Page::Root, 1000).pixels());
        assert(!ui.overlayVisible());
    }

    {
        g_block = "chromatic-run";
        SimpleMidiController controller;
        controller.turnPreset(5);
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 61, 62, 63, 64, 65, 66, 67}, 125, 0, 60);
        release(controller, 0);
        release(controller, 12);
        press(controller, 19);
        assertEvents(press(controller, 0, 0), {on(60)});
        walkRun(controller, {60, 57, 55, 53}, 125, 0, 60);
        release(controller, 0);
        release(controller, 19);
    }

    {
        g_block = "page-cycle";
        SimpleMidiController controller;
        assert(controller.page() == amen::PerformancePage::Harmony);
        assertEvents(togglePage(controller), {});
        assert(controller.page() == amen::PerformancePage::Pattern);
        assertEvents(togglePage(controller), {});
        assert(controller.page() == amen::PerformancePage::None);
        assertEvents(togglePage(controller), {});
        assert(controller.page() == amen::PerformancePage::Harmony);
    }

    {
        g_block = "none-page-notes";
        SimpleMidiController controller;
        togglePage(controller);
        togglePage(controller);
        assert(controller.page() == amen::PerformancePage::None);
        assert(!controller.harmonyActive() && !controller.patternHeld());
        for (uint8_t key = amen::SimpleMidiController::kHarmonyStartKey;
             key < controller.kNoteKeyCount; ++key) {
            const uint8_t note = static_cast<uint8_t>(60 + amen::scaleDegreeOffset(controller.scale(), key));
            assertEvents(press(controller, key), {on(note)});
            assertEvents(release(controller, key), {off(note)});
        }
        togglePage(controller);
        assertEvents(press(controller, 12), {});
        SimpleMidiController withSlot = controller;
        togglePage(withSlot);
        togglePage(withSlot);
        assert(withSlot.page() == amen::PerformancePage::None && withSlot.harmonyActive());
        assertEvents(press(withSlot, 0), {on(60)});
        assertEvents(press(withSlot, 13), {on(83)});
        assertEvents(release(withSlot, 0), {off(60)});
        assertEvents(release(withSlot, 13), {off(83)});
        assertEvents(release(withSlot, 12), {});
        release(controller, 12);
    }

    {
        g_block = "none-cancels-run";
        SimpleMidiController controller;
        togglePage(controller);
        assertEvents(press(controller, 0, 0), {on(60)});
        press(controller, 12, 0);
        assertEvents(tick(controller, 125), {off(60), on(62)});
        assertEvents(togglePage(controller), {off(62), on(60)});
        assert(controller.page() == amen::PerformancePage::None && !controller.runActive());
        assertEvents(release(controller, 12), {});
        release(controller, 0);
    }

    {
        g_block = "none-page-exact-ui";
        amen::SimpleMidiController controller;
        amen::OledUi ui;
        togglePage(controller);
        togglePage(controller);
        assert(controller.page() == amen::PerformancePage::None);
        amen::MonoFramebuffer expected;
        expected.drawText(0, 0, "O5 C", 2);
        expected.drawText(96, 0, "NONE", 2);
        expected.drawText(0, 11, "MAJOR ", 2);
        expected.drawText(37, 22, "(MAJOR)", 2);
        assert(ui.render(controller, amen::E2Page::Root, 0).pixels() == expected.pixels());
        ui.showPage(10);
        expected.clear();
        expected.drawText(0, 0, "PAGE", 2);
        expected.drawText(96, 0, "NONE", 2);
        expected.drawText(49, 18, "NONE", 2);
        assert(ui.render(controller, amen::E2Page::Root, 10).pixels() == expected.pixels());
    }

    {
        g_block = "gm-kit-notes-roll-and-channel";
        SimpleMidiController controller;
        controller.turnPreset(6);
        assert(controller.drums());
        assert(controller.midiChannel() == 10);
        assert(!controller.turnOctave(5) && !controller.turnRoot(3));
        constexpr std::array<std::array<uint8_t, 2>, 20> expected{{
            {{36, 56}}, {{38, 58}}, {{39, 59}}, {{40, 57}}, {{42, 60}},
            {{44, 62}}, {{46, 64}}, {{48, 66}}, {{49, 68}}, {{50, 70}},
            {{51, 72}}, {{53, 74}}, {{54, 75}}, {{55, 76}}, {{56, 77}},
            {{57, 78}}, {{59, 80}}, {{60, 81}}, {{63, 82}}, {{64, 83}}
        }};
        for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
            const EventList down = press(controller, key);
            assert(down.count == 1 && down.items[0].note == expected[key][0]);
            const EventList up = release(controller, key);
            assert(up.count == 1 && up.items[0].note == expected[key][0]);
        }
        for (uint8_t key = 0; key < amen::kGmDrumNotes.size(); ++key)
            assert(amen::kGmDrumNotes[key] == expected[key][0]);
        assert(std::string(amen::gmDrumLabel(0)) == "BD" && std::string(amen::gmDrumLabel(19)) == "LC");
        togglePage(controller);
        press(controller, 12);
        assertEvents(press(controller, 0, 0), {on(36)});
        assert(controller.runShape() == amen::RunShape::Repeat);
        assertEvents(tick(controller, 93), {});
        assertEvents(tick(controller, 94), {off(36)});
        assertEvents(tick(controller, 125), {on(36)});
        assertEvents(release(controller, 0), {off(36)});
        release(controller, 12);
    }

    {
        g_block = "gm-kit-home-label";
        SimpleMidiController controller;
        amen::OledUi ui;
        controller.turnPreset(6);
        const auto idle = ui.render(controller, amen::E2Page::Root, 0).pixels();
        press(controller, 0);
        const auto drumLabel = ui.render(controller, amen::E2Page::Root, 0).pixels();
        assert(drumLabel != idle);
        assert(controller.currentNoteName()[0] != '\0' &&
               std::string(controller.currentNoteName()) == "BD");
        release(controller, 0);
        assert(ui.render(controller, amen::E2Page::Root, 0).pixels() == idle);
    }
}
}  // namespace

int main() {
    {
        constexpr std::array<std::array<uint8_t, 7>, 8> expectedIntervals{{
            {{0, 2, 4, 5, 7, 9, 11}},
            {{0, 2, 3, 5, 7, 9, 10}},
            {{0, 1, 3, 5, 7, 8, 10}},
            {{0, 2, 4, 6, 7, 9, 11}},
            {{0, 2, 4, 5, 7, 9, 10}},
            {{0, 2, 3, 5, 7, 8, 10}},
            {{0, 1, 3, 5, 6, 8, 10}},
            {{0, 2, 3, 5, 7, 8, 11}},
        }};

        for (uint8_t mode = 0; mode < amen::kDiatonicModeCount; ++mode) {
            const auto selected = static_cast<amen::DiatonicMode>(mode);
            assert(amen::modeName(selected)[0] != '\0');
            assert(amen::modeShortName(selected)[0] != '\0');
            assert(amen::modeDescription(selected)[0] == '(');
        }
        for (uint8_t mode = 0; mode < expectedIntervals.size(); ++mode) {
            const auto selected = static_cast<amen::DiatonicMode>(mode);
            for (uint8_t degree = 0; degree < 7; ++degree)
                assert(amen::scaleDegreeOffset(selected, degree) == expectedIntervals[mode][degree]);
            assert(amen::scaleDegreeOffset(selected, 7) == 12);
            assert(amen::scaleDegreeOffset(selected, 11) == 12 + expectedIntervals[mode][4]);
        }
        for (uint8_t degree = 0; degree < 12; ++degree)
            assert(amen::scaleDegreeOffset(amen::DiatonicMode::Chromatic, degree) == degree);
        assert(amen::scaleDegreeOffset(amen::DiatonicMode::Chromatic, 12) == 12);

        assert(amen::pitchClassName(1) == std::string("Db"));
        constexpr std::array<char, 4> eb{{'E', 'b', '\0', '\0'}};
        constexpr std::array<char, 4> ab{{'A', 'b', '\0', '\0'}};
        constexpr std::array<char, 4> b{{'B', '\0', '\0', '\0'}};
        constexpr std::array<char, 4> ebb{{'E', 'b', 'b', '\0'}};
        assert(amen::spellScaleDegree(3, amen::DiatonicMode::Ionian, 0).text == eb);
        assert(amen::spellScaleDegree(3, amen::DiatonicMode::Ionian, 3).text == ab);
        assert(amen::spellScaleDegree(5, amen::DiatonicMode::Lydian, 3).text == b);
        assert(amen::spellScaleDegree(1, amen::DiatonicMode::Locrian, 1).text == ebb);
        assert(amen::kDiatonicModeCount == expectedIntervals.size() + 1);
        constexpr std::array<char, 7> letters{{'C', 'D', 'E', 'F', 'G', 'A', 'B'}};
        constexpr std::array<int, 7> natural{{0, 2, 4, 5, 7, 9, 11}};
        for (uint8_t mode = 0; mode < expectedIntervals.size(); ++mode)
        for (uint8_t root = 0; root < 12; ++root)
        for (uint8_t degree = 0; degree < 12; ++degree) {
            const auto spelling = amen::spellScaleDegree(root, static_cast<amen::DiatonicMode>(mode), degree).text;
            const auto letter = std::find(letters.begin(), letters.end(), spelling[0]);
            assert(letter != letters.end() && spelling.back() == '\0');
            int pitch = natural[static_cast<std::size_t>(letter - letters.begin())];
            for (std::size_t i = 1; i < spelling.size() && spelling[i] != '\0'; ++i) {
                assert(spelling[i] == '#' || spelling[i] == 'b');
                pitch += spelling[i] == '#' ? 1 : -1;
            }
            assert((pitch + 12) % 12 == (root + 12 * (degree / 7) + expectedIntervals[mode][degree % 7]) % 12);
            const auto rootLetter = std::find(letters.begin(), letters.end(), amen::pitchClassName(root)[0]);
            assert(letter - letters.begin() == (rootLetter - letters.begin() + degree) % 7);
        }
        for (uint8_t root = 0; root < 12; ++root)
        for (uint8_t degree = 0; degree < 12; ++degree)
            assert(std::string(amen::spellScaleDegree(root, amen::DiatonicMode::Chromatic, degree).text.data()) ==
                   amen::pitchClassName((root + degree) % 12));
    }

    for (const amen::ChordRecipe& recipe : amen::kChordRecipes) {
        assert(recipe.name[0] != '\0');
        assert(recipe.voiceCount >= 1 && recipe.voiceCount <= amen::kMaxRecipeVoices);
        assert(recipe.degrees[0] == 0 && recipe.octaveDisplacements[0] == 0);
        for (uint8_t voice = 0; voice < recipe.voiceCount; ++voice) {
            assert(recipe.degrees[voice] <= 16);
            assert(recipe.octaveDisplacements[voice] % 12 == 0);
        }
    }
    assert(amen::harmonySlotName(amen::MusicalPreset::Major, amen::kHarmonySlotCount)[0] == '\0');
    assert(amen::harmonySlotName(amen::MusicalPreset::Major, 255)[0] == '\0');

    {
        SimpleMidiController controller;
        g_block = "basics";
        constexpr std::array<uint8_t, 12> cIonian{{60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79}};
        assert(controller.rootPitchClass() == 0);
        assert(controller.preset() == amen::MusicalPreset::Major);
        assert(controller.octaveNumber() == 5);
        assert(controller.rootNote() == 60);
        assert(controller.highestNote() == 79);
        for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
            assertNoteOns(press(controller, key), {cIonian[key]});
            assertEvents(release(controller, key), {off(cIonian[key])});
        }
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 0), {});
        assertEvents(release(controller, 0), {off(60)});
        assertEvents(release(controller, 0), {});
        assertEvents(press(controller, controller.kShiftKey), {});
        assert(controller.currentNoteName()[0] == '\0');
    }

    {
        // première action sur un buffer nul ou vide : rien, pas de plantage
        SimpleMidiController controller;
        g_block = "nullptr";
        assert(controller.press(0, nullptr, kCap) == 0);
        assert(controller.press(0, nullptr, 0) == 0);
        MidiCommand buffer[kCap];
        assert(controller.press(12, buffer, 0) == 0);
        assert(controller.press(12, nullptr, kCap) == 0);
        assert(!controller.harmonyActive() && controller.heldCount() == 0);
        assertEvents(press(controller, 12), {});
        assert(controller.release(12, buffer, 0) == 0);
        assert(controller.release(12, nullptr, kCap) == 0);
        assert(controller.harmonyActive());
        assertEvents(press(controller, 0), {on(60), on(64), on(67)});
        assert(controller.release(0, buffer, 0) == 0);
        assert(controller.release(0, nullptr, kCap) == 0);
        assert(controller.heldCount() == 1);
        assertEvents(release(controller, 0), {off(60), off(64), off(67)});
    }

    {
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assert(std::string(controller.currentNoteName()) == "C");
        assertEvents(press(controller, 2), {on(64)});
        assert(std::string(controller.currentNoteName()) == "E");
        assertEvents(release(controller, 2), {off(64)});
        assert(std::string(controller.currentNoteName()) == "C");
        assertEvents(release(controller, 0), {off(60)});
        assert(controller.currentNoteName()[0] == '\0');
    }

    {
        SimpleMidiController controller;
        g_block = "state-changes";
        assert(controller.turnRoot(-1));
        assert(controller.rootPitchClass() == 11);
        assert(controller.turnRoot(1));
        assert(controller.rootPitchClass() == 0);
        assert(!controller.turnRoot(12));
        assert(controller.turnPreset(-1));
        assert(controller.preset() == amen::MusicalPreset::Prism);
        assert(!controller.drums());
        assert(controller.turnPreset(1));
        assert(controller.preset() == amen::MusicalPreset::Major);
        assert(controller.turnPreset(6));
        assert(controller.preset() == amen::MusicalPreset::GmKit);
        assert(controller.midiChannel() == amen::SimpleMidiController::kDrumChannel);
        assert(controller.turnPreset(2));
        assert(controller.preset() == amen::MusicalPreset::Major);
        assert(!controller.turnPreset(amen::kMusicalPresetCount));

        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 0), {});
        controller.turnRoot(2);
        controller.turnPreset(1);
        controller.turnOctave(1);
        assertEvents(press(controller, 3), {on(79)});
        assertEvents(release(controller, 0), {off(60)});
        assertEvents(release(controller, 3), {off(79)});
    }

    {
        g_block = "spelling";
        SimpleMidiController spellingController;
        spellingController.turnRoot(3);
        assertEvents(press(spellingController, 0), {on(63)});
        assert(std::string(spellingController.currentNoteName()) == "Eb");
        spellingController.turnRoot(4);
        spellingController.turnPreset(3);
        assert(std::string(spellingController.currentNoteName()) == "Eb");
        assertEvents(release(spellingController, 0), {off(63)});
    }

    {
        g_block = "press-under-color";
        // Déclenchement sous couleur : la racine émise est la note de départ, pas la fondamentale.
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 12), {on(64), on(67)});
        assertEvents(release(controller, 0), {off(60), off(64), off(67)});
        assertEvents(release(controller, 12), {});
    }

    {
        g_block = "triad-seventh-triad";
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 12), {on(64), on(67)});
        assertEvents(press(controller, 13), {on(71)});
        assertEvents(release(controller, 13), {off(71)});
        assertEvents(release(controller, 12), {off(64), off(67)});
    }

    {
        g_block = "press-after-color";
        // Racine pressée après la couleur : accord complet direct.
        SimpleMidiController controller;
        assertEvents(press(controller, 12), {});
        assert(controller.harmonyActive());
        assert(std::string(controller.harmonyName()) == "TRIAD");
        assertEvents(press(controller, 0), {on(60), on(64), on(67)});
        assertEvents(release(controller, 12), {off(64), off(67)});
        assert(!controller.harmonyActive());
    }

    {
        g_block = "two-roots";
        // Deux racines : chaque degré prend sa propre hauteur, aucune note n'est éteinte.
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 2), {on(64)});
        assertEvents(press(controller, 12), {on(67), on(71)});
        assertEvents(release(controller, 0), {off(60)});
        assertEvents(release(controller, 2), {off(64), off(67), off(71)});
    }

    {
        g_block = "shared-chord-press";
        SimpleMidiController controller;
        assertEvents(press(controller, 12), {});
        assertEvents(press(controller, 0), {on(60), on(64), on(67)});
        assertEvents(press(controller, 2), {on(71)});
        assertEvents(release(controller, 2), {off(71)});
        assertEvents(release(controller, 0), {off(60), off(64), off(67)});
    }

    {
        g_block = "owner-transfer";
        SimpleMidiController controller;
        assertEvents(press(controller, 16), {});
        assertEvents(press(controller, 0), {on(60), on(62), on(67)});
        assertEvents(press(controller, 1), {on(64), on(69)});
        assertEvents(press(controller, 12), {on(65)});
        assertEvents(release(controller, 12), {off(65)});
        assertEvents(release(controller, 0), {off(60), off(67)});
        assertEvents(release(controller, 1), {off(62), off(64), off(69)});
    }

    {
        g_block = "atomic-capacity";
        const auto checkRejected = [](SimpleMidiController initial, uint8_t key, bool down) {
            SimpleMidiController expected = initial;
            const EventList accepted = down ? press(expected, key) : release(expected, key);
            assert(accepted.count > 1);
            for (uint8_t capacity = 0; capacity < accepted.count; ++capacity) {
                SimpleMidiController actual = initial;
                std::array<MidiCommand, kCap> buffer;
                buffer.fill(on(127));
                assert((down ? actual.press(key, buffer.data(), capacity)
                             : actual.release(key, buffer.data(), capacity)) == 0);
                for (const MidiCommand& event : buffer)
                    assert(event.type == MidiCommandType::NoteOn && event.note == 127 && event.velocity == 100);
                assert(actual.heldCount() == initial.heldCount());
                assert(actual.harmonyStackSize() == initial.harmonyStackSize());
                assert(std::string(actual.currentNoteName()) == initial.currentNoteName());
                assert(std::string(actual.harmonyName()) == initial.harmonyName());
                assert((down ? actual.press(key, nullptr, kCap) : actual.release(key, nullptr, kCap)) == 0);
                EventList retried;
                retried.count = down ? actual.press(key, retried.items, accepted.count)
                                     : actual.release(key, retried.items, accepted.count);
                assert(retried.count == accepted.count);
                for (uint8_t i = 0; i < accepted.count; ++i) {
                    assert(retried.items[i].type == accepted.items[i].type);
                    assert(retried.items[i].note == accepted.items[i].note);
                }
                SimpleMidiController reference = expected;
                for (uint8_t held = 0; held < SimpleMidiController::kShiftKey; ++held) {
                    const EventList a = release(actual, held);
                    const EventList b = release(reference, held);
                    assert(a.count == b.count);
                    for (uint8_t i = 0; i < a.count; ++i) {
                        assert(a.items[i].type == b.items[i].type);
                        assert(a.items[i].note == b.items[i].note);
                    }
                    assert(std::string(actual.currentNoteName()) == reference.currentNoteName());
                    assert(std::string(actual.harmonyName()) == reference.harmonyName());
                }
            }
        };
        SimpleMidiController controller;
        press(controller, 12);
        press(controller, 1);
        checkRejected(controller, 0, true);
        press(controller, 0);
        controller.turnRoot(3);
        controller.turnPreset(2);
        controller.turnOctave(1);
        checkRejected(controller, 0, false);
        checkRejected(controller, 14, true);
        press(controller, 14);
        checkRejected(controller, 14, false);
        release(controller, 14);
        checkRejected(controller, 12, false);
    }

    {
        g_block = "all-keys-colors";
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            if (amen::SimpleMidiController::isDrumPreset(static_cast<amen::MusicalPreset>(preset))) continue;
            for (uint8_t rootClass = 0; rootClass < 12; ++rootClass)
            for (int octave = -5; octave <= 3; ++octave) {
            SimpleMidiController controller;
            controller.turnPreset(preset);
            controller.turnOctave(octave);
            controller.turnRoot(rootClass);
            std::array<bool, 128> active{};
            std::array<bool, SimpleMidiController::kKeyCount> held{};
            const auto apply = [&active](const EventList& events) {
                bool sawOn = false;
                int lastOff = -1;
                int lastOn = -1;
                for (uint8_t i = 0; i < events.count; ++i) {
                    const MidiCommand& event = events.items[i];
                    assert(event.note < 128);
                    if (event.type == MidiCommandType::NoteOn) {
                        assert(!active[event.note] && event.note > lastOn && event.velocity == 100);
                        active[event.note] = true;
                        sawOn = true;
                        lastOn = event.note;
                    } else {
                        assert(event.type == MidiCommandType::NoteOff && active[event.note]);
                        assert(!sawOn && event.note > lastOff && event.velocity == 0);
                        active[event.note] = false;
                        lastOff = event.note;
                    }
                }
            };
            const auto checkUnion = [&](int slot) {
                std::array<bool, 128> expected{};
                for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
                    if (!held[key]) continue;
                    const int root = controller.rootNote() + amen::scaleDegreeOffset(controller.scale(), key);
                    const amen::ChordRecipe* recipe = slot < 0 ? nullptr : amen::harmonySlotRecipe(controller.preset(), static_cast<uint8_t>(slot));
                    for (uint8_t voice = 0; voice < (recipe ? recipe->voiceCount : 1); ++voice) {
                        int note = root + (recipe ? amen::scaleDegreeOffset(controller.scale(), key + recipe->degrees[voice]) -
                            amen::scaleDegreeOffset(controller.scale(), key) + recipe->octaveDisplacements[voice] : 0);
                        while (note > 127) note -= 12;
                        expected[note] = true;
                    }
                }
                assert(active == expected);
            };
            for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
                apply(press(controller, key));
                held[key] = true;
                checkUnion(-1);
            }
            for (uint8_t slot = 0; slot < controller.kHarmonyKeyCount; ++slot) {
                apply(press(controller, controller.kHarmonyStartKey + slot));
                checkUnion(slot);
            }
            for (int slot = controller.kHarmonyKeyCount - 1; slot >= 0; --slot) {
                apply(release(controller, static_cast<uint8_t>(controller.kHarmonyStartKey + slot)));
                checkUnion(slot - 1);
            }
            for (uint8_t slot = 0; slot < controller.kHarmonyKeyCount; ++slot) {
                apply(press(controller, controller.kHarmonyStartKey + slot));
                for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
                    apply(release(controller, key));
                    held[key] = false;
                    checkUnion(slot);
                }
                for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
                    apply(press(controller, key));
                    held[key] = true;
                    checkUnion(slot);
                }
                apply(release(controller, controller.kHarmonyStartKey + slot));
                checkUnion(-1);
            }
            for (uint8_t key = 0; key < controller.kKeyCount; ++key) apply(release(controller, key));
            for (bool sounding : active) assert(!sounding);
            }
        }
    }

    {
        g_block = "seventh-ninth";
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 13), {on(64), on(67), on(71)});
        assertEvents(press(controller, 14), {on(74)});
    }

    {
        g_block = "sixth-six9";
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 18), {on(64), on(67), on(69)});
        assertEvents(press(controller, 19), {on(74)});
    }

    {
        g_block = "lifo";
        // Empilement LIFO : relâcher le sommet restaure la couleur précédente encore tenue.
        SimpleMidiController controller;
        assertEvents(press(controller, 0), {on(60)});
        assertEvents(press(controller, 12), {on(64), on(67)});
        assertEvents(press(controller, 13), {on(71)});
        assert(std::string(controller.harmonyName()) == "SEVENTH");
        assertEvents(release(controller, 13), {off(71)});
        assert(std::string(controller.harmonyName()) == "TRIAD");
        assertEvents(press(controller, 13), {on(71)});
        assertEvents(release(controller, 12), {});
        assert(std::string(controller.harmonyName()) == "SEVENTH");
        assertEvents(release(controller, 13), {off(64), off(67), off(71)});
        assert(!controller.harmonyActive());
        assertEvents(release(controller, 0), {off(60)});
    }

    {
        g_block = "pedigree";
        // Pedigree indépendant : une couleur sans racine puis une racine puis l'inverse.
        SimpleMidiController controller;
        assertEvents(press(controller, 12), {});
        assertEvents(press(controller, 0), {on(60), on(64), on(67)});
        assertEvents(release(controller, 0), {off(60), off(64), off(67)});
        assert(controller.harmonyActive());
    }

    {
        g_block = "bounds";
        // Pire cas réel : éolien, root B, octave 3 -> le degré le plus haut est 126, sous 127.
        SimpleMidiController controller;
        assert(controller.turnOctave(100));
        assert(controller.octave() == 3);
        assert(controller.turnRoot(-1));
        assert(controller.turnPreset(1));
        assert(controller.rootNote() == 107);
        assert(controller.highestNote() == 126);
        assertEvents(press(controller, 11), {on(126)});
        assertEvents(release(controller, 11), {off(126)});
    }

    {
        g_block = "low-bound";
        // Limites basses : le degré le plus bas atteint la note 0 sans passer sous.
        SimpleMidiController controller;
        assert(controller.turnOctave(-100));
        assert(controller.octave() == -5);
        assert(controller.rootNote() == 0);
        assertEvents(press(controller, 0), {on(0)});
        assertEvents(release(controller, 0), {off(0)});
    }

    {
        g_block = "folding";
        SimpleMidiController controller;
        controller.turnPreset(3);
        assert(controller.turnOctave(100));
        assert(controller.turnRoot(-1));
        assertEvents(press(controller, 0), {on(107)});
        assertEvents(press(controller, 18), {on(114), on(116), on(121), on(123)});
        assertEvents(release(controller, 0), {off(107), off(114), off(116), off(121), off(123)});
    }

    {
        g_block = "stress";
        // Stress : chaque NoteOn a son NoteOff, même en relâchant dans le désordre.
        SimpleMidiController stress;
        std::array<bool, 128> sounding{};
        const auto apply = [&sounding](const EventList& list) {
            assert(list.count <= kCap);
            for (uint8_t i = 0; i < list.count; ++i) {
                const MidiCommand& command = list.items[i];
                if (command.type == MidiCommandType::NoteOn) {
                    assert(!sounding[command.note]);
                    sounding[command.note] = true;
                } else if (command.type == MidiCommandType::NoteOff) {
                    assert(sounding[command.note]);
                    sounding[command.note] = false;
                }
            }
        };

        apply(press(stress, 0));
        apply(press(stress, 3));
        apply(press(stress, 5));
        apply(press(stress, 12));
        apply(press(stress, 15));
        apply(press(stress, 13));
        apply(press(stress, 2));
        apply(press(stress, 18));
        stress.turnRoot(4);
        stress.turnPreset(2);
        stress.turnOctave(1);
        apply(press(stress, 7));
        apply(release(stress, 12));
        apply(release(stress, 2));
        apply(release(stress, 18));
        apply(release(stress, 13));
        apply(press(stress, 19));
        apply(release(stress, 19));
        apply(release(stress, 0));
        apply(release(stress, 3));
        apply(release(stress, 5));
        apply(release(stress, 7));
        apply(release(stress, 15));
        for (const int count : sounding) assert(count == 0);
    }

    {
        g_block = "full-range";
        // Gamme complète de root et mode aux deux extrémités d'octave.
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            SimpleMidiController rangeController;
            rangeController.turnOctave(-100);
            rangeController.turnPreset(static_cast<int>(preset));
            for (uint8_t root = 0; root < 12; ++root) {
                if (root != 0) rangeController.turnRoot(1);
                for (uint8_t key = 0; key < rangeController.kKeyCount; ++key) {
                    const EventList down = press(rangeController, key);
                    assert(down.count == 1);
                    assert(down.items[0].note <= 127);
                    const EventList up = release(rangeController, key);
                    assert(up.count == 1);
                    assert(up.items[0].note == down.items[0].note);
                }
            }
        }
    }

    {
        amen::SimpleMidiController uiController;
        amen::OledUi ui;
        const auto idle = ui.render(uiController, amen::E2Page::Root, 0).pixels();
        g_block = "ui";
        assert(press(uiController, 0).count == 1);
        const auto playing = ui.render(uiController, amen::E2Page::Root, 1).pixels();
        assert(playing != idle);
        assertEvents(release(uiController, 0), {off(60)});
        assert(ui.render(uiController, amen::E2Page::Root, 2).pixels() == idle);

        const auto beforeOctave = ui.render(uiController, amen::E2Page::Root, 3).pixels();
        uiController.turnOctave(1);
        const auto changedOctave = ui.render(uiController, amen::E2Page::Root, 4).pixels();
        assert(changedOctave != beforeOctave);

        ui.showE1(amen::E1Page::Octave, 10);
        const auto octaveOverlay = ui.render(uiController, amen::E2Page::Root, 10).pixels();
        assert(ui.overlayVisible() && octaveOverlay != changedOctave);
        assert(ui.render(uiController, amen::E2Page::Root, 810).pixels() == changedOctave);
        assert(!ui.overlayVisible());

        ui.showE2(amen::E2Page::Root, 820);
        const auto rootOverlay = ui.render(uiController, amen::E2Page::Root, 820).pixels();
        ui.showE2(amen::E2Page::Preset, 830);
        const auto scaleOverlay = ui.render(uiController, amen::E2Page::Preset, 830).pixels();
        assert(rootOverlay != scaleOverlay && ui.overlayVisible());

        auto previousMode = ui.render(uiController, amen::E2Page::Preset, 1630).pixels();
        for (uint8_t preset = 1; preset < amen::kMusicalPresetCount; ++preset) {
            uiController.turnPreset(1);
            const auto currentMode = ui.render(uiController, amen::E2Page::Preset, 1630 + preset).pixels();
            assert(currentMode != previousMode);
            previousMode = currentMode;
        }
    }

    {
        g_block = "ui-e1-tempo-page";
        amen::SimpleMidiController controller;
        amen::OledUi ui;
        ui.showE1(amen::E1Page::Tempo, 10);
        amen::MonoFramebuffer expected;
        expected.drawText(0, 0, "TEMPO", 2);
        expected.drawText(96, 0, "HARM", 2);
        expected.drawText(84, 0, "*", 2);
        expected.drawText(42, 12, "120", 4);
        expected.fillRect(103, 27, 5, 1);
        expected.fillRect(103, 31, 5, 1);
        expected.fillRect(103, 28, 1, 3);
        expected.fillRect(107, 28, 1, 3);
        expected.fillRect(112, 27, 5, 5);
        expected.fillRect(121, 27, 5, 1);
        expected.fillRect(121, 31, 5, 1);
        expected.fillRect(121, 28, 1, 3);
        expected.fillRect(125, 28, 1, 3);
        assert(ui.render(controller, amen::E2Page::Root, 10).pixels() == expected.pixels());
    }

    {
        g_block = "ui-e1-frequency-page";
        amen::SimpleMidiController controller;
        amen::OledUi ui;
        assert(controller.turnFrequency(1, 0));
        ui.showE1(amen::E1Page::Frequency, 10);
        amen::MonoFramebuffer expected;
        expected.drawText(0, 0, "FREQUENCY", 2);
        expected.drawText(96, 0, "HARM", 2);
        expected.drawText(84, 0, "*", 2);
        expected.drawText(41, 16, "8.9 HZ", 2);
        expected.fillRect(103, 27, 5, 1);
        expected.fillRect(103, 31, 5, 1);
        expected.fillRect(103, 28, 1, 3);
        expected.fillRect(107, 28, 1, 3);
        expected.fillRect(112, 27, 5, 1);
        expected.fillRect(112, 31, 5, 1);
        expected.fillRect(112, 28, 1, 3);
        expected.fillRect(116, 28, 1, 3);
        expected.fillRect(121, 27, 5, 5);
        assert(ui.render(controller, amen::E2Page::Root, 10).pixels() == expected.pixels());
    }

    {
        g_block = "ui-harmony";
        // UI harmonie : la couleur apparaît sur l'accueil et l'overlay affiche son nom.
        amen::SimpleMidiController uiController;
        amen::OledUi ui;
        const auto idle = ui.render(uiController, amen::E2Page::Root, 0).pixels();
        assert(press(uiController, 12).count == 0);
        const auto colored = ui.render(uiController, amen::E2Page::Root, 1).pixels();
        assert(colored != idle);
        ui.showHarmony(10);
        const auto harmonyOverlay = ui.render(uiController, amen::E2Page::Root, 10).pixels();
        assert(ui.overlayVisible() && harmonyOverlay != colored);
        assert(ui.render(uiController, amen::E2Page::Root, 20).pixels() == harmonyOverlay);
        assert(ui.render(uiController, amen::E2Page::Root, 810).pixels() == colored);
        assert(!ui.overlayVisible());
        assert(press(uiController, 13).count == 0);
        const auto recolored = ui.render(uiController, amen::E2Page::Root, 900).pixels();
        assert(recolored != colored);
        assertEvents(release(uiController, 13), {});
        assert(ui.render(uiController, amen::E2Page::Root, 1000).pixels() == colored);
        assertEvents(release(uiController, 12), {});
        assert(ui.render(uiController, amen::E2Page::Root, 1100).pixels() == idle);
    }

    {
        g_block = "exact-degree-chords";
        constexpr std::array<std::array<uint8_t, 4>, 3> chords{{
            {{60, 64, 67, 71}}, {{62, 65, 69, 72}}, {{64, 67, 71, 74}}
        }};
        for (uint8_t key = 0; key < 3; ++key) {
            SimpleMidiController controller;
            press(controller, 12);
            const auto& notes = chords[key];
            assertNoteOns(press(controller, key), {notes[0], notes[1], notes[2]});
            assertEvents(press(controller, 13), {on(notes[3])});
            assertEvents(release(controller, key), {off(notes[0]), off(notes[1]), off(notes[2]), off(notes[3])});
            SimpleMidiController seventh;
            press(seventh, 13);
            assertNoteOns(press(seventh, key), {notes[0], notes[1], notes[2], notes[3]});
        }
        SimpleMidiController harmonic;
        harmonic.turnPreset(2);
        harmonic.turnRoot(9);
        assert(harmonic.scale() == amen::DiatonicMode::HarmonicMinor);
        assert(std::string(amen::modeName(harmonic.scale())) == "HARM MIN");
        assertEvents(press(harmonic, 6), {on(80)});
        assert(std::string(harmonic.currentNoteName()) == "G#");
        assertEvents(release(harmonic, 6), {off(80)});
        press(harmonic, 13);
        assertNoteOns(press(harmonic, 4), {76, 80, 83, 86});
        assertEvents(release(harmonic, 4), {off(76), off(80), off(83), off(86)});
        SimpleMidiController cinema;
        cinema.turnPreset(3);
        press(cinema, 12);
        assertNoteOns(press(cinema, 0), {60, 67, 76});
        assertEvents(press(cinema, 13), {on(71)});
        assertEvents(press(cinema, 14), {on(74)});
        assertEvents(release(cinema, 0), {off(60), off(67), off(71), off(74), off(76)});
        SimpleMidiController dark;
        dark.turnPreset(4);
        press(dark, 15);
        assertNoteOns(press(dark, 0), {60, 61, 63, 67});
        assertEvents(press(dark, 16), {off(67)});
        assertEvents(release(dark, 0), {off(60), off(61), off(63)});
    }

    {
        g_block = "chromatic-scale";
        SimpleMidiController chromatic;
        chromatic.turnPreset(5);
        assert(chromatic.preset() == amen::MusicalPreset::Chromatic);
        assert(chromatic.scale() == amen::DiatonicMode::Chromatic);
        assert(std::string(chromatic.presetName()) == "CHROMATIC");
        assert(std::string(amen::modeDescription(chromatic.scale())) == "(12 SEMITONES)");
        for (uint8_t key = 0; key < chromatic.kKeyCount; ++key) {
            assertEvents(press(chromatic, key), {on(60 + key)});
            assertEvents(release(chromatic, key), {off(60 + key)});
        }
        assert(chromatic.rootNote() == 60 && chromatic.highestNote() == 71);
        assertEvents(press(chromatic, 1), {on(61)});
        assert(std::string(chromatic.currentNoteName()) == "Db");
        assertEvents(release(chromatic, 1), {off(61)});
        assertEvents(press(chromatic, 6), {on(66)});
        assert(std::string(chromatic.currentNoteName()) == "F#");
        assertEvents(release(chromatic, 6), {off(66)});
        chromatic.turnRoot(2);
        assertEvents(press(chromatic, 3), {on(65)});
        assert(std::string(chromatic.currentNoteName()) == "F");
        assertEvents(release(chromatic, 3), {off(65)});
        chromatic.turnOctave(-100);
        assertEvents(press(chromatic, 0), {on(2)});
        assertEvents(release(chromatic, 0), {off(2)});
        chromatic.turnOctave(100);
        assertEvents(press(chromatic, 11), {on(109)});
        assertEvents(release(chromatic, 11), {off(109)});
        chromatic.turnOctave(-3);
        chromatic.turnRoot(-2);
        press(chromatic, 12);
        assertNoteOns(press(chromatic, 0), {60, 64, 67});
        assertEvents(press(chromatic, 13), {on(71)});
        assertEvents(release(chromatic, 13), {off(71)});
        assertEvents(release(chromatic, 0), {off(60), off(64), off(67)});
        release(chromatic, 12);
    }

    {
        g_block = "frozen-context-mixed-presets";
        SimpleMidiController controller;
        press(controller, 12);
        assertNoteOns(press(controller, 1), {62, 65, 69});
        controller.turnPreset(3);
        controller.turnRoot(3);
        controller.turnOctave(1);
        assert(controller.hasHeldPresetMismatch());
        assert(std::string(controller.currentNoteName()) == "D");
        assert(std::string(controller.harmonyName()) == "OPEN TRIAD");
        assertEvents(press(controller, 13), {on(72)});
        assertNoteOns(press(controller, 0), {75, 82, 86, 91});
        assert(std::string(controller.currentNoteName()) == "Eb");
        assertEvents(press(controller, 14), {on(76), on(89)});
        assertEvents(release(controller, 13), {});
        assertEvents(release(controller, 14), {off(72), off(76), off(86), off(89)});
        assertEvents(release(controller, 1), {off(62), off(65), off(69)});
        assert(!controller.hasHeldPresetMismatch());
        assertEvents(release(controller, 0), {off(75), off(82), off(91)});
        assertEvents(release(controller, 12), {});
        assert(controller.heldCount() == 0);
    }

    {
        g_block = "mixed-shared-ownership";
        SimpleMidiController controller;
        press(controller, 12);
        assertNoteOns(press(controller, 0), {60, 64, 67});
        controller.turnPreset(4);
        assertNoteOns(press(controller, 2), {63, 70});
        assertEvents(release(controller, 0), {off(60), off(64)});
        assert(!controller.hasHeldPresetMismatch());
        assertEvents(release(controller, 2), {off(63), off(67), off(70)});
    }

    {
        g_block = "preset-data-and-wrapping";
        constexpr std::array<const char*, amen::kMusicalPresetCount> names{
            {"MAJOR", "MINOR", "HARM MIN", "CINEMA", "DARK", "CHROMATIC", "GM KIT", "PRISM"}};
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            SimpleMidiController controller;
            controller.turnPreset(preset);
            assert(std::string(controller.presetName()) == names[preset]);
            if (controller.drums()) {
                assert(controller.midiChannel() == amen::SimpleMidiController::kDrumChannel);
                assert(std::string(amen::gmDrumLabel(0)) == "BD");
                press(controller, 0);
                assert(std::string(controller.currentNoteName()) == "BD");
                release(controller, 0);
                assert(controller.runShapeName() != nullptr);
                controller.turnPreset(1);
                assert(static_cast<uint8_t>(controller.preset()) == (preset + 1) % amen::kMusicalPresetCount);
                continue;
            }
            for (uint8_t root = 0; root < 12; ++root) {
                press(controller, 0);
                assert(std::string(controller.currentNoteName()) == amen::pitchClassName(root));
                release(controller, 0);
                controller.turnRoot(1);
            }
            for (uint8_t slot = 0; slot < amen::kHarmonySlotCount; ++slot) {
                const auto* recipe = amen::harmonySlotRecipe(controller.preset(), slot);
                assert(recipe && std::string(recipe->name).size() * 8 - 2 <= 128);
                std::array<bool, 128> notes{};
                for (uint8_t voice = 0; voice < recipe->voiceCount; ++voice)
                    notes[amen::scaleDegreeOffset(controller.scale(), recipe->degrees[voice]) + recipe->octaveDisplacements[voice]] = true;
                for (uint8_t other = 0; other < slot; ++other) {
                    const auto* earlier = amen::harmonySlotRecipe(controller.preset(), other);
                    std::array<bool, 128> previous{};
                    for (uint8_t voice = 0; voice < earlier->voiceCount; ++voice)
                        previous[amen::scaleDegreeOffset(controller.scale(), earlier->degrees[voice]) + earlier->octaveDisplacements[voice]] = true;
                    assert(notes != previous);
                }
            }
            assert(controller.turnPreset(1));
            assert(static_cast<uint8_t>(controller.preset()) == (preset + 1) % amen::kMusicalPresetCount);
        }
        SimpleMidiController controller;
        int64_t expected = 0;
        for (int delta : {std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), -1000003, 1000003, 0, 500}) {
            const int64_t next = ((expected + delta) % amen::kMusicalPresetCount + amen::kMusicalPresetCount) % amen::kMusicalPresetCount;
            assert(controller.turnPreset(delta) == (next != expected));
            expected = next;
            assert(static_cast<uint8_t>(controller.preset()) == expected);
        }
        assert(amen::harmonySlotRecipe(static_cast<amen::MusicalPreset>(255), 0) == nullptr);
    }

    {
        g_block = "ui-exact-presets-and-mismatch";
        SimpleMidiController controller;
        amen::OledUi ui;
        press(controller, 0);
        controller.turnPreset(2);
        amen::MonoFramebuffer expected;
        expected.drawText(0, 0, "O5 C", 2);
        expected.drawText(96, 0, "HARM", 2);
        expected.drawText(0, 11, "HARM MIN*", 2);
        expected.drawText(static_cast<int>(std::strlen("HARM MIN*")) * 8 + 4, 11, "C", 2);
        expected.drawText(25, 22, "(MINOR #7)", 2);
        assert(ui.render(controller, amen::E2Page::Preset, 0).pixels() == expected.pixels());
        controller.turnPreset(-2);
        assert(!controller.hasHeldPresetMismatch());
        release(controller, 0);
        ui.showE2(amen::E2Page::Preset, 1);
        expected.clear();
        expected.drawText(0, 0, "PRESET", 2);
        expected.drawText(96, 0, "HARM", 2);
        expected.drawText(45, 14, "MAJOR", 2);
        expected.fillRect(112, 27, 5, 1);
        expected.fillRect(112, 31, 5, 1);
        expected.fillRect(112, 28, 1, 3);
        expected.fillRect(116, 28, 1, 3);
        expected.fillRect(121, 27, 5, 5);
        assert(ui.render(controller, amen::E2Page::Preset, 1).pixels() == expected.pixels());
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            if (controller.drums()) {
                controller.turnPreset(1);
                continue;
            }
            for (uint8_t slot = 0; slot < amen::kHarmonySlotCount; ++slot) {
                press(controller, 12 + slot);
                ui.showHarmony(10);
                expected.clear();
                expected.drawText(0, 0, "HARMONY", 2);
                expected.drawText(96, 0, "HARM", 2);
                const int length = static_cast<int>(std::string(controller.harmonyName()).size());
                const int scale = length * 16 - 4 <= 128 ? 4 : 2;
                expected.drawText((128 - (length * 4 * scale - scale)) / 2, 12, controller.harmonyName(), scale);
                assert(ui.render(controller, amen::E2Page::Preset, 10).pixels() == expected.pixels());
                release(controller, 12 + slot);
            }
            controller.turnPreset(1);
        }
    }

    testPatterns();
    std::cout << "AMEN MIDI preset, pattern and ownership tests: PASS\n";
}
