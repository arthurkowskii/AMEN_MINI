#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace amen {
constexpr std::size_t kPadCount = 12, kFxPadCount = 8, kMaxSources = 32, kMaxNotes = 6, kMaxEvents = 256;
enum class ChordShape : uint8_t { Note, Sus2, Triad, Sus4, Sixth, Seventh, Ninth, Eleventh, Thirteenth, Count };
enum class HarmonyPreset : uint8_t { MajorBasic, MinorBasic, Chromatic, Cinematic, Dark, Debussy, Ambient, Count };
enum class FxType : uint8_t { Blank, Strum, Arp, RunUp, RunDown, TranceGate, NoteRepeat, Random, Velocity };
enum class FxMode : uint8_t { Gate, Latch };
enum class ProfileId : uint8_t { Serum, Pigments, Falcon, GenericOrchestral, Count };
enum class MidiType : uint8_t { NoteOn, NoteOff, ControlChange };
struct MidiEvent { MidiType type{}; uint8_t channel{1}, data1{}, data2{}; uint32_t atMs{}; };

class EventBuffer {
 public:
  bool push(MidiEvent event) noexcept {
    if (size_ == events_.size()) return false;
    events_[size_++] = event;
    return true;
  }
  bool pushCritical(MidiEvent event) noexcept {
    if (push(event)) return true;
    std::size_t victim = size_;
    for (std::size_t i = 0; i < size_; ++i) {
      if (events_[i].type != MidiType::NoteOff) { victim = i; break; }
    }
    if (victim == size_) victim = 0; // A saturated critical queue stays bounded and keeps the newest recovery.
    for (std::size_t i = victim + 1; i < size_; ++i) events_[i - 1] = events_[i];
    events_[size_ - 1] = event;
    return true;
  }
  void clear() noexcept { size_ = 0; }
  std::size_t size() const noexcept { return size_; }
  const MidiEvent& operator[](std::size_t i) const noexcept { return events_[i]; }
 private:
  std::array<MidiEvent, kMaxEvents> events_{};
  std::size_t size_{};
};

// Zero is permanently invalid. Tokens are process-lifetime identities, not pad generations.
struct SourceToken {
  uint64_t value{};
  friend constexpr bool operator==(SourceToken a, SourceToken b) noexcept { return a.value == b.value; }
};
struct HarmonySnapshot { HarmonyPreset preset{}; ChordShape shape{}; uint8_t root{60}, variation{}, range{3}; };
struct NoteSet { std::array<uint8_t, kMaxNotes> notes{}; uint8_t count{}; };
}
