#include "simple_midi_controller.h"
#include "oled_ui.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int main() {
    amen::SimpleMidiController controller;

    assert(controller.baseNote() == 60);
    const auto first = controller.press(0);
    assert(first.type == amen::MidiCommandType::NoteOn);
    assert(first.note == 60 && first.velocity == 100);
    assert(controller.press(0).type == amen::MidiCommandType::None);

    const auto second = controller.press(4);
    assert(second.type == amen::MidiCommandType::NoteOn && second.note == 64);
    assert(controller.release(4).type == amen::MidiCommandType::NoteOff);

    assert(controller.turnOctave(1));
    assert(controller.baseNote() == 72);
    const auto heldRelease = controller.release(0);
    assert(heldRelease.type == amen::MidiCommandType::NoteOff);
    assert(heldRelease.note == 60);
    assert(controller.press(19).note == 91);
    assert(controller.release(19).note == 91);

    assert(controller.turnOctave(100));
    assert(controller.octave() == 4);
    assert(controller.press(19).note == 127);
    assert(controller.release(19).note == 127);
    assert(!controller.turnOctave(1));

    assert(controller.turnOctave(-100));
    assert(controller.octave() == -5);
    assert(controller.press(0).note == 0);
    assert(controller.release(0).note == 0);
    assert(controller.press(20).type == amen::MidiCommandType::None);

    amen::MonoFramebuffer framebuffer;
    framebuffer.setPixel(-1, -1);
    framebuffer.setPixel(128, 32);
    for (const uint8_t value : framebuffer.pixels()) assert(value == 0);
    framebuffer.setPixel(127, 31);
    assert(framebuffer.pixels().back() == 0x80);

    amen::SimpleMidiController uiController;
    amen::OledUi ui;
    const auto idle = ui.render(uiController, 0).pixels();
    assert(uiController.press(0).note == 60);
    const auto playing = ui.render(uiController, 10).pixels();
    assert(playing != idle);
    uiController.turnOctave(1);
    ui.showOctave(20);
    const auto overlay = ui.render(uiController, 20).pixels();
    assert(ui.overlayVisible() && overlay != playing);
    const auto home = ui.render(uiController, 820).pixels();
    assert(!ui.overlayVisible() && home != overlay);

    std::cout << "AMEN MIDI V0 tests: PASS\n";
}
