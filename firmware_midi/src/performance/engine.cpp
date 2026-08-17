#include "performance/engine.hpp"
#include "controls/control_surface.hpp"
#include "music/harmony.hpp"
#include "profiles/instrument_profiles.hpp"
#include <algorithm>

namespace amen {
PerformanceEngine::PerformanceEngine() noexcept {
  harmony_.root = 60; harmony_.range = 3; harmony_.shape = ChordShape::Triad;
  for (uint8_t i = 0; i < kFxPadCount; ++i) { fx_[i].type = static_cast<FxType>(i + 1); fx_[i].mode = FxMode::Gate; }
  const auto& profiles = instrumentProfiles();
  for (std::size_t i = 0; i < profiles.size(); ++i) profileCc_[i] = profiles[i].cc;
}
bool PerformanceEngine::setProfile(ProfileId profile) noexcept {
  if (static_cast<std::size_t>(profile) >= profileCc_.size()) return false;
  profile_ = profile; return true;
}
void PerformanceEngine::setProfileCc(ProfileId profile, uint8_t encoder, uint8_t cc) noexcept {
  const auto index = static_cast<std::size_t>(profile);
  if (index < profileCc_.size() && encoder < 7) profileCc_[index][encoder] = cc;
}
uint8_t PerformanceEngine::profileCc(ProfileId profile, uint8_t encoder) const noexcept {
  const auto index = static_cast<std::size_t>(profile);
  return index < profileCc_.size() && encoder < 7 ? profileCc_[index][encoder] : 0;
}
PerformanceEngine::Source* PerformanceEngine::findDegree(uint8_t degree) noexcept {
  for (auto& source : sources_) if (source.active && source.degree == degree % 12) return &source;
  return nullptr;
}
PerformanceEngine::Source* PerformanceEngine::allocate(uint8_t degree) noexcept {
  for (auto& source : sources_) {
    if (source.active) continue;
    source = {}; source.active = true; source.degree = degree % 12;
    source.token = {nextToken_++};
    if (nextToken_ == 0) nextToken_ = 1; // uint64 wrap cannot create the reserved invalid token.
    return &source;
  }
  return nullptr;
}
bool PerformanceEngine::anySource() const noexcept { for (const auto& source : sources_) if (source.active) return true; return false; }
uint8_t PerformanceEngine::maxActiveNoteCount() const noexcept {
  uint8_t count = 1;
  for (const auto& source : sources_) if (source.active) count = std::max(count, source.notes.count);
  return count;
}
void PerformanceEngine::noteDown(uint8_t degree, uint8_t velocity, uint32_t now) noexcept {
  degree %= 12;
  if (auto* old = findDegree(degree)) {
    if (old->physical) return;
    release(*old, now);
  }
  const bool hadSources = anySource();
  auto* source = allocate(degree); if (source == nullptr) return;
  source->physical = true; source->held = hold_; source->velocity = velocity; source->snapshot = harmony_; source->notes = makeVoicing(source->snapshot, degree);
  if (activeFx_ == FxType::Blank) {
    source->rendered = source->notes;
    source->renderedVelocity = velocity;
    acquireRendered(*source, now);
    return;
  }
  if (!hadSources || !algorithm_.active() || activeFx_ == FxType::Strum) algorithm_.start(activeFx_, now);
  const uint16_t interval = static_cast<uint16_t>(60000U / bpm_ / 2U);
  applyAlgorithmFrame(algorithm_.tick(now, interval, maxActiveNoteCount()), now);
}
void PerformanceEngine::noteUp(uint8_t degree, uint32_t now) noexcept {
  if (auto* source = findDegree(degree % 12)) { source->physical = false; if (!source->held) release(*source, now); }
}
void PerformanceEngine::noteUp(SourceToken token, uint32_t now) noexcept {
  if (token.value == 0) return;
  for (auto& source : sources_) if (source.active && source.token == token) { source.physical = false; if (!source.held) release(source, now); return; }
}
SourceToken PerformanceEngine::currentToken(uint8_t degree) const noexcept {
  for (const auto& source : sources_) if (source.active && source.degree == degree % 12) return source.token;
  return {};
}
void PerformanceEngine::release(Source& source, uint32_t now) noexcept {
  registry_.releaseOwner(source.token, events_, now); source = {};
}
bool PerformanceEngine::sourceActive(uint8_t degree) const noexcept {
  for (const auto& source : sources_) if (source.active && source.degree == degree % 12) return true;
  return false;
}
bool PerformanceEngine::assignFx(uint8_t pad, FxType type, FxMode mode) noexcept {
  if (pad >= kFxPadCount || static_cast<uint8_t>(type) > static_cast<uint8_t>(FxType::Velocity) ||
      static_cast<uint8_t>(mode) > static_cast<uint8_t>(FxMode::Latch)) return false;
  fx_[pad] = {}; fx_[pad].type = type; fx_[pad].mode = mode;
  if (activeFxPad_ == static_cast<int8_t>(pad)) chooseFx(0);
  return true;
}
void PerformanceEngine::closeSources(uint32_t now) noexcept {
  for (auto& source : sources_) if (source.active) {
    registry_.releaseOwner(source.token, events_, now);
    source.rendered = {};
  }
  syncPending_ = false;
}
void PerformanceEngine::startAndEnterFx(FxType type, uint32_t now, bool emitInitial) noexcept {
  activeFx_ = type;
  if (type == FxType::Blank) { algorithm_.cancel(); return; }
  algorithm_.start(type, now);
  if (emitInitial && anySource()) {
    const uint16_t interval = static_cast<uint16_t>(60000U / bpm_ / 2U);
    applyAlgorithmFrame(algorithm_.tick(now, interval, maxActiveNoteCount()), now);
  }
}
void PerformanceEngine::chooseFx(uint32_t now) noexcept {
  int8_t winner = -1; uint64_t order = 0;
  for (uint8_t i = 0; i < kFxPadCount; ++i) {
    const bool eligible = fx_[i].mode == FxMode::Gate ? fx_[i].physicalDown : fx_[i].latched;
    if (eligible && fx_[i].pressOrder >= order) { order = fx_[i].pressOrder; winner = static_cast<int8_t>(i); }
  }
  if (winner == activeFxPad_) return;
  const bool wasDirect = activeFxPad_ < 0;
  const bool becomesDirect = winner < 0;
  if (wasDirect && !becomesDirect) closeSources(now);
  activeFxPad_ = winner;
  if (becomesDirect) { activeFx_ = FxType::Blank; algorithm_.cancel(); restoreSources(now); return; }
  const FxType next = fx_[static_cast<uint8_t>(winner)].type;
  const bool enteringStrum = !wasDirect && activeFx_ != FxType::Strum && next == FxType::Strum;
  if (enteringStrum) closeSources(now);
  // FX->FX never bridges through direct notes; Strum replaces old ownership with its first accumulated frame.
  startAndEnterFx(next, now, wasDirect || enteringStrum);
}
void PerformanceEngine::fxDown(uint8_t pad, uint32_t now) noexcept {
  if (pad >= kFxPadCount) return;
  auto& slot = fx_[pad]; slot.physicalDown = true; slot.pressOrder = nextPressOrder_++;
  if (nextPressOrder_ == 0) nextPressOrder_ = 1;
  if (slot.mode == FxMode::Latch) slot.latched = !slot.latched;
  chooseFx(now);
}
void PerformanceEngine::fxUp(uint8_t pad, uint32_t now) noexcept {
  if (pad >= kFxPadCount) return;
  fx_[pad].physicalDown = false; chooseFx(now);
}
void PerformanceEngine::shiftDown(uint32_t now) noexcept { if (!shift_) { shift_ = true; shiftDownAt_ = now; } }
void PerformanceEngine::shiftUp(uint32_t now) noexcept {
  if (!shift_) return;
  shift_ = false;
  if (now - shiftDownAt_ < kDebounceMs) return;
  if (haveFirstClick_ && now - lastShiftClick_ <= doubleClickWindow_) { haveFirstClick_ = false; toggleHold(now); }
  else { haveFirstClick_ = true; lastShiftClick_ = now; }
}
void PerformanceEngine::toggleHold(uint32_t now) noexcept {
  hold_ = !hold_;
  if (hold_) { for (auto& source : sources_) if (source.active && source.physical) source.held = true; return; }
  for (auto& source : sources_) if (source.active) source.held = false;
  for (auto& source : sources_) if (source.active && !source.physical) release(source, now);
}
void PerformanceEngine::encoderTurn(uint8_t encoder, int delta, uint32_t now) noexcept {
  if (encoder < 1 || encoder > 7) return;
  if (shift_) {
    const uint8_t index = static_cast<uint8_t>(encoder - 1);
    ccValues_[index] = static_cast<uint8_t>(applyRelative(ccValues_[index], delta, 0, 127));
    const auto profileIndex = static_cast<std::size_t>(profile_);
    if (profileIndex < profileCc_.size()) (void)events_.push({MidiType::ControlChange, 1, profileCc_[profileIndex][index], ccValues_[index], now});
    return;
  }
  if (activeFx_ != FxType::Blank) {
    if (encoder == 7) bpm_ = static_cast<uint16_t>(applyRelative(bpm_, delta, 30, 300));
    else expression_ = static_cast<uint8_t>(applyRelative(expression_, delta, 0, 127));
    return;
  }
  switch (encoder) {
    case 1: harmony_.preset = static_cast<HarmonyPreset>(applyRelative(static_cast<int>(harmony_.preset), delta, 0, 6)); break;
    case 2: harmony_.root = static_cast<uint8_t>(applyRelative(harmony_.root, delta, 0, 127)); break;
    case 3: harmony_.variation = static_cast<uint8_t>(applyRelative(harmony_.variation, delta, 0, 7)); break;
    case 4: harmony_.range = static_cast<uint8_t>(applyRelative(harmony_.range, delta, 0, 6)); break;
    case 5: harmony_.shape = static_cast<ChordShape>(applyRelative(static_cast<int>(harmony_.shape), delta, 0, 8)); break;
    case 6: expression_ = static_cast<uint8_t>(applyRelative(expression_, delta, 0, 127)); break;
    case 7: bpm_ = static_cast<uint16_t>(applyRelative(bpm_, delta, 30, 300)); break;
    default: break;
  }
}
void PerformanceEngine::encoderClick(uint8_t encoder, uint32_t now) noexcept { if (shift_ && encoder == 7) panic(now); }
void PerformanceEngine::restoreSources(uint32_t now) noexcept {
  for (auto& source : sources_) if (source.active) {
    registry_.releaseOwner(source.token, events_, now);
    source.rendered = source.notes;
    source.renderedVelocity = source.velocity;
    acquireRendered(source, now);
  }
}
void PerformanceEngine::appendRendered(Source& source, uint8_t note) noexcept {
  for (uint8_t i = 0; i < source.rendered.count; ++i)
    if (source.rendered.notes[i] == note) return;
  if (source.rendered.count < source.rendered.notes.size())
    source.rendered.notes[source.rendered.count++] = note;
}
void PerformanceEngine::acquireRendered(Source& source, uint32_t now) noexcept {
  for (uint8_t i = 0; i < source.rendered.count; ++i) {
    const uint8_t note = source.rendered.notes[i];
    if (registry_.ownedBy(note, source.token)) continue;
    if (!registry_.acquire(note, source.token, events_, now, source.renderedVelocity)) syncPending_ = true;
  }
}
void PerformanceEngine::servicePending(uint32_t now) noexcept {
  syncPending_ = false;
  for (auto& source : sources_) if (source.active) acquireRendered(source, now);
}
void PerformanceEngine::applyAlgorithmFrame(const AlgorithmFrame& frame, uint32_t now) noexcept {
  if (!frame.emit) return;
  for (auto& source : sources_) if (source.active) {
    if (activeFx_ != FxType::Strum) {
      registry_.releaseOwner(source.token, events_, now);
      source.rendered = {};
    }
    source.renderedVelocity = frame.velocity;
    if (activeFx_ == FxType::TranceGate && !frame.gateOpen) continue;
    if (activeFx_ == FxType::NoteRepeat || activeFx_ == FxType::Velocity || activeFx_ == FxType::TranceGate) {
      source.rendered = source.notes;
    } else if (frame.noteIndex >= 0) {
      const uint8_t index = static_cast<uint8_t>(frame.noteIndex);
      const bool cyclesNotes = activeFx_ == FxType::Arp || activeFx_ == FxType::Random;
      if (!cyclesNotes && index >= source.notes.count) continue;
      const uint8_t renderedIndex = cyclesNotes ? static_cast<uint8_t>(index % source.notes.count) : index;
      source.renderedVelocity = expression_;
      appendRendered(source, source.notes.notes[renderedIndex]);
    }
    acquireRendered(source, now);
  }
}
void PerformanceEngine::tick(uint32_t now) noexcept {
  if (syncPending_) { servicePending(now); return; }
  if (!algorithm_.active() || !anySource()) return;
  const uint16_t interval = static_cast<uint16_t>(60000U / bpm_ / 2U);
  applyAlgorithmFrame(algorithm_.tick(now, interval, maxActiveNoteCount()), now);
}
void PerformanceEngine::panic(uint32_t now) noexcept {
  registry_.panic(events_, now);
  for (auto& source : sources_) source = {};
  for (auto& slot : fx_) { slot.physicalDown = false; slot.latched = false; slot.pressOrder = 0; }
  activeFx_ = FxType::Blank; activeFxPad_ = -1; algorithm_.cancel(); hold_ = false; haveFirstClick_ = false; shift_ = false; syncPending_ = false;
}
}
