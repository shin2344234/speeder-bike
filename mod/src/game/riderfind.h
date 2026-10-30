#pragma once
#include <cstdint>

// The words riderfix edits in Kliff's riding charts, found by what they are
// (an action's key, the branch an action loops on) as each chart loads, so
// the same search works on the shipped charts, on Broomy's with nodes
// spliced in, and on the next build. speeder/research/rider_words.py does
// the same search in Python; `build.bat riderfind CHARTS` checks that both
// find the same words in the shipped charts.
namespace bm::riderfind
{
    // A word of a chart: its offset, the shipped value and the one riderfix
    // writes.
    struct Swap { uint32_t offset, shipped, now; };

    // The push-off clip's path as shipped, the name riderfix writes in its
    // room (zero-padded), and the time the hand IK events get so they never
    // fire. Broomy writes the same into the same buffer as the chart loads,
    // so FindLower takes either state: whichever mod sees the chart second
    // finds the first one's edits already in.
    inline constexpr char kPushOffOld[] = "1_pc/1_phm/00_riding/cd_phm_rd_wyvern_basic_00_00_air_move_fall_walk_end_00.paa";
    inline constexpr char kPushOffNew[] = "1_pc/1_phm/00_riding/cd_phm_rd_broom_basic_00_00_nor_std_takeoff_00.paa";
    static_assert(sizeof kPushOffNew <= sizeof kPushOffOld, "the new name must fit the old one's room");
    inline constexpr uint32_t kIkNever = 0x447A0000;   // 1000.0f

    // ride_test3_lower.paac. `swaps` go in while the speeder is ridden and
    // come out after; the hand IK instances (their time word) and the
    // push-off path (after its length byte) change once, as the chart loads.
    constexpr int kLowerSwaps = 11, kIkMax = 4;
    struct Lower
    {
        Swap swaps[kLowerSwaps];
        uint32_t ik[kIkMax];
        int ikCount;
        uint32_t pushOff;
        uint32_t ground, air, takeoffSlot, loop, loopSlot, exit;   // the indexes, for the log and the check
    };
    bool FindLower(const uint8_t* data, uint32_t size, Lower& out, const char*& why);

    // ride_upper.paac. `swaps` go in while the speeder is ridden; the donor
    // record and `donor` change once.
    constexpr int kUpperSwaps = 2;
    struct Upper
    {
        uint32_t idle, donorRecord;   // the two action records
        Swap swaps[kUpperSwaps];
        Swap donor[2];
        uint32_t dismount;            // the branch checked only from the broom's dismounts
        uint32_t loop, dismountIndex;
    };
    bool FindUpper(const uint8_t* data, uint32_t size, Upper& out, const char*& why);

    // A RideOn chart built from the Wyvern's m0004_ride_dragon_upper.paac:
    // the branches of its ground idle F5150415, by offset. riderfix told the
    // mounts apart by these until 29 October; only the riderfind check
    // calls it now.
    constexpr int kIdleMax = 8;
    bool FindRideOnIdle(const uint8_t* data, uint32_t size, uint32_t* at, int& count, const char*& why);

    // The bytes riderfix copies from the broom idle's action record over the
    // donor's: all of it but these, which stay the donor's.
    struct Field { uint32_t at, size; };
    extern const Field kDonorKeeps[3];
    constexpr uint32_t kSpeederUpper = 0xEFD5260B;
}
