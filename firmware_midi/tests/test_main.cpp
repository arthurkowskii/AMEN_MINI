#include "algorithms/algorithms.hpp"
#include "controls/control_surface.hpp"
#include "midi/note_registry.hpp"
#include "music/harmony.hpp"
#include "performance/engine.hpp"
#include "profiles/instrument_profiles.hpp"
#include "teensy/adapter.hpp"
#include "teensy/teensy_pinmap.h"
#include "ui/text_model.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __func__ << ':' << __LINE__ << " " #x "\n"; ++failures; } } while (false)
using namespace amen;

MidiEvent ev(MidiType t, uint8_t note, uint8_t value, uint32_t at) { return {t, 1, note, value, at}; }
bool same(const MidiEvent& a, const MidiEvent& b) { return a.type == b.type && a.channel == b.channel && a.data1 == b.data1 && a.data2 == b.data2 && a.atMs == b.atMs; }
void expectTrace(const PerformanceEngine& e, const std::initializer_list<MidiEvent>& expected) {
  CHECK(e.events().size() == expected.size());
  std::size_t i = 0;
  for (const auto& item : expected) { if (i < e.events().size()) CHECK(same(e.events()[i], item)); ++i; }
}
void clickShift(PerformanceEngine& e, uint32_t down) { e.shiftDown(down); e.shiftUp(down + 30); }
void doubleShift(PerformanceEngine& e, uint32_t down) { clickShift(e, down); clickShift(e, down + 100); }

void testHarmonyGoldenAndExhaustive() {
  struct Golden { HarmonyPreset preset; uint8_t degree; ChordShape shape; std::array<uint8_t, 6> notes; uint8_t count; };
  const std::array<Golden, 14> goldens{{
    {HarmonyPreset::MajorBasic, 0, ChordShape::Triad, {60,64,67,0,0,0}, 3},
    {HarmonyPreset::MajorBasic, 1, ChordShape::Seventh, {62,65,69,72,0,0}, 4},
    {HarmonyPreset::MinorBasic, 0, ChordShape::Triad, {60,63,67,0,0,0}, 3},
    {HarmonyPreset::MinorBasic, 4, ChordShape::Seventh, {67,70,74,77,0,0}, 4},
    {HarmonyPreset::Chromatic, 0, ChordShape::Triad, {60,64,66,0,0,0}, 3},
    {HarmonyPreset::Chromatic, 6, ChordShape::Sus2, {66,67,72,0,0,0}, 3},
    {HarmonyPreset::Cinematic, 0, ChordShape::Triad, {60,63,67,0,0,0}, 3},
    {HarmonyPreset::Cinematic, 5, ChordShape::Ninth, {69,72,76,79,83,0}, 5},
    {HarmonyPreset::Dark, 0, ChordShape::Triad, {60,63,66,0,0,0}, 3},
    {HarmonyPreset::Dark, 6, ChordShape::Seventh, {70,73,76,80,0,0}, 4},
    {HarmonyPreset::Debussy, 0, ChordShape::Triad, {60,64,67,0,0,0}, 3},
    {HarmonyPreset::Debussy, 3, ChordShape::Eleventh, {66,70,73,77,80,85}, 6},
    {HarmonyPreset::Ambient, 0, ChordShape::Triad, {60,64,67,0,0,0}, 3},
    {HarmonyPreset::Ambient, 8, ChordShape::Sixth, {79,83,86,88,0,0}, 4}
  }};
  for (const auto& g : goldens) {
    const auto n = makeVoicing({g.preset, g.shape, 60, 0, 3}, g.degree);
    CHECK(n.count == g.count);
    for (uint8_t i = 0; i < n.count; ++i) CHECK(n.notes[i] == g.notes[i]);
  }
  CHECK(harmonyCatalog().size() == 7);
  for (uint8_t p = 0; p < static_cast<uint8_t>(HarmonyPreset::Count); ++p) {
    const auto preset = static_cast<HarmonyPreset>(p);
    CHECK(variationCount(preset) >= 1);
    CHECK(std::strlen(variationName(preset, 0)) > 0);
    for (uint8_t v = 0; v < variationCount(preset); ++v)
      for (uint8_t degree = 0; degree < 12; ++degree)
        for (uint8_t shape = 0; shape < static_cast<uint8_t>(ChordShape::Count); ++shape) {
          HarmonySnapshot s{preset, static_cast<ChordShape>(shape), 60, v, 3};
          const auto a = makeVoicing(s, degree), b = makeVoicing(s, degree);
          CHECK(voicingIsMidiBounded(a)); CHECK(a.count == b.count); CHECK(a.notes == b.notes);
        }
  }
}

