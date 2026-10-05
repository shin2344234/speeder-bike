"""Put the speeder's files into the plugin.

    py -3 speeder/pack_assets.py <the broom's .pac_xml> <the broom's .prefab>

(both unpacked). Copies what build_mesh.py, build_rider.py and
make_textures.py wrote under speeder/out/files into mod/assets, adds the
speeder's prefab, prefab data, and material file, and writes
mod/src/speeder.rc, which carries each file as an RCDATA resource, and
mod/src/game/speederfiles.h, which lists them with the shipped file the find
and existence checks see for a path no pack holds.

The speeder's appearance names the cut Phoenix's prefab
(CD_M0004_00_Phoenix_00_0001), which nothing else uses. The plugin serves at
its paths the broom's own prefab and prefab data, the setup Broomy runs
with, with the prefab naming the speeder's mesh,
cd_r0032_00_speed_0001.pac, beside the broom's and the same length, so the
prefab's sizes hold. The Phoenix's own prefab crashed the game on the first
call (4 October): it differs from the broom's in more than its mesh. Broomy
keeps the broom's files. Kliff's riding clips stay at the broom rider's
paths, which Broomy serves too, so the plugin leaves those to Broomy when
Broomy is installed (`shared`). Aliases are paths no pack holds that load a
shipped file as they are.
"""

import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.join(HERE, "..", "mod")
FILES = os.path.join(HERE, "out", "files")
ASSETS = os.path.join(MOD, "assets")
FIRST_ID = 201

BROOM_TEX = "character/texture/cd_t0000_broom_0001"
SPEEDER_TEX = "character/texture/cd_r0032_00_speeder_0001"
BROOM = "4_riding/cd_r0032_00_broom/cd_r0032_00_broom_0001"
SPEED = "4_riding/cd_r0032_00_broom/cd_r0032_00_speed_0001"
BROOM_MESH = "character/model/" + BROOM + ".pac"
PHOENIX = "2_mon/cd_m0004_00_dragon/cd_m0004_00_phoenix/cd_m0004_00_phoenix_00_0001"
MESH = "character/model/" + SPEED + ".pac"
MATERIAL = "character/modelproperty/" + SPEED + ".pac_xml"
PREFAB = "character/bin__/prefab/" + PHOENIX + ".prefab"
PREFAB_DATA = "character/prefab/" + PHOENIX + ".prefabdata_xml"
# The Phoenix's prefab's own eight bytes at +6 (an id, not a hash of its
# name), which the copy of the broom's keeps, so the two prefabs stay apart.
PHOENIX_PREFAB_ID = bytes.fromhex("bf93ba147ae46f2b")
# The broom's own prefab data, as it ships: a BOM and the broom's skeleton.
PREFAB_TEXT = (b'\xef\xbb\xbf<NudePrefabData>\r\n\t<SkeletonName FileName="4_riding/cd_r0032_00_Broom/'
               b'cd_r0032_00_Broom.pab"/>\r\n</NudePrefabData>\r\n\r\n')
RIDER = "character/motion/1_pc/1_phm/00_riding/cd_phm_rd_broom_basic_00_00_"
RIDER_LOD = "character/motion/motion_lod__/1_pc/1_phm/00_riding/cd_phm_rd_broom_basic_00_00_"

# Paths no pack holds, and the shipped file standing in for each.
STAND_INS = {
    SPEEDER_TEX + ".dds": BROOM_TEX + ".dds",
    SPEEDER_TEX + "_n.dds": BROOM_TEX + "_n.dds",
    SPEEDER_TEX + "_sp.dds": BROOM_TEX + "_sp.dds",
    MESH: BROOM_MESH,
    MATERIAL: "character/modelproperty/" + BROOM + ".pac_xml",
    # make_icons.py's, each standing in for a shipped texture of its size.
    "ui/texture/image/portraitimage/cd_mercenary_portrait_riding_speeder_1.dds":
        "ui/texture/image/portraitimage/cd_mercenary_portrait_domestic_animal_riding_alpineibex_1.dds",
    "ui/texture/cd_icon_map_speeder.dds": "ui/texture/image/customizeimage/cd_customize_empty_image.dds",
}

# Paths no pack holds, loaded as the shipped file beside them: the mesh's
# physics, found by the mesh's name, and the attack tables of the speeder's
# charts (broomchart.h).
ALIASES = {
    "character/bin__/meshphysics/" + SPEED + ".hkx": "character/bin__/meshphysics/" + BROOM + ".hkx",
    "actionchart/bin__/attackinfo/upperaction/4_riding/r0014_boat_upper.paatt":
        "actionchart/bin__/attackinfo/upperaction/2_mon/m0004_dragon_upper.paatt",
    "actionchart/bin__/attackinfo/upperaction/2_mon/m0010_tanksmall_upper.paatt":
        "actionchart/bin__/attackinfo/upperaction/2_mon/m0004_ride_dragon_upper.paatt",
}

