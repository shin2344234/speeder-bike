#include "game/riderfix.h"

#include <Windows.h>
#include <cstring>

#include "core/log.h"
#include "game/chartwalk.h"
#include "game/farhook.h"
#include "game/gamefile.h"
#include "game/mem.h"
#include "game/riderfind.h"

namespace
{
    // The branch check: (chart component, u32* error, u8 layer, ..., 8th
    // argument a pointer whose first field is the branch record). The error
    // is 0 when the branch may be taken. The record's target action hash is
    // at +0x14.
    constexpr uintptr_t kRva_BranchCheck = 0x2516560;   // 0x25152D0 on 3392, 0x232E3D0 on 2976
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
    // with the shipped word and the one they hold while the speeder is
    // ridden. Broomy points them at Kliff's broom idle, 588C2001, whose blend
    // Broomy serves with its own clips. The speeder points them at 01B8F8D3,
    // a state of the cut orca rider that nothing reaches: its branches are
    // the broom idle's (the shared exit, a loop on its own end) and its node
    // plays orca_rider_move_ing, which the plugin serves as Kliff's speeder
    // lean blend (speeder/make_blends.py). No interaction names an orca mount
    // key. Shipped, the branches cut (-1) between two dragon poses that play
    // the same clip; between the broom's ground and air states they crossfade
    // over 10 frames, as Kliff's upper chart does. On exe 1.0.0.2976 these
    // were branches 397 and 400; 3392 renumbered them (412 and 415), so each
    // record is found by what it is as the chart loads (FindLower).
    //
    // The air states also take it; the speeder never flies, but a state of
    // the Wyvern's it reaches for a frame should not crouch him.
    //
    // The push-off. Kliff's lower layer takes the mount's action key, so the
    // broom's takeoff runs Kliff's 117F8F51, whose slot on the ground branch
    // shares it with seven other states. That slot alone moves to the spare
    // action's loop branch, rewritten as the ground branch into FDA30B13 with
    // a 3 frame fade. FDA30B13 is a spare Kliff action no branch or chart
    // reaches; it plays the push-off under a name of its own,
    // cd_phm_rd_broom_basic_00_00_nor_std_takeoff_00 (Seen below writes it
    // into the chart), and lasts the clip's 136 frames. Kliff's lower chart
    // moves on only when a broom chart commands it, never on anim_end; the
    // flight start commands it (crossfadepatches.h, b27), so the clip's round
    // of broom idle after the 36 frame kick is only a margin. The loop's own
    // slot points at the shared clip-end exit to the broom idle in case the
    // end is ever reached. Seth, 3 October: "his feet touched the ground".
    using bm::riderfind::Swap;
    constexpr DWORD kRiddenMs = 3000;
    using bm::riderfind::kPushOffOld;
    using bm::riderfind::kPushOffNew;