void testGlobalTokensRejectInterDegreeStaleRelease() {
  PerformanceEngine e;
  e.noteDown(0, 100, 0); const auto stale = e.currentToken(0); e.noteUp(stale, 1);
  e.clearEvents(); e.noteDown(1, 100, 2); const auto fresh = e.currentToken(1);
  CHECK(stale.value != 0 && fresh.value != 0 && stale.value != fresh.value);
  e.noteUp(stale, 3);
  CHECK(e.sourceActive(1)); CHECK(e.events().size() == 3);
  e.noteUp(fresh, 4); CHECK(!e.sourceActive(1)); CHECK(e.events().size() == 6);
}

void testRegistryTransactionalOverflowAndPriorityRelease() {
  EventBuffer out;
  for (std::size_t i = 0; i < kMaxEvents; ++i) CHECK(out.push(ev(MidiType::ControlChange, 1, 2, 0)));
  NoteRegistry r; const SourceToken owner{1};
  CHECK(!r.acquire(60, owner, out, 1, 100)); CHECK(r.owners(60) == 0);
  out.clear(); CHECK(r.acquire(60, owner, out, 2, 100));
  for (std::size_t i = out.size(); i < kMaxEvents; ++i) CHECK(out.push(ev(MidiType::ControlChange, 1, 2, 3)));
  r.release(60, owner, out, 4);
  CHECK(r.owners(60) == 0); CHECK(out.size() == kMaxEvents);
  bool recovered = false; for (std::size_t i = 0; i < out.size(); ++i) recovered |= out[i].type == MidiType::NoteOff && out[i].data1 == 60;
  CHECK(recovered);
}

void testRegistryOwnersRejectsOutOfRangeNotes() {
  NoteRegistry registry;
  EventBuffer out;
  CHECK(registry.acquire(127, SourceToken{1}, out, 0, 100));
  CHECK(registry.owners(127) == 1);
  CHECK(registry.owners(128) == 0);
  CHECK(registry.owners(255) == 0);
}

void testRestoreRetriesAfterHostDrainWithoutDuplicateOwnership() {
  PerformanceEngine e;
  e.noteDown(0, 100, 0);
  e.clearEvents();
  CHECK(e.assignFx(0, FxType::Arp, FxMode::Latch));
  e.fxDown(0, 1);
  e.clearEvents();
  for (std::size_t i = 0; i < kMaxEvents; ++i)
    CHECK(e.mutableEventsForTest().push(ev(MidiType::ControlChange, 1, 2, 2)));

  e.fxDown(0, 10);
  CHECK(e.activeFx() == FxType::Blank);
  CHECK(e.syncPending());

  e.clearEvents();
  e.tick(11);
  CHECK(!e.syncPending());
  expectTrace(e, {
    ev(MidiType::NoteOn,60,100,11),
    ev(MidiType::NoteOn,64,100,11),
    ev(MidiType::NoteOn,67,100,11)
  });
  CHECK(e.noteOwners(60) == 1);
  CHECK(e.noteOwners(64) == 1);
  CHECK(e.noteOwners(67) == 1);

  e.clearEvents();
  e.tick(12);
  CHECK(e.events().size() == 0);
  CHECK(e.noteOwners(60) == 1);
  CHECK(e.noteOwners(64) == 1);
  CHECK(e.noteOwners(67) == 1);
}

void testTeensyAdapterRetriesImmediatelyAfterDrain() {
  class SaturatingPort final : public amen::teensy::HardwarePort {
   public:
    void scanInputs(PerformanceEngine& engine, uint32_t now) override {
      if (scanned) return;
      scanned = true;
      for (std::size_t i = engine.events().size(); i < kMaxEvents; ++i)
        CHECK(engine.mutableEventsForTest().push(ev(MidiType::ControlChange, 1, 2, now)));
      engine.fxDown(0, now);
    }
    void send(const MidiEvent& event) override {
      if (event.type == MidiType::NoteOn) ++noteOns[event.data1];
    }
    bool scanned{};
    std::array<uint8_t, 128> noteOns{};
  };

  PerformanceEngine e;
  e.noteDown(0, 100, 0);
  e.clearEvents();
  CHECK(e.assignFx(0, FxType::Arp, FxMode::Latch));
  e.fxDown(0, 1);
  e.clearEvents();

  SaturatingPort port;
  amen::teensy::service(port, e, 10);
  CHECK(!e.syncPending());
  CHECK(e.events().size() == 0);
  CHECK(port.noteOns[60] == 1);
  CHECK(port.noteOns[64] == 1);
  CHECK(port.noteOns[67] == 1);
  CHECK(e.noteOwners(60) == 1 && e.noteOwners(64) == 1 && e.noteOwners(67) == 1);
}

