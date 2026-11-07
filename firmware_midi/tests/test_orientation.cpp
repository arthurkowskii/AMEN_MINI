#include "front_panel.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <iostream>

using namespace amen;
constexpr uint8_t capacity = SimpleMidiController::kMaxEventsPerAction;

void testMapping() {
    SimpleMidiController c;
    assert(c.orientation() == Orientation::Vertical);
    assert(c.degreeKeyCount() == 12 && c.functionKeyCount() == 8);
    for (uint8_t key = 0; key <= 20; ++key) assert(c.keyForPhysical(key) == key);
    assert(c.setOrientation(Orientation::Horizontal));
    assert(c.degreeKeyCount() == 15 && c.functionKeyCount() == 5);
    // PCB rotated clockwise: SW1 is top-left, SW20 bottom-right.
    constexpr std::array<uint8_t, 20> expected{{
        15, 10, 5, 0, 16, 11, 6, 1, 17, 12, 7, 2, 18, 13, 8, 3, 19, 14, 9, 4}};
    std::array<bool, 20> seen{};
    for (uint8_t physical = 0; physical < 20; ++physical) {
        const auto key = c.keyForPhysical(physical);
        assert(key == expected[physical]);
        assert(!seen[key]);
        seen[key] = true;
        assert(c.physicalForKey(key) == physical);
    }
    assert(c.keyForPhysical(20) == 20);
    assert(c.keyForPhysical(255) == 255);
    assert(c.physicalForKey(255) == 255);
    for (uint8_t i = 0; i < 20; ++i) {
        const auto key = c.keyInScanOrder(i);
        assert(key == (i < 5 ? i + 15 : i - 5));
    }
    assert(c.keyInScanOrder(20) == 255);
    assert(!c.setOrientation(static_cast<Orientation>(255)));
    assert(c.orientation() == Orientation::Horizontal);
}

void testNotesAndRoles() {
    for (const auto page : {PerformancePage::Harmony, PerformancePage::Pattern, PerformancePage::Notes}) {
        SimpleMidiController c;
        MidiCommand out[capacity]{};
        assert(c.setOrientation(Orientation::Horizontal));
        while (c.page() != page) c.turnPage(1, out, capacity);
        const uint8_t count = page == PerformancePage::Notes ? 20 : 15;
        for (uint8_t key = 0; key < count; ++key) {
            assert(c.press(key, 0, out, capacity) == 1);
            assert(out[0].type == MidiCommandType::NoteOn && out[0].note == 60 + key);
            assert(c.heldCount() == 1 && !c.patternHeld() && !c.harmonyActive());
            assert(c.release(key, 0, out, capacity) == 1);
            assert(out[0].type == MidiCommandType::NoteOff && out[0].note == 60 + key);
        }
        for (uint8_t slot = 0; slot < 5 && page != PerformancePage::Notes; ++slot) {
            assert(c.press(15 + slot, 0, out, capacity) == 0);
            assert(c.heldCount() == 0);
            if (page == PerformancePage::Harmony) assert(c.harmonyStackSize() == 1);
            else assert(c.patternHeld() && c.patternSlot() == slot);
            c.release(15 + slot, 0, out, capacity);
        }
    }
}

void testHarmonyAndPatterns() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[capacity]{};
    c.setOrientation(Orientation::Horizontal);
    c.turnPage(1, out, capacity); // Harmony
    c.press(15, 0, out, capacity);
    assert(c.press(14, 0, out, capacity) == 3); // Three voices on the fifteenth note.
    assert(c.heldCount() == 1 && c.harmonyActive());
    assert(c.release(14, 0, out, capacity) == 3);
    c.release(15, 0, out, capacity);
    c.turnPage(1, out, capacity); // Pattern
    for (const bool patternFirst : {false, true}) {
        c.press(patternFirst ? 19 : 14, 0, out, capacity);
        c.press(patternFirst ? 14 : 19, 0, out, capacity);
        assert(c.runActive() && c.runSourceKey() == 14 && c.patternSlot() == 4);
        const auto assignment = c.slotAssignment(4);
        panel.turn(c, 0, 1, 0, out, capacity);
        assert(c.slotAssignment(4) != assignment);
        assert(panel.parameterPage() == E1Page::Octave);
        c.release(19, 0, out, capacity);
        assert(!c.runActive() && !c.patternHeld());
        c.release(14, 0, out, capacity);
        assert(c.heldCount() == 0);
    }
}

