#pragma once
#include "algorithms/algorithms.hpp"
#include "common/types.hpp"
#include "midi/note_registry.hpp"

namespace amen {
class PerformanceEngine {
 public:
  PerformanceEngine() noexcept;
  void noteDown(uint8_t degree, uint8_t velocity, uint32_t now) noexcept;
  void noteUp(uint8_t degree, uint32_t now) noexcept;
  void noteUp(SourceToken token, uint32_t now) noexcept;
  SourceToken currentToken(uint8_t degree) const noexcept;
  void fxDown(uint8_t pad, uint32_t now) noexcept;
  void fxUp(uint8_t pad, uint32_t now) noexcept;
  bool assignFx(uint8_t pad, FxType type, FxMode mode) noexcept;
  void shiftDown(uint32_t now) noexcept;
  void shiftUp(uint32_t now) noexcept;
  void encoderTurn(uint8_t encoder, int delta, uint32_t now) noexcept;
  void encoderClick(uint8_t encoder, uint32_t now) noexcept;
  void tick(uint32_t now) noexcept;
  void servicePending(uint32_t now) noexcept;
  void panic(uint32_t now) noexcept;
  const EventBuffer& events() const noexcept { return events_; }
  EventBuffer& mutableEventsForTest() noexcept { return events_; }
  void clearEvents() noexcept { events_.clear(); }
  HarmonySnapshot harmony() const noexcept { return harmony_; }
  bool hold() const noexcept { return hold_; }
  bool sourceActive(uint8_t degree) const noexcept;
  FxType activeFx() const noexcept { return activeFx_; }
  ProfileId profile() const noexcept { return profile_; }
  uint8_t ccValue(uint8_t index) const noexcept { return index < ccValues_.size() ? ccValues_[index] : 0; }
  uint16_t bpm() const noexcept { return bpm_; }
  uint8_t expression() const noexcept { return expression_; }
  bool setProfile(ProfileId profile) noexcept;
  void setProfileCc(ProfileId profile, uint8_t encoder, uint8_t cc) noexcept;
  uint8_t profileCc(ProfileId profile, uint8_t encoder) const noexcept;
  void setDoubleClickWindow(uint16_t ms) noexcept { doubleClickWindow_ = ms; }
  const AlgorithmScheduler& algorithm() const noexcept { return algorithm_; }
  bool syncPending() const noexcept { return syncPending_; }
  uint8_t noteOwners(std::size_t note) const noexcept { return registry_.owners(note); }
 private:
  struct Source { bool active{}, physical{}, held{}; uint8_t degree{}, velocity{100}, renderedVelocity{100}; SourceToken token{}; HarmonySnapshot snapshot{}; NoteSet notes{}, rendered{}; };
  struct FxSlot { FxType type{FxType::Blank}; FxMode mode{FxMode::Gate}; bool physicalDown{}, latched{}; uint64_t pressOrder{}; };
  Source* findDegree(uint8_t degree) noexcept;
  Source* allocate(uint8_t degree) noexcept;
  bool anySource() const noexcept;
  uint8_t maxActiveNoteCount() const noexcept;
  void release(Source& source, uint32_t now) noexcept;
  void chooseFx(uint32_t now) noexcept;
  void closeSources(uint32_t now) noexcept;
  void restoreSources(uint32_t now) noexcept;
  void acquireRendered(Source& source, uint32_t now) noexcept;
  static void appendRendered(Source& source, uint8_t note) noexcept;
  void applyAlgorithmFrame(const AlgorithmFrame& frame, uint32_t now) noexcept;
  void startAndEnterFx(FxType type, uint32_t now, bool emitInitial) noexcept;
  void toggleHold(uint32_t now) noexcept;

  HarmonySnapshot harmony_{};
  NoteRegistry registry_{};
  EventBuffer events_{};
  std::array<Source, kMaxSources> sources_{};
  uint64_t nextToken_{1};
  std::array<FxSlot, kFxPadCount> fx_{};
  uint64_t nextPressOrder_{1};
  FxType activeFx_{FxType::Blank};
  int8_t activeFxPad_{-1};
  AlgorithmScheduler algorithm_{};
  ProfileId profile_{ProfileId::Serum};
  std::array<std::array<uint8_t, 7>, static_cast<std::size_t>(ProfileId::Count)> profileCc_{};
  std::array<uint8_t, 7> ccValues_{{64,64,64,64,64,100,40}};
  uint16_t bpm_{120};
  uint8_t expression_{100};
  bool shift_{};
  uint32_t shiftDownAt_{}, lastShiftClick_{};
  bool haveFirstClick_{};
  uint16_t doubleClickWindow_{350};
  static constexpr uint16_t kDebounceMs = 20;
  bool hold_{};
  bool syncPending_{};
};
}
