// The offline check of the speeder's engine sounds: build.bat soundcheck.
// A fake ride through stand-ins for riderfix and analogspeed, at 1% volume,
// at the default ground speeds (walk 19.6 m/s, run 38.5): mount, stand,
// walk, run (dipping to 88% of the top at 6 s and to a walk for 0.1 s at
// 7 s, as a blend's clips do, neither of which may end the boost), back to
// a walk, stop, get off at 13 s, get back on at 15 s, and leave it at 17 s
// some other way than its dismounts (thrown off), so he is no longer seated.
// The RideOn chart runs throughout, as it does in the game while the
// speeder is out. It fails unless the sounds that play are, in order, the
// start-up, acceleration (walk), boost start (the run is the top speed),
// boost stop, the deceleration (stop), the shutdown as he starts getting
// off, the start-up as he gets back on and the shutdown 2 s after he is
// thrown off.
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/speedersound.h"

static std::vector<std::string> g_lines;
static ULONGLONG g_start;
static float Now() { return (GetTickCount64() - g_start) / 1000.0f; }
static float g_shutdownAt = -1.0f, g_lastShutdownAt = -1.0f;

namespace bm::Log
{
    void Write(const char*, const char* fmt, ...)
    {
        char line[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof line, fmt, ap);
        va_end(ap);
        printf("%5.2f  %s\n", Now(), line);
        g_lines.push_back(line);
        if (strcmp(line, "[sound] shutdown.") == 0)
        {
            if (g_shutdownAt < 0.0f) g_shutdownAt = Now();
            g_lastShutdownAt = Now();
        }
    }
}

namespace bm::riderfix
{
    uint32_t MsSinceRidden() { return 0; }
    uint32_t MsSinceSeated() { return Now() < 17.0f ? 0 : static_cast<uint32_t>((Now() - 17.0f) * 1000.0f); }
    bool GettingOff() { return Now() >= 13.0f && Now() < 15.0f; }
}

namespace bm::analogspeed
{
    // (from second, speed, boost)
    struct Step { float at, speed; bool boost; };
    constexpr Step kRide[] = { { 0, 0, false }, { 1, 19.6f, false }, { 4.5f, 38.5f, false }, { 6, 34, false },
                               { 6.5f, 38.5f, false }, { 7, 10, false },
                               { 7.1f, 38.5f, false }, { 8.5f, 19.6f, false },
                               { 12, 0, false } };
    float LastSpeed(uint32_t* age, bool* boost)
    {
        const Step* s = kRide;
        for (const Step& k : kRide) if (Now() >= k.at) s = &k;
        *age = s->speed > 0 ? 0 : 1000;
        *boost = s->boost;
        return s->speed;
    }
}

int main()
{
    g_start = GetTickCount64();
    if (!bm::speedersound::Start(GetModuleHandleW(nullptr), 1, 38.5f)) return 1;
    Sleep(20000);
    bm::speedersound::Stop();
    const char* want[] = { "startup.", "accelerate.", "boost start.", "boost stop.", "boost stop.", "shutdown.", "startup.", "shutdown." };
    const size_t n = sizeof want / sizeof want[0];
    std::vector<std::string> got;
    for (const std::string& l : g_lines)
        if (l.rfind("[sound] ", 0) == 0 && l.find("engine") == std::string::npos) got.push_back(l.substr(8));
    bool ok = got.size() == n;
    for (size_t i = 0; ok && i < n; ++i) ok = got[i] == want[i];
    ok = ok && g_shutdownAt >= 13.0f && g_shutdownAt < 13.5f && g_lastShutdownAt >= 19.0f && g_lastShutdownAt < 19.5f;
    printf(ok ? "PASS\n" : "FAIL: %zu sounds\n", got.size());
    return ok ? 0 : 1;
}
