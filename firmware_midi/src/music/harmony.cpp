#include "music/harmony.hpp"

namespace amen {
namespace {
struct Recipe { int8_t second, third, fourth, fifth, sixth, seventh, ninth, eleventh, thirteenth; };
constexpr Recipe recipe(int third, int fifth, int seventh, int second = 2, int fourth = 5,
                        int sixth = 9, int ninth = 14, int eleventh = 17, int thirteenth = 21) {
  return {static_cast<int8_t>(second), static_cast<int8_t>(third), static_cast<int8_t>(fourth),
          static_cast<int8_t>(fifth), static_cast<int8_t>(sixth), static_cast<int8_t>(seventh),
          static_cast<int8_t>(ninth), static_cast<int8_t>(eleventh), static_cast<int8_t>(thirteenth)};
}
using RecipeRow = std::array<Recipe, 12>;

// Explicit degree harmonizations. Artistic rows are deterministic prototypes, not listening-validated claims.
constexpr std::array<RecipeRow, 7> kRecipes{{
  RecipeRow{{recipe(4,7,11),recipe(3,7,10),recipe(3,7,10),recipe(4,7,11),recipe(4,7,10),recipe(3,7,10),recipe(3,6,10),recipe(4,7,11),recipe(3,7,10),recipe(3,7,10),recipe(4,7,11),recipe(4,7,10)}},
  RecipeRow{{recipe(3,7,10),recipe(3,6,10),recipe(4,7,11),recipe(3,7,10),recipe(3,7,10),recipe(4,7,11),recipe(4,7,10),recipe(3,7,10),recipe(3,6,10),recipe(4,7,11),recipe(3,7,10),recipe(3,7,10)}},
  RecipeRow{{recipe(4,6,10,1,5,9,13,18,21),recipe(3,7,10,1),recipe(4,7,11,1),recipe(3,6,9,1),recipe(4,8,11,1),recipe(3,7,10,1),recipe(4,6,10,1),recipe(3,7,11,1),recipe(4,7,10,1),recipe(3,6,10,1),recipe(4,7,11,1),recipe(3,7,10,1)}},
  RecipeRow{{recipe(3,7,10,2,5,9,14,17,21),recipe(3,7,10),recipe(4,8,11),recipe(3,7,10),recipe(4,7,10),recipe(3,7,10),recipe(4,7,11),recipe(3,7,10),recipe(4,8,11),recipe(3,7,10),recipe(4,7,10),recipe(3,7,10)}},
  RecipeRow{{recipe(3,6,10,1,5,8,13,17,20),recipe(3,7,10,1),recipe(3,6,9,1),recipe(4,7,10,1),recipe(3,6,10,1),recipe(3,7,9,1),recipe(3,6,10,1),recipe(3,6,10,1),recipe(3,7,10,1),recipe(3,6,9,1),recipe(4,7,10,1),recipe(3,6,10,1)}},
  RecipeRow{{recipe(4,7,11,2,6,9,14,18,21),recipe(4,7,10,2,6),recipe(3,7,10,2,6),recipe(4,7,11,2,6,9,14,19,21),recipe(4,7,11,2,6),recipe(4,7,10,2,6),recipe(3,6,10,2,6),recipe(4,7,11,2,6),recipe(4,7,10,2,6),recipe(3,7,10,2,6),recipe(4,7,11,2,6),recipe(4,7,10,2,6)}},
  RecipeRow{{recipe(4,7,11,2,5,9,14,19,21),recipe(3,7,10,2,5,9,14,19,21),recipe(4,7,11,2,5,9,14,19,21),recipe(4,7,10,2,5,9,14,19,21),recipe(4,7,11,2,5,9,14,19,21),recipe(3,7,10,2,5,9,14,19,21),recipe(4,7,11,2,5,9,14,19,21),recipe(4,7,10,2,5,9,14,19,21),recipe(4,7,11,2,5,9,14,19,21),recipe(3,7,10,2,5,9,14,19,21),recipe(4,7,11,2,5,9,14,19,21),recipe(4,7,10,2,5,9,14,19,21)}}
}};
constexpr std::array<uint8_t, 9> kCounts{{1,3,3,3,4,4,5,6,6}};
constexpr std::array<uint8_t, 7> kVariationCounts{{2,2,2,3,3,3,3}};
constexpr const char* kVariationNames[7][3] = {
  {"Close", "First inversion", ""}, {"Close", "First inversion", ""}, {"Cluster", "Open", ""},
  {"Wide", "Rising", "Drop 2"}, {"Low", "Inverted", "Drop 2"},
  {"Planed", "First inversion", "Open fifth"}, {"Cloud", "First inversion", "Drop 2"}
};
void insertionSort(NoteSet& set) noexcept {
  for (uint8_t i = 1; i < set.count; ++i) {
    const uint8_t value = set.notes[i]; uint8_t j = i;
    while (j > 0 && set.notes[j - 1] > value) { set.notes[j] = set.notes[j - 1]; --j; }
    set.notes[j] = value;
  }
}
int boundedMidi(int value) noexcept { while (value < 0) value += 12; while (value > 127) value -= 12; return value; }
}

const std::array<PresetInfo, 7>& harmonyCatalog() noexcept {
  static constexpr std::array<PresetInfo, 7> catalog{{
    {"Major Basic", {0,2,4,5,7,9,11,12,14,16,17,19}}, {"Minor Basic", {0,2,3,5,7,8,10,12,14,15,17,19}},
    {"Chromatic", {0,1,2,3,4,5,6,7,8,9,10,11}}, {"Cinematic", {0,2,3,5,7,9,10,12,14,15,17,19}},
    {"Dark", {0,1,3,5,6,7,10,12,13,15,17,18}}, {"Debussy / Impressionist", {0,2,4,6,7,9,11,12,14,16,18,19}},
    {"Ambient", {0,2,4,7,9,11,14,16,19,21,23,26}}
  }};
  return catalog;
}
const char* chordShapeName(ChordShape shape) noexcept {
  static constexpr const char* names[] = {"NOTE","SUS2","TRIAD","SUS4","SIXTH","SEVENTH","NINTH","ELEVENTH","THIRTEENTH"};
  const auto index = static_cast<std::size_t>(shape); return index < 9 ? names[index] : "?";
}
uint8_t variationCount(HarmonyPreset preset) noexcept {
  const auto index = static_cast<std::size_t>(preset); return index < kVariationCounts.size() ? kVariationCounts[index] : 0;
}
const char* variationName(HarmonyPreset preset, uint8_t variation) noexcept {
  const auto index = static_cast<std::size_t>(preset); return index < 7 && variation < kVariationCounts[index] ? kVariationNames[index][variation] : "?";
}
NoteSet makeVoicing(const HarmonySnapshot& snapshot, uint8_t degree) noexcept {
  const auto presetIndex = static_cast<std::size_t>(snapshot.preset) < 7 ? static_cast<std::size_t>(snapshot.preset) : 0;
  const auto shapeIndex = static_cast<std::size_t>(snapshot.shape) < 9 ? static_cast<std::size_t>(snapshot.shape) : 0;
  const auto& r = kRecipes[presetIndex][degree % 12];
  const std::array<int8_t, 6> intervalsByShape[9] = {
    {{0,0,0,0,0,0}}, {{0,r.second,r.fifth,0,0,0}}, {{0,r.third,r.fifth,0,0,0}}, {{0,r.fourth,r.fifth,0,0,0}},
    {{0,r.third,r.fifth,r.sixth,0,0}}, {{0,r.third,r.fifth,r.seventh,0,0}},
    {{0,r.third,r.fifth,r.seventh,r.ninth,0}}, {{0,r.third,r.fifth,r.seventh,r.ninth,r.eleventh}},
    {{0,r.third,r.fifth,r.seventh,r.ninth,r.thirteenth}}
  };
  NoteSet out{}; out.count = kCounts[shapeIndex];
  const int base = static_cast<int>(snapshot.root) + harmonyCatalog()[presetIndex].degrees[degree % 12] + (static_cast<int>(snapshot.range) - 3) * 12;
  for (uint8_t i = 0; i < out.count; ++i) out.notes[i] = static_cast<uint8_t>(boundedMidi(base + intervalsByShape[shapeIndex][i]));
  const uint8_t count = variationCount(static_cast<HarmonyPreset>(presetIndex));
  const uint8_t variation = count == 0 ? 0 : static_cast<uint8_t>(snapshot.variation % count);
  if (variation == 1 && out.count > 1) out.notes[0] = static_cast<uint8_t>(boundedMidi(out.notes[0] + 12));
  if (variation == 2 && out.count > 2) out.notes[out.count - 2] = static_cast<uint8_t>(boundedMidi(static_cast<int>(out.notes[out.count - 2]) - 12));
  insertionSort(out);
  return out;
}
bool voicingIsMidiBounded(const NoteSet& notes) noexcept {
  if (notes.count == 0 || notes.count > kMaxNotes) return false;
  for (uint8_t i = 1; i < notes.count; ++i) if (notes.notes[i] < notes.notes[i - 1]) return false;
  return true;
}
}