    // Kliff's left hand IK. 01B8F8D3's node fires a frame event at 0 s that
    // switches on the LimbIK's left hand (posemodifierdata.xml; limb hash
    // FE419E03 "lefthand") with its target on the mount's B_IK_L_Hand. The
    // broom skeleton has no such bone, so his left fist went to the seat, at
    // the front of his belt, whatever either layer played (Seth, 5 October,
    // with marked clips on both layers; the CD Animator session found the
    // event). Broomy's 588C2001 fires none, so on the broom the hand follows
    // the clip. The node's instances of the event fire at 1000 s instead,
    // past the end of every clip, so they never fire. The event itself is
    // shared by many states and stays as it is. On 2976 it was one instance
    // of type 0x65; 3392 numbers the type 0x67 and the node has two.
    // Broomy writes the same as the chart loads; whichever mod is second
    // writes it again (riderfind.h).
    constexpr uint32_t kNever = bm::riderfind::kIkNever;

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
    // know it is the speeder in time. With the end of the mount pointed at
    // EFD5260B, the layer entered it with nothing loaded and his left arm
    // held the mount's last frame, the broom pose (Seth, 20:22, with both
    // layers playing marked clips). So the mount still ends in the broom
    // idle; while the speeder is ridden, the broom idle lasts half a second
    // and its loop leads to EFD5260B, whose clip loads as the broom idle
    // starts. Until the hand IK above was off, that did not move his left
    // hand (Seth, 5 October: never on the grip).
    // The branch of the upper chart checked only from the broom's two
    // dismounts (18064912, 8B293B1B), which the speeder plays too: the
    // layer checks it from the moment Kliff starts getting off (Seth, 5
    // October: the shutdown sound should play when he gets off). The
    // speeder's RideOn chart runs on after he is off, until the game pauses
    // (Seth, 09:42: no start-up on getting back on), so getting off ends
    // with the upper layer's next check of any other branch, which comes
    // with the mount. The first such check over kSettleMs after the dismount
    // counts, past any the old state makes as the dismount begins.
    constexpr LONG kSettleMs = 400;
    volatile uint32_t g_dismountOff = 0;
    volatile LONG g_dismountAt = 0, g_upperOtherAt = 0;
    // While Kliff sits on the speeder his upper layer checks, every frame,
    // branches that lead to the broom's two dismounts: from the mount, the
    // broom idle and EFD5260B (SpeederBike.log, 5 October 09:07, the layer's
    // checks of two such branches every 2 s for the whole ride, which is the
    // log's limit). No horse or dragon state leads there, so a check of one
    // is Kliff on the speeder (or Broomy), whoever else rides nearby. Off
    // the speeder by any other way than those dismounts (thrown off as the
    // speeder faints, Buxunqingmo's 1.0.2 log at 12:44) the checks stop,
    // while the speeder's RideOn chart runs on.
    constexpr uint32_t kBroomDismounts[] = { 0x18064912, 0x8B293B1B };
    volatile LONG g_seatedAt = 0;
    Swap g_upperSwaps[bm::riderfind::kUpperSwaps] = {};
    volatile uintptr_t g_upperBase = 0;
    volatile uint32_t g_upperSize = 0;

