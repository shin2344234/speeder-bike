# Speeder Bike

A plugin for Crimson Desert that adds a ground-only speeder bike as a mount
of its own, in the saddle wedge with the horses, with the engine sounds from
Return of the Jedi.

It started as a fork of Broomy 1.0.2, which turns the cut flying broom into a
mount, and it keeps Broomy's machinery: the rows the game reads, the riding
charts made from the Wyvern's and the grant that gives a save the mount.
Broomy and Speeder Bike can be installed together.

Target: Crimson Desert 2.03.02 (exe 1.0.0.2976). Players should read
`mod/README.md`, which ships with the plugin and covers installing, settings
and the known limits.

## How it works

The plugin is the whole install. There's no pack group and no `0.papgt`
edit. The game reads its own files and the plugin hands it changed copies:

- The speeder's rows in nine static tables, under keys of its own:
  characterinfo 1900002 (Riding_Speeder_1), mercenaryinfo 88, vehicleinfo
  20001, uimaptextureinfo 1900002 and the appearance index row (1900002, -2).
  Three shipped lists gain the speeder: the Vehicle mercenary group, the
  saddle wedge's reserve slot (VehicleSlot), and Group_Invisible, the map
  filter group every map row must be in.
- A portrait for the saddle wedge and a map icon, both rendered from the
  model in Blender by `speeder/make_icons.py`. The map icon is a copy of the
  hired ibex's, drawn with the speeder's own silhouette, which the map's
  texture list gains.
- Charts made from the Wyvern's riding charts, served under the cut boat's
  chart packages, with every branch into a flying state cut, so the speeder
  never takes off. Every animation path names a clip of the speeder's own,
  `cd_rd_speed_basic_*`, each the broom idle's first frame held for the
  length of the broom clip it replaces, so the speeder hangs still on its
  seat bone whatever state it is in.
- The speeder's mesh, material and skeleton binding, served at the paths of
  the cut Phoenix's prefab, which nothing else uses, and its textures under
  new names.
- Kliff's own speeder clips, `cd_phm_rd_speed_basic_*`, from
  `speeder/build_rider.py`: his broom idle's first frame with his origin on
  the seat bone where the game puts it, his pelvis on the saddle, each fist
  closed palm down on its grip bar with the elbow aimed by a pole, and each
  foot flat in its stirrup by IK, his spine bent forward 0 to 26 degrees from idle to top
  speed, and his head turned back to look ahead. The script reads every
  clip back from its file, measures fists and feet against the mesh (within
  half a millimetre) and renders the fit. His lean blend
  (`speeder/make_blends.py`) places them on rings at 20, 45, 75 and 100
  percent of the top speed. His lower chart reaches it through a state of
  the cut orca rider that nothing else uses: while the speeder is ridden,
  `riderfix.cpp` points the two branches that would crouch him in the
  dragon's pose at that state, and the plugin serves the orca rider's blend
  as his speeder blend.

The mesh is skinned to `B_Rider_01`, the broom's seat bone, which stays level
in every riding clip, so the speeder rides level and still turns with the
body.

The engine is the speeder bike's own sound, cut from the Star Wars SFX
Archive's "Speeder Bike.wav" by `speeder/make_engine.py`: idle, close engine
and boost loops, and the acceleration, boost start, boost stop, start-up and
shutdown takes. The plugin plays them through XAudio2 while the speeder is
ridden. The idle crosses into the close engine and the pitch rises with the
speed. From 90 percent of the top speed the boost start plays and a softened
whine joins the engine, until the speed stays under 85 percent for 300 ms. A
jump in speed of 30 percent of the top speed plays the acceleration, or the
boost's stop, quieter, when slowing down. The start-up plays on mounting and
the shutdown as Kliff starts to get off. If he leaves the speeder any other
way, such as being thrown off, the shutdown plays 2 seconds after his
riding layers stop checking the broom's dismounts.

A fainted speeder is a new one when called. The game refuses to call a
mount whose actor is down, so when its own check finds the speeder fainted,
the plugin clears the speeder's roster entry, as loading a save does, and
the call spawns a fresh speeder.

## Beside Broomy

The ASI loader takes plugins in name order, so Broomy.asi loads first and
its hooks go in first. Speeder Bike finds each hooked function by its
address on this exe when Broomy's hook hides the pattern it searches for,
and puts its own hook on top of Broomy's, so both run. Broomy installs some
hooks a few seconds after start, so Speeder Bike waits for those before
adding its own. The tables, the package list and the charts it builds start
from Broomy's, which already hold Broomy's rows.

Broomy serves the broom's clips, Kliff's broom clips and his broom lean
blend at the broom's paths. The speeder names none of them: its charts play
its own clips, and Kliff's lower chart plays his speeder blend through the
orca rider's state, so he rides the speeder in its own pose with Broomy
installed or not. Only his broom idle, which his upper chart plays on any
mount that uses Broom_Ride, is served at the broom's path, and only without
Broomy.

## Building

MSVC Build Tools 2022, with the CMake and Ninja they bundle.

    mod\build.bat

The plugin lands in `mod\dist\SpeederBike.asi`. It needs Ultimate ASI Loader
in the game's `bin64`.

Three offline checks run without the game:

    mod\build.bat stackcheck IN     the tables, package list and charts, alone and on Broomy's
    mod\build.bat chartcheck IN OUT the charts, written out with their aliases
    mod\build.bat soundcheck        a fake ride through the engine sounds

`stackcheck` compiles Broomy's own builders from `../Broomy/mod/src` (CMake's
`BROOMY_SRC`). IN holds the shipped files `tests/stackcheck.cpp` lists.

`speeder/` has the scripts that make the files in `mod/assets`. They need the
model from Sketchfab (THIRD_PARTY_NOTICES.md) unpacked into `speeder/src`,
Blender 5.2 and CD Animator, found beside this repo or at `CD_ANIMATOR`.
`make_blends.py` and `pack_assets.py` read the game's packs from the folder
`CRIMSON_DESERT` names:

    py -3 speeder/make_engine.py ["Speeder Bike.wav"]
    py -3 speeder/make_textures.py <broom textures, unpacked>
    blender -b --factory-startup --python speeder/build_mesh.py -- <broom .pac, plain>
    blender -b --factory-startup --python speeder/build_rider.py
    py -3 speeder/pack_assets.py <broom .pac_xml, unpacked>

`speeder/check_pacwrite.py` checks the mesh writer against the broom's own
mesh, and `speeder/research/ground_patches.py` writes the ground-only branch
cuts from the Wyvern's charts.

## License

MIT. See LICENSE and THIRD_PARTY_NOTICES.md.
