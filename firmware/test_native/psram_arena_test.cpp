#include "psram_arena.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" {
uint8_t external_psram_size = 0;
}

namespace {
constexpr std::uintptr_t kExtmemBase = 0x70000000U;
constexpr std::size_t kBytesPerMib = 1024U * 1024U;

void* returnedAllocation = nullptr;
void* freedAllocation = nullptr;
std::size_t requestedBytes = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void resetAllocator(void* allocation) {
    returnedAllocation = allocation;
    freedAllocation = nullptr;
    requestedBytes = 0;
}

void testDefaultCapacityLeavesHeadroom() {
    static_assert(PsramArena::kDefaultCapacityBytes == 7U * kBytesPerMib,
                  "default arena payload must reserve one MiB of installed PSRAM");
}

void testInternalFallbackIsRejectedAndFreed() {
    external_psram_size = 8;
    void* fallback = reinterpret_cast<void*>(0x20200000U);
    resetAllocator(fallback);

    PsramArena arena;
    require(!arena.begin(4096), "internal extmem_malloc fallback must be rejected");
    require(arena.error() == PsramArena::Error::AllocationFailed,
            "fallback rejection must preserve allocation diagnostics");
    require(!arena.ready(), "fallback rejection must clear the arena pointer");
    require(arena.capacitySamples() == 0, "failed allocation must retain zero capacity");
    require(freedAllocation == fallback, "fallback allocation must be released with extmem_free");
}

void testAllocationCrossingInstalledPsramEndIsRejected() {
    external_psram_size = 8;
    void* crossing = reinterpret_cast<void*>(kExtmemBase + 8U * kBytesPerMib - 1024U);
    resetAllocator(crossing);

    PsramArena arena;
    require(!arena.begin(2048), "allocation extending past installed PSRAM must be rejected");
    require(arena.error() == PsramArena::Error::AllocationFailed,
            "out-of-range allocation must report allocation failure");
    require(arena.capacitySamples() == 0, "out-of-range allocation must retain zero capacity");
    require(freedAllocation == crossing, "out-of-range allocation must be released");
}

void testAllocationFullyInsideInstalledPsramIsAccepted() {
    external_psram_size = 8;
    void* external = reinterpret_cast<void*>(kExtmemBase + 4096U);
    resetAllocator(external);

    PsramArena arena;
    require(arena.begin(4096), "allocation inside installed PSRAM must be accepted");
    require(arena.ready(), "accepted external allocation must make arena ready");
    require(arena.error() == PsramArena::Error::None,
            "accepted external allocation must clear diagnostics");
    require(arena.capacityBytes() == 4096, "accepted allocation must expose requested capacity");
    require(requestedBytes == 4096, "arena must request the full configured payload");
    require(freedAllocation == nullptr, "accepted external allocation must not be freed");
}
}  // namespace

extern "C" void* extmem_malloc(std::size_t size) {
    requestedBytes = size;
    return returnedAllocation;
}

extern "C" void extmem_free(void* ptr) {
    freedAllocation = ptr;
}

int main() {
    testDefaultCapacityLeavesHeadroom();
    testInternalFallbackIsRejectedAndFreed();
    testAllocationCrossingInstalledPsramEndIsRejected();
    testAllocationFullyInsideInstalledPsramIsAccepted();
    std::cout << "All PSRAM arena tests passed\n";
    return 0;
}