# build_rider.py's clips. The speeder's (cd_rd_speed_basic_*, the names its
# charts give, broomchart.cpp) stand in as the broom's clip of the same name
# and take its metadata; the takeoff, which only the plugin has, the idle's.
# Kliff's (cd_phm_rd_speed_basic_*, the names make_blends.py gives his lean
# blend) all stand in as his broom idle and take its metadata: each is 100
# frames, as it is.
MOTION, LOD, META = "character/motion/", "character/motion/motion_lod__/", "actionchart/bin__/animmeta/"
MOUNT_DIR, RIDER_DIR = "4_riding/cd_r0032_00_broom/", "1_pc/1_phm/00_riding/"
MOUNT_CLIPS = ("nor_std_idle_01", "nor_std_mount_l_00", "nor_std_mount_r_00", "nor_std_dismount_l_00",
               "nor_std_dismount_r_00", "nor_move_walk_f_ing_00", "nor_move_walkfast_f_ing_00", "nor_move_run_f_ing_00",
               "nor_move_runfast_f_ing_00", "nor_move_walkfast_f_75u_ing_00", "nor_move_walkfast_f_75d_ing_00",
               "nor_std_takeoff_00")
RIDER_CLIPS = ("nor_std_idle_01", "nor_move_walk_f_ing_00", "nor_move_walkfast_f_ing_00", "nor_move_run_f_ing_00",
               "nor_move_runfast_f_ing_00", "nor_move_walkfast_f_75u_ing_00", "nor_move_walkfast_f_75d_ing_00")
KLIFF_IDLE = RIDER_DIR + "cd_phm_rd_broom_basic_00_00_nor_std_idle_01"
for _c in MOUNT_CLIPS:
    _own = MOUNT_DIR + "cd_rd_speed_basic_00_00_" + _c
    _broom = MOUNT_DIR + "cd_rd_broom_basic_00_00_" + ("nor_std_idle_01" if _c == "nor_std_takeoff_00" else _c)
    STAND_INS[MOTION + _own + ".paa"] = MOTION + _broom + ".paa"
    STAND_INS[LOD + _own + "_lod.paa"] = LOD + _broom + "_lod.paa"
    ALIASES[META + _own + ".paa_metabin"] = META + _broom + ".paa_metabin"
for _c in RIDER_CLIPS:
    _own = RIDER_DIR + "cd_phm_rd_speed_basic_00_00_" + _c
    STAND_INS[MOTION + _own + ".paa"] = MOTION + KLIFF_IDLE + ".paa"
    STAND_INS[LOD + _own + "_lod.paa"] = LOD + KLIFF_IDLE + "_lod.paa"
    ALIASES[META + _own + ".paa_metabin"] = META + KLIFF_IDLE + ".paa_metabin"
STAND_INS["character/binary/motionblending/speed_riding_move.motionblending"] = \
    "character/binary/motionblending/broom_riding_move.motionblending"

# The cut orca rider's states that riderfix.cpp gives Kliff's charts while
# the speeder is ridden: the lower one plays this walk clip beside its blend,
# the upper one (his left arm) the underwater idle. Every file ships, so none
# needs a stand-in: his speeder idle goes in for each clip, and his broom
# idle's metadata for the orca's, whose events are a swimmer's.
ORCAS = ("1_pc/1_phm/cd_phm_rd_orca_basic_00_00_nor_move_underwater_walkfast_f_ing_00",
         "1_pc/1_phm/cd_phm_rd_orca_basic_00_00_nor_std_underwater_idle_00")
ORCA_FILES = {}
for _o in ORCAS:
    ORCA_FILES[MOTION + _o + ".paa"] = MOTION + RIDER_DIR + "cd_phm_rd_speed_basic_00_00_nor_std_idle_01.paa"
    ORCA_FILES[LOD + _o + "_lod.paa"] = LOD + RIDER_DIR + "cd_phm_rd_speed_basic_00_00_nor_std_idle_01_lod.paa"
ORCA_METAS = [META + _o + ".paa_metabin" for _o in ORCAS]


def material(broom_xml):
    text = open(broom_xml, "rb").read().decode("utf-8")
    for tail in ("", "_n", "_sp"):
        old = f'_path="{BROOM_TEX}{tail}.dds"'
        assert text.count(old) == 1, old
        text = text.replace(old, f'_path="{SPEEDER_TEX}{tail}.dds"')
    # No height map: a parameter left out is the shader's default.
    start = text.index('<MaterialParameterTexture StringItemID="_heightTexture"')
    end = text.index("</MaterialParameterTexture>", start) + len("</MaterialParameterTexture>")
    line = text.rindex("\n", 0, start)
    text = text[:line] + text[end:]
    assert BROOM_TEX not in text
    return text.encode("utf-8")