void testPanicClearsFullQueueAndCannotLeakStaleFrames() {
  PerformanceEngine e; e.noteDown(0, 100, 0); e.assignFx(0, FxType::Arp, FxMode::Latch); e.fxDown(0, 1);
  for (std::size_t i = e.events().size(); i < kMaxEvents; ++i) e.mutableEventsForTest().push(ev(MidiType::ControlChange, 1, 2, 2));
  e.panic(10);
  CHECK(!e.sourceActive(0)); CHECK(e.activeFx() == FxType::Blank); CHECK(!e.algorithm().active());
  bool cc123 = false; for (std::size_t i = 0; i < e.events().size(); ++i) cc123 |= e.events()[i].type == MidiType::ControlChange && e.events()[i].data1 == 123;
  CHECK(cc123); CHECK(e.events().size() < kMaxEvents);
  e.clearEvents(); e.tick(100000); CHECK(e.events().size() == 0);
}

void testFxArbitrationAndExactTransitions() {
  PerformanceEngine e;
  e.assignFx(0, FxType::Arp, FxMode::Latch); e.assignFx(1, FxType::RunUp, FxMode::Gate); e.assignFx(2, FxType::Random, FxMode::Gate);
  e.fxDown(0, 0); e.fxUp(0, 1); CHECK(e.activeFx() == FxType::Arp);
  e.fxDown(1, 2); CHECK(e.activeFx() == FxType::RunUp); e.fxUp(1, 3); CHECK(e.activeFx() == FxType::Arp);
  e.fxDown(1, 4); e.fxDown(2, 5); CHECK(e.activeFx() == FxType::Random); e.fxUp(2, 6); CHECK(e.activeFx() == FxType::RunUp);
  e.noteDown(0, 100, 10); // FX-first: only first arp note, never full triad.
  expectTrace(e, {ev(MidiType::NoteOn, 60, 100, 10)});
  e.clearEvents(); e.fxUp(1, 11); CHECK(e.activeFx() == FxType::Arp); CHECK(e.events().size() == 0); // FX->FX has no direct bridge.
  e.fxDown(0, 12); // toggle latch off -> direct snapshot restored
  CHECK(e.activeFx() == FxType::Blank);
  expectTrace(e, {ev(MidiType::NoteOff,60,0,12), ev(MidiType::NoteOn,60,100,12), ev(MidiType::NoteOn,64,100,12), ev(MidiType::NoteOn,67,100,12)});
}

void testNoteFirstAndFxFirstTraces() {
  PerformanceEngine noteFirst; noteFirst.noteDown(0, 90, 0); noteFirst.clearEvents();
  noteFirst.assignFx(0, FxType::RunUp, FxMode::Gate); noteFirst.fxDown(0, 10);
  expectTrace(noteFirst, {ev(MidiType::NoteOff,60,0,10), ev(MidiType::NoteOff,64,0,10), ev(MidiType::NoteOff,67,0,10), ev(MidiType::NoteOn,60,100,10)});
  PerformanceEngine fxFirst; fxFirst.assignFx(0, FxType::Strum, FxMode::Gate); fxFirst.fxDown(0, 0); fxFirst.noteDown(0, 90, 5);
  expectTrace(fxFirst, {ev(MidiType::NoteOn,60,100,5)});
  fxFirst.tick(255); fxFirst.tick(505);
  expectTrace(fxFirst, {ev(MidiType::NoteOn,60,100,5), ev(MidiType::NoteOn,64,100,255), ev(MidiType::NoteOn,67,100,505)});
}

