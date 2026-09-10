#pragma once

#include <array>
#include <cstdint>

namespace amen {

// General MIDI Level 1 percussion map (MIDI notes 35-81), selected on the 20 pads.
static constexpr std::array<uint8_t, 20> kGmDrumNotes{{
    36, 38, 39, 40, 42, 44, 46, 48, 49, 50,
    51, 53, 54, 55, 56, 57, 59, 60, 63, 64
}};

static constexpr std::array<const char*, 20> kGmDrumLabels{{
    "BD", "SD", "CP", "ES", "CH", "PH", "OH", "MT", "CR", "HT",
    "RD", "RB", "TB", "SP", "CB", "C2", "RC", "BG", "OC", "LC"
}};

constexpr const char* gmDrumLabel(uint8_t key) noexcept {
    return key < kGmDrumNotes.size() ? kGmDrumLabels[key] : "";
}

}