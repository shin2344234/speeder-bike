"""The speeder stays on the ground. Seth, 4 October: "make sure the speeder
can't take off as its a ground only vehicle".

The Wyvern's riding lower chart marks every state in the air with bit 40 of
its action record (byte +0x0D, bit 0; takeoff_patches.py in Broomy's research
has the story): the takeoff, the flight idle, hold to descend and the rest.
Every branch in the three charts whose target, for either layer, is one of
those states gets frame tag 0x27, which no clip window carries, so it never
fires (flourish_patches.py does the same for rolls and glides). Branches that
start inside the air states are left alone: nothing reaches them now. For
each ground state that loses a branch, the exits it keeps are printed, so no
state is left without one.

    py -3 speeder/research/ground_patches.py CHARTS

CHARTS holds the shipped m0004_dragon_upper.paac, m0004_ride_dragon_lower.paac
and m0004_ride_dragon_upper.paac. Needs a paac_schema.py that reads them on the
path: BROOMY_RESEARCH names its folder, Broomy's private/research/newmount by
default. For exe 1.0.0.3392 that is the port in
patch-dlc/work/speeder-bike/3392/newmount until Broomy's own reads 3392.
Writes mod/src/game/groundpatches.h, or into OUTDIR when set; BUILD names the
build in the header.
"""
import os
import struct
import sys

sys.path.insert(0, os.environ.get("BROOMY_RESEARCH", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Broomy", "private", "research", "newmount")))
import paac_schema as ps   # noqa: E402
import chartgraph as cg    # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.environ.get("OUTDIR") or os.path.join(HERE, "..", "..", "mod", "src", "game"), "groundpatches.h")
BUILD = os.environ.get("BUILD", "1.0.0.3392")
NEVER = 0x27
SIZE = ps.LAYOUTS["Action"].size      # 0x114 on 2976, 0x120 on 3392
BRANCH = ps.LAYOUTS["Branch"].size    # 52 on 2976, 56 on 3392
BIT = 40
ENTRY, FLY_IDLE, GROUND_IDLE = 0x866C7489, 0xA9D16F76, 0x622E5BEC
CHARTS = [(0, "m0004_dragon_upper.paac"), (1, "m0004_ride_dragon_lower.paac"), (2, "m0004_ride_dragon_upper.paac")]


def load(folder, name):
    d = open(os.path.join(folder, name), "rb").read()
    tr = []
    c = ps.Chart.parse(d, trace=tr)
    return d, c, tr


def airborne(folder):
    d, c, tr = load(folder, CHARTS[1][1])
    names = [n for n, _ in tr]
    i = names.index("actions")
    start, end = tr[i - 1][1], tr[i][1]
    assert end - start == SIZE * len(c.actions)
    air = set()
    for k, a in enumerate(c.actions):
        base = start + k * SIZE
        assert struct.unpack_from("<I", d, base + 0x18)[0] == a.key_hash
        if struct.unpack_from("<I", d, base + 0x08 + (BIT // 32) * 4)[0] & (1 << (BIT % 32)):
            air.add(a.key_hash)
    return air


def main():
    folder = sys.argv[1]
    air = airborne(folder)
    assert 0x117F8F51 in air and 0xE066175D in air   # the takeoff and the flight idle
    print("air states in the lower chart: %s" % " ".join(sorted(cg.nm(h) for h in air)))
    rows = []
    for index, f in CHARTS:
        d, c, tr = load(folder, f)
        names = [n for n, _ in tr]
        start = tr[names.index("branches") - 1][1] + 4
        assert all(t.tag != NEVER for n in c.nodes for t in n.frame_tags)
        owners = {}
        for a in c.actions:
            for s in c.action_slots(a):
                owners.setdefault(s.branch_index, set()).add(a.key_hash)
        lower = index == 1
        cut = set()
        for bi, b in enumerate(c.branches):
            into = (lower and b.target_hash in air) or b.target_hash2 in air
            if not into:
                continue
            own = owners.get(bi, set())
            if lower and own and own <= air:
                continue   # starts in the air, which nothing reaches now
            cut.add(bi)
            at = start + BRANCH * bi + 0x2C
            w = struct.unpack_from("<I", d, at)[0]
            assert (w >> 8) & 0xFF == b.frame_tag
            to = b.target_hash if lower and b.target_hash in air else b.target_hash2
            rows.append((index, at, w, (w & ~0xFF00) | (NEVER << 8), "%s b%d -> %s" % (f[:-5], bi, cg.nm(to))))
        for a in c.actions:
            if lower and a.key_hash in air:
                continue
            mine = [s.branch_index for s in c.action_slots(a)]
            if not any(bi in cut for bi in mine):
                continue
            left = sorted(set(cg.nm(c.branches[bi].target_hash) for bi in mine if bi not in cut))
            print("%s %s keeps %d exits: %s" % (f[:-5], cg.nm(a.key_hash), len(left), " ".join(left)))

    # The speeder's body is the Wyvern's m0004_dragon_upper (chart 0). Its
    # entry 866C7489 goes to the swim idle, to the ground idle 622E5BEC, and
    # by its last branch to the Wyvern's fly idle A9D16F76, which only loops
    # on itself. A speeder that arrives above the ground (Buxunqingmo, 10
    # October: "always spawns in the air on the first summon") hung there
    # until a second call. The last branch now goes to the ground idle, as in
    # Broomy 1.0.5 (private/research/newmount/ground_spawn_patches.py).
    d, c, tr = load(folder, CHARTS[0][1])
    named = {n: e for n, e in tr if n != "node"}
    assert c.default_action_hash == ENTRY
    slots = c.action_slots(c.find_action(ENTRY))
    assert [c.branches[s.branch_index].target_hash for s in slots] == [0x6FECC1D6, GROUND_IDLE, FLY_IDLE]
    index = slots[2].branch_index
    assert sum(1 for s in c.slots if s.branch_index == index) == 1, "another slot shares the branch"
    at = named["condition_offsets"] + 4 + index * BRANCH + 0x14
    assert struct.unpack_from("<I", d, at)[0] == FLY_IDLE
    rows.insert(0, (0, at, FLY_IDLE, GROUND_IDLE,
                    "m0004_dragon_upper entry 866C7489 b%d, the fly idle -> the ground idle" % index))

    with open(OUT, "w", newline="\r\n") as o:
        o.write("#pragma once\n#include <cstdint>\n\n#include \"game/hoverpatches.h\"\n\n")
        o.write("// Generated by speeder/research/ground_patches.py from the Wyvern's charts\n")
        o.write("// (build " + BUILD + "). Every branch into a state in the air, the takeoff\n")
        o.write("// among them, gets frame tag 0x27, which no clip window carries, so it\n")
        o.write("// never fires: the speeder stays on the ground. The first entry is the\n")
        o.write("// Wyvern body chart's entry, whose last branch goes to the ground idle, not\n")
        o.write("// the fly idle, so a speeder that arrives above the ground does not hang.\n")
        o.write("namespace bm::broomchart\n{\n    inline constexpr HoverPatch kGroundPatches[] = {\n")
        for r in rows:
            o.write("        { %d, 0x%05X, 0x%08X, 0x%08X },   // %s\n" % r)
        o.write("    };\n}\n")
    for r in rows:
        print("chart %d @%05X %08X -> %08X  %s" % r)


main()
