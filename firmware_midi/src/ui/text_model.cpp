#include "ui/text_model.hpp"
#include "music/harmony.hpp"
#include "profiles/instrument_profiles.hpp"
#include <cstdio>
namespace amen { UiTextModel makeUiText(const PerformanceEngine& e)noexcept{UiTextModel m{};auto h=e.harmony();std::snprintf(m.line1.data(),m.line1.size(),"%s",harmonyCatalog()[static_cast<std::size_t>(h.preset)].name);std::snprintf(m.line2.data(),m.line2.size(),"ROOT %u  %s",h.root,chordShapeName(h.shape));std::snprintf(m.line3.data(),m.line3.size(),"%s  BPM %u",instrumentProfiles()[static_cast<std::size_t>(e.profile())].name,e.bpm());std::snprintf(m.line4.data(),m.line4.size(),"HOLD %s  FX %u",e.hold()?"ON":"OFF",static_cast<unsigned>(e.activeFx()));return m;} }