void testStrumRendersEachNoteFromHeterogeneousSources() {
  PerformanceEngine e;
  e.noteDown(0, 100, 0); // Source A snapshots the initial triad.
  e.encoderTurn(5, 6, 1); // New sources now snapshot THIRTEENTH (six notes).
  e.clearEvents();
  e.assignFx(0, FxType::Strum, FxMode::Gate); e.fxDown(0, 10);
  e.clearEvents();

  e.noteDown(1, 100, 11); // Source B joins while Strum is active.
  e.tick(261); e.tick(511); e.tick(761); e.tick(1011); e.tick(1261);

  expectTrace(e, {
    ev(MidiType::NoteOn,62,100,11),
    ev(MidiType::NoteOn,64,100,261), ev(MidiType::NoteOn,65,100,261),
    ev(MidiType::NoteOn,67,100,511), ev(MidiType::NoteOn,69,100,511),
    ev(MidiType::NoteOn,72,100,761), ev(MidiType::NoteOn,76,100,1011),
    ev(MidiType::NoteOn,83,100,1261)
  });
  CHECK(!e.algorithm().active());
}

void testFxToStrumClosesPreviousOwnershipBeforeFirstFrame() {
  PerformanceEngine e;
  e.noteDown(0, 100, 0); e.clearEvents();
  e.assignFx(0, FxType::Arp, FxMode::Latch); e.fxDown(0, 10);
  e.tick(260); e.tick(510); // Arp currently owns only G67.
  e.clearEvents();

  e.assignFx(1, FxType::Strum, FxMode::Gate); e.fxDown(1, 600);

  expectTrace(e, {
    ev(MidiType::NoteOff,67,0,600),
    ev(MidiType::NoteOn,60,100,600)
  });
}

void testFiniteFxWaitsForSourcesAndRestartsOnJoin() {
  PerformanceEngine e; e.assignFx(0, FxType::RunDown, FxMode::Latch); e.fxDown(0, 0);
  e.tick(10000); CHECK(e.events().size() == 0); CHECK(e.algorithm().active());
  e.noteDown(0, 100, 10001); CHECK(e.events().size() == 1); CHECK(e.events()[0].data1 == 67);
  e.tick(10251); e.tick(10501); CHECK(!e.algorithm().active());
  e.noteUp(0, 10502); e.clearEvents(); e.noteDown(1, 100, 11000);
  CHECK(e.algorithm().active()); CHECK(e.events().size() == 1); CHECK(e.events()[0].data1 == 69);
}

void testLatchedArpRestartsAfterLongEmptyWait() {
  PerformanceEngine e;
  CHECK(e.assignFx(0, FxType::Arp, FxMode::Latch));
  e.fxDown(0, 0);
  e.tick(0x80000000U);
  CHECK(e.events().size() == 0);

  constexpr uint32_t firstNoteAt = 0x80000010U;
  e.noteDown(0, 100, firstNoteAt);
  expectTrace(e, {ev(MidiType::NoteOn, 60, 100, firstNoteAt)});
}

void testHoldDisablePreservesPhysicalSources() {
  PerformanceEngine e; e.noteDown(0, 100, 0); doubleShift(e, 20); CHECK(e.hold());
  e.noteDown(1, 100, 200); e.noteUp(1, 201); CHECK(e.sourceActive(1));
  e.clearEvents(); doubleShift(e, 400); CHECK(!e.hold());
  CHECK(e.sourceActive(0)); CHECK(!e.sourceActive(1));
  expectTrace(e, {ev(MidiType::NoteOff,62,0,530), ev(MidiType::NoteOff,65,0,530), ev(MidiType::NoteOff,69,0,530)});
  e.clearEvents(); e.noteUp(0, 600); CHECK(!e.sourceActive(0)); CHECK(e.events().size() == 3);
}

