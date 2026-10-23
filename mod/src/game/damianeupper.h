#pragma once
#include <cstddef>

#include "game/damiane.h"

// Broomy 1.0.5's splices into ride_upper.paac, copied from its
// damianepatches.h. 1.0.5 rewrote the upper chart for the escort actions
// (368976 bytes where 1.0.3's was 356368), so the 1.0.3 layout in
// damianepatches.h no longer finds the speeder's words there, and the
// engine sound never started (Seth, 23 October 2026). Only the offsets are
// needed, so the blob is left out. Both layouts are tried, so an older
// Broomy keeps working.
namespace bm::damiane
{
    inline constexpr Splice kUpper105Splices[] = {
        { 0x0, 4, 0, 4 }, { 0x1FCC6, 0, 4, 242 }, { 0x1FDB8, 0, 246, 242 }, { 0x1FEE2, 0, 488, 2554 },
        { 0x2000C, 0, 3042, 2554 }, { 0x200F2, 0, 5596, 2350 }, { 0x201D8, 0, 7946, 2350 }, { 0x20DB4, 0, 10296, 2096 },
        { 0x2102E, 2, 12392, 2 }, { 0x25119, 0, 12394, 368 }, { 0x25147, 2, 12762, 2 }, { 0x25193, 0, 12764, 0 },
        { 0x25436, 2, 12764, 2 }, { 0x27872, 4, 12766, 4 }, { 0x27908, 2, 12770, 2 }, { 0x2878A, 4, 12772, 4 },
        { 0x28CEE, 4, 12776, 4 }, { 0x28D84, 2, 12780, 2 }, { 0x28F16, 4, 12782, 4 }, { 0x2BF9A, 4, 12786, 4 },
        { 0x2D97A, 4, 12790, 4 }, { 0x2E3C4, 2, 12794, 2 }, { 0x306C2, 4, 12796, 4 }, { 0x3499A, 4, 12800, 4 },
        { 0x34A30, 2, 12804, 2 }, { 0x34EFE, 4, 12806, 4 }, { 0x35462, 4, 12810, 4 }, { 0x369F2, 4, 12814, 4 },
        { 0x36A88, 2, 12818, 2 }, { 0x3790A, 4, 12820, 4 }, { 0x379A0, 2, 12824, 2 }, { 0x39DB2, 4, 12826, 4 },
        { 0x3D8FE, 4, 12830, 4 }, { 0x3F956, 4, 12834, 4 }, { 0x40FA6, 0, 12838, 1380 }, { 0x4299C, 4, 14218, 4 },
        { 0x49B60, 0, 14222, 156 }, { 0x49B74, 4, 14378, 4 }, { 0x535E8, 0, 14382, 224 },
    };

    inline constexpr Chart kUpper105 = { kCharts[0].path, 354460, 0xA72292A8, 368976, 0xD1728BF6, kUpper105Splices,
                                         sizeof kUpper105Splices / sizeof kUpper105Splices[0], nullptr };
}
