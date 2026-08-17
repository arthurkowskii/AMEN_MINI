#include "performance/engine.hpp"
#include "ui/text_model.hpp"
#include <iostream>

namespace {
const char* typeName(amen::MidiType type) {
  switch (type) { case amen::MidiType::NoteOn: return "NOTE ON"; case amen::MidiType::NoteOff: return "NOTE OFF"; case amen::MidiType::ControlChange: return "CC"; }
  return "?";
}
void dump(amen::PerformanceEngine& engine, const char* label) {
  std::cout << "\n-- " << label << " (" << engine.events().size() << " events) --\n";
  for (std::size_t i = 0; i < engine.events().size(); ++i) {
    const auto& event = engine.events()[i];
    std::cout << event.atMs << "ms " << typeName(event.type) << " ch=" << unsigned(event.channel)
              << " d1=" << unsigned(event.data1) << " d2=" << unsigned(event.data2) << '\n';
  }
  engine.clearEvents();
}
void shiftClick(amen::PerformanceEngine& engine, uint32_t at) { engine.shiftDown(at); engine.shiftUp(at + 30); }
}

int main() {
  using namespace amen;
  PerformanceEngine engine;
  engine.encoderTurn(1, 5, 0); // Debussy; default shape is TRIAD.
  std::cout << "AMEN MIDI host harness: Debussy prototype + Serum\n";
  const auto ui = makeUiText(engine);
  std::cout << ui.line1.data() << " | " << ui.line2.data() << " | " << ui.line3.data() << '\n';

  engine.noteDown(0, 96, 10); dump(engine, "direct Debussy triad");
  engine.noteUp(0, 20); dump(engine, "snapshot release");

  (void)engine.setProfile(ProfileId::Falcon);
  engine.shiftDown(30); engine.encoderTurn(1, 7, 31); engine.shiftUp(60);
  dump(engine, "Falcon configurable CC routing");

  shiftClick(engine, 100); shiftClick(engine, 200);
  engine.noteDown(4, 105, 240); engine.noteUp(4, 250);
  std::cout << "HOLD=" << (engine.hold() ? "ON" : "OFF") << " source degree 4=" << engine.sourceActive(4) << '\n';
  dump(engine, "held source output");

  engine.assignFx(0, FxType::Arp, FxMode::Latch); engine.fxDown(0, 300); engine.tick(550);
  dump(engine, "latch ARP frames");

  engine.panic(600); dump(engine, "PANIC recovery (fresh queue)");
  engine.tick(100000); dump(engine, "post-panic future tick (must stay empty)");
  std::cout << "Final HOLD=" << engine.hold() << " FX=" << unsigned(engine.activeFx()) << '\n';
  return 0;
}
