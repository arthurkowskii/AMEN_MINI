#pragma once
#include <array>
#include <cstddef>
namespace amen::teensy {
inline constexpr std::array<int,5> kColumns{{0,1,2,3,4}}; // COL_SHIFT=4, SW21 only
inline constexpr std::array<int,5> kRows{{5,6,9,14,15}};
inline constexpr int kSda=18,kScl=19;
inline constexpr std::array<std::array<int,3>,7> kEncoders{{{{16,17,35}},{{22,24,36}},{{25,26,37}},{{27,28,38}},{{29,30,39}},{{31,32,40}},{{33,34,41}}}};
constexpr bool uniqueDigitalPins() noexcept {
 std::array<int,33> pins{};
 std::size_t n=0;
 for(int p:kColumns) pins[n++]=p;
 for(int p:kRows) pins[n++]=p;
 for(const auto& e:kEncoders) for(int p:e) pins[n++]=p;
 pins[n++]=kSda;
 pins[n++]=kScl;
 for(std::size_t i=0;i<n;++i) {
  for(std::size_t j=i+1;j<n;++j) {
   if(pins[i]==pins[j]) return false;
  }
 }
 return true;
}
static_assert(uniqueDigitalPins(),"AMEN MINI digital pin assignments must be unique");
}
