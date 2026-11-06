#include "front_panel.h"
#include "oled_ui.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

using namespace amen;
constexpr uint8_t kCapacity = SimpleMidiController::kMaxEventsPerAction;

void assertCc(const MidiCommand& event, uint8_t number, uint8_t value) {
    assert(event.type == MidiCommandType::ControlChange);
    if (event.note != number || event.velocity != value)
        std::cerr << "CC mismatch: " << int(event.note) << ':' << int(event.velocity)
                  << " expected " << int(number) << ':' << int(value) << '\n';
    assert(event.note == number && event.velocity == value);
}

void drawDots(MonoFramebuffer& frame, uint8_t page) {
    for (uint8_t i = 0; i < 5; ++i) {
        const int x = 85 + i * 9;
        if (i == page) frame.fillRect(x, 27, 5, 5);
        else {
            frame.fillRect(x, 27, 5, 1);
            frame.fillRect(x, 31, 5, 1);
            frame.fillRect(x, 28, 1, 3);
            frame.fillRect(x + 4, 28, 1, 3);
        }
    }
}

void testParameterCycle() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    assert(panel.parameterPage() == E1Page::Octave);
    assert(panel.turn(c, 0, 1, 0, out, kCapacity).count == 0);
    assert(c.octaveNumber() == 6 && c.rootPitchClass() == 0 && c.tempo() == 120);
    for (uint8_t page = 1; page < 5; ++page) {
        assert(panel.click(c, 0, out, kCapacity).feedback == PanelFeedback::Parameter);
        assert(static_cast<uint8_t>(panel.parameterPage()) == page);
        const ClockMode clock = c.clockMode();
        const uint16_t tempo = c.tempo();
        const uint16_t frequency = c.frequencyTenths();
        const uint8_t root = c.rootPitchClass();
        const MusicalPreset preset = c.preset();
        const auto result = panel.turn(c, 0, 1, 100, out, kCapacity);
        assert(result.feedback == PanelFeedback::Parameter && result.count == 0);
        if (page == 1) {
            assert(c.tempo() == tempo + 1 && c.clockMode() == ClockMode::Tempo);
            assert(c.frequencyTenths() == frequency && c.rootPitchClass() == root && c.preset() == preset);
        } else if (page == 2) {
            assert(c.frequencyTenths() > frequency && c.clockMode() == ClockMode::Frequency);
            assert(c.tempo() == tempo && c.rootPitchClass() == root && c.preset() == preset);
        } else if (page == 3) {
            assert(c.rootPitchClass() == root + 1 && c.preset() == preset);
            assert(c.clockMode() == clock && c.tempo() == tempo);
        } else {
            assert(c.preset() == MusicalPreset::GmKit && c.rootPitchClass() == root);
            assert(c.clockMode() == clock && c.tempo() == tempo);
        }
    }
    panel.click(c, 0, out, kCapacity);
    assert(panel.parameterPage() == E1Page::Octave);
    assert(c.octaveNumber() == 6);
    panel.turn(c, 0, -1, 200, out, kCapacity); // GM KIT ignores register edits.
    assert(c.octaveNumber() == 6);
    panel.click(c, 0, out, kCapacity); // Selecting TEMPO alone doesn't activate it.
    assert(c.clockMode() == ClockMode::Frequency);
    panel.turn(c, 0, 1, 200, out, kCapacity);
    assert(c.clockMode() == ClockMode::Tempo && c.tempo() == 122);
}

