"""The speeder's two riding blends, from the game's broom blends.

    py -3 speeder/make_blends.py

The speeder's charts name speed_riding_move.motionblending wherever the
Wyvern's name a blend (broomchart.cpp): the broom's broom_riding_move with
every clip renamed to the speeder's own (build_rider.py), which all hold the
same still pose, so where the blend lands does not matter.

Kliff's lower chart plays his lean blend while the speeder is ridden. Broomy
serves its own broom_rider_move at the broom's path, so riderfix.cpp sends
him to a state of the cut orca rider instead, 01B8F8D3, whose node plays
orca_rider_move_ing.motionblending and whose branches are the broom idle's.
That file here is the broom's rider blend rebuilt on rings at the speeder's
speeds (Broomy's broom_blend.py, which this follows) with his speeder clips:
walk at 20% of the top speed, walkfast at 45, run at 75 and runfast at the
top, so he leans further the faster the speeder goes. Every one of his
clips is 100 frames, so every phase entry is [0, 100]. Its speed eases at
1.5 a second, the value Broomy's leanblend.cpp knows Kliff's lean blend by,
so with Broomy installed the lean follows the speed every frame.

Writes both under speeder/out/files at the game's paths.
"""

import copy
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.environ.get("CD_ANIMATOR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "CD animator")), "cd_animator"))
from formats import motionblend as mc  # noqa: E402
from formats import paz  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = os.path.join(HERE, "out", "files")
GAME = os.environ.get("CRIMSON_DESERT") or sys.exit("Set CRIMSON_DESERT to the game's folder, the one holding 0009.")
BLENDS = "character/binary/motionblending/"
MOUNT_SRC, MOUNT_OUT = BLENDS + "broom_riding_move.motionblending", BLENDS + "speed_riding_move.motionblending"
RIDER_SRC, RIDER_OUT = BLENDS + "phm/broom_rider_move.motionblending", BLENDS + "phm/orca_rider_move_ing.motionblending"
RENAMES = ((b"cd_rd_broom_basic_", b"cd_rd_speed_basic_"), (b"cd_phm_rd_broom_basic_", b"cd_phm_rd_speed_basic_"))

# The ground top speed at GroundSpeed=350: 3.5 times the Wyvern's 11 m/s run.
TOP = 38.5
IDLE, WALK, WALKFAST, RUN, RUNFAST, UP, DOWN = range(7)
RINGS = [(0.20 * TOP, WALK), (0.45 * TOP, WALKFAST), (0.75 * TOP, RUN), (TOP, RUNFAST), (2 * TOP, RUNFAST)]
RAYS = [-90.0, -75.0, 0.0, 75.0, 90.0]
SMOOTHING = 1.5
FRAMES = 100


def shipped(paths):
    want = {p.lower() for p in paths}
    out = {}
    for pamt in ("0009", "0010", "0012"):
        for e in paz.read_index(os.path.join(GAME, pamt, "0.pamt")):
            if e.path.lower() in want and e.path.lower() not in out:
                out[e.path.lower()] = paz.read(e)
    missing = want - set(out)
    assert not missing, missing
    return out


def renamed(data):
    for old, new in RENAMES:
        assert len(old) == len(new)
        data = data.replace(old, new)
    return data


def rider_blend(d):
    """Broomy's broom_blend.py build(), on the speeder's rings, every ray
    level."""
    mb = mc.parse(d)
    assert mc.write(mb) == d
    root = mb.root.values
    names = root["_animationFileNames"]
    assert names[UP].endswith(b"_75u_ing_00.paa") and names[DOWN].endswith(b"_75d_ing_00.paa")
    assert names[RUNFAST].endswith(b"runfast_f_ing_00.paa")

    points = [((0.0, 0.0), IDLE)]
    at = {}
    for ri, (r, level) in enumerate(RINGS):
        for ai, a in enumerate(RAYS):
            x = 0.0 if abs(a) == 90.0 else r * math.cos(math.radians(a))
            y = r * math.sin(math.radians(a))
            at[(ri, ai)] = len(points)
            points.append(((x, y), level))

    def ccw(i, j, k):
        (ax, ay), (bx, by), (cx, cy) = points[i][0], points[j][0], points[k][0]
        cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        assert abs(cross) > 1e-6
        return (i, j, k) if cross > 0 else (i, k, j)

    tris = []
    for ai in range(len(RAYS) - 1):
        tris.append(ccw(0, at[(0, ai)], at[(0, ai + 1)]))
        for ri in range(len(RINGS) - 1):
            a, b = at[(ri, ai)], at[(ri, ai + 1)]
            c, e = at[(ri + 1, ai)], at[(ri + 1, ai + 1)]
            tris.append(ccw(a, c, e))
            tris.append(ccw(a, e, b))

    f32 = lambda v: struct.unpack("<f", struct.pack("<f", v))[0]  # noqa: E731
    examples = root["_motionExamples"]
    template = examples.items[0]
    first_id = max(e.id for e in examples.items) + 1
    new_examples = []
    for i, ((x, y), level) in enumerate(points):
        e = copy.deepcopy(template)
        e.id = first_id + i
        e.values["_animationDataIndex"] = [level]
        e.values["_animationDataProbability"] = [1]
        e.values["_parameters"] = [f32(x), f32(y)]
        new_examples.append(e)
    examples.items = new_examples
    examples.K = new_examples[-1].id

    tset = root["_delaunayTriangles"].items[0]
    tlist = tset.values["_triangles"]
    ttemplate = tlist.items[0]
    tid = first_id + len(points)
    new_tris = []
    for t in tris:
        e = copy.deepcopy(ttemplate)
        e.id = tid
        tid += 1
        vs = [points[i][0] for i in t]
        e.values["_vert"] = [(f32(x), f32(y)) for x, y in vs]
        e.values["_index"] = list(t)
        e.values["_center"] = (f32(sum(v[0] for v in vs) / 3), f32(sum(v[1] for v in vs) / 3))
        new_tris.append(e)
    tlist.items = new_tris
    tlist.K = new_tris[-1].id

    xs = [p[0][0] for p in points]
    ys = [p[0][1] for p in points]
    tset.values["_min"] = (f32(min(xs)), f32(min(ys)))
    tset.values["_max"] = (f32(max(xs)), f32(max(ys)))
    mm = root["_parameterMinMax"]
    root["_parameterMinMax"] = [f32(min(xs)), f32(max(xs)), f32(min(ys)), f32(max(ys))] + list(mm[4:])
    out = mc.write(mb)
    assert mc.write(mc.parse(out)) == out
    for c in (IDLE, WALK, WALKFAST, RUN, RUNFAST, UP, DOWN):
        root["_phaseInfo"].items[c].values["_phases"] = [0, FRAMES]
    mc.set_scalar(mb, root["_dimensions"].items[0], "_parameterSmoothingFactor", SMOOTHING)
    out = mc.write(mb)
    assert mc.write(mc.parse(out)) == out
    return out, len(points), len(tris)


def write(game, data):
    path = os.path.join(FILES, *game.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print("wrote", game, len(data), "bytes")


def main():
    src = shipped([MOUNT_SRC, RIDER_SRC])
    mount = renamed(src[MOUNT_SRC])
    assert b"broom_basic" not in mount
    write(MOUNT_OUT, mount)
    rider, points, tris = rider_blend(src[RIDER_SRC])
    rider = renamed(rider)
    assert b"broom_basic" not in rider and rider.count(b"cd_phm_rd_speed_basic_") == 7
    write(RIDER_OUT, rider)
    print(f"Kliff's blend: {points} examples, {tris} triangles; rings at "
          + ", ".join(f"{r:.1f}" for r, _ in RINGS) + " m/s")


main()
