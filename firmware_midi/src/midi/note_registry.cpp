#include "midi/note_registry.hpp"

namespace amen {
bool NoteRegistry::ownedBy(uint8_t note, SourceToken owner) const noexcept {
  if (note >= counts_.size() || owner.value == 0) return false;
  for (uint8_t i = 0; i < counts_[note]; ++i)
    if (owners_[note][i] == owner) return true;
  return false;
}

bool NoteRegistry::acquire(uint8_t note, SourceToken owner, EventBuffer& out, uint32_t now, uint8_t velocity) noexcept {
  if (note >= counts_.size() || owner.value == 0) return false;
  auto& count = counts_[note];
  for (uint8_t i = 0; i < count; ++i) if (owners_[note][i] == owner) return false;
  if (count >= kMaxSources) return false;
  // The first owner is committed only if its externally visible NoteOn is queued.
  if (count == 0 && !out.push({MidiType::NoteOn, 1, note, velocity, now})) return false;
  owners_[note][count++] = owner;
  return true;
}

void NoteRegistry::release(uint8_t note, SourceToken owner, EventBuffer& out, uint32_t now) noexcept {
  if (note >= counts_.size()) return;
  auto& count = counts_[note];
  for (uint8_t i = 0; i < count; ++i) {
    if (!(owners_[note][i] == owner)) continue;
    owners_[note][i] = owners_[note][count - 1];
    --count;
    if (count == 0) (void)out.pushCritical({MidiType::NoteOff, 1, note, 0, now});
    return;
  }
}

void NoteRegistry::releaseOwner(SourceToken owner, EventBuffer& out, uint32_t now) noexcept {
  for (uint16_t note = 0; note < 128; ++note) release(static_cast<uint8_t>(note), owner, out, now);
}

void NoteRegistry::panic(EventBuffer& out, uint32_t now) noexcept {
  // Panic owns a fresh queue: recovery cannot be crowded out by stale normal events.
  out.clear();
  for (uint16_t note = 0; note < 128; ++note) {
    if (counts_[note] != 0) (void)out.pushCritical({MidiType::NoteOff, 1, static_cast<uint8_t>(note), 0, now});
    counts_[note] = 0;
  }
  // Channel 1 is currently the only routed note channel. CC123 covers any receiver drift.
  (void)out.pushCritical({MidiType::ControlChange, 1, 123, 0, now});
}
}