void testShiftPriority() {
    for (uint8_t page = 0; page < 5; ++page) {
        SimpleMidiController c;
        FrontPanel panel;
        MidiCommand out[kCapacity]{};
        for (uint8_t i = 0; i < page; ++i) panel.click(c, 0, out, kCapacity);
        assert(c.press(c.kShiftKey, 0, out, kCapacity) == 1);
        assertCc(out[0], 64, 127);
        const auto selected = panel.parameterPage();
        assert(panel.click(c, 0, out, kCapacity).feedback == PanelFeedback::None);
        const auto result = panel.turn(c, 0, 1, 0, out, kCapacity);
        assert(result.feedback == PanelFeedback::Shift && result.count == 1);
        assertCc(out[0], 64, 0);
        assert(c.shiftMode() == ShiftMode::Mod && panel.parameterPage() == selected);
        assert(c.octaveNumber() == 5 && c.tempo() == 120 && c.rootPitchClass() == 0);
        assert(c.preset() == MusicalPreset::Chromatic);
        // E4 no longer selects Shift or changes the preset.
        panel.turn(c, 3, 1, 0, out, kCapacity);
        assert(c.shiftMode() == ShiftMode::Mod && c.preset() == MusicalPreset::Chromatic);
        assert(c.release(c.kShiftKey, 0, out, kCapacity) == 0);
    }
}

void testRelocatedControls() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    assert(panel.click(c, 1, out, kCapacity).feedback == PanelFeedback::None);
    panel.turn(c, 1, -1, 0, out, kCapacity); // NONE -> PATTERN.
    assert(c.page() == PerformancePage::Pattern);
    assert(panel.click(c, 1, out, kCapacity).feedback == PanelFeedback::Bank);
    assert(c.patternBank() == PatternBank::FutureArp);
    c.press(12, 0, out, kCapacity);
    c.press(0, 0, out, kCapacity);
    const RunShape running = c.runShape();
    const RunShape assigned = c.slotAssignment(0);
    assert(panel.turn(c, 3, 1, 1, out, kCapacity).feedback == PanelFeedback::MidiCc);
    assert(c.slotAssignment(0) == assigned);
    assert(panel.turn(c, 0, 1, 1, out, kCapacity).feedback == PanelFeedback::PatternEdit);
    assert(c.slotAssignment(0) != assigned && c.runShape() == running && c.runActive());
    c.press(c.kShiftKey, 0, out, kCapacity);
    assert(panel.click(c, 3, out, kCapacity).feedback == PanelFeedback::SmartVoicing);
    assert(c.smartVoicing());
    panel.click(c, 3, out, kCapacity);
    assert(!c.smartVoicing());
    c.release(c.kShiftKey, 0, out, kCapacity);
    panel.turn(c, 2, 1, 2, out, kCapacity);
    assert(c.velocity() == 103 && c.modulation() == 0);
    assert(panel.click(c, 2, out, kCapacity).feedback == PanelFeedback::Modulation);
    assert(panel.turn(c, 2, 1, 2, out, kCapacity).count == 1);
    assertCc(out[0], 1, 5);
    assert(c.velocity() == 103);
    panel.turn(c, 2, std::numeric_limits<int>::max(), 2, out, kCapacity);
    assertCc(out[0], 1, 127);
    panel.turn(c, 2, std::numeric_limits<int>::min(), 2, out, kCapacity);
    assertCc(out[0], 1, 0);
    assert(panel.click(c, 2, out, kCapacity).feedback == PanelFeedback::Velocity);
    panel.turn(c, 2, std::numeric_limits<int>::min(), 2, out, kCapacity);
    assert(c.velocity() == 1);
    panel.turn(c, 2, std::numeric_limits<int>::max(), 2, out, kCapacity);
    assert(c.velocity() == 127);
    panel.turn(c, 1, 1, 10, out, kCapacity); // PATTERN -> NONE restores source.
    assert(c.page() == PerformancePage::Notes && !c.runActive());
    assert(c.release(0, 10, out, kCapacity) == 1);
    assert(out[0].type == MidiCommandType::NoteOff && out[0].note == 60);
    assert(c.release(12, 10, out, kCapacity) == 0);
}

