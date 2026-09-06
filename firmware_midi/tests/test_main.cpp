#include "diatonic_scales.h"
#include "simple_midi_controller.h"
#include "oled_ui.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

int main() {
    constexpr std::array<std::array<uint8_t, 7>, 7> expectedIntervals{{
        {{0, 2, 4, 5, 7, 9, 11}},
        {{0, 2, 3, 5, 7, 9, 10}},
        {{0, 1, 3, 5, 7, 8, 10}},
        {{0, 2, 4, 6, 7, 9, 11}},
        {{0, 2, 4, 5, 7, 9, 10}},
        {{0, 2, 3, 5, 7, 8, 10}},
        {{0, 1, 3, 5, 6, 8, 10}},
    }};

    for (uint8_t mode = 0; mode < amen::kDiatonicModeCount; ++mode) {
        const auto selected = static_cast<amen::DiatonicMode>(mode);
        assert(amen::modeName(selected)[0] != '\0');
        assert(amen::modeShortName(selected)[0] != '\0');
        assert(amen::modeDescription(selected)[0] == '(');
        for (uint8_t degree = 0; degree < 7; ++degree)
            assert(amen::scaleDegreeOffset(selected, degree) == expectedIntervals[mode][degree]);
        assert(amen::scaleDegreeOffset(selected, 7) == 12);
        assert(amen::scaleDegreeOffset(selected, 11) == 12 + expectedIntervals[mode][4]);
    }

    assert(amen::pitchClassName(1) == std::string("Db"));
    constexpr std::array<char, 4> eb{{'E', 'b', '\0', '\0'}};
    constexpr std::array<char, 4> ab{{'A', 'b', '\0', '\0'}};
    constexpr std::array<char, 4> b{{'B', '\0', '\0', '\0'}};
    constexpr std::array<char, 4> ebb{{'E', 'b', 'b', '\0'}};
    assert(amen::spellScaleDegree(3, amen::DiatonicMode::Ionian, 0).text == eb);
    assert(amen::spellScaleDegree(3, amen::DiatonicMode::Ionian, 3).text == ab);
    assert(amen::spellScaleDegree(5, amen::DiatonicMode::Lydian, 3).text == b);
    assert(amen::spellScaleDegree(1, amen::DiatonicMode::Locrian, 1).text == ebb);

    amen::SimpleMidiController controller;
    constexpr std::array<uint8_t, 12> cIonian{{60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79}};
    assert(controller.rootPitchClass() == 0);
    assert(controller.mode() == amen::DiatonicMode::Ionian);
    assert(controller.octaveNumber() == 5);
    assert(controller.rootNote() == 60);
    assert(controller.highestNote() == 79);
    for (uint8_t key = 0; key < controller.kKeyCount; ++key) {
        const auto command = controller.press(key);
        assert(command.type == amen::MidiCommandType::NoteOn);
        assert(command.note == cIonian[key] && command.velocity == 100);
        assert(controller.release(key).note == cIonian[key]);
    }
    assert(controller.press(12).type == amen::MidiCommandType::None);
    assert(controller.press(19).type == amen::MidiCommandType::None);
    assert(controller.currentNoteName()[0] == '\0');

    assert(controller.press(0).note == 60);
    assert(std::string(controller.currentNoteName()) == "C");
    assert(controller.press(2).note == 64);
    assert(std::string(controller.currentNoteName()) == "E");
    assert(controller.release(2).note == 64);
    assert(std::string(controller.currentNoteName()) == "C");
    assert(controller.release(0).note == 60);
    assert(controller.currentNoteName()[0] == '\0');

    assert(controller.turnRoot(-1));
    assert(controller.rootPitchClass() == 11);
    assert(controller.turnRoot(1));
    assert(controller.rootPitchClass() == 0);
    assert(!controller.turnRoot(12));
    assert(controller.turnMode(-1));
    assert(controller.mode() == amen::DiatonicMode::Locrian);
    assert(controller.turnMode(1));
    assert(controller.mode() == amen::DiatonicMode::Ionian);
    assert(!controller.turnMode(7));

    const auto held = controller.press(0);
    assert(held.note == 60);
    assert(std::string(controller.currentNoteName()) == "C");
    assert(controller.press(0).type == amen::MidiCommandType::None);
    controller.turnRoot(2);
    controller.turnMode(1);
    controller.turnOctave(1);
    const auto heldRelease = controller.release(0);
    assert(heldRelease.type == amen::MidiCommandType::NoteOff);
    assert(heldRelease.note == 60);
    assert(controller.currentNoteName()[0] == '\0');

    amen::SimpleMidiController spellingController;
    spellingController.turnRoot(3);
    assert(spellingController.press(0).note == 63);
    assert(std::string(spellingController.currentNoteName()) == "Eb");
    spellingController.turnRoot(4);
    spellingController.turnMode(3);
    assert(std::string(spellingController.currentNoteName()) == "Eb");
    spellingController.release(0);

    for (uint8_t mode = 0; mode < amen::kDiatonicModeCount; ++mode) {
        amen::SimpleMidiController rangeController;
        rangeController.turnOctave(-100);
        rangeController.turnMode(mode);
        for (uint8_t root = 0; root < 12; ++root) {
            if (root != 0) rangeController.turnRoot(1);
            for (uint8_t key = 0; key < rangeController.kKeyCount; ++key) {
                const auto command = rangeController.press(key);
                assert(command.note <= 127);
                rangeController.release(key);
            }
        }
        rangeController.turnOctave(100);
        for (uint8_t key = 0; key < rangeController.kKeyCount; ++key) {
            const auto command = rangeController.press(key);
            assert(command.note <= 127);
            rangeController.release(key);
        }
    }

    amen::SimpleMidiController limitController;
    assert(limitController.turnOctave(-100));
    assert(limitController.octave() == -5 && limitController.rootNote() == 0);
    assert(limitController.octaveNumber() == 0);
    assert(!limitController.turnOctave(-1));
    limitController.turnRoot(-1);
    limitController.turnMode(-1);
    assert(limitController.turnOctave(100));
    assert(limitController.octave() == 3);
    assert(limitController.octaveNumber() == 8);
    assert(limitController.highestNote() <= 127);
    assert(!limitController.turnOctave(1));

    amen::MonoFramebuffer framebuffer;
    framebuffer.setPixel(-1, -1);
    framebuffer.setPixel(128, 32);
    for (const uint8_t value : framebuffer.pixels()) assert(value == 0);
    framebuffer.setPixel(127, 31);
    assert(framebuffer.pixels().back() == 0x80);

    amen::SimpleMidiController uiController;
    amen::OledUi ui;
    const auto idle = ui.render(uiController, amen::E2Page::Root, 0).pixels();
    assert(uiController.press(0).note == 60);
    const auto playing = ui.render(uiController, amen::E2Page::Root, 1).pixels();
    assert(playing != idle);
    uiController.release(0);
    assert(ui.render(uiController, amen::E2Page::Root, 2).pixels() == idle);

    const auto beforeOctave = ui.render(uiController, amen::E2Page::Root, 3).pixels();
    uiController.turnOctave(1);
    const auto changedOctave = ui.render(uiController, amen::E2Page::Root, 4).pixels();
    assert(changedOctave != beforeOctave);

    ui.showOctave(10);
    const auto octaveOverlay = ui.render(uiController, amen::E2Page::Root, 10).pixels();
    assert(ui.overlayVisible() && octaveOverlay != changedOctave);
    assert(ui.render(uiController, amen::E2Page::Root, 810).pixels() == changedOctave);
    assert(!ui.overlayVisible());

    ui.showE2(amen::E2Page::Root, 820);
    const auto rootOverlay = ui.render(uiController, amen::E2Page::Root, 820).pixels();
    ui.showE2(amen::E2Page::Scale, 830);
    const auto scaleOverlay = ui.render(uiController, amen::E2Page::Scale, 830).pixels();
    assert(rootOverlay != scaleOverlay && ui.overlayVisible());

    auto previousMode = ui.render(uiController, amen::E2Page::Scale, 1630).pixels();
    for (uint8_t mode = 1; mode < amen::kDiatonicModeCount; ++mode) {
        uiController.turnMode(1);
        const auto currentMode = ui.render(uiController, amen::E2Page::Scale, 1630 + mode).pixels();
        assert(currentMode != previousMode);
        previousMode = currentMode;
    }

    std::cout << "AMEN MIDI diatonic scale tests: PASS\n";
}