void testSafeSwitchAndRelease() {
    SimpleMidiController c;
    FrontPanel panel;
    MidiCommand out[capacity]{};
    c.press(0, 0, out, capacity);
    assert(!c.setOrientation(Orientation::Horizontal));
    c.press(c.kShiftKey, 0, out, capacity);
    panel.click(c, 1, out, capacity);
    assert(c.orientation() == Orientation::Vertical);
    assert(c.release(0, 0, out, capacity) == 1);
    panel.click(c, 1, out, capacity);
    assert(c.orientation() == Orientation::Horizontal && c.shiftHeld());
    assert(c.release(c.kShiftKey, 0, out, capacity) == 1);
    // Held roles survive page changes, including notes above the split.
    c.press(19, 0, out, capacity);
    c.turnPage(1, out, capacity);
    assert(!c.setOrientation(Orientation::Vertical));
    assert(c.release(19, 0, out, capacity) == 1);
    c.press(19, 0, out, capacity); // Harmony role
    c.turnPage(1, out, capacity);
    assert(c.release(19, 0, out, capacity) == 0);
    assert(!c.harmonyActive());
    c.press(14, 0, out, capacity);
    c.press(19, 0, out, capacity);
    assert(c.runActive());
    assert(!c.setOrientation(Orientation::Vertical));
    c.turnPage(1, out, capacity);
    assert(!c.runActive());
    c.release(14, 0, out, capacity);
    c.release(19, 0, out, capacity);
    assert(c.setOrientation(Orientation::Vertical));
    assert(c.heldCount() == 0 && !c.patternHeld());
    // Invalid buffers do not trigger the panel setting, nor consume a bank change.
    c.press(c.kShiftKey, 0, out, capacity);
    panel.click(c, 1, nullptr, capacity);
    panel.click(c, 1, out, 0);
    assert(c.orientation() == Orientation::Vertical);
    c.release(c.kShiftKey, 0, out, capacity);
    c.turnPage(-1, out, capacity);
    const auto bank = c.patternBank();
    c.press(c.kShiftKey, 0, out, capacity);
    panel.click(c, 1, out, capacity);
    assert(c.orientation() == Orientation::Horizontal && c.patternBank() == bank);
    c.release(c.kShiftKey, 0, out, capacity);
    panel.click(c, 1, out, capacity);
    assert(c.patternBank() != bank);
}

void testPhysicalSnapshotAndTransactions() {
    for (const auto orientation : {Orientation::Vertical, Orientation::Horizontal}) {
        for (uint8_t preset = 0; preset < kMusicalPresetCount; ++preset) {
            SimpleMidiController c;
            MidiCommand out[capacity]{};
            c.setOrientation(orientation);
            c.turnPreset(preset);
            std::array<bool, 21> previous{};
            // Exercise exactly the logical-order/physical-contact conversion used in loop().
            const auto snapshot = [&](const std::array<bool, 21>& contacts) {
                for (uint8_t i = 0; i < 20; ++i) {
                    const auto key = c.keyInScanOrder(i);
                    const auto physical = c.physicalForKey(key);
                    if (contacts[physical] == previous[physical]) continue;
                    if (contacts[physical]) c.press(key, 0, out, capacity);
                    else c.release(key, 0, out, capacity);
                    previous[physical] = contacts[physical];
                }
            };
            for (const auto page : {PerformancePage::Harmony, PerformancePage::Pattern, PerformancePage::Notes}) {
                while (c.page() != page) c.turnPage(1, out, capacity);
                // Every pad down/up, including a note and function arriving simultaneously.
                std::array<bool, 21> contacts{};
                contacts.fill(true);
                snapshot(contacts);
                assert(c.heldCount() == (page == PerformancePage::Notes ? 20 : c.degreeKeyCount()));
                if (page == PerformancePage::Harmony) assert(c.harmonyStackSize() == c.functionKeyCount());
                if (page == PerformancePage::Pattern) assert(c.patternStackSize() == c.functionKeyCount());
                contacts.fill(false);
                snapshot(contacts);
                assert(c.heldCount() == 0 && !c.harmonyActive() && !c.patternHeld() && !c.runActive());
            }
        }
    }
    SimpleMidiController c;
    MidiCommand out[capacity]{};
    c.setOrientation(Orientation::Horizontal);
    c.turnPage(1, out, capacity);
    c.press(15, 0, out, capacity);
    assert(c.press(14, 0, out, 2) == 0); // Chord needs three NoteOn events.
    assert(c.heldCount() == 0);
    assert(c.press(14, 0, out, capacity) == 3);
    assert(c.release(14, 0, out, 2) == 0);
    assert(c.heldCount() == 1);
    assert(c.release(14, 0, out, capacity) == 3);
    c.release(15, 0, out, capacity);
    c.turnPage(1, out, capacity);
    FrontPanel panel;
    for (uint8_t bank = 0; bank < kPatternBankCount; ++bank) {
        for (uint8_t slot = 0; slot < 5; ++slot) {
            c.press(15 + slot, 0, out, capacity);
            assert(c.patternSlot() == slot);
            assert(c.currentPattern() == c.slotAssignment(slot));
            panel.turn(c, 0, 1, 0, out, capacity);
            assert(c.currentPattern() == c.slotAssignment(slot));
            c.press(14, 0, out, capacity);
            assert(c.runActive());
            c.tick(200000, out, capacity);
            c.release(14, 200001, out, capacity);
            c.release(15 + slot, 200002, out, capacity);
            assert(!c.runActive() && c.heldCount() == 0);
        }
        c.nextPatternBank();
    }
}

int main() {
    testMapping();
    testNotesAndRoles();
    testHarmonyAndPatterns();
    testSafeSwitchAndRelease();
    testPhysicalSnapshotAndTransactions();
    std::cout << "Orientation mapping, notes, roles, patterns and safe switching passed\n";
}
