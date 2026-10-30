"""The words riderfix.cpp edits in Kliff's riding charts and reads in the speeder's RideOn chart, found
by what they are (an action's key, the branch an action loops on) instead of by offset, so the same
search works on every build.

    py -3 speeder/research/rider_words.py CHARTS [--2976]

CHARTS holds ride_upper.paac, ride_test3_lower.paac and m0004_ride_dragon_upper.paac as shipped.
Needs a paac_schema.py that reads them: BROOMY_RESEARCH names its folder (for exe 1.0.0.3392,
patch-dlc/work/speeder-bike/3392/newmount). With --2976 the 2976 layout is read and the result is
compared with the offsets riderfix.cpp had on that build; without it, mod/tests/riderwords.h is
written (or into OUTDIR when set): every word riderfix edits, its shipped value and the one it writes.
The plugin does the same search in C++ as each chart loads (mod/src/game/riderfind.cpp), so it works
on Broomy's charts with nodes spliced in too; `build.bat riderfind CHARTS` checks that the C++ search
finds what this one does.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get("BROOMY_RESEARCH", os.path.join(HERE, "..", "..", "..", "Broomy", "private",
                                                                   "research", "newmount")))
import paac_schema as ps  # noqa: E402

OUT = os.path.join(os.environ.get("OUTDIR") or os.path.join(HERE, "..", "..", "mod", "tests"), "riderwords.h")

# Action keys (hashlittle of the name), unchanged across builds.
TAKEOFF = 0x117F8F51        # Kliff's lower state the broom takeoff runs
SPARE = 0xFDA30B13          # spare lower action that plays the push-off
ORCA_RIDER = 0x01B8F8D3     # cut orca rider state the speeder idle runs in, lower chart
GROUND_DISPATCH = 0x07407E6A
AIR_DISPATCH = 0x90337E5C
BROOM_IDLE = 0x588C2001     # upper
ORCA_UNDERWATER = 0xEFD5260B
DISMOUNT_TARGET = 0xD69FE049
BROOM_DISMOUNTS = (0x18064912, 0x8B293B1B)
RIDE_IDLE = 0xF5150415      # the Wyvern RideOn chart's ground idle
PUSH_OFF = "1_pc/1_phm/00_riding/cd_phm_rd_wyvern_basic_00_00_air_move_fall_walk_end_00.paa"

# What riderfix.cpp had on exe 1.0.0.2976, for the --2976 check.
OLD = {
    "kBranch397": 0x6F3F1, "kBranch400": 0x6F48D, "kSlot934": 0x796B4 - 0x0B, "kBranch439": 0x6FC79,
    "kSlot1036": 0x7A1DC - 0x0B, "kSpareDuration": 0x1D4D0, "kPushOffAt": 0x31339, "kIkAt": 0x216BF,
    "kIdleRecord": 0x2E2DA, "kDonorRecord": 0x3F062, "kBranch544": 0x49834 - 0x14, "kIdleDuration": 0x1FBD8,
    "kDonorDuration": 0x1BD26, "kDonorRateEnd": 0x1BDDA, "kDismountBranch": 0x4998C,
    "kRideOnIdle0": 0xC0B4, "kRideOnIdle1": 0xC5FC, "kRideOnIdle2": 0xC630,
}


def load(folder, name):
    d = open(os.path.join(folder, name), "rb").read()
    tr = []
    c = ps.Chart.parse(d, trace=tr)
    assert c.pack() == d
    names = [n for n, _ in tr]
    sec = {}
    for i, (n, e) in enumerate(tr):
        if n != "node":
            sec[n] = (tr[i - 1][1] if i else 0, e)
    node_starts = [4] + [e for n, e in tr if n == "node"][:-1]
    return d, c, sec, node_starts


def action_at(c, sec, key):
    k = [a.key_hash for a in c.actions].index(key)
    start = sec["actions"][0]
    return start + k * ps.LAYOUTS["Action"].size, c.actions[k]


def branch_at(sec, bi):
    return sec["branches"][0] + 4 + bi * ps.LAYOUTS["Branch"].size


def slot_at(sec, si):
    return sec["slots"][0] + 4 + si * ps.LAYOUTS["Slot"].size


def marked(c, d, rec, field, marker):
    """Offset of `field` of `rec` in the file: set a marker, pack, find it."""
    old = getattr(rec, field)
    setattr(rec, field, marker)
    m = c.pack()
    setattr(rec, field, old)
    v = struct.pack("<f", marker) if isinstance(marker, float) else struct.pack("<I", marker)
    at = m.find(v)
    assert at > 0 and m.find(v, at + 1) < 0
    return at


def common(c, a):
    """An action's node that every rider plays (group hash 0); Broomy splices one for Damiane beside it."""
    return one((i for i in range(a.node_first, a.node_first + a.node_count) if c.nodes[i].header.group_hash == 0),
               "common node of %08X" % a.key_hash)


