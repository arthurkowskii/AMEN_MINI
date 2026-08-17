#pragma once
#include "common/types.hpp"
namespace amen {
struct PresetInfo { const char* name; std::array<int8_t, 12> degrees; };
const std::array<PresetInfo, 7>& harmonyCatalog() noexcept;
const char* chordShapeName(ChordShape shape) noexcept;
uint8_t variationCount(HarmonyPreset preset) noexcept;
const char* variationName(HarmonyPreset preset, uint8_t variation) noexcept;
NoteSet makeVoicing(const HarmonySnapshot& snapshot, uint8_t degree) noexcept;
bool voicingIsMidiBounded(const NoteSet& notes) noexcept;
}
