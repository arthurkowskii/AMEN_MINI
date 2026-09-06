#include "simple_midi_controller.h"

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

    std::cout << "AMEN MIDI V0 tests: PASS\n";
}
