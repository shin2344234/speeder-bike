#pragma once
#include <Windows.h>

// The speeder's engine. The game has no sound for it, so the plugin plays
// sounds of its own (resources 301 to 308, from speeder/make_engine.py)
// through XAudio2: three loops that fade in while the speeder is ridden,
// cross from the idle to the close engine and rise in pitch with its speed,
// with the boost's whine near the top speed, and fade out on dismount, in a
// menu or when the game is not the foreground window; one-shots for
// speeding up, slowing down, start-up and shutdown.
namespace bm::speedersound
{
    // From the worker, after riderfix and analogspeed are installed.
    // `module` holds the sounds; `volume` is the ini's EngineVolume, 0 to
    // 100, and 0 plays nothing; `topSpeed` the speeder's run speed in m/s.
    bool Start(HMODULE module, int volume, float topSpeed);
    void Stop();
}