def one(xs, what):
    xs = list(xs)
    assert len(xs) == 1, (what, xs)
    return xs[0]


def lower(folder):
    d, c, sec, starts = load(folder, "ride_test3_lower.paac")
    out = {}
    # The two default branches of Kliff's lower dispatchers: the ones that take the ground and the
    # air dispatcher to their default state, cut (-1 frames) between two dragon poses; 2976 b397, b400.
    def dispatcher(target):
        return one((bi for bi, b in enumerate(c.branches) if b.target_hash == target and b.time_a == -1.0
                    and b.kind == 4 and (b.flags & 2) and b.cond_count == 0),
                   "default branch to %08X" % target)
    b397, b400 = dispatcher(GROUND_DISPATCH), dispatcher(AIR_DISPATCH)
    out["kBranch397"], out["kBranch400"] = branch_at(sec, b397), branch_at(sec, b400)
    # The takeoff's slot that shares b397.
    ta = c.find_action(TAKEOFF)
    s934 = one((ta.slot_first + i for i, s in enumerate(c.action_slots(ta)) if s.branch_index == b397), "takeoff slot")
    out["kSlot934"] = slot_at(sec, s934)
    # The spare action FDA30B13: one node; its slot whose branch loops on itself at the clip's end
    # (2976 slot 1036, b439), which no other slot uses.
    sp = c.find_action(SPARE)
    s1036 = one((sp.slot_first + i for i, s in enumerate(c.action_slots(sp))
                 if c.branches[s.branch_index].target_hash == SPARE), "spare loop slot")
    b439 = c.slots[s1036].branch_index
    assert c.branches[b439].kind == 7
    assert sum(1 for s in c.slots if s.branch_index == b439) == 1
    out["kSlot1036"], out["kBranch439"] = slot_at(sec, s1036), branch_at(sec, b439)
    # Branch 671: the clip-end exit to the broom idle that most states share (14 on 2976).
    owners = {}
    for a in c.actions:
        for s in c.action_slots(a):
            owners.setdefault(s.branch_index, set()).add(a.key_hash)
    ends = sorted(((len(owners.get(bi, ())), bi) for bi, b in enumerate(c.branches)
                   if b.kind == 7 and b.target_hash == BROOM_IDLE), reverse=True)
    assert ends and (len(ends) == 1 or ends[0][0] > ends[1][0]), ends[:3]
    b671 = ends[0][1]
    out["kSpareDuration"] = starts[common(c, sp)] + 0x04
    assert abs(struct.unpack_from("<f", d, out["kSpareDuration"])[0] - 0.6) < 1e-6
    # The push-off clip's path, once in the file.
    p = PUSH_OFF.encode() + b"\0"
    assert d.count(bytes([len(p)]) + p) == 1
    out["kPushOffAt"] = d.find(bytes([len(p)]) + p) + 1
    # 01B8F8D3's node switches on the LimbIK's left hand (limb hash FE419E03, "lefthand") at 0 s:
    # one event of type 0x65 on 2976, two of type 0x67 on 3392.
    oa = c.find_action(ORCA_RIDER)
    w = struct.unpack("<%dI" % (len(c.frame_event_buffer) // 4), c.frame_event_buffer)
    insts = [fi for ni in range(oa.node_first, oa.node_first + oa.node_count)
             for fi in c.nodes[ni].frame_event_instances
             if w[fi.event_index] & 0xFFFF == ps.EVT(0x65) and fi.time == 0.0 and fi.time_end == -1.0]
    assert insts and all(w[fi.event_index + ps.EVW(3)] == 0xFE419E03 for fi in insts), insts
    out["ikAts"] = [marked(c, d, fi, "time", 4321.25) for fi in insts]
    out["kIkAt"] = out["ikAts"][0]
    out["b397"], out["b400"], out["b439"], out["b671"], out["s934"], out["s1036"] = b397, b400, b439, b671, s934, s1036
    return d, out


def upper(folder):
    d, c, sec, starts = load(folder, "ride_upper.paac")
    out = {}
    out["kIdleRecord"], idle = action_at(c, sec, BROOM_IDLE)
    out["kDonorRecord"], donor = action_at(c, sec, ORCA_UNDERWATER)
    # The broom idle's loop: its anim-end branch back to itself (2976 b544).
    b544 = one((s.branch_index for s in c.action_slots(idle) if c.branches[s.branch_index].target_hash == BROOM_IDLE),
               "broom idle loop")
    out["kBranch544"] = branch_at(sec, b544)
    out["kIdleDuration"] = starts[common(c, idle)] + 0x04
    out["kDonorDuration"] = starts[common(c, donor)] + 0x04
    out["kDonorRateEnd"] = marked(c, d, c.nodes[common(c, donor)].animation_speeds[0], "time_end", 4321.25)
    # Branch 551: the one both of the broom's dismounts leave by, to D69FE049. Shipped, no other
    # state has it; Broomy's chart with Damiane's nodes gives it to two actions of its own as well.
    def leave(key):
        return one((s.branch_index for s in c.action_slots(c.find_action(key))
                    if c.branches[s.branch_index].target_hash == DISMOUNT_TARGET), "dismount of %08X" % key)
    b551 = leave(BROOM_DISMOUNTS[0])
    assert leave(BROOM_DISMOUNTS[1]) == b551
    out["kDismountBranch"] = branch_at(sec, b551)
    out["b544"], out["b551"] = b544, b551
    return d, out


def rideon(folder):
    d, c, sec, starts = load(folder, "m0004_ride_dragon_upper.paac")
    a = c.find_action(RIDE_IDLE)
    bis = sorted(s.branch_index for s in c.action_slots(a))
    assert len(bis) >= 3, bis   # three on 2976, four on 3392
    return d, {"kRideOnIdle%d" % i: branch_at(sec, bi) for i, bi in enumerate(bis)}




def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def u32(d, at):
    return struct.unpack_from("<I", d, at)[0]


def slot_swap(d, slot, to):
    """A slot's branch index (u16 at +0x0C) as the u32 a byte before it, as riderfix swaps it."""
    at = slot + 0x0B
    w = u32(d, at)
    return at, w, (w & ~0xFFFF00) | (to << 8)


def main():
    folder = sys.argv[1]
    old = "--2976" in sys.argv
    if old:
        ps.use_format(2976)
    dl, lo = lower(folder)
    du, up = upper(folder)
    dr, ro = rideon(folder)
    found = dict(lo, **up, **ro)
    for k, v in sorted(found.items()):
        print("%-16s %s" % (k, [hex(x) for x in v] if isinstance(v, list) else hex(v)))
    if old:
        bad = [(k, hex(v), hex(found[k])) for k, v in OLD.items() if found[k] != v]
        print("2976 offsets that differ from riderfix.cpp:", bad or "none")
        sys.exit(1 if bad else 0)
    cut, ten = f32(-1.0), f32(10.0)
    g, a, l = lo["kBranch397"], lo["kBranch400"], lo["kBranch439"]
    assert u32(dl, slot_swap(dl, lo["kSlot934"], 0)[0]) >> 8 & 0xFFFF == lo["b397"]
    assert u32(dl, slot_swap(dl, lo["kSlot1036"], 0)[0]) >> 8 & 0xFFFF == lo["b439"]
    assert u32(dl, l + 0x08) == f32(1.0) and u32(dl, lo["kSpareDuration"]) == f32(0.6)
    lower_rows = [
        (g + 0x14, GROUND_DISPATCH, ORCA_RIDER, "ground dispatcher default -> the speeder idle"),
        (g + 0x08, cut, ten, "its crossfade, a cut -> 10 frames"),
        (a + 0x14, AIR_DISPATCH, ORCA_RIDER, "air dispatcher default -> the speeder idle"),
        (a + 0x08, cut, ten, "its crossfade, a cut -> 10 frames"),
        slot_swap(dl, lo["kSlot934"], lo["b439"]) + ("the takeoff's slot -> the spare loop branch",),
        (l + 0x08, f32(1.0), f32(3.0), "the loop branch's crossfade 1 -> 3 frames"),
    ] + [(l + f, u32(dl, l + f), u32(dl, g + f), "the loop branch's +0x%02X as the ground branch's" % f)
         for f in (0x1C, 0x24, 0x28)] + [
        slot_swap(dl, lo["kSlot1036"], lo["b671"]) + ("the spare's loop slot -> the shared clip-end exit",),
        (lo["kSpareDuration"], f32(0.6), 0x40911111, "FDA30B13 lasts 136 frames"),
    ]
    for at in lo["ikAts"]:
        assert u32(dl, at) == 0 and u32(dl, at + 4) == cut
    upper_rows = [
        (up["kBranch544"] + 0x14, BROOM_IDLE, ORCA_UNDERWATER, "the broom idle's loop -> EFD5260B"),
        (up["kIdleDuration"], u32(du, up["kIdleDuration"]), f32(0.5), "the broom idle lasts 0.5 s"),
    ]
    donor_rows = [
        (up["kDonorDuration"], u32(du, up["kDonorDuration"]), 0x40555556, "EFD5260B lasts 100 frames"),
        (up["kDonorRateEnd"], u32(du, up["kDonorRateEnd"]), 0x405BBBBC, "its play-rate window ends at 3.43 s"),
    ]
    for at, was, now, why in lower_rows:
        assert u32(dl, at) == was, (why, hex(at))
    for at, was, now, why in upper_rows + donor_rows:
        assert u32(du, at) == was, (why, hex(at))
    p = PUSH_OFF.encode() + b"\0"
    assert dl[lo["kPushOffAt"] - 1] == len(p) and dl[lo["kPushOffAt"]:lo["kPushOffAt"] + len(p)] == p
    build = os.environ.get("BUILD", "1.0.0.3392")
    with open(OUT, "w", newline="\r\n") as o:
        o.write("#pragma once\n#include <cstdint>\n\n")
        o.write("// Generated by speeder/research/rider_words.py from the shipped ride_upper.paac,\n")
        o.write("// ride_test3_lower.paac and m0004_ride_dragon_upper.paac (build %s): what\n" % build)
        o.write("// riderfind.cpp must find in those files, for tests/riderfind.cpp.\n")
        o.write("namespace riderwords\n{\n")
        o.write("    struct Row { uint32_t offset, shipped, now; };\n")
        o.write("    inline constexpr uint32_t kLowerSize = %d, kUpperSize = %d, kRideOnSize = %d;\n" % (
            len(dl), len(du), len(dr)))
        for name, rows in (("kLower", lower_rows), ("kUpper", upper_rows), ("kDonor", donor_rows)):
            o.write("    inline constexpr Row %s[] = {\n" % name)
            for at, was, now, why in rows:
                o.write("        { 0x%05X, 0x%08X, 0x%08X },   // %s\n" % (at, was, now, why))
            o.write("    };\n")
        o.write("    inline constexpr uint32_t kIk[] = { %s };   // the hand IK instances' time word\n" % ", ".join(
            "0x%05X" % x for x in lo["ikAts"]))
        o.write("    inline constexpr uint32_t kPushOff = 0x%05X;\n" % lo["kPushOffAt"])
        o.write("    inline constexpr uint32_t kIdleRecord = 0x%05X, kDonorRecord = 0x%05X, kDismount = 0x%05X;\n" % (
            up["kIdleRecord"], up["kDonorRecord"], up["kDismountBranch"]))
        o.write("    inline constexpr uint32_t kRideOnIdle[] = { %s };\n" % ", ".join(
            "0x%05X" % ro["kRideOnIdle%d" % i] for i in range(len(ro))))
        o.write("}\n")
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