void testContextualPatternAssignment() {
    for (uint8_t page = 0; page < 5; ++page) {
        for (bool active : {false, true}) {
            SimpleMidiController c;
            FrontPanel panel;
            MidiCommand out[kCapacity]{};
            for (uint8_t i = 0; i < page; ++i) panel.click(c, 0, out, kCapacity);
            const auto selected = panel.parameterPage();
            panel.turn(c, 1, -1, 0, out, kCapacity);
            c.press(12, 0, out, kCapacity);
            if (active) c.press(0, 0, out, kCapacity);
            const auto shape = c.runShape();
            const auto assignment = c.slotAssignment(0);
            assert(panel.click(c, 0, out, kCapacity).feedback == PanelFeedback::PatternEdit);
            assert(panel.parameterPage() == selected);
            const auto result = panel.turn(c, 0, 1, 0, out, kCapacity);
            assert(result.feedback == PanelFeedback::PatternEdit && result.count == 0);
            assert(c.slotAssignment(0) != assignment);
            assert(c.octaveNumber() == 5 && c.tempo() == 120 && c.rootPitchClass() == 0);
            assert(c.preset() == MusicalPreset::Chromatic && c.clockMode() == ClockMode::Tempo);
            assert(c.runActive() == active && c.runShape() == shape);
            assert(panel.parameterPage() == selected);
            // Shift always wins, even with a pattern pad held.
            c.press(c.kShiftKey, 0, out, kCapacity);
            const auto edited = c.slotAssignment(0);
            assert(panel.turn(c, 0, 1, 0, out, kCapacity).feedback == PanelFeedback::Shift);
            assert(c.shiftMode() == ShiftMode::Mod && c.slotAssignment(0) == edited);
            c.release(c.kShiftKey, 0, out, kCapacity);
            c.release(12, 0, out, kCapacity);
            assert(panel.turn(c, 0, 1, 0, out, kCapacity).feedback == PanelFeedback::Parameter);
            assert(panel.parameterPage() == selected);
            if (page == 0) assert(c.octaveNumber() == 6);
            if (page == 1) assert(c.tempo() == 121);
            if (page == 2) assert(c.clockMode() == ClockMode::Frequency);
            if (page == 3) assert(c.rootPitchClass() == 1);
            if (page == 4) assert(c.preset() == MusicalPreset::GmKit);
        }
    }
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    panel.turn(c, 1, -1, 0, out, kCapacity);
    c.press(12, 0, out, kCapacity);
    c.press(13, 0, out, kCapacity);
    const auto first = c.slotAssignment(0);
    const auto second = c.slotAssignment(1);
    panel.turn(c, 0, 1, 0, out, kCapacity);
    assert(c.slotAssignment(0) == first && c.slotAssignment(1) != second);
    c.release(13, 0, out, kCapacity);
    panel.turn(c, 0, 1, 0, out, kCapacity);
    assert(c.slotAssignment(0) != first);
    // A stale pattern role across a page change must not hijack E1.
    panel.turn(c, 1, 1, 0, out, kCapacity);
    assert(c.page() == PerformancePage::Notes && c.patternHeld());
    assert(panel.turn(c, 0, 1, 0, out, kCapacity).feedback == PanelFeedback::Parameter);
    assert(c.octaveNumber() == 6);
    c.release(12, 0, out, kCapacity);
    // A note pad held in NONE is not a pattern assignment gesture.
    c.press(12, 0, out, kCapacity);
    panel.turn(c, 0, 1, 0, out, kCapacity);
    assert(c.octaveNumber() == 7);
    c.release(12, 0, out, kCapacity);
    // REPEAT edits the division rather than the shape, using the same E1 gesture.
    panel.turn(c, 1, -1, 0, out, kCapacity);
    for (uint8_t i = 0; i < 4; ++i) panel.click(c, 1, out, kCapacity);
    assert(c.patternBank() == PatternBank::Repeat);
    c.press(12, 0, out, kCapacity);
    const auto division = c.slotDivision(0);
    panel.turn(c, 0, 1, 0, out, kCapacity);
    assert(c.slotDivision(0) != division && c.octaveNumber() == 7);
    OledUi ui;
    ui.showPanel(PanelFeedback::PatternEdit, panel, 0, 10);
    OledUi expected;
    expected.showPatternEdit(10);
    assert(ui.render(c, panel, 10).pixels() == expected.render(c, panel, 10).pixels());
}

