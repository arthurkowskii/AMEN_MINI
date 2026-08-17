#include "profiles/instrument_profiles.hpp"
namespace amen { const std::array<InstrumentProfile,4>& instrumentProfiles() noexcept { static constexpr std::array<InstrumentProfile,4> p{{
 {"Serum",{{"Macro 1","Macro 2","Macro 3","Macro 4","Filter","Expression","Reverb"}},{{16,17,18,19,74,11,91}}},
 {"Pigments",{{"Macro 1","Macro 2","Macro 3","Macro 4","Brightness","Expression","Space"}},{{20,21,22,23,74,11,91}}},
 {"Falcon",{{"Macro 1","Macro 2","Macro 3","Macro 4","Timbre","Expression","FX"}},{{24,25,26,27,74,11,92}}},
 {"Generic Orchestral",{{"Dynamics","Vibrato","Attack","Release","Brightness","Expression","Reverb"}},{{1,2,73,72,74,11,91}}}}}; return p; } }
