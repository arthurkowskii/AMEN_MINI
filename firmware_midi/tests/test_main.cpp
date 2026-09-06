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
        assert(amen::kDiatonicModeCount == expectedIntervals.size());
        constexpr std::array<char, 7> letters{{'C', 'D', 'E', 'F', 'G', 'A', 'B'}};
        constexpr std::array<int, 7> natural{{0, 2, 4, 5, 7, 9, 11}};
        for (uint8_t mode = 0; mode < amen::kDiatonicModeCount; ++mode)
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
        assert(controller.preset() == amen::MusicalPreset::Dark);
        assert(controller.turnPreset(1));
        assert(controller.preset() == amen::MusicalPreset::Major);
        assert(!controller.turnPreset(5));

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
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset)
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

        ui.showOctave(10);
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
        constexpr std::array<const char*, 5> names{{"MAJOR", "MINOR", "HARM MIN", "CINEMA", "DARK"}};
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            SimpleMidiController controller;
            controller.turnPreset(preset);
            assert(std::string(controller.presetName()) == names[preset]);
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
            const int64_t next = ((expected + delta) % 5 + 5) % 5;
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
        expected.drawText(0, 0, "O5 C HARM MIN*", 2);
        expected.drawText(58, 12, "C", 4);
        assert(ui.render(controller, amen::E2Page::Preset, 0).pixels() == expected.pixels());
        controller.turnPreset(-2);
        assert(!controller.hasHeldPresetMismatch());
        release(controller, 0);
        ui.showE2(amen::E2Page::Preset, 1);
        expected.clear();
        expected.drawText(0, 0, "PRESET", 2);
        expected.drawText(45, 14, "MAJOR", 2);
        expected.fillRect(112, 2, 5, 1);
        expected.fillRect(112, 6, 5, 1);
        expected.fillRect(112, 3, 1, 3);
        expected.fillRect(116, 3, 1, 3);
        expected.fillRect(121, 2, 5, 5);
        assert(ui.render(controller, amen::E2Page::Preset, 1).pixels() == expected.pixels());
        for (uint8_t preset = 0; preset < amen::kMusicalPresetCount; ++preset) {
            for (uint8_t slot = 0; slot < amen::kHarmonySlotCount; ++slot) {
                press(controller, 12 + slot);
                ui.showHarmony(10);
                expected.clear();
                expected.drawText(0, 0, "HARMONY", 2);
                const int length = static_cast<int>(std::string(controller.harmonyName()).size());
                const int scale = length * 16 - 4 <= 128 ? 4 : 2;
                expected.drawText((128 - (length * 4 * scale - scale)) / 2, 12, controller.harmonyName(), scale);
                assert(ui.render(controller, amen::E2Page::Preset, 10).pixels() == expected.pixels());
                release(controller, 12 + slot);
            }
            controller.turnPreset(1);
        }
    }

    std::cout << "AMEN MIDI preset and ownership tests: PASS\n";
}