void testMidiCc() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    for (uint8_t encoder = 4; encoder < 7; ++encoder) {
        const uint8_t slot = encoder - 4;
        assert(panel.midiCcValue(slot) == 0);
        assert(panel.midiCcMultiplier(slot) == 8);
        panel.click(c, encoder, out, kCapacity); // Select X1 for fine-step routing checks.
        assert(panel.turn(c, encoder, 1, 0, out, kCapacity).count == 1);
        assertCc(out[0], 20 + slot, 1);
        assert(panel.turn(c, encoder, 200, 0, out, kCapacity).count == 1);
        assertCc(out[0], 20 + slot, 127);
        assert(panel.turn(c, encoder, 1, 0, out, kCapacity).count == 0);
        assert(panel.turn(c, encoder, -1, 0, out, kCapacity).count == 1);
        assertCc(out[0], 20 + slot, 126);
        assert(panel.turn(c, encoder, std::numeric_limits<int>::min(), 0, out, kCapacity).count == 1);
        assertCc(out[0], 20 + slot, 0);
        assert(panel.turn(c, encoder, -1, 0, out, kCapacity).count == 0);
        assert(panel.turn(c, encoder, std::numeric_limits<int>::max(), 0, out, kCapacity).count == 1);
        assertCc(out[0], 20 + slot, 127);
    }
    panel.turn(c, 4, -27, 0, out, kCapacity);
    assert(panel.midiCcValue(0) == 100 && panel.midiCcValue(1) == 127 && panel.midiCcValue(2) == 127);
    assert(c.modulation() == 0 && c.velocity() == 100 && !c.smartVoicing());
    assert(c.page() == PerformancePage::Notes && c.preset() == MusicalPreset::Chromatic);
    assert(panel.parameterPage() == E1Page::Octave);
    c.press(c.kShiftKey, 0, out, kCapacity);
    panel.turn(c, 5, -1, 0, out, kCapacity);
    assertCc(out[0], 21, 126);
    assert(c.shiftMode() == ShiftMode::Hold && c.shiftHeld());
    c.release(c.kShiftKey, 0, out, kCapacity);
    assertCc(out[0], 64, 0);
    assert(FrontPanel{}.midiCcValue(0) == 0); // Volatile, silent initial state.
}

