#include "game/riderfix.h"

#include <Windows.h>
#include <cstring>

#include "core/log.h"
#include "game/damiane.h"
#include "game/farhook.h"
#include "game/gamefile.h"
#include "game/mem.h"

namespace
{
    // The branch check: (chart component, u32* error, u8 layer, ..., 8th
    // argument a pointer whose first field is the branch record). The error
    // is 0 when the branch may be taken. The record's target action hash is
    // at +0x14.
    constexpr uintptr_t kRva_BranchCheck = 0x232E3D0;
    constexpr uint8_t kBranchCheckHead[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x4C, 0x89, 0x48, 0x20, 0x44,
                                             0x88, 0x40, 0x18, 0x48, 0x89, 0x48, 0x08 };

    typedef uint64_t (*FnCheck)(uintptr_t, uint32_t*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    FnCheck g_original = nullptr;

    struct Last { uintptr_t comp; uint8_t layer; uint32_t target; };
    constexpr int kSlots = 64;
    Last g_last[kSlots] = {};
    int g_used = 0;
    SRWLOCK g_lock = SRWLOCK_INIT;
    volatile LONG g_lines = 0;
    bool g_log = false;
    struct Check { uintptr_t comp; uint32_t off; DWORD tick; uint8_t layer; };
    constexpr int kChecks = 512;
    Check g_checks[kChecks] = {};
    int g_checkCount = 0;

    // The rider fix: the two default branches of Kliff's lower dispatchers,
    // by their offset in ride_test3_lower.paac, with the shipped word and the
    // one they hold while the speeder is ridden. Broomy points them at
    // Kliff's broom idle, 588C2001, whose blend Broomy serves with its own
    // clips. The speeder points them at 01B8F8D3, a state of the cut orca
    // rider that nothing reaches: its branches are the broom idle's (the
    // shared exit on 1A5, a loop on its own end) and its node plays
    // orca_rider_move_ing, which the plugin serves as Kliff's speeder lean
    // blend (speeder/make_blends.py). No interaction names an orca mount key. +0x14 is the target, +0x08 the
    // crossfade in frames (f32). Shipped, the branches cut (-1) between two
    // dragon poses that play the same clip; between the broom's ground and
    // air states they crossfade over 10 frames, as Kliff's upper chart does.
    struct Swap { uint32_t offset; uint32_t shipped; uint32_t broomy; };
    constexpr uint32_t kCut = 0xBF800000, kTenFrames = 0x41200000;
    constexpr uint32_t kSpeederIdle = 0x01B8F8D3;
    constexpr Swap kSwaps[] = {
        { 0x6F3F1 + 0x14, 0x07407E6A, kSpeederIdle },   // branch 397, ground states -> the speeder idle
        { 0x6F3F1 + 0x08, kCut, kTenFrames },
        // The air states also take it; the speeder never flies, but a state
        // of the Wyvern's it reaches for a frame should not crouch him.
        { 0x6F48D + 0x14, 0x90337E5C, kSpeederIdle },   // branch 400, air states -> the speeder idle
        { 0x6F48D + 0x08, kCut, kTenFrames },
        // The push-off. Kliff's lower layer takes the mount's action key, so
        // the broom's takeoff runs Kliff's 117F8F51, whose default slot 934
        // shares branch 397 with seven other states. Slot 934 alone moves to
        // branch 439, rewritten as 397 into FDA30B13 with a 3 frame fade.
        // FDA30B13 is a spare Kliff action no branch or chart reaches; it
        // plays the push-off under a name of its own,
        // cd_phm_rd_broom_basic_00_00_nor_std_takeoff_00 (Seen below writes
        // it into the chart), and lasts the
        // clip's 136 frames. Kliff's lower chart moves on only when a broom
        // chart commands it, never on anim_end; the flight start commands it
        // (crossfadepatches.h, b27), so the clip's round of broom idle after
        // the 36 frame kick is only a margin. Slot 1036 points at branch 671
        // in case the end is ever reached. Branch 439 had only slot 1036,
        // FDA30B13 looping on itself. Seth, 3 October: "his feet touched the
        // ground".
        { 0x796B4, 0x00018D7F, 0x0001B77F },          // slot 934 (u16 at +1): branch 397 -> 439
        { 0x6FC79 + 0x08, 0x3F800000, 0x40400000 },   // branch 439 crossfade 1 -> 3 frames (the feet land at frame 6)
        { 0x6FC79 + 0x1C, 0x00000007, 0x00000204 },   // branch 439 kind, as 397
        { 0x6FC79 + 0x24, 0x0000001D, 0x00000000 },   // branch 439 condition, as 397
        { 0x6FC79 + 0x28, 0x00010000, 0x00000000 },
        { 0x7A1DC, 0x0001B77F, 0x00029F7F },          // slot 1036 (u16 at +1): branch 439 -> 671
        { 0x1D4D0, 0x3F19999A, 0x40911111 },          // FDA30B13 duration 0.6 s -> 4.53 s (136 frames)
    };
    constexpr DWORD kRiddenMs = 3000;
    constexpr uint32_t kPushOffAt = 0x31339;   // string 193, after its length byte (80)
    constexpr char kPushOffOld[] = "1_pc/1_phm/00_riding/cd_phm_rd_wyvern_basic_00_00_air_move_fall_walk_end_00.paa";
    constexpr char kPushOffNew[] = "1_pc/1_phm/00_riding/cd_phm_rd_broom_basic_00_00_nor_std_takeoff_00.paa";
    static_assert(sizeof kPushOffNew <= sizeof kPushOffOld, "the new name must fit the old one's room");
    constexpr size_t kSwapCount = sizeof kSwaps / sizeof kSwaps[0];

    // Kliff's left hand IK. 01B8F8D3's node fires frame event 777 at 0 s,
    // type 0x65, which switches on the LimbIK's left hand
    // (posemodifierdata.xml) with its target on the mount's B_IK_L_Hand.
    // The broom skeleton has no such bone, so his left fist went to the
    // seat, at the front of his belt, whatever either layer played (Seth,
    // 5 October, with marked clips on both layers; the CD Animator session
    // found the event). Broomy's 588C2001 fires no 0x65, so on the broom
    // the hand follows the clip. The node's instance of the event fires at
    // 1000 s instead, past the end of every clip, so it never fires. The
    // event itself is shared by 77 states and stays as it is.
    constexpr uint32_t kIkAt = 0x216BF;   // the instance record: time, time_end, event index
    constexpr uint32_t kIkWords[] = { 0x00000000, 0xBF800000, 777 };
    constexpr uint32_t kNever = 0x447A0000;   // 1000.0f

    // Kliff's upper riding chart, ride_upper.paac, drives his left arm, the
    // rein hand, and on every mount using Broom_Ride it plays the broom idle
    // 588C2001, the shipped clip (Broomy serves none at that path): on the
    // speeder his left hand sat at his belt (Seth, 4 October). EFD5260B is a
    // state of the cut orca rider that only its own loop reaches. As the
    // chart loads, its record becomes the broom idle's, branches and flags,
    // keeping its own key and its one node, which plays the orca rider's
    // underwater idle; the plugin serves his speeder idle under that name,
    // and the node's length becomes that clip's 100 frames.
    //
    // A state's clip has to be loaded before the layer enters it. Entering
    // a state loads the clips of the states its branches lead to: the
    // mount's are the dismounts and the broom idle (CDAnimator.log, 4
    // October, 20:21:51). The speeder's own chart is first checked a few
    // milliseconds after that load (12 ms at 20:47:31), so the plugin cannot
    // know it is the speeder in time. With the end of the mount (branch 550)
    // pointed at EFD5260B, the layer entered it with nothing loaded and his
    // left arm held the mount's last frame, the broom pose (Seth, 20:22,
    // with both layers playing marked clips). So the mount still ends in the
    // broom idle; while the speeder is ridden, the broom idle lasts half a
    // second and its loop (branch 544) leads to EFD5260B, whose clip loads
    // as the broom idle starts. Until the hand IK below was off, that did
    // not move his left hand (Seth, 5 October: never on the grip).
    // Offsets are in the shipped file.
    constexpr uint32_t kBroomIdle = 0x588C2001, kSpeederUpper = 0xEFD5260B;
    constexpr uint32_t kIdleRecord = 0x2E2DA, kDonorRecord = 0x3F062, kActionSize = 0x114, kKeyAt = 0x18;
    constexpr Swap kUpperSwaps[] = {
        { 0x49834, kBroomIdle, kSpeederUpper },   // branch 544 (+0x14), the broom idle's loop
        { 0x1FBD8, 0x40555556, 0x3F000000 },      // Kliff's broom idle node (+0x04): 3.33 s -> 0.5 s
    };
    constexpr size_t kUpperCount = sizeof kUpperSwaps / sizeof kUpperSwaps[0];
    constexpr Swap kDonorTimes[] = {
        { 0x1BD26, 0x40D55556, 0x40555556 },      // EFD5260B's node: 6.67 s -> 3.33 s
        { 0x1BDDA, 0x40D88889, 0x405BBBBC },      // its play-rate window ends at 3.43 s, as the broom idle's
    };
    // Branch 551 of the upper chart, checked only from the broom's two
    // dismounts (18064912, 8B293B1B), which the speeder plays too: the
    // layer checks it from the moment Kliff starts getting off (Seth, 5
    // October: the shutdown sound should play when he gets off). The
    // speeder's RideOn chart runs on after he is off, until the game pauses
    // (Seth, 09:42: no start-up on getting back on), so getting off ends
    // with the upper layer's next check of any other branch, which comes
    // with the mount. The first such check over kSettleMs after the dismount
    // counts, past any the old state makes as the dismount begins. +0x14 is
    // the branch's target.
    constexpr uint32_t kDismountBranch = 0x4998C, kDismountTarget = 0xD69FE049;
    constexpr LONG kSettleMs = 400;
    volatile uint32_t g_dismountOff = 0;
    volatile LONG g_dismountAt = 0, g_upperOtherAt = 0;
    struct Field { uint32_t at, size; };
    constexpr Field kDonorKeeps[] = { { kKeyAt, 4 }, { 0x54, 4 }, { 0xEA, 2 } };   // key, node_first, node_count
    uint32_t g_upperAt[kUpperCount] = {};
    volatile uintptr_t g_upperBase = 0;
    volatile uint32_t g_upperSize = 0;

    // The broom idle's record over the donor's in a chart buffer, the
    // donor's `keeps` kept.
    void CopyRecord(const uint8_t* data, uint32_t idle, uint32_t donor, const Field* keeps, size_t count)
    {
        uint8_t* d = const_cast<uint8_t*>(data);
        uint8_t keep[kActionSize];
        memcpy(keep, d + donor, kActionSize);
        memcpy(d + donor, d + idle, kActionSize);
        for (size_t i = 0; i < count; ++i) memcpy(d + donor + keeps[i].at, keep + keeps[i].at, keeps[i].size);
    }
    // Each swap's offset in the lower chart the game has: the shipped file,
    // or the one with Damiane's nodes (broomy.cpp).
    uint32_t g_at[kSwapCount] = {};
    volatile uintptr_t g_lowerBase = 0;
    volatile uint32_t g_lowerSize = 0;
    volatile uintptr_t g_rideOnBase = 0;
    volatile uint32_t g_rideOnSize = 0;
    volatile LONG g_rideOnAt = 0;
    volatile LONG g_swapped = 0;   // 1 while the broom targets are in
    SRWLOCK g_swapLock = SRWLOCK_INIT;

    bool In(uintptr_t a, uintptr_t base, uint32_t size) { return base && a >= base && a < base + size; }

    void SetSwap(bool on)
    {
        AcquireSRWLockExclusive(&g_swapLock);
        const uintptr_t base = g_lowerBase;
        if (base && (g_swapped != 0) != on)
        {
            bool ok = true;
            for (size_t i = 0; i < kSwapCount; ++i)
            {
                const Swap& w = kSwaps[i];
                uint32_t* at = reinterpret_cast<uint32_t*>(base + g_at[i]);
                const uint32_t want = on ? w.broomy : w.shipped, was = on ? w.shipped : w.broomy;
                if (*at == was) *at = want;
                else if (*at != want) ok = false;
            }
            if (const uintptr_t upper = g_upperBase)
                for (size_t i = 0; i < kUpperCount; ++i)
                {
                    const Swap& w = kUpperSwaps[i];
                    uint32_t* at = reinterpret_cast<uint32_t*>(upper + g_upperAt[i]);
                    const uint32_t want = on ? w.broomy : w.shipped, was = on ? w.shipped : w.broomy;
                    if (*at == was) *at = want;
                    else if (*at != want) ok = false;
                }
            InterlockedExchange(&g_swapped, on ? 1 : 0);
            LOG(ok ? "[rider] Kliff's lower chart %s." : "[rider] Kliff's lower chart %s, but a branch held other bytes.",
                on ? "takes his broom states (the speeder is ridden)" : "is back to its shipped targets");
        }
        ReleaseSRWLockExclusive(&g_swapLock);
    }

    uint64_t CheckDetour(uintptr_t comp, uint32_t* err, uintptr_t layer, uintptr_t a4, uintptr_t a5, uintptr_t a6,
                         uintptr_t a7, uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12)
    {
        uintptr_t pre = 0;
        if (a8 && bm::mem::ReadPtr(a8, &pre))
        {
            if (In(pre, g_rideOnBase, g_rideOnSize)) InterlockedExchange(&g_rideOnAt, static_cast<LONG>(GetTickCount()));
            // Either of Kliff's riding layers, so the targets are in before
            // the upper layer leaves the mount.
            else if (In(pre, g_lowerBase, g_lowerSize) || In(pre, g_upperBase, g_upperSize))
            {
                if (g_dismountOff && pre == g_upperBase + g_dismountOff)
                    InterlockedExchange(&g_dismountAt, static_cast<LONG>(GetTickCount()));
                else if (In(pre, g_upperBase, g_upperSize))
                    InterlockedExchange(&g_upperOtherAt, static_cast<LONG>(GetTickCount()));
                const LONG at = g_rideOnAt;
                const bool ridden = at && GetTickCount() - static_cast<DWORD>(at) < kRiddenMs;
                if (ridden != (g_swapped != 0)) SetSwap(ridden);
            }
        }
        const uint64_t r = g_original(comp, err, layer, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        if (!g_log) return r;
        // Every check of a branch in Kliff's riding charts, by its offset in
        // the chart, at most once per 2 s for each component, layer and
        // branch: the branches a layer checks are its current state's.
        const bool upper = In(pre, g_upperBase, g_upperSize), lower = !upper && In(pre, g_lowerBase, g_lowerSize);
        if (upper || lower)
        {
            const uint32_t off = static_cast<uint32_t>(pre - (upper ? g_upperBase : g_lowerBase));
            const uint8_t l = static_cast<uint8_t>(layer);
            const DWORD now = GetTickCount();
            bool due = false;
            AcquireSRWLockExclusive(&g_lock);
            int i = 0;
            while (i < g_checkCount && !(g_checks[i].comp == comp && g_checks[i].layer == l && g_checks[i].off == off)) ++i;
            if (i == g_checkCount && g_checkCount < kChecks) g_checks[g_checkCount++] = { comp, off, now - 2000, l };
            if (i < g_checkCount && now - g_checks[i].tick >= 2000)
            {
                g_checks[i].tick = now;
                due = true;
            }
            ReleaseSRWLockExclusive(&g_lock);
            uint32_t got = 1, to = 0;
            if (due && err && bm::mem::Read32(reinterpret_cast<uintptr_t>(err), &got) && bm::mem::Read32(pre + 0x14, &to) &&
                InterlockedIncrement(&g_lines) <= 20000)
                LOG("[check] comp %p layer %u %s +0x%X -> %08X %s", reinterpret_cast<void*>(comp), l,
                    upper ? "upper" : "lower", off, to, got == 0 ? "taken" : "not taken");
        }
        uintptr_t rec = 0;
        uint32_t code = 1, target = 0;
        if (!a8 || !err || !bm::mem::Read32(reinterpret_cast<uintptr_t>(err), &code) || code != 0) return r;
        if (!bm::mem::ReadPtr(a8, &rec) || !bm::mem::Read32(rec + 0x14, &target)) return r;
        const uint8_t l = static_cast<uint8_t>(layer);
        bool fresh = false;
        AcquireSRWLockExclusive(&g_lock);
        int i = 0;
        while (i < g_used && !(g_last[i].comp == comp && g_last[i].layer == l)) ++i;
        if (i == g_used && g_used < kSlots) g_last[g_used++] = { comp, l, 0 };
        if (i < g_used && g_last[i].target != target)
        {
            g_last[i].target = target;
            fresh = true;
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (fresh && InterlockedIncrement(&g_lines) <= 20000)
            LOG("[state] comp %p layer %u -> %08X (record %p)", reinterpret_cast<void*>(comp), l, target,
                reinterpret_cast<void*>(rec));
        return r;
    }

    // Where a word of the shipped lower chart sits in the one with Damiane's
    // nodes (damiane.h).
    uint32_t Moved(uint32_t offset) { return bm::damiane::Moved(bm::damiane::kCharts[1], offset); }

    // Whether the chart holds every swap's shipped word where the layout puts
    // it; fills `at` with the offsets.
    bool Fits(const uint8_t* data, uint32_t size, bool damiane, uint32_t* at)
    {
        for (size_t i = 0; i < kSwapCount; ++i)
        {
            at[i] = damiane ? Moved(kSwaps[i].offset) : kSwaps[i].offset;
            if (at[i] + 4 > size || *reinterpret_cast<const uint32_t*>(data + at[i]) != kSwaps[i].shipped) return false;
        }
        return true;
    }

    uint32_t Peek32(const uint8_t* data, uint32_t at) { return *reinterpret_cast<const uint32_t*>(data + at); }

    // The upper chart: the shipped file or Broomy's with Damiane's nodes.
    void SeenUpper(const uint8_t* data, uint32_t size)
    {
        for (int damiane = 0; damiane < 2; ++damiane)
        {
            const auto at = [&](uint32_t offset) {
                return damiane ? bm::damiane::Moved(bm::damiane::kCharts[0], offset) : offset;
            };
            const uint32_t idle = at(kIdleRecord), donor = at(kDonorRecord);
            uint32_t targets[kUpperCount];
            bool fits = idle + kActionSize <= size && donor + kActionSize <= size &&
                        Peek32(data, idle + kKeyAt) == kBroomIdle && Peek32(data, donor + kKeyAt) == kSpeederUpper;
            for (size_t i = 0; fits && i < kUpperCount; ++i)
            {
                targets[i] = at(kUpperSwaps[i].offset);
                fits = targets[i] + 4 <= size && Peek32(data, targets[i]) == kUpperSwaps[i].shipped;
            }
            for (const Swap& w : kDonorTimes)
                fits = fits && at(w.offset) + 4 <= size && Peek32(data, at(w.offset)) == w.shipped;
            if (!fits) continue;
            CopyRecord(data, idle, donor, kDonorKeeps, sizeof kDonorKeeps / sizeof kDonorKeeps[0]);
            for (const Swap& w : kDonorTimes) memcpy(const_cast<uint8_t*>(data) + at(w.offset), &w.broomy, 4);
            AcquireSRWLockExclusive(&g_swapLock);
            memcpy(g_upperAt, targets, sizeof targets);
            const uint32_t dismount = at(kDismountBranch);
            g_dismountOff = dismount + 0x18 <= size && Peek32(data, dismount + 0x14) == kDismountTarget ? dismount : 0;
            g_upperBase = reinterpret_cast<uintptr_t>(data);
            g_upperSize = size;
            ReleaseSRWLockExclusive(&g_swapLock);
            LOG("[rider] Kliff's upper chart has the speeder idle as %08X%s.", kSpeederUpper,
                damiane ? ", in the chart with Damiane's nodes" : "");
            if (!g_dismountOff) LOG_ERR("[rider] the broom dismount's branch is not where it should be, so the shutdown sound waits until he is off.");
            return;
        }
        AcquireSRWLockExclusive(&g_swapLock);
        g_upperBase = 0;
        ReleaseSRWLockExclusive(&g_swapLock);
        LOG_ERR("[rider] ride_upper.paac is not a file this knows, so Kliff's left hand keeps the broom idle.");
    }

    void Seen(const char* path, const uint8_t* data, uint32_t size)
    {
        const size_t n = strlen(path);
        if (n < 5 || _stricmp(path + n - 5, ".paac") != 0) return;
        if (!strstr(path, "ride") && !strstr(path, "riding")) return;
        if (g_log) LOG("[state] chart %s at %p (%u bytes)", path, data, size);
        if (strstr(path, "/ride_upper.paac")) return SeenUpper(data, size);
        if (!strstr(path, "/ride_test3_lower.paac")) return;
        static_assert(sizeof bm::damiane::kCharts / sizeof bm::damiane::kCharts[0] == 2, "kCharts[1] is the lower chart");
        uint32_t at[kSwapCount];
        const bool shipped = Fits(data, size, false, at);
        const bool damiane = !shipped && Fits(data, size, true, at);
        const bool known = shipped || damiane;
        AcquireSRWLockExclusive(&g_swapLock);
        g_lowerBase = known ? reinterpret_cast<uintptr_t>(data) : 0;
        g_lowerSize = size;
        if (known) memcpy(g_at, at, sizeof at);
        InterlockedExchange(&g_swapped, 0);
        ReleaseSRWLockExclusive(&g_swapLock);
        if (!known) LOG_ERR("[rider] ride_test3_lower.paac is not the shipped file, so Kliff keeps the dragon pose.");
        const uint32_t ikAt = damiane ? Moved(kIkAt) : kIkAt;
        if (known && ikAt + sizeof kIkWords <= size && memcmp(data + ikAt, kIkWords, sizeof kIkWords) == 0)
        {
            memcpy(const_cast<uint8_t*>(data) + ikAt, &kNever, 4);
            LOG("[rider] Kliff's left hand IK stays off on the speeder%s.", damiane ? ", in the lower chart with Damiane's nodes" : "");
        }
        else if (known)
            LOG_ERR("[rider] 01B8F8D3's hand IK event is not where it should be, so his left hand may go to his belt.");
        const uint32_t pushOffAt = damiane ? Moved(kPushOffAt) : kPushOffAt;
        // The push-off's own name. FDA30B13 plays the clip at string 193;
        // the chart hashes its paths as it parses, which is after this read,
        // so the name is written here, padded with zeros inside the old
        // string's room as broomchart.cpp does. Nothing reaches FDA30B13
        // unless Broomy is ridden, so it keeps the new name throughout.
        if (known && pushOffAt + sizeof kPushOffOld <= size &&
            memcmp(data + pushOffAt, kPushOffOld, sizeof kPushOffOld) == 0)
        {
            uint8_t* name = const_cast<uint8_t*>(data) + pushOffAt;
            memset(name, 0, sizeof kPushOffOld);
            memcpy(name, kPushOffNew, sizeof kPushOffNew - 1);
            LOG("[rider] Kliff's push-off plays as %s%s.", kPushOffNew,
                damiane ? ", in the lower chart with Damiane's nodes" : "");
        }
    }
}

namespace bm::riderfix
{
    uint32_t MsSinceRidden()
    {
        const LONG at = g_rideOnAt;
        return at ? GetTickCount() - static_cast<DWORD>(at) : 0xFFFFFFFFu;
    }

    bool GettingOff()
    {
        const LONG off = g_dismountAt, other = g_upperOtherAt;
        return off && !(other && other - off > kSettleMs);
    }

    void NoteServed(const char* path, uintptr_t base, uint32_t size)
    {
        if (!strstr(path, "m0010_tanksmall_upper.paac")) return;
        g_rideOnSize = size;
        g_rideOnBase = base;
    }

    bool Install(bool logStates)
    {
        g_log = logStates;
        const uintptr_t at = bm::mem::Game().base + kRva_BranchCheck;
        uint8_t head[sizeof kBranchCheckHead] = {};
        // Broomy.asi, loaded first, hooks the same check.
        if (!bm::farhook::Patched(at) &&
            (!bm::mem::ReadBytes(at, head, sizeof head) || memcmp(head, kBranchCheckHead, sizeof head) != 0))
        {
            LOG_ERR("[rider] the branch check is not at +0x%llX on this exe.", static_cast<unsigned long long>(kRva_BranchCheck));
            return false;
        }
        char why[160] = {};
        if (!bm::farhook::Chain("branch check", at, reinterpret_cast<void*>(&CheckDetour),
                              reinterpret_cast<void**>(&g_original), why, sizeof why))
        {
            LOG_ERR("[rider] could not hook the branch check: %s", why);
            return false;
        }
        bm::gamefile::Observe(&Seen);
        LOG(logStates ? "[rider] Kliff rides the speeder in his broom poses; each chart layer's next action is logged."
                      : "[rider] Kliff rides the speeder in his broom poses.");
        return true;
    }
}
