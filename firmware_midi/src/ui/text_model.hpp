#pragma once
#include "performance/engine.hpp"
#include <array>
namespace amen { struct UiTextModel {std::array<char,32> line1{},line2{},line3{},line4{};}; UiTextModel makeUiText(const PerformanceEngine& engine) noexcept; }