void testE4MidiCcAndNotes() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    assert(c.page() == PerformancePage::Notes);
    static_assert(FrontPanel::midiCcSlot(3) == 3);
    static_assert(FrontPanel::midiCcSlot(4) == 0);
    static_assert(FrontPanel::midiCcSlot(5) == 1);
    static_assert(FrontPanel::midiCcSlot(6) == 2);
    static_assert(FrontPanel::midiCcSlot(255) == 4);
    assert(panel.midiCcValue(3) == 0);
    panel.click(c, 3, out, kCapacity); // Select X1 for fine-step routing checks.
    for (int delta : {1, 200, -1, -200}) {
        const int64_t next = static_cast<int64_t>(panel.midiCcValue(3)) + delta;
        const uint8_t value = next < 0 ? 0 : (next > 127 ? 127 : static_cast<uint8_t>(next));
        const auto result = panel.turn(c, 3, delta, 0, out, kCapacity);
        assert(result.count == 1 && result.feedback == PanelFeedback::MidiCc);
        assertCc(out[0], 23, value);
        assert(panel.midiCcValue(3) == value);
    }
    assert(panel.turn(c, 3, -1, 0, out, kCapacity).count == 0);
    assert(panel.turn(c, 3, std::numeric_limits<int>::max(), 0, out, kCapacity).count == 1);
    assertCc(out[0], 23, 127);
    assert(panel.turn(c, 3, 1, 0, out, kCapacity).count == 0);
    assert(panel.turn(c, 3, std::numeric_limits<int>::min(), 0, out, kCapacity).count == 1);
    assertCc(out[0], 23, 0);
    for (uint8_t slot = 0; slot < 3; ++slot) assert(panel.midiCcValue(slot) == 0);
    assert(c.modulation() == 0 && c.velocity() == 100 && !c.smartVoicing());
    c.press(c.kShiftKey, 0, out, kCapacity);
    const auto click = panel.click(c, 3, out, kCapacity);
    assert(click.count == 0 && click.feedback == PanelFeedback::SmartVoicing && c.smartVoicing());
    assert(panel.midiCcValue(3) == 0);
    panel.click(c, 3, out, kCapacity);
    assert(!c.smartVoicing());
    c.release(c.kShiftKey, 0, out, kCapacity);
    // CC23 stays independent of Shift and of an active pattern assignment.
    c.press(c.kShiftKey, 0, out, kCapacity);
    assert(panel.turn(c, 3, 1, 0, out, kCapacity).count == 1);
    assertCc(out[0], 23, 1);
    assert(c.shiftMode() == ShiftMode::Hold);
    c.release(c.kShiftKey, 0, out, kCapacity);
    panel.turn(c, 1, -1, 0, out, kCapacity);
    c.press(12, 0, out, kCapacity);
    c.press(0, 0, out, kCapacity);
    const auto assigned = c.slotAssignment(0);
    const auto running = c.runShape();
    assert(panel.turn(c, 3, 1, 0, out, kCapacity).count == 1);
    assertCc(out[0], 23, 2);
    assert(c.runActive() && c.runShape() == running && c.slotAssignment(0) == assigned);
    // Exact E4 overlay in NOTES must display CC23, not the neighboring CC20.
    SimpleMidiController notes;
    OledUi ui;
    ui.showPanel(PanelFeedback::MidiCc, panel, 3, 10);
    MonoFramebuffer expected;
    expected.drawText(0, 0, "E4 CC23", 2);
    expected.drawText(88, 0, "NOTES", 2);
    expected.drawText(58, 12, "2", 4);
    expected.drawText(0, 27, "CH1");
    expected.drawText(112, 27, "X1");
    assert(ui.render(notes, panel, 10).pixels() == expected.pixels());
    ui.showSmartVoicing(20);
    expected.clear();
    expected.drawText(0, 0, "SMART VOICING");
    expected.drawText(88, 0, "NOTES", 2);
    expected.drawText(53, 18, "OFF", 2);
    assert(ui.render(notes, panel, 20).pixels() == expected.pixels());
}