    // Each swap in the lower chart the game has, found as it loads.
    Swap g_swaps[bm::riderfind::kLowerSwaps] = {};
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
            for (const Swap& w : g_swaps)
            {
                uint32_t* at = reinterpret_cast<uint32_t*>(base + w.offset);
                const uint32_t want = on ? w.now : w.shipped, was = on ? w.shipped : w.now;
                if (*at == was) *at = want;
                else if (*at != want) ok = false;
            }
            if (const uintptr_t upper = g_upperBase)
                for (const Swap& w : g_upperSwaps)
                {
                    uint32_t* at = reinterpret_cast<uint32_t*>(upper + w.offset);
                    const uint32_t want = on ? w.now : w.shipped, was = on ? w.shipped : w.now;
                    if (*at == was) *at = want;
                    else if (*at != want) ok = false;
                }
            // A branch holding the other mount's target when ours go in is
            // Broomy's and the speeder's swaps meeting on the same words:
            // the other mod lets go in its own detour on this same check, so
            // ours go in on the next one. Before, the swap counted as in and
            // those branches kept the other mount's targets (SpeederBike.log,
            // 23 October 2026, 11:23:29).
            static volatile LONG s_heldLines = 0;
            if (on && !ok)
            {
                if (InterlockedIncrement(&s_heldLines) <= 20)
                    LOG("[rider] Kliff's lower chart holds the other mount's targets, so the speeder's go in on the next check.");
            }
            else
            {
                InterlockedExchange(&g_swapped, on ? 1 : 0);
                LOG(ok ? "[rider] Kliff's lower chart %s." : "[rider] Kliff's lower chart %s, but a branch held other bytes.",
                    on ? "takes his broom states (the speeder is ridden)" : "is back to its shipped targets");
            }
        }
        ReleaseSRWLockExclusive(&g_swapLock);
    }

    // Which mount Kliff is on. Both mounts' RideOn charts are checked every
    // frame from their first ride to the end of the session, ridden or not
    // (SpeederBike.log, 11:08 to 11:09, 23 October 2026), so a chart that
    // runs says nothing. A mount Kliff is not on settles into a state and
    // stays there; the one he rides changes state as he gets on, sets off,
    // stops and boosts. So the mount whose chart last went into another
    // state is the one he is on. A state change is a branch the game lets
    // through (the check's error 0) to a state other than the one that chart
    // last went to: a clip-end loop back into the same state is none, and
    // neither is settling into the ground idle F5150415 of the Wyvern's
    // chart both are built from. Until 29 October this counted every check
    // outside that idle, but on 3392 the parked speeder rests in the flight
    // state E066175D and checks its branch back to the idle every frame, so
    // both charts counted as moving and each mount took Kliff in turn every
    // frame: the speeder's engine ran on Broomy (Seth, 29 October; the
    // Broomy session found it and made the same change on its side).
    // Getting on is the exception. A mount action ends in the ground idle,
    // so getting on and staying on the ground changed no mount's state, and
    // the speeder ridden before still counted as Kliff's until Broomy took
    // off. Getting on Broomy within 3 s of leaving the speeder, while the
    // speeder's chart was still checked, started the engine on Broomy
    // (Seth, 30 October; SpeederBike.log startup at 08:24:35.345, 20 ms after
    // Broomy's chart was first checked). The mount actions' branch to the
    // ground idle gives the rider 082F0420 (+0x18), and on 3392 only the
    // chart Kliff got on passed it, every frame of the mount (Broomy.06.log,
    // +0xC3D1); a parked chart passes +0xBF39 instead. So passing a branch
    // to the ground idle with that rider action counts as a change of state,
    // though it is no change. Broomy f171acd does the same.
    volatile uintptr_t g_broomBase = 0;
    volatile uint32_t g_broomSize = 0;
    volatile LONG g_speederMovedAt = 0, g_broomMovedAt = 0;
    volatile LONG g_onBroomyLogged = 0;
    // The state each chart last went to: the speeder's, Broomy's.
    volatile LONG g_state[2] = {};
    constexpr uint32_t kGroundIdle = 0xF5150415;
    constexpr uint32_t kMountedRider = 0x082F0420;

    bool OnBroomy()
    {
        const LONG b = g_broomMovedAt, sp = g_speederMovedAt;
        return b && (!sp || static_cast<LONG>(b - sp) > 0);
    }

    // Notes each branch a mount's chart takes into another state.
    void Track(uintptr_t pre)
    {
        int chart = -1;
        uintptr_t base = 0;
        if (In(pre, g_rideOnBase, g_rideOnSize)) chart = 0, base = g_rideOnBase;
        else if (In(pre, g_broomBase, g_broomSize)) chart = 1, base = g_broomBase;
        if (chart < 0) return;
        uint32_t target = 0, rider = 0;
        if (!bm::mem::Read32(pre + 0x14, &target) || !target) return;
        const bool mounted = target == kGroundIdle && bm::mem::Read32(pre + 0x18, &rider) && rider == kMountedRider;
        if (static_cast<uint32_t>(InterlockedExchange(&g_state[chart], static_cast<LONG>(target))) == target &&
            !mounted)
            return;
        // Settling into the ground idle is what a mount left alone does, so
        // it says nothing about the rider. Getting on ends there too.
        if (target == kGroundIdle && !mounted) return;
        InterlockedExchange(chart == 0 ? &g_speederMovedAt : &g_broomMovedAt, static_cast<LONG>(GetTickCount() | 1));
        const LONG on = OnBroomy() ? 1 : 0;
        if (InterlockedExchange(&g_onBroomyLogged, on) != on)
            LOG(on ? "[rider] Broomy's chart went to %08X (+0x%X%s) after the speeder's last changed state, so Kliff is "
                     "on Broomy."
                   : "[rider] the speeder's chart went to %08X (+0x%X%s) after Broomy's last changed state, so Kliff is "
                     "on the speeder.",
                target, static_cast<uint32_t>(pre - base), mounted ? ", getting on" : "");
    }

    uint64_t CheckDetour(uintptr_t comp, uint32_t* err, uintptr_t layer, uintptr_t a4, uintptr_t a5, uintptr_t a6,
                         uintptr_t a7, uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12)
    {
        uintptr_t pre = 0;
        const bool known = a8 && bm::mem::ReadPtr(a8, &pre);
        if (known)
        {
            if (In(pre, g_rideOnBase, g_rideOnSize)) InterlockedExchange(&g_rideOnAt, static_cast<LONG>(GetTickCount()));
            // Either of Kliff's riding layers, so the targets are in before
            // the upper layer leaves the mount.
            else if (In(pre, g_lowerBase, g_lowerSize) || In(pre, g_upperBase, g_upperSize))
            {
                if (g_dismountOff && pre == g_upperBase + g_dismountOff)
                    InterlockedExchange(&g_dismountAt, static_cast<LONG>(GetTickCount()));
                else if (In(pre, g_upperBase, g_upperSize))
                {
                    InterlockedExchange(&g_upperOtherAt, static_cast<LONG>(GetTickCount()));
                    uint32_t to = 0;
                    if (bm::mem::Read32(pre + 0x14, &to) && (to == kBroomDismounts[0] || to == kBroomDismounts[1]))
                        InterlockedExchange(&g_seatedAt, static_cast<LONG>(GetTickCount()));
                }
                const LONG at = g_rideOnAt;
                const bool ridden = at && GetTickCount() - static_cast<DWORD>(at) < kRiddenMs && !OnBroomy();
                if (ridden != (g_swapped != 0)) SetSwap(ridden);
            }
        }
        const uint64_t r = g_original(comp, err, layer, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        uint32_t passed = 1;
        if (known && err && bm::mem::Read32(reinterpret_cast<uintptr_t>(err), &passed) && passed == 0) Track(pre);
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

    uint32_t Peek32(const uint8_t* data, uint32_t at) { return *reinterpret_cast<const uint32_t*>(data + at); }

    bool Holds(const uint8_t* data, const Swap* swaps, int count)
    {
        for (int i = 0; i < count; ++i)
            if (Peek32(data, swaps[i].offset) != swaps[i].shipped) return false;
        return true;
    }

    // The upper chart: the shipped file, or Broomy's with Damiane's nodes.
    void SeenUpper(const uint8_t* data, uint32_t size)
    {
        bm::riderfind::Upper u;
        const char* why = "";
        if (!bm::riderfind::FindUpper(data, size, u, why))
        {
            AcquireSRWLockExclusive(&g_swapLock);
            g_upperBase = 0;
            g_dismountOff = 0;
            ReleaseSRWLockExclusive(&g_swapLock);
            LOG_ERR("[rider] ride_upper.paac (%u bytes) is not a chart this knows (%s), so Kliff's left hand keeps the broom idle.",
                    size, why);
            return;
        }
        // The broom idle's record over the donor's, the donor's key and node
        // run kept.
        uint8_t* d = const_cast<uint8_t*>(data);
        uint8_t keep[bm::chartwalk::kActionSize];
        memcpy(keep, d + u.donorRecord, sizeof keep);
        memcpy(d + u.donorRecord, d + u.idle, sizeof keep);
        for (const auto& k : bm::riderfind::kDonorKeeps) memcpy(d + u.donorRecord + k.at, keep + k.at, k.size);
        for (const Swap& w : u.donor) memcpy(d + w.offset, &w.now, 4);
        AcquireSRWLockExclusive(&g_swapLock);
        memcpy(g_upperSwaps, u.swaps, sizeof g_upperSwaps);
        g_dismountOff = u.dismount;
        g_upperBase = reinterpret_cast<uintptr_t>(data);
        g_upperSize = size;
        ReleaseSRWLockExclusive(&g_swapLock);
        LOG("[rider] Kliff's upper chart (%u bytes) has the speeder idle as %08X; the broom idle loops on branch %u, the "
            "broom dismounts leave by branch %u.",
            size, bm::riderfind::kSpeederUpper, u.loop, u.dismountIndex);
    }

    void Seen(const char* path, const uint8_t* data, uint32_t size)
    {
        const size_t n = strlen(path);
        if (n < 5 || _stricmp(path + n - 5, ".paac") != 0) return;
        if (strstr(path, "/m0002_armadillo.paac") && GetModuleHandleW(L"Broomy.asi"))
        {
            InterlockedExchange(&g_state[1], 0);
            g_broomSize = size;
            g_broomBase = reinterpret_cast<uintptr_t>(data);
            LOG("[rider] Broomy's riding chart, %s, is at %p (%u bytes).", path, data, size);
        }
        if (!strstr(path, "ride") && !strstr(path, "riding")) return;
        if (g_log) LOG("[state] chart %s at %p (%u bytes)", path, data, size);
        if (strstr(path, "/ride_upper.paac")) return SeenUpper(data, size);
        if (!strstr(path, "/ride_test3_lower.paac")) return;
        bm::riderfind::Lower f;
        const char* why = "";
        const bool known = bm::riderfind::FindLower(data, size, f, why) && Holds(data, f.swaps, bm::riderfind::kLowerSwaps);
        AcquireSRWLockExclusive(&g_swapLock);
        g_lowerBase = known ? reinterpret_cast<uintptr_t>(data) : 0;
        g_lowerSize = size;
        if (known) memcpy(g_swaps, f.swaps, sizeof g_swaps);
        InterlockedExchange(&g_swapped, 0);
        ReleaseSRWLockExclusive(&g_swapLock);
        if (!known)
        {
            LOG_ERR("[rider] ride_test3_lower.paac (%u bytes) is not a chart this knows (%s), so Kliff keeps the dragon pose.",
                    size, why);
            return;
        }
        LOG("[rider] Kliff's lower chart (%u bytes): the dispatchers' default branches are %u and %u, the takeoff's slot "
            "%u moves to branch %u, its loop slot %u to branch %u.",
            size, f.ground, f.air, f.takeoffSlot, f.loop, f.loopSlot, f.exit);
        for (int i = 0; i < f.ikCount; ++i) memcpy(const_cast<uint8_t*>(data) + f.ik[i], &kNever, 4);
        LOG("[rider] Kliff's left hand IK stays off on the speeder (%d event%s).", f.ikCount, f.ikCount == 1 ? "" : "s");
        // The push-off's own name. FDA30B13 plays the clip at that path; the
        // chart hashes its paths as it parses, which is after this read, so
        // the name is written here, padded with zeros inside the old
        // string's room as broomchart.cpp does. Nothing reaches FDA30B13
        // unless Broomy is ridden, so it keeps the new name throughout.
        uint8_t* name = const_cast<uint8_t*>(data) + f.pushOff;
        memset(name, 0, sizeof kPushOffOld);
        memcpy(name, kPushOffNew, sizeof kPushOffNew - 1);
        LOG("[rider] Kliff's push-off plays as %s.", kPushOffNew);
    }
}

namespace bm::riderfix
{
    uint32_t MsSinceRidden()
    {
        const LONG at = g_rideOnAt;
        return at && !OnBroomy() ? GetTickCount() - static_cast<DWORD>(at) : 0xFFFFFFFFu;
    }

    uint32_t MsSinceSeated()
    {
        const LONG at = g_seatedAt;
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
        InterlockedExchange(&g_state[0], 0);
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

// Broomy.asi asks for the speeder's RideOn chart here, to tell which mount
// Kliff is on the same way OnBroomy does from this side. 1 and the chart's
// place once it has been served, 0 before.
extern "C" __declspec(dllexport) int SpeederBikeRideOnChart(uintptr_t* base, uint32_t* size)
{
    const uintptr_t b = g_rideOnBase;
    const uint32_t n = g_rideOnSize;
    if (!b || !n || !base || !size) return 0;
    *base = b;
    *size = n;
    return 1;
}
