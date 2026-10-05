#include "game/speedersound.h"

#include <Windows.h>
#include <xaudio2.h>
#include <atomic>
#include <cmath>

#include "core/log.h"
#include "game/analogspeed.h"
#include "game/riderfix.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    // The sounds (src/speeder.rc, from speeder/make_engine.py): mono 16-bit
    // PCM at 24 kHz. Three loops, then the one-shots.
    enum Sound { kIdle, kClose, kBoost, kAccelerate, kBoostStart, kBoostStop, kStartup, kShutdown, kSounds };
    constexpr int kFirstResource = 301;
    constexpr const char* kNames[kSounds] = { "idle",       "close",      "boost",   "accelerate",
                                              "boost start", "boost stop", "startup", "shutdown" };
    constexpr DWORD kRate = 24000;
    // The RideOn chart is checked every frame while the speeder is ridden,
    // so a gap this long means Kliff got off or a menu paused the game.
    constexpr DWORD kRiddenMs = 400;
    // A speed record older than this means the speeder stands still.
    constexpr DWORD kSpeedMs = 250;
    // The speeder's top speed on the ground (m/s), from Start: it reaches
    // the top pitch and the full close engine there, and from kBoostShare
    // of it on a soft whine joins the engine, since a ground vehicle never
    // gets the Wyvern's flight boost. The recording keeps its character
    // only over a small pitch range. Until 5 October the top pitch was 1.35
    // and the boost's full whine replaced the engine there; Seth found it
    // annoying and chose the engine at 1.2 with the whine softened under it
    // (make_engine.py's WHINE_CUT and WHINE_SHARE).
    float g_top = 38.5f;
    constexpr float kBoostShare = 0.9f;
    // and lasts until it has been under this share for kBoostEndMs. Each
    // clip in a blend notes its own speed, so on takeoff, as the start blends
    // into the run, the speed read here jumps between them from one tick to
    // the next; without the wait the boost started and stopped 18 times in
    // 5 s, each restarting its one-shot (Seth, 5 October 09:56: "a super
    // loud noise" on first taking off).
    constexpr float kBoostEndShare = 0.85f;
    constexpr ULONGLONG kBoostEndMs = 300;
    constexpr float kRestPitch = 0.95f;
    constexpr float kTopPitch = 1.2f;
    constexpr float kMaxRatio = 2.0f;
    // A change of speed plays the acceleration or the deceleration when the
    // speed moves this share of the top speed from where it was about half
    // a second ago: from a stop to the walk, or walk to run. Then not again
    // for kCooldownMs.
    constexpr float kJumpShare = 0.3f;
    constexpr float kFollow = 2.0f;   // per second, how fast "half a second ago" catches up
    constexpr DWORD kCooldownMs = 3000;
    constexpr float kDecelVolume = 0.6f;   // the boost's end, quieter, stands in for slowing down
    // Riding that starts this long after the last ride plays the start-up;
    // a shorter gap, such as a menu, only fades the engine back in.
    constexpr ULONGLONG kRestartMs = 5000;

    std::atomic<bool> g_stop{ false };
    HANDLE g_thread = nullptr;
    HMODULE g_module = nullptr;
    float g_volume = 0.6f;

    struct Clip
    {
        const BYTE* data = nullptr;
        UINT32 size = 0;
        IXAudio2SourceVoice* voice = nullptr;
    };

    bool GameInFront()
    {
#ifdef SPEEDER_SOUNDCHECK
        return true;   // tests/soundcheck.cpp has no game window
#endif
        DWORD pid = 0;
        const HWND w = GetForegroundWindow();
        return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
    }

    XAUDIO2_BUFFER Buffer(const Clip& c, bool looping)
    {
        XAUDIO2_BUFFER buf = {};
        buf.AudioBytes = c.size;
        buf.pAudioData = c.data;
        buf.LoopCount = looping ? XAUDIO2_LOOP_INFINITE : 0;
        return buf;
    }

    // Resource `id` and a silent voice for it; a loop starts playing at once.
    bool Make(IXAudio2* audio, int id, bool looping, Clip& c)
    {
        HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
        HGLOBAL h = res ? LoadResource(g_module, res) : nullptr;
        c.data = h ? static_cast<const BYTE*>(LockResource(h)) : nullptr;
        c.size = res ? SizeofResource(g_module, res) : 0;
        if (!c.data || !c.size) return false;
        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = kRate * 2;
        if (FAILED(audio->CreateSourceVoice(&c.voice, &fmt, 0, kMaxRatio))) return false;
        if (FAILED(c.voice->SetVolume(0.0f))) return false;
        if (!looping) return true;
        const XAUDIO2_BUFFER buf = Buffer(c, true);
        return SUCCEEDED(c.voice->SubmitSourceBuffer(&buf)) && SUCCEEDED(c.voice->Start(0));
    }

    // A one-shot from its start, cutting off any play of it still going.
    void Play(Clip& c, float volume, int sound)
    {
        c.voice->Stop(0);
        c.voice->FlushSourceBuffers();
        const XAUDIO2_BUFFER buf = Buffer(c, false);
        c.voice->SetVolume(volume);
        if (SUCCEEDED(c.voice->SubmitSourceBuffer(&buf))) c.voice->Start(0);
        static volatile LONG logged = 0;
        if (InterlockedIncrement(&logged) <= 40) LOG("[sound] %s.", kNames[sound]);
    }

    float Toward(float from, float to, float rate, float dt)
    {
        const float k = rate * dt > 1.0f ? 1.0f : rate * dt;
        return from + (to - from) * k;
    }

    DWORD WINAPI Run(LPVOID)
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            LOG_ERR("[sound] COM did not start, so the speeder is silent.");
            return 0;
        }
        IXAudio2* audio = nullptr;
        IXAudio2MasteringVoice* master = nullptr;
        Clip clips[kSounds];
        HRESULT hr = XAudio2Create(&audio, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (SUCCEEDED(hr)) hr = audio->CreateMasteringVoice(&master);
        int missing = -1;
        for (int i = 0; i < kSounds && SUCCEEDED(hr) && missing < 0; ++i)
            if (!Make(audio, kFirstResource + i, i <= kBoost, clips[i])) missing = i;
        if (FAILED(hr) || missing >= 0)
        {
            LOG_ERR("[sound] XAudio2 failed (0x%08lX%s%s), so the speeder is silent.", static_cast<unsigned long>(hr),
                    missing >= 0 ? ", no voice for the " : "", missing >= 0 ? kNames[missing] : "");
            if (audio) audio->Release();   // destroys the voices too
            CoUninitialize();
            return 0;
        }
        LOG("[sound] the speeder's engine is ready at %.0f%% volume.", g_volume * 100.0f);

        float vol = 0.0f, mix = 0.0f, boostMix = 0.0f, pitch = kRestPitch, before = 0.0f;
        bool wasOn = false, wasBoost = false, wasRidden = false;
        ULONGLONG last = GetTickCount64(), quietUntil = 0, riddenAt = 0, highAt = 0;
        while (!g_stop.load())
        {
            Sleep(33);
            const ULONGLONG now = GetTickCount64();
            const float dt = static_cast<float>(now - last) / 1000.0f;
            last = now;

            // The RideOn chart runs on after Kliff gets off, so the engine
            // stops as he starts getting off and starts again as he mounts.
            const bool off = bm::riderfix::GettingOff();
            const bool ridden = bm::riderfix::MsSinceRidden() < kRiddenMs && !off, front = GameInFront();
            const bool on = ridden && front;
            if (ridden != wasRidden && front)
            {
                if (ridden && (!riddenAt || now - riddenAt >= kRestartMs)) Play(clips[kStartup], g_volume, kStartup);
                else if (!ridden) Play(clips[kShutdown], g_volume, kShutdown);
            }
            if (ridden) riddenAt = now;
            else if (off) riddenAt = 0;   // a new mount always starts up
            wasRidden = ridden;
            uint32_t age = 0;
            bool boost = false;
            float speed = bm::analogspeed::LastSpeed(&age, &boost);
            if (age >= kSpeedMs || !(speed > 0.0f)) speed = 0.0f, boost = false;
            boost = boost || speed >= (wasBoost ? kBoostEndShare : kBoostShare) * g_top;
            if (boost || !wasBoost) highAt = now;
            else if (now - highAt < kBoostEndMs) boost = true;
            const float jump = kJumpShare * g_top;
            float s = speed / g_top;
            if (s > 1.0f) s = 1.0f;
            s = std::sqrt(s);

            if (on && wasOn)
            {
                int shot = -1;
                float loud = g_volume;
                const bool free = !boost && now >= quietUntil;
                if (boost && !wasBoost) shot = kBoostStart;
                else if (!boost && wasBoost) shot = kBoostStop;
                else if (free && speed - before >= jump) shot = kAccelerate;
                else if (free && before - speed >= jump) shot = kBoostStop, loud *= kDecelVolume;
                if (shot >= 0)
                {
                    Play(clips[shot], loud, shot);
                    quietUntil = now + kCooldownMs;
                }
            }
            if (!front)
                for (int i = kAccelerate; i < kSounds; ++i) clips[i].voice->SetVolume(0.0f);
            before = on ? Toward(before, speed, kFollow, dt) : 0.0f;
            wasBoost = on && boost;

            vol = Toward(vol, on ? g_volume : 0.0f, 4.0f, dt);
            mix = Toward(mix, s, 2.0f, dt);
            boostMix = Toward(boostMix, boost ? 1.0f : 0.0f, 3.0f, dt);
            pitch = Toward(pitch, kRestPitch + (kTopPitch - kRestPitch) * s, 2.0f, dt);
            // Equal power from the idle at rest to the close engine at speed;
            // the soft whine fades in over them while boosting, its level
            // set in the file.
            const float a = vol < 0.001f ? 0.0f : vol;
            clips[kIdle].voice->SetVolume(a * std::cos(mix * 1.5707963f));
            clips[kClose].voice->SetVolume(a * std::sin(mix * 1.5707963f));
            clips[kBoost].voice->SetVolume(a * boostMix);
            clips[kIdle].voice->SetFrequencyRatio(pitch);
            clips[kClose].voice->SetFrequencyRatio(pitch);
            if (on != wasOn)
            {
                wasOn = on;
                LOG(on ? "[sound] engine on." : "[sound] engine off.");
            }
        }
        for (Clip& c : clips) c.voice->DestroyVoice();
        master->DestroyVoice();
        audio->Release();
        CoUninitialize();
        return 0;
    }
}

namespace bm::speedersound
{
    bool Start(HMODULE module, int volume, float topSpeed)
    {
        if (topSpeed > 1.0f) g_top = topSpeed;
        if (volume <= 0)
        {
            LOG("[sound] EngineVolume=0, so the speeder is silent.");
            return false;
        }
        g_module = module;
        g_volume = (volume > 100 ? 100 : volume) / 100.0f;
        g_thread = CreateThread(nullptr, 0, &Run, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void Stop()
    {
        g_stop.store(true);
        if (g_thread)
        {
            WaitForSingleObject(g_thread, 2000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
    }
}
