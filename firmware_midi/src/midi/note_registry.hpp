#pragma once
#include "common/types.hpp"
namespace amen {
class NoteRegistry { public: bool acquire(uint8_t note,SourceToken owner,EventBuffer& out,uint32_t now,uint8_t velocity) noexcept; void release(uint8_t note,SourceToken owner,EventBuffer& out,uint32_t now) noexcept; void releaseOwner(SourceToken owner,EventBuffer& out,uint32_t now) noexcept; void panic(EventBuffer& out,uint32_t now) noexcept; uint8_t owners(std::size_t note)const noexcept{return note < counts_.size() ? counts_[note] : 0;} bool ownedBy(uint8_t note,SourceToken owner)const noexcept;
 private: std::array<uint8_t,128> counts_{}; std::array<std::array<SourceToken,kMaxSources>,128> owners_{};
}; }