void testFxSemanticsAcrossFrames() {
  PerformanceEngine repeat; repeat.noteDown(0,100,0); repeat.clearEvents(); repeat.assignFx(0,FxType::NoteRepeat,FxMode::Gate); repeat.fxDown(0,10); repeat.tick(260);
  expectTrace(repeat, {ev(MidiType::NoteOff,60,0,10),ev(MidiType::NoteOff,64,0,10),ev(MidiType::NoteOff,67,0,10),ev(MidiType::NoteOn,60,100,10),ev(MidiType::NoteOn,64,100,10),ev(MidiType::NoteOn,67,100,10),ev(MidiType::NoteOff,60,0,260),ev(MidiType::NoteOff,64,0,260),ev(MidiType::NoteOff,67,0,260),ev(MidiType::NoteOn,60,100,260),ev(MidiType::NoteOn,64,100,260),ev(MidiType::NoteOn,67,100,260)});
  PerformanceEngine gate; gate.assignFx(0,FxType::TranceGate,FxMode::Gate); gate.fxDown(0,0); gate.noteDown(0,100,1); gate.tick(251); CHECK(gate.events().size()==6); CHECK(gate.events()[3].type==MidiType::NoteOff);
  PerformanceEngine velocity; velocity.assignFx(0,FxType::Velocity,FxMode::Gate); velocity.fxDown(0,0); velocity.noteDown(0,70,1); CHECK(velocity.events().size()==3); CHECK(velocity.events()[0].data2==32); velocity.tick(251); CHECK(velocity.events()[6].data2==63);
  PerformanceEngine randomA, randomB; randomA.assignFx(0,FxType::Random,FxMode::Gate); randomB.assignFx(0,FxType::Random,FxMode::Gate); randomA.fxDown(0,0); randomB.fxDown(0,0); randomA.noteDown(0,100,1); randomB.noteDown(0,100,1); CHECK(randomA.events()[0].data1==randomB.events()[0].data1);
}

void testSchedulerAllFxFrames() {
  AlgorithmScheduler strum; strum.start(FxType::Strum, 0);
  CHECK(strum.tick(0,10,3).noteIndex == 0); CHECK(strum.tick(10,10,3).noteIndex == 1); CHECK(strum.tick(20,10,3).noteIndex == 2); CHECK(!strum.active());
  AlgorithmScheduler arp; arp.start(FxType::Arp,0);
  CHECK(arp.tick(0,10,3).noteIndex == 0); CHECK(arp.tick(10,10,3).noteIndex == 1); CHECK(arp.tick(20,10,3).noteIndex == 2); CHECK(arp.tick(30,10,3).noteIndex == 0);
  AlgorithmScheduler up; up.start(FxType::RunUp,0);
  CHECK(up.tick(0,10,3).noteIndex == 0); CHECK(up.tick(10,10,3).noteIndex == 1); CHECK(up.tick(20,10,3).noteIndex == 2); CHECK(!up.active());
  AlgorithmScheduler down; down.start(FxType::RunDown,0);
  CHECK(down.tick(0,10,3).noteIndex == 2); CHECK(down.tick(10,10,3).noteIndex == 1); CHECK(down.tick(20,10,3).noteIndex == 0); CHECK(!down.active());
  AlgorithmScheduler gate; gate.start(FxType::TranceGate,0); CHECK(gate.tick(0,10,3).gateOpen); CHECK(!gate.tick(10,10,3).gateOpen);
  AlgorithmScheduler repeat; repeat.start(FxType::NoteRepeat,0); CHECK(repeat.tick(0,10,3).emit); CHECK(repeat.tick(10,10,3).emit);
  AlgorithmScheduler velocity; velocity.start(FxType::Velocity,0);
  CHECK(velocity.tick(0,10,3).velocity == 32); CHECK(velocity.tick(10,10,3).velocity == 63); CHECK(velocity.tick(20,10,3).velocity == 94); CHECK(velocity.tick(30,10,3).velocity == 125);
  AlgorithmScheduler randomA, randomB; randomA.start(FxType::Random,0); randomB.start(FxType::Random,0);
  for (uint32_t now=0; now<50; now+=10) CHECK(randomA.tick(now,10,3).noteIndex == randomB.tick(now,10,3).noteIndex);
  for (uint8_t raw=1; raw<=8; ++raw) { AlgorithmScheduler cancelled; cancelled.start(static_cast<FxType>(raw),0); cancelled.cancel(); CHECK(!cancelled.tick(100,10,3).emit); }
}

void testSchedulerWrapAndCancel() {
  AlgorithmScheduler a; a.start(FxType::Arp, UINT32_MAX - 5U);
  CHECK(a.tick(UINT32_MAX - 5U, 10, 3).emit); CHECK(!a.tick(UINT32_MAX - 1U, 10, 3).emit); CHECK(a.tick(4U, 10, 3).emit);
  a.cancel(); CHECK(!a.tick(100,10,3).emit);
}

