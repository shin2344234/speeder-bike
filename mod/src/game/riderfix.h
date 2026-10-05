#pragma once
#include <cstdint>

// Kliff's riding pose on the speeder. His lower riding chart,
// ride_test3_lower, enters the state with the same key as the mount's
// current state. The speeder's charts keep the Wyvern's state keys, and from
// those the lower chart's branch 397 (ground) and branch 400 (air) send
// every mount that is not a Wyvern to the big dragon's stand idle, a full
// body crouch. While the speeder is ridden the two branches point at the
// cut orca rider's 01B8F8D3 instead, which plays his speeder lean blend
// (riderfix.cpp says why that state). The speeder counts as ridden while its
// RideOn chart (the served tank chart) has been checked in the last 3
// seconds; that chart runs while it is ridden and on after Kliff gets off
// (GettingOff marks that). Otherwise the branches
// keep their shipped targets, so riding the dragon is unchanged, and
// Broomy's own copy of this fix points them at its broom idle while Broomy
// is ridden. His upper chart, which holds his left arm, goes from the broom
// idle to the cut orca rider's EFD5260B the same way, a step later than the
// lower, so its clip is loaded first.
//
// Both go through the branch check (+0x232E3D0). Install(true) also logs
// each chart layer's next action, for testing.
namespace bm::riderfix
{
    bool Install(bool logStates);
    // From broomy.cpp's chart patch: where each of Broomy's served charts is.
    void NoteServed(const char* path, uintptr_t base, uint32_t size);
    // Milliseconds since the speeder's RideOn chart was last checked, which
    // is every frame while it is ridden; a large number before the first.
    uint32_t MsSinceRidden();
    // From the start of Kliff's broom dismount until his upper riding layer
    // is back in another state, which it is when he mounts again.
    bool GettingOff();
}