void testMidiCcMultipliers() {
    constexpr uint8_t numbers[]{23, 20, 21, 22};
    for (uint8_t encoder = 3; encoder < 7; ++encoder) {
        SimpleMidiController c;
        FrontPanel panel;
        MidiCommand out[kCapacity]{};
        const uint8_t slot = FrontPanel::midiCcSlot(encoder);
        assert(panel.midiCcMultiplier(slot) == 8);
        panel.turn(c, encoder, 8, 0, out, kCapacity);
        for (uint8_t multiplier : {8, 1, 2, 4}) {
            assert(panel.midiCcMultiplier(slot) == multiplier);
            assert(panel.turn(c, encoder, 1, 0, out, kCapacity).count == 1);
            assertCc(out[0], numbers[encoder - 3], 64 + multiplier);
            assert(panel.turn(c, encoder, -1, 0, out, kCapacity).count == 1);
            assertCc(out[0], numbers[encoder - 3], 64);
            assert(panel.turn(c, encoder, 3, 0, out, kCapacity).count == 1);
            assertCc(out[0], numbers[encoder - 3], 64 + 3 * multiplier);
            panel.turn(c, encoder, -3, 0, out, kCapacity);
            assertCc(out[0], numbers[encoder - 3], 64);
            // Exact overlay after rotation, including the independent multiplier.
            OledUi ui;
            ui.showPanel(PanelFeedback::MidiCc, panel, encoder, 10);
            MonoFramebuffer expected;
            char text[20];
            std::snprintf(text, sizeof(text), "E%u CC%u", encoder + 1, numbers[encoder - 3]);
            expected.drawText(0, 0, text, 2);
            expected.drawText(88, 0, "NOTES", 2);
            expected.drawText(50, 12, "64", 4);
            expected.drawText(0, 27, "CH1");
            std::snprintf(text, sizeof(text), "X%u", multiplier);
            expected.drawText(112, 27, text);
            assert(ui.render(c, panel, 10).pixels() == expected.pixels());
            // Saturate, never wrap, and reverse immediately from the limit.
            assert(panel.turn(c, encoder, std::numeric_limits<int>::max(), 0, out, kCapacity).count == 1);
            assertCc(out[0], numbers[encoder - 3], 127);
            assert(panel.turn(c, encoder, 1, 0, out, kCapacity).count == 0);
            panel.turn(c, encoder, -1, 0, out, kCapacity);
            assertCc(out[0], numbers[encoder - 3], 127 - multiplier);
            panel.turn(c, encoder, std::numeric_limits<int>::min(), 0, out, kCapacity);
            assertCc(out[0], numbers[encoder - 3], 0);
            assert(panel.turn(c, encoder, -1, 0, out, kCapacity).count == 0);
            panel.turn(c, encoder, 64 / multiplier, 0, out, kCapacity);
            assert(panel.midiCcValue(slot) == 64);
            // Click only changes step size, not value, MIDI output or voicing.
            out[0] = {MidiCommandType::None, 99, 99};
            const auto click = panel.click(c, encoder, out, kCapacity);
            assert(click.count == 0 && click.feedback == PanelFeedback::MidiCc);
            assert(out[0].type == MidiCommandType::None && out[0].note == 99 && out[0].velocity == 99);
            assert(panel.midiCcValue(slot) == 64 && !c.smartVoicing());
            const uint8_t nextMultiplier = multiplier == 8 ? 1 : static_cast<uint8_t>(multiplier * 2);
            assert(panel.midiCcMultiplier(slot) == nextMultiplier);
            ui.showPanel(click.feedback, panel, encoder, 20);
            expected.clear();
            std::snprintf(text, sizeof(text), "E%u CC%u", encoder + 1, numbers[encoder - 3]);
            expected.drawText(0, 0, text, 2);
            expected.drawText(88, 0, "NOTES", 2);
            expected.drawText(50, 12, "64", 4);
            expected.drawText(0, 27, "CH1");
            std::snprintf(text, sizeof(text), "X%u", nextMultiplier);
            expected.drawText(112, 27, text);
            assert(ui.render(c, panel, 20).pixels() == expected.pixels());
            for (uint8_t other = 0; other < 4; ++other) {
                if (other == slot) continue;
                assert(panel.midiCcValue(other) == 0 && panel.midiCcMultiplier(other) == 8);
            }
        }
        assert(panel.midiCcMultiplier(slot) == 8);
    }
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    panel.click(c, 3, out, kCapacity); // Ordinary E4 click: X1, not Smart Voicing.
    assert(panel.midiCcMultiplier(3) == 1 && !c.smartVoicing());
    c.press(c.kShiftKey, 0, out, kCapacity);
    const auto shortcut = panel.click(c, 3, out, kCapacity);
    assert(shortcut.count == 0 && shortcut.feedback == PanelFeedback::SmartVoicing);
    assert(c.smartVoicing() && panel.midiCcMultiplier(3) == 1 && panel.midiCcValue(3) == 0);
    panel.click(c, 3, out, kCapacity);
    assert(!c.smartVoicing() && panel.midiCcMultiplier(3) == 1);
    panel.click(c, 4, out, kCapacity); // Shift does not steal other CC clicks.
    assert(panel.midiCcMultiplier(0) == 1);
    c.release(c.kShiftKey, 0, out, kCapacity);
    assert(panel.turn(c, 3, 1, 0, out, kCapacity).count == 1);
    assertCc(out[0], 23, 1);
    assert(panel.midiCcMultiplier(255) == 0);
    for (uint8_t slot = 0; slot < 4; ++slot) assert(FrontPanel{}.midiCcMultiplier(slot) == 8);
}

