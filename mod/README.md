# Speeder Bike 1.0.0

For Crimson Desert 2.03.02, exe 1.0.0.2976.

A speeder bike in the style of Return of the Jedi, as a mount of its own in
the saddle wedge. It rides on the ground only, with the movie's engine
sounds. No other mount is changed, and it runs beside Broomy.

## Installing

Copy `SpeederBike.asi` into the game's `bin64` folder, next to
`CrimsonDesert.exe`, with the game closed. You need an ASI loader there
already. Ultimate ASI Loader installed as `winmm.dll` is what most Crimson
Desert setups use, and the Definitive Mod Manager installs one for you.

No game file is modified. The first time it runs, the plugin writes
`SpeederBike.ini` beside itself with every setting at its default. An ini
you already have is left alone.

## Getting the speeder

Load a save. A few seconds later the plugin looks for the speeder among your
mounts, and if it is missing, a wild horse the game has loaded joins your
roster as the speeder. It happens once per save. A save that has the speeder
already is left alone.

The speeder joins the saddle wedge on the Character radial, after your
horses. Scroll to it there and Kliff calls it the way he calls a horse. A
blue speeder marks it on the map and the minimap.

## Riding

Mount from either side. The engine starts as Kliff gets on and shuts down as
he gets off. It idles at rest, climbs in pitch as you speed up, and a soft
whine joins it at full speed. The jump key does nothing, since the speeder
never leaves the ground.

On a controller the speed follows how far the left stick is pushed. The
keyboard always moves at full speed.

## Settings

All of them live in `SpeederBike.ini` and are read when the game starts.

    GiveSpeeder=1      give the speeder to a save that does not have it
    GroundSpeed=350    speed, percent of the Wyvern's on the ground
    SlowestPush=15     speed at the lightest stick push, percent of full
    EngineVolume=60    the engine sound, percent; 0 turns it off

At the defaults the speeder runs at 38.5 m/s, about 140 km/h. Set
`SlowestPush=100` for one speed whatever the push.

INI Master (https://www.nexusmods.com/crimsondesert/mods/3578) can edit these
with a label and range for each.

## With Broomy

Both plugins can be installed together, and each mount gets its own entry
in the saddle wedge. Broomy needs to be 1.0.3 or newer, the version this was
checked against. Kliff keeps his speeder pose on the speeder and his broom
pose on Broomy.

## Uninstalling

Delete the `SpeederBike` files from `bin64`. A save that has the speeder
still loads, and the speeder leaves the saddle wedge. If you save without the
plugin, that save loses the speeder, and putting the plugin back gives you a
new one.

## Known limits

- The saddle wedge is the only way to call the speeder.
- A call while falling brings Blackstar, as it does with a horse chosen.
- Some places refuse every summon, the Abyss Nexus among them. That is the
  game's own rule.