void testRelativeDeltaUsesWideArithmeticBeforeClamp() {
  CHECK(applyRelative(64, std::numeric_limits<int>::min(), 0, 127) == 0);
  CHECK(applyRelative(64, std::numeric_limits<int>::max(), 0, 127) == 127);
  CHECK(applyRelative(std::numeric_limits<int>::max(), 1,
                      std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) ==
        std::numeric_limits<int>::max());
  CHECK(applyRelative(std::numeric_limits<int>::min(), -1,
                      std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) ==
        std::numeric_limits<int>::min());
}

void testInvalidFxEnumsAreRejectedAndSchedulerCancels() {
  PerformanceEngine e;
  const auto invalidType = static_cast<FxType>(255);
  const auto invalidMode = static_cast<FxMode>(255);
  CHECK(!e.assignFx(0, invalidType, FxMode::Gate));
  CHECK(!e.assignFx(0, FxType::Arp, invalidMode));
  e.fxDown(0, 0);
  CHECK(static_cast<uint8_t>(e.activeFx()) <= static_cast<uint8_t>(FxType::Velocity));
  const auto ui = makeUiText(e);
  CHECK(std::strstr(ui.line4.data(), "FX 1") != nullptr);

  AlgorithmScheduler scheduler;
  scheduler.start(invalidType, 0);
  CHECK(!scheduler.active());
  CHECK(scheduler.type() == FxType::Blank);
  CHECK(!scheduler.tick(0, 10, 3).emit);
}

void testInvalidProfilesAreBounded() {
  PerformanceEngine e; const auto invalid = static_cast<ProfileId>(255);
  CHECK(!e.setProfile(invalid)); CHECK(e.profile() == ProfileId::Serum);
  const auto safeUi = makeUiText(e); CHECK(std::strstr(safeUi.line3.data(), "Serum") != nullptr);
  CHECK(e.profileCc(invalid, 0) == 0); CHECK(e.profileCc(ProfileId::Serum, 255) == 0);
  e.setProfileCc(invalid, 0, 99); e.setProfileCc(ProfileId::Serum, 255, 99);
  e.encoderTurn(0, 1, 0); e.encoderTurn(255, 1, 0); CHECK(e.events().size() == 0);
  CHECK(instrumentProfiles().size() == static_cast<std::size_t>(ProfileId::Count));
  for (uint8_t p=0; p<static_cast<uint8_t>(ProfileId::Count); ++p) {
    const auto profile=static_cast<ProfileId>(p); CHECK(e.setProfile(profile));
    for (uint8_t encoder=0; encoder<7; ++encoder) { const uint8_t cc=static_cast<uint8_t>(10+p*7+encoder); e.setProfileCc(profile,encoder,cc); CHECK(e.profileCc(profile,encoder)==cc); }
  }
}

void testPinMapAndUi() {
  static_assert(amen::teensy::uniqueDigitalPins());
  CHECK(amen::teensy::kSda == 18 && amen::teensy::kScl == 19);
  PerformanceEngine e; auto ui = makeUiText(e); CHECK(std::strstr(ui.line1.data(), "Major") != nullptr); CHECK(std::strstr(ui.line3.data(), "Serum") != nullptr);
}
}

int main() {
  testHarmonyGoldenAndExhaustive(); testGlobalTokensRejectInterDegreeStaleRelease(); testRegistryTransactionalOverflowAndPriorityRelease();
  testRegistryOwnersRejectsOutOfRangeNotes(); testRestoreRetriesAfterHostDrainWithoutDuplicateOwnership();
  testTeensyAdapterRetriesImmediatelyAfterDrain();
  testPanicClearsFullQueueAndCannotLeakStaleFrames(); testFxArbitrationAndExactTransitions(); testNoteFirstAndFxFirstTraces();
  testStrumRendersEachNoteFromHeterogeneousSources(); testFxToStrumClosesPreviousOwnershipBeforeFirstFrame();
  testFiniteFxWaitsForSourcesAndRestartsOnJoin(); testLatchedArpRestartsAfterLongEmptyWait();
  testHoldDisablePreservesPhysicalSources(); testFxSemanticsAcrossFrames();
  testSchedulerAllFxFrames(); testSchedulerWrapAndCancel(); testRelativeDeltaUsesWideArithmeticBeforeClamp();
  testInvalidFxEnumsAreRejectedAndSchedulerCancels(); testInvalidProfilesAreBounded(); testPinMapAndUi();
  if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
  std::cout << "AMEN MIDI compliance tests: PASS\n"; return 0;
}