void testInvalidInputs() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    for (uint8_t encoder = 0; encoder < 8; ++encoder) {
        out[0] = {MidiCommandType::None, 99, 99};
        assert(panel.turn(c, encoder, 1, 0, out, 0).count == 0);
        assert(panel.turn(c, encoder, 1, 0, nullptr, kCapacity).count == 0);
        assert(panel.click(c, encoder, out, 0).count == 0);
        assert(panel.click(c, encoder, nullptr, kCapacity).count == 0);
        assert(out[0].type == MidiCommandType::None && out[0].note == 99 && out[0].velocity == 99);
    }
    assert(panel.turn(c, 255, 1, 0, out, kCapacity).feedback == PanelFeedback::None);
    assert(panel.click(c, 255, out, kCapacity).feedback == PanelFeedback::None);
    assert(panel.turn(c, 0, 0, 0, out, kCapacity).feedback == PanelFeedback::None);
    assert(panel.parameterPage() == E1Page::Octave && !panel.modulationSelected());
    assert(panel.midiCcValue(255) == 0 && c.octaveNumber() == 5 && c.velocity() == 100);
    for (uint8_t slot = 0; slot < 4; ++slot) {
        assert(panel.midiCcValue(slot) == 0 && panel.midiCcMultiplier(slot) == 8);
    }
}

void testFocusedRegressions() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[kCapacity]{};
    assert(panel.turn(c, 3, 1, 0, out, kCapacity).feedback == PanelFeedback::MidiCc);
    c.turnPreset(1); // GM KIT: ROOT is frozen as well as OCTAVE.
    for (uint8_t i = 0; i < 3; ++i) panel.click(c, 0, out, kCapacity);
    panel.turn(c, 0, 1, 0, out, kCapacity);
    assert(c.rootPitchClass() == 0 && c.drums());

    SimpleMidiController running;
    FrontPanel runningPanel;
    runningPanel.turn(running, 1, -1, 0, out, kCapacity);
    running.press(12, 0, out, kCapacity);
    running.press(0, 0, out, kCapacity);
    const int16_t note = running.runNote();
    for (uint8_t i = 0; i < 5; ++i) {
        runningPanel.click(running, 0, out, kCapacity);
        assert(running.clockMode() == ClockMode::Tempo && running.tempo() == 120);
        assert(running.runActive() && running.runNote() == note);
    }
    for (uint8_t encoder = 4; encoder < 7; ++encoder) {
        assert(runningPanel.turn(running, encoder, 1, 0, out, kCapacity).count == 1);
        assertCc(out[0], encoder + 16, 8);
        assert(running.runActive() && running.runNote() == note);
    }
    // Same-pass click then turn must edit the newly selected E1/E3 page.
    FrontPanel fresh;
    SimpleMidiController target;
    fresh.click(target, 0, out, kCapacity);
    fresh.turn(target, 0, 1, 0, out, kCapacity);
    assert(target.tempo() == 121 && target.octaveNumber() == 5);
    fresh.click(target, 2, out, kCapacity);
    assert(fresh.turn(target, 2, 1, 0, out, kCapacity).count == 1);
    assertCc(out[0], 1, 5);
    assert(target.velocity() == 100);

    // The tempo glyph must never intersect the navigation dots, at any value.
    OledUi ui;
    for (int tempo = 20; tempo <= 300; ++tempo) {
        target.turnTempo(tempo - target.tempo(), 0);
        ui.showE1(E1Page::Tempo, 10);
        const auto actual = ui.render(target, E2Page::Root, 10).pixels();
        MonoFramebuffer dots;
        drawDots(dots, 1);
        for (int x = 0; x < 128; ++x) {
            for (int y = 27; y < 32; ++y) {
                const size_t index = static_cast<size_t>(y / 8) * 128 + x;
                const auto mask = static_cast<uint8_t>(1U << (y % 8));
                assert((actual[index] & mask) == (dots.pixels()[index] & mask));
            }
        }
    }
}

