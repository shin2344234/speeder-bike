"""Kliff on the speeder: the speeder's own riding clips and Kliff's.

    blender -b --factory-startup --python speeder/build_rider.py -- [--renders-only]

Both sides hold still. The game seats Kliff's origin on the mount's
B_Rider_01 once and then plays his clip and the mount's side by side, so
any motion on one side that the other does not share moves his hands off
the grips. Broomy serves its own broom clips at the broom's paths, so the
speeder's charts name clips of their own (cd_rd_speed_basic_*, the
broom's names with "speed" for "broom"): the broom idle's first frame
held for each clip's shipped length, so the speeder hangs level and still
on B_Rider_01, the bone its mesh is skinned to.

Kliff's clips (cd_phm_rd_speed_basic_*) are his broom idle's first frame
held for 100 frames, the length of the idle whose metadata they take:

  - his origin on B_Rider_01, as the game seats him (Broomy's KNOWLEDGE.md:
    CD Animator's seat_rider puts it at the clip's first record, 3.5 cm
    lower), and his pelvis keyed back to where build_mesh.py fitted the
    saddle under it;
  - his spine bent forward by LEAN degrees, a share on each spine bone,
    and his neck and head turned back so he looks ahead;
  - each fist closed palm down on its grip bar, the wrist straight: the
    hand turned so its knuckles point along the forearm with the finger
    roots running outward along the bar, and the arm IK'd, its elbow bent
    out and back toward ELBOW_POLE, until the middle of the finger roots
    sits where a fist on a 4 cm bar has them;
  - each boot's sole flat on its stirrup's foot plate, the toe TOE_GAP
    short of the plate's front end under the toe loop, the toes straight,
    the leg IK'd the same way.

Each clip is read back from the written file and measured, and rendered on
the speeder: speeder/out/rider_<clip>.png and rider_grid.png.

Writes under speeder/out/files at the game's paths: the speeder's clips and
their LODs under character/motion/4_riding/cd_r0032_00_broom, Kliff's
under character/motion/1_pc/1_phm/00_riding, and his idle once more under
the broom rider's idle name, which the plugin serves only without Broomy.
"""