def prefab(broom_prefab):
    data = bytearray(open(broom_prefab, "rb").read())
    old = ("character/model/" + BROOM + ".pac").encode()
    new = MESH.encode()
    assert len(old) == len(new) and data.count(old) == 1 and data[:4] == b"\xff\xff\x04\x00"
    at = data.index(old)
    data[at:at + len(old)] = new
    data[6:14] = PHOENIX_PREFAB_ID
    return bytes(data)


def shipped_file(game):
    """A file as the packs hold it, unpacked."""
    sys.path.insert(0, os.path.join(os.environ.get("CD_ANIMATOR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "CD animator")), "cd_animator"))
    from formats import paz
    root = os.environ.get("CRIMSON_DESERT") or sys.exit("Set CRIMSON_DESERT to the game's folder, the one holding 0009.")
    for group in ("0009", "0010", "0012"):
        for e in paz.read_index(os.path.join(root, group, "0.pamt")):
            if e.path.lower() == game.lower():
                return paz.read(e)
    raise FileNotFoundError(game)


def write(paths, game, data):
    dst = os.path.join(ASSETS, *game.split("/"))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    paths.append(game)


def main():
    # mod/assets/sound is make_engine.py's.
    for top in ("character", "ui", "actionchart"):
        if os.path.isdir(os.path.join(ASSETS, top)):
            shutil.rmtree(os.path.join(ASSETS, top))
    paths = []
    for root, _dirs, names in os.walk(FILES):
        for n in names:
            src = os.path.join(root, n)
            game = os.path.relpath(src, FILES).replace("\\", "/")
            game = MESH if game == BROOM_MESH else game
            dst = os.path.join(ASSETS, *game.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            paths.append(game)
    write(paths, MATERIAL, material(sys.argv[1]))
    write(paths, PREFAB, prefab(sys.argv[2]))
    write(paths, PREFAB_DATA, PREFAB_TEXT)
    for game, ours in ORCA_FILES.items():
        write(paths, game, open(os.path.join(FILES, *ours.split("/")), "rb").read())
    idle_meta = shipped_file(META + KLIFF_IDLE + ".paa_metabin")
    for meta in ORCA_METAS:
        write(paths, meta, idle_meta)
    paths.sort()
    assert MESH in paths and BROOM_MESH not in paths
    missing = [p for p in STAND_INS if p not in paths]
    assert not missing, missing

    with open(os.path.join(MOD, "src", "speeder.rc"), "w", newline="\r\n") as f:
        f.write("// Generated by speeder/pack_assets.py: the speeder's files, one RCDATA\n")
        f.write("// resource each, listed in game/speederfiles.h.\n\n")
        for i, p in enumerate(paths):
            f.write(f'{FIRST_ID + i} RCDATA "../assets/{p}"\n')
        f.write("\n// The engine sounds from speeder/make_engine.py (game/speedersound.cpp).\n")
        for i, name in enumerate(("engine_idle", "engine_close", "engine_boost", "accelerate", "boost_start",
                                  "boost_stop", "startup", "shutdown")):
            f.write(f'{301 + i} RCDATA "../assets/sound/{name}.raw"\n')
    with open(os.path.join(MOD, "src", "game", "speederfiles.h"), "w", newline="\r\n") as f:
        f.write("#pragma once\n\n")
        f.write("// Generated by speeder/pack_assets.py: the files the plugin hands the game\n")
        f.write("// for the speeder, each an RCDATA resource of the plugin (src/speeder.rc).\n")
        f.write("// A path no pack holds has a shipped file for the find and existence checks;\n")
        f.write("// one with no resource (0) loads that file as it is. Broomy serves the\n")
        f.write("// shared paths too, so the plugin leaves them to Broomy when it is there.\n")
        f.write("namespace bm::speederfiles\n{\n")
        f.write("    struct File\n    {\n        const char* path;\n        const char* standIn;\n        int resource;\n"
                "        bool shared;\n    };\n\n")
        f.write("    inline constexpr File kFiles[] = {\n")
        for i, p in enumerate(paths):
            stand = f'"{STAND_INS[p]}"' if p in STAND_INS else "nullptr"
            shared = "true" if p.startswith(RIDER) or p.startswith(RIDER_LOD) else "false"
            f.write(f'        {{ "{p}", {stand}, {FIRST_ID + i}, {shared} }},\n')
        for p, stand in sorted(ALIASES.items()):
            f.write(f'        {{ "{p}", "{stand}", 0, false }},\n')
        f.write("    };\n}\n")
    total = sum(os.path.getsize(os.path.join(ASSETS, *p.split("/"))) for p in paths)
    print(f"{len(paths)} files, {total} bytes")
    for p in paths:
        print(" ", p)


main()