void testUi() {
    SimpleMidiController c;
    FrontPanel panel;
    OledUi ui;
    MidiCommand out[kCapacity]{};
    for (uint8_t page = 0; page < 5; ++page) {
        ui.showPanel(PanelFeedback::Parameter, panel, 0, 10);
        MonoFramebuffer expected;
        const char* labels[]{"OCTAVE", "TEMPO", "FREQUENCY", "ROOT", "PRESET"};
        expected.drawText(0, 0, labels[page], 2);
        expected.drawText(88, 0, "NOTES", 2);
        if (page == 0) expected.drawText(50, 12, "O5", 4);
        if (page == 1) {
            expected.drawText(47, 12, "120", 3);
            expected.drawText(76, 0, "*", 2);
        }
        if (page == 2) expected.drawText(41, 16, "8.1 HZ", 2);
        if (page == 3) expected.drawText(58, 12, "C", 4);
        if (page == 4) expected.drawText(29, 14, "CHROMATIC", 2);
        drawDots(expected, page);
        assert(ui.render(c, panel, 10).pixels() == expected.pixels());
        const auto home = ui.render(c, E2Page::Root, 810).pixels();
        MonoFramebuffer hint;
        const char* hints[]{"OCT", "BPM", "HZ", "ROOT", "PRESET"};
        hint.drawText(64, 2, hints[page]);
        const auto actual = ui.render(c, panel, 810).pixels();
        for (size_t i = 0; i < actual.size(); ++i) assert(actual[i] == (home[i] | hint.pixels()[i]));
        assert(!ui.overlayVisible());
        panel.click(c, 0, out, kCapacity);
    }
    for (uint8_t encoder = 4; encoder < 7; ++encoder) {
        panel.click(c, encoder, out, kCapacity); // X1 permits exact fixture values.
        for (int value : {0, 1, 127}) {
            panel.turn(c, encoder, value - panel.midiCcValue(encoder - 4), 0, out, kCapacity);
            ui.showPanel(PanelFeedback::MidiCc, panel, encoder, 10);
            MonoFramebuffer expected;
            char text[20];
            std::snprintf(text, sizeof(text), "E%u CC%u", encoder + 1, encoder + 16);
            expected.drawText(0, 0, text, 2);
            expected.drawText(88, 0, "NOTES", 2);
            std::snprintf(text, sizeof(text), "%d", value);
            expected.drawText((128 - (static_cast<int>(std::strlen(text)) * 16 - 4)) / 2, 12, text, 4);
            expected.drawText(0, 27, "CH1");
    expected.drawText(112, 27, "X1");
            assert(ui.render(c, panel, 809).pixels() == expected.pixels());
            ui.render(c, panel, 810);
            assert(!ui.overlayVisible());
        }
    }
    c.turnPreset(1);
    assert(c.drums() && c.midiChannel() == 10);
    ui.showPanel(PanelFeedback::MidiCc, panel, 4, 10);
    const auto drumCc = ui.render(c, panel, 10).pixels();
    c.turnPreset(-1);
    assert(ui.render(c, panel, 10).pixels() != drumCc);
}

}

int main() {
    testParameterCycle();
    testShiftPriority();
    testRelocatedControls();
    testContextualPatternAssignment();
    testMidiCc();
    testE4MidiCcAndNotes();
    testMidiCcMultipliers();
    testInvalidInputs();
    testUi();
    testFocusedRegressions();
    std::cout << "AMEN MIDI front panel routing, MIDI CC and exact OLED tests: PASS\n";
}