import json
import math
import os
import shutil
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get("CD_ANIMATOR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "CD animator")))
import cd_animator  # noqa: E402
from cd_animator import anim, clip, export, packs  # noqa: E402

OUT = os.path.join(HERE, "out")
FILES = os.path.join(OUT, "files")
PAC = os.path.join(HERE, "..", "mod", "assets", "character", "model", "4_riding", "cd_r0032_00_broom",
                   "cd_r0032_00_speed_0001.pac")
BASE_COLOR = os.path.join(HERE, "src", "textures", "SpeederBikeForSketch_BaseColor.png")
SKELETON = "4_riding/cd_r0032_00_broom/cd_r0032_00_broom"
MOUNT_DIR = "4_riding/cd_r0032_00_broom/"
RIDER_DIR = "1_pc/1_phm/00_riding/"
BROOM_MOUNT, MOUNT = "cd_rd_broom_basic_00_00_", "cd_rd_speed_basic_00_00_"
BROOM_RIDER, RIDER = "cd_phm_rd_broom_basic_00_00_", "cd_phm_rd_speed_basic_00_00_"
IDLE = "nor_std_idle_01"
# Every clip the speeder's charts name (broomchart.cpp's kRules), and the
# takeoff, which is a clip of the plugin's own with no shipped length.
MOUNT_CLIPS = (IDLE, "nor_std_mount_l_00", "nor_std_mount_r_00", "nor_std_dismount_l_00", "nor_std_dismount_r_00",
               "nor_move_walk_f_ing_00", "nor_move_walkfast_f_ing_00", "nor_move_run_f_ing_00",
               "nor_move_runfast_f_ing_00", "nor_move_walkfast_f_75u_ing_00", "nor_move_walkfast_f_75d_ing_00",
               "nor_std_takeoff_00")
TAKEOFF_FRAMES = 136
FRAMES = 100        # Kliff's idle's length
# Forward bend of his spine, in degrees, by clip. make_rider_blend.py puts
# walk to runfast on rings at 20, 45, 75 and 100% of the speeder's top
# speed; the climb and dive clips are never reached on the ground but the
# blend names them, so they are walkfast's.
LEAN = {IDLE: 0.0, "nor_move_walk_f_ing_00": 4.0, "nor_move_walkfast_f_ing_00": 10.0,
        "nor_move_run_f_ing_00": 18.0, "nor_move_runfast_f_ing_00": 26.0,
        "nor_move_walkfast_f_75u_ing_00": 10.0, "nor_move_walkfast_f_75d_ing_00": 10.0}
SEAT = "B_Rider_01"
SIDES = ("L", "R")
SPINE = ("Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2")
LOOK = (("Bip01 Neck", 0.5), ("Bip01 Head", 1.0))
FINGERS = ("Finger1", "Finger2", "Finger3", "Finger4")
FORWARD = Vector((0.0, -1.0, 0.0))   # the speeder's nose, and Kliff's facing
UP = Vector((0.0, 0.0, 1.0))
# A hand on its bar: the knuckles point this far below level, along the
# forearm (build() follows the forearm from here), and the finger roots run
# from the index outward along the bar (left is +X, as Kliff faces -Y).
KNUCKLE_PITCH = 25.0
# The middle of the finger roots of a fist closed palm down on a 4 cm bar
# sits FIST_REACH from the bar's centre, FIST_TURN degrees above the knuckles'
# direction: up and ahead of the bar for knuckles pointing 25 degrees down
# (Seth saw the fists close on the grips with those, 5 October).
FIST_REACH, FIST_TURN = 0.0311, 70.0
# Where each elbow points, from halfway between shoulder and grip: out to
# his side, back and down, in metres; the arm IK's pole. With no pole the
# elbows hung straight down, the forearms level and the wrists cocked 30
# degrees (Seth's screenshot, 5 October).
ELBOW_POLE = (0.15, 0.40, 0.15)
OUTWARD = {"L": Vector((1.0, 0.0, 0.0)), "R": Vector((-1.0, 0.0, 0.0))}
# Kliff's boot in his rest pose, standing flat: the ball of the foot (Bip01
# Toe0's head) is SOLE_UNDER above the sole and the toe tip TOE_AHEAD in front
# of it (measured on his outfit, 5 October). Each sole lies on its stirrup's
# plate with the toe tip TOE_GAP short of the front end. Until then the
# boots hung 6 cm above the plates and 12 cm back, heels past their ends
# (Seth: "fix his feet so they fit in the stirrups").
SOLE_UNDER, TOE_AHEAD, TOE_GAP = 0.0173, 0.0837, 0.01


def update(f):
    bpy.context.scene.frame_set(f)
    bpy.context.view_layer.update()


def fetch(name):
    return packs.fetch_clip(packs.index(), name)


def length(obj, path):
    action, _ = anim.import_clip(bpy.context, obj, path)
    return round(action["cd_duration"] * anim.FPS)


def out_path(directory, leaf, lod=False):
    if lod:
        return os.path.join(FILES, "character", "motion", "motion_lod__", *directory.split("/"), leaf + "_lod.paa")
    return os.path.join(FILES, "character", "motion", *directory.split("/"), leaf + ".paa")


def write(obj, action, directory, leaf):
    path = out_path(directory, leaf)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    _written, problems = export.export_with_companions(obj, action, path)
    lod = out_path(directory, leaf, lod=True)
    os.makedirs(os.path.dirname(lod), exist_ok=True)
    shutil.move(path[:-4] + "_lod.paa", lod)
    os.remove(path[:-4] + ".paa_metabin")   # the plugin hands the game the shipped metadata
    for p in problems:
        print("  note:", p)
    return path


# ---- the speeder ------------------------------------------------------------

def mount_clips(broom):
    idle = fetch(MOUNT_DIR + BROOM_MOUNT + IDLE)
    for suffix in MOUNT_CLIPS:
        frames = TAKEOFF_FRAMES if suffix == "nor_std_takeoff_00" else length(broom, fetch(MOUNT_DIR + BROOM_MOUNT + suffix))
        action = clip.new_clip(bpy.context, broom, idle, MOUNT + suffix, frames, hold=True)
        write(broom, action, MOUNT_DIR, MOUNT + suffix)
        print(f"speeder {suffix}: the idle's first frame held for {frames} frames")
    # Leave the speeder on its held idle for Kliff.
    return clip.new_clip(bpy.context, broom, idle, "steady", FRAMES, hold=True)


# ---- Kliff ------------------------------------------------------------------

def seat(kliff, broom):
    """His origin on B_Rider_01, turned as the clip's first record turns him."""
    anim.seat_rider(kliff, broom)
    update(0)
    m = kliff.matrix_basis.copy()
    m.translation = broom.pose.bones[SEAT].matrix.translation
    kliff.matrix_basis = m
    update(0)


def seat_frame(broom):
    return broom.matrix_world @ broom.pose.bones[SEAT].matrix


def fist_offset(pitch):
    """From a grip bar's centre to the middle of the finger roots, for
    knuckles pointing `pitch` degrees below level."""
    a = math.radians(FIST_TURN - pitch)
    return (FORWARD * math.cos(a) + UP * math.sin(a)) * FIST_REACH


def targets(broom, fit):
    """The grip bars' centres, the balls of the feet's points and the hips."""
    s = seat_frame(broom)
    grips = {k: s @ Vector(v) for k, v in fit["grips"].items()}
    pegs = {}
    for k in fit["stirrups"]:
        d, n, front = plate(broom, fit, k)
        pegs[k] = front - d * (TOE_AHEAD + TOE_GAP) + n * SOLE_UNDER
    return grips, pegs, s @ Vector(fit["hips"])


def plate(broom, fit, side):
    """A stirrup's foot plate: its direction toward the front end, the
    normal of its top, and the middle of its front end, in world space."""
    s = seat_frame(broom)
    front, rear = s @ Vector(fit["stirrups"][side]["front"]), s @ Vector(fit["stirrups"][side]["rear"])
    d = (front - rear).normalized()
    n = d.cross(Vector((1.0, 0.0, 0.0))).normalized()
    if n.dot(UP) < 0:
        n.negate()
    return d, n, front


def fist(kliff, side):
    w = kliff.matrix_world
    pts = [w @ kliff.pose.bones[f"Bip01 {side} {f}"].head for f in FINGERS]
    return sum(pts, Vector()) / len(pts)


def palm_down(kliff, side, pitch=KNUCKLE_PITCH):
    """The hand's world turn that puts it palm down on its crosswise bar,
    the knuckles `pitch` degrees below level."""
    w = kliff.matrix_world
    pose = kliff.pose.bones
    roots = [w @ pose[f"Bip01 {side} {f}"].head for f in FINGERS]
    a0 = (sum(roots, Vector()) / len(roots) - (w @ pose[f"Bip01 {side} Hand"].head)).normalized()
    b0 = roots[-1] - roots[0]
    b0 = (b0 - a0 * b0.dot(a0)).normalized()
    pitch = math.radians(pitch)
    a = (FORWARD * math.cos(pitch) - UP * math.sin(pitch)).normalized()
    b = OUTWARD[side] - a * OUTWARD[side].dot(a)
    b.normalize()
    m0 = Matrix((a0, b0, a0.cross(b0))).transposed()
    m1 = Matrix((a, b, a.cross(b))).transposed()
    return (m1 @ m0.inverted()).to_quaternion() @ (w @ pose[f"Bip01 {side} Hand"].matrix).to_quaternion()


def ball(kliff, side):
    return kliff.matrix_world @ kliff.pose.bones[f"Bip01 {side} Toe0"].head


def key_all(pb, path):
    for f in (0, FRAMES):
        pb.keyframe_insert(path, frame=f)


def set_world(kliff, pb, m):
    pb.matrix = kliff.matrix_world.inverted() @ m
    bpy.context.view_layer.update()


def local_axis(pb, world_axis):
    m = pb.matrix.to_3x3() @ pb.matrix_basis.to_3x3().inverted()
    return (m.inverted() @ world_axis).normalized()


def lean_sign(kliff, pb, axis):
    head = kliff.pose.bones["Bip01 Head"]
    base = pb.rotation_quaternion.copy()
    before = kliff.matrix_world @ head.head
    pb.rotation_quaternion = Quaternion(axis, math.radians(10)) @ base
    bpy.context.view_layer.update()
    after = kliff.matrix_world @ head.head
    pb.rotation_quaternion = base
    bpy.context.view_layer.update()
    return 1 if (after - before).dot(FORWARD) > 0 else -1


def world_turn(kliff, pb, q):
    w = kliff.matrix_world.to_quaternion()
    m = pb.matrix.copy()
    turned = (w.inverted() @ q @ w).to_matrix().to_4x4() @ m
    turned.translation = m.translation
    pb.matrix = turned


def build(kliff, broom, fit, suffix):
    """Kliff's clip for `suffix`, with its IK still on. Returns the action
    and the IK pieces to take off after export."""
    template = fetch(RIDER_DIR + BROOM_RIDER + IDLE)
    action = clip.new_clip(bpy.context, kliff, template, RIDER + suffix, FRAMES, hold=True)
    seat(kliff, broom)
    pose = kliff.pose.bones
    grips, pegs, hips = targets(broom, fit)

    # Pelvis back where the saddle was fitted under it: move his root.
    update(0)
    pelvis = kliff.matrix_world @ pose["Bip01 Pelvis"].head
    root = pose["Bip01"]
    m = kliff.matrix_world @ root.matrix
    m.translation += hips - pelvis
    set_world(kliff, root, m)
    key_all(root, "location")

    # Where his head faces before the lean.
    update(0)
    look = (kliff.matrix_world @ pose["Bip01 Head"].matrix).to_quaternion()
    hands = {s: palm_down(kliff, s) for s in SIDES}
    # Each foot as it stands flat in his rest pose, turned so its forward
    # and up are its plate's. The toes go back to rest, straight.
    feet = {}
    bones = kliff.data.bones
    w3 = kliff.matrix_world.to_3x3()
    for s in SIDES:
        up0 = (w3 @ Vector((0.0, 0.0, 1.0))).normalized()
        f0 = w3 @ (bones[f"Bip01 {s} Toe0"].head_local - bones[f"Bip01 {s} Foot"].head_local)
        f0 = (f0 - up0 * f0.dot(up0)).normalized()
        d, n, _front = plate(broom, fit, s)
        m0 = Matrix((f0, up0, f0.cross(up0))).transposed()
        m1 = Matrix((d, n, d.cross(n))).transposed()
        rest = (kliff.matrix_world @ bones[f"Bip01 {s} Foot"].matrix_local).to_quaternion()
        feet[s] = (m1 @ m0.inverted()).to_quaternion() @ rest
        toe = pose[f"Bip01 {s} Toe0"]
        toe.rotation_quaternion = Quaternion()
        key_all(toe, "rotation_quaternion")

    # The spine.
    deg = LEAN[suffix]
    share = math.radians(deg) / len(SPINE)
    for n in SPINE:
        pb = pose[n]
        axis = local_axis(pb, Vector((1.0, 0.0, 0.0)))
        pb.rotation_quaternion = Quaternion(axis, share * lean_sign(kliff, pb, axis)) @ pb.rotation_quaternion
        bpy.context.view_layer.update()
        key_all(pb, "rotation_quaternion")
    # The head back to its facing: the neck takes its share, the head the rest.
    for n, part in LOOK:
        update(0)
        fix = look @ (kliff.matrix_world @ pose["Bip01 Head"].matrix).to_quaternion().inverted()
        if fix.w < 0:
            fix.negate()
        world_turn(kliff, pose[n], Quaternion().slerp(fix, part))
        bpy.context.view_layer.update()
        key_all(pose[n], "rotation_quaternion")

    # Hands and feet: an empty for each, which the limb's IK reaches for
    # and whose turn the hand or foot copies; walked until the fist or the
    # ball of the foot lands on its point. Each arm's IK has a pole that
    # bends the elbow out and back, and the hand's knuckles are turned to
    # follow the forearm, which moves the fist's point on the bar.
    made = []
    limbs = []
    arms = {}
    pitch = {s: KNUCKLE_PITCH for s in SIDES}
    w = kliff.matrix_world
    for s in SIDES:
        for bone, end, rot, point, find in ((f"Bip01 {s} Forearm", f"Bip01 {s} Hand", hands[s],
                                             lambda s=s: grips[s] + fist_offset(pitch[s]), lambda k, s=s: fist(k, s)),
                                            (f"Bip01 {s} Calf", f"Bip01 {s} Foot", feet[s],
                                             lambda s=s: pegs[s], lambda k, s=s: ball(k, s))):
            place = bpy.data.objects.new(f"place {bone}", None)
            bpy.context.scene.collection.objects.link(place)
            place.rotation_mode = "QUATERNION"
            place.location = w @ pose[bone].tail
            place.rotation_quaternion = rot
            ik = pose[bone].constraints.new("IK")
            ik.target, ik.chain_count = place, 2
            if bone.endswith("Forearm"):
                pole = bpy.data.objects.new(f"elbow {s}", None)
                bpy.context.scene.collection.objects.link(pole)
                out, back, down = ELBOW_POLE
                pole.location = ((w @ pose[f"Bip01 {s} UpperArm"].head) + grips[s]) / 2 + OUTWARD[s] * out                     - FORWARD * back - UP * down
                # -180 points the elbow at the pole on this rig (measured).
                ik.pole_target, ik.pole_angle = pole, math.radians(-180.0)
                arms[s] = place
            keep = pose[end].constraints.new("COPY_ROTATION")
            keep.target = place
            made.append((pose[bone], ik, pose[end], keep, place))
            limbs.append((place, point, find))
    for turn in range(6):
        for _round in range(15):
            worst = 0.0
            update(0)
            for place, point, find in limbs:
                err = point() - find(kliff)
                worst = max(worst, err.length)
                place.location = place.location + err
            bpy.context.view_layer.update()
            if worst < 0.0005:
                break
        if turn == 5:
            break
        for s, place in arms.items():
            fore = (w @ pose[f"Bip01 {s} Hand"].head) - (w @ pose[f"Bip01 {s} Forearm"].head)
            fore = (fore - OUTWARD[s] * fore.dot(OUTWARD[s])).normalized()
            pitch[s] = math.degrees(math.asin(max(-1.0, min(1.0, -fore.dot(UP)))))
            place.rotation_quaternion = palm_down(kliff, s, pitch[s])
        bpy.context.view_layer.update()
    for place, _point, _find in limbs:
        place.keyframe_insert("location", frame=0)
        place.keyframe_insert("rotation_quaternion", frame=0)

    # Kliff's LimbIK (posemodifierdata.xml) pulls each B_IK hand and foot
    # bone onto its B_TL_IK target when a state turns it on. The broom
    # template has no tracks for the targets, which would leave them at rest
    # at his belt. On the IK bones themselves the IK has nothing to move.
    # (His left hand at his belt in game, 4 October, was not this: see
    # riderfix.cpp.)
    update(0)
    for s in SIDES:
        for limb in ("Hand", "Foot"):
            target = pose[f"B_TL_IK_{s}_{limb}_00"]
            target.matrix = pose[f"B_IK_{s}_{limb}"].matrix.copy()
            bpy.context.view_layer.update()
            key_all(target, "location")
            key_all(target, "rotation_quaternion")
    return action, made


def unpin(made):
    for pb, ik, end, keep, place in made:
        pole = ik.pole_target
        pb.constraints.remove(ik)
        if pole:
            bpy.data.objects.remove(pole)
        end.constraints.remove(keep)
        bpy.data.objects.remove(place)


def measure(kliff, broom, fit):
    """How far each fist and foot is from its point, on the clip as read
    back from the file, every 10 frames: for a fist, how far the middle of
    its finger roots is off FIST_REACH from the bar's centre."""
    grips, pegs, hips = targets(broom, fit)
    worst = {"hands": 0.0, "feet": 0.0, "pelvis": 0.0, "ik": 0.0}
    pose = kliff.pose.bones
    for f in range(0, FRAMES + 1, 10):
        update(f)
        for s in SIDES:
            worst["hands"] = max(worst["hands"], abs((fist(kliff, s) - grips[s]).length - FIST_REACH))
            worst["feet"] = max(worst["feet"], (ball(kliff, s) - pegs[s]).length)
            for limb in ("Hand", "Foot"):
                worst["ik"] = max(worst["ik"], (pose[f"B_TL_IK_{s}_{limb}_00"].head - pose[f"B_IK_{s}_{limb}"].head).length)
        worst["pelvis"] = max(worst["pelvis"],
                              ((kliff.matrix_world @ kliff.pose.bones["Bip01 Pelvis"].head) - hips).length)
    return worst


# ---- renders ----------------------------------------------------------------

def setup_render(broom):
    bpy.context.view_layer.objects.active = broom
    bpy.ops.cd_animator.import_pac(filepath=PAC, textures=False)
    mesh = next(o for o in bpy.data.objects if o.type == "MESH" and o.parent is broom)
    mat = mesh.data.materials[0]
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(BASE_COLOR)
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    mat.node_tree.nodes.active = tex
    scene = bpy.context.scene
    try:
        scene.render.engine = "BLENDER_EEVEE"
    except TypeError:
        scene.render.engine = "BLENDER_EEVEE_NEXT"
    scene.world = bpy.data.worlds.new("w")
    scene.world.use_nodes = True
    bg = next(n for n in scene.world.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = (0.25, 0.25, 0.27, 1)
    bg.inputs["Strength"].default_value = 1.5
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (0.9, 0.2, -0.6)
    scene.collection.objects.link(sun)
    scene.render.resolution_x, scene.render.resolution_y = 640, 640
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    return cam


VIEWS = (("side", (1.0, 0.0, 0.05), 2.4), ("front", (0.55, -1.0, 0.35), 2.4), ("hand_side", (1.0, -0.15, 0.2), 0.45),
         ("hands_front", (0.0, -1.0, 0.25), 0.8), ("hands_top", (0.15, -0.35, 1.0), 0.8), ("feet", (1.0, 0.25, 0.05), 0.9))


def render(cam, broom, fit, name):
    grips, pegs, hips = targets(broom, fit)
    centre = {"side": hips + Vector((0, -0.2, -0.05)), "front": hips + Vector((0, -0.2, -0.05)),
              "hand_side": grips["L"], "hands_front": (grips["L"] + grips["R"]) / 2,
              "hands_top": (grips["L"] + grips["R"]) / 2, "feet": (pegs["L"] + pegs["R"]) / 2}
    shots = []
    for view, d, scale in VIEWS:
        v = Vector(d).normalized()
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = scale
        cam.location = centre[view] + v * 6
        cam.rotation_euler = (-v).to_track_quat("-Z", "Y").to_euler()
        path = os.path.join(OUT, f"rider_{name}_{view}.png")
        bpy.context.scene.render.filepath = path
        update(0)
        bpy.ops.render.render(write_still=True)
        shots.append(path)
    return shots


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    with open(os.path.join(OUT, "fit.json")) as f:
        fit = json.load(f)
    cd_animator.register()
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    bpy.ops.cd_animator.game_skeleton(skeleton=SKELETON)
    broom = bpy.context.object
    cam = setup_render(broom)
    if "--renders-only" in argv:
        steady = clip.new_clip(bpy.context, broom, fetch(MOUNT_DIR + BROOM_MOUNT + IDLE), "steady", FRAMES, hold=True)
    else:
        steady = mount_clips(broom)
    broom.animation_data.action = steady
    bpy.ops.cd_animator.game_character(clip=RIDER_DIR + BROOM_RIDER + IDLE, gear="AWAY", cloth=False, sway=False,
                                       voice=False)
    kliff = next(o for o in bpy.data.objects if o.type == "ARMATURE" and o is not broom)
    bpy.context.view_layer.objects.active = kliff

    report = []
    for suffix in LEAN:
        path = out_path(RIDER_DIR, RIDER + suffix)
        if "--renders-only" not in argv:
            action, made = build(kliff, broom, fit, suffix)
            path = write(kliff, action, RIDER_DIR, RIDER + suffix)
            unpin(made)
        # Read back from the file, as the game will play it.
        anim.import_clip(bpy.context, kliff, path)
        seat(kliff, broom)
        worst = measure(kliff, broom, fit)
        line = (f"{suffix}: lean {LEAN[suffix]:.0f} degrees; fists within {worst['hands'] * 1000:.1f} mm of the "
                f"grips, feet within {worst['feet'] * 1000:.1f} mm of the footrests, pelvis within "
                f"{worst['pelvis'] * 1000:.1f} mm of the saddle fit, IK targets within {worst['ik'] * 1000:.1f} mm of "
                f"the hands and feet")
        print(line)
        report.append(line)
        if suffix in (IDLE, "nor_move_runfast_f_ing_00"):
            render(cam, broom, fit, suffix)
    if "--renders-only" not in argv:
        idle = out_path(RIDER_DIR, RIDER + IDLE)
        for lod in (False, True):
            src = out_path(RIDER_DIR, RIDER + IDLE, lod)
            dst = out_path(RIDER_DIR, BROOM_RIDER + IDLE, lod)
            shutil.copyfile(src, dst)
        print("copied", os.path.basename(idle), "under the broom rider's idle name")
    with open(os.path.join(OUT, "rider_fit.txt"), "w") as f:
        f.write("\n".join(report) + "\n")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "rider.blend"))
    sys.stdout.flush()
    os._exit(0)


main()
