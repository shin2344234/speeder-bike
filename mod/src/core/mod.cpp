#include "core/mod.h"

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/paths.h"
#include "game/broomchart.h"
#include "game/analogspeed.h"
#include "game/broomy.h"
#include "game/equipfix.h"
#include "game/farhook.h"
#include "game/gamefile.h"
#include "game/grant.h"
#include "game/mem.h"
#include "game/aimrate.h"
#include "game/riderfix.h"
#include "game/speederfiles.h"
#include "game/speedersound.h"
#include "ini_default.h"
#include "version.h"

namespace
{
    std::atomic<bool> g_stop{false};
    HANDLE g_thread = nullptr;

    // The game is not the only process that loads this plugin.
    // crashpad_handler.exe does too, with a 671,744-byte image.
    constexpr size_t kMinGameImage = 64ull * 1024 * 1024;

    bool IsGame() { return bm::mem::Game().base && bm::mem::Game().size >= kMinGameImage; }

    int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

    int ReadSetting(const wchar_t* key, int fallback)
    {
        return static_cast<int>(GetPrivateProfileIntW(L"settings", key, fallback, bm::Paths::File(BM_INI).c_str()));
    }

    // A DMM install is the plugin on its own, so there is no ini beside it.
    // Write the documented one out when there is none. An existing file is
    // never touched.
    void WriteDefaultIni()
    {
        const std::wstring path = bm::Paths::File(BM_INI);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
        {
            LOG("[ini] %ls could not be written; every default is compiled in, so the plugin still runs.", BM_INI);
            return;
        }
        const bool ok = fwrite(kDefaultIni, 1, kDefaultIniSize, f) == kDefaultIniSize;
        fclose(f);
        LOG(ok ? "[ini] no %ls beside the plugin, so one was written with every setting at its default."
               : "[ini] %ls was created but not written in full. Delete it and it will be written again.", BM_INI);
    }

    bool g_serving = false;
    HMODULE g_self = nullptr;
    // Broomy.asi, loaded before this plugin (the loader takes them in name
    // order). Then the broom's own files and Kliff's riding clips are
    // Broomy's to serve, and the hooks Broomy also has go on top of its.
    bool g_broomy = false;
    // The ground speed in percent of the Wyvern's, whose run is 11 m/s.
    int g_groundSpeed = 0;
    constexpr float kWyvernRun = 11.0f;
    const std::string g_alias;   // an alias's bytes: none, the stand-in loads as it is

    // The speeder's mesh, material, textures and Kliff's riding clips
    // (speederfiles.h), read from the plugin's resources at start-up, in
    // the order of kFiles. They come before the broom's own clips, so the
    // speeder's takeoff replaces the push-off.
    std::vector<std::string> g_speeder;

    bool LoadSpeeder(HMODULE module)
    {
        for (const bm::speederfiles::File& f : bm::speederfiles::kFiles)
        {
            if (!f.resource)
            {
                g_speeder.emplace_back();
                continue;
            }
            HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(f.resource), MAKEINTRESOURCEW(10));   // RT_RCDATA
            HGLOBAL h = res ? LoadResource(module, res) : nullptr;
            const void* data = h ? LockResource(h) : nullptr;
            if (!data)
            {
                LOG_ERR("[speeder] resource %d (%s) is missing, so the speeder has no files of its own.", f.resource,
                        f.path);
                g_speeder.clear();
                return false;
            }
            g_speeder.emplace_back(static_cast<const char*>(data), SizeofResource(module, res));
        }
        LOG("[speeder] %zu files ready%s.", g_speeder.size(),
            g_broomy ? "; Broomy is installed, so the broom rider's idle stays Broomy's" : "");
        return true;
    }

    bool EndsWithI(const char* path, size_t n, const char* tail)
    {
        const size_t t = strlen(tail);
        return n >= t && _stricmp(path + n - t, tail) == 0;
    }

    // Called on every async load, Read, find and existence check.
    const std::string* Supply(const char* path, std::string* standIn)
    {
        const size_t n = strlen(path);
        for (size_t i = 0; i < g_speeder.size(); ++i)
        {
            const bm::speederfiles::File& f = bm::speederfiles::kFiles[i];
            if ((f.shared && g_broomy) || !EndsWithI(path, n, f.path)) continue;
            if (standIn && f.standIn) *standIn = f.standIn;
            static volatile LONG logged = 0;
            if (!standIn && InterlockedIncrement(&logged) <= 24)
                LOG("[speeder] the speeder's %s goes in for %s.", strrchr(f.path, '/') + 1, path);
            return &g_speeder[i];
        }
        return nullptr;
    }

    // Broomy's worker thread hooks the request sender, the hire and the
    // movement update a few seconds after start, each found by a pattern
    // that starts at the hooked entry. A hook of
    // this plugin's there first would hide the entry from Broomy, so these
    // wait for Broomy's, which they then go on top of. The movement update
    // is Broomy's last.
    void WaitForBroomy()
    {
        constexpr DWORD kMaxWaitMs = 60000;
        const DWORD start = GetTickCount();
        while (!g_stop.load() && !bm::analogspeed::CallTaken() && GetTickCount() - start < kMaxWaitMs) Sleep(250);
        if (bm::analogspeed::CallTaken())
            LOG("[mod] Broomy's hooks are in (%lu ms); the speeder's go on top of them.", GetTickCount() - start);
        else
            LOG("[mod] Broomy's movement hook did not appear in %lu seconds, so the speeder's hooks go in now.",
                kMaxWaitMs / 1000);
    }

    DWORD WINAPI Worker(LPVOID)
    {
        WriteDefaultIni();
        const bool give = ReadSetting(L"GiveSpeeder", 1) != 0;
        LOG("[mod] %s %s for Crimson Desert 2.03.02 (exe 1.0.0.2976). GiveSpeeder=%d. Broomy is %s.", BM_NAME,
            BM_VERSION, give ? 1 : 0, g_broomy ? "installed too" : "not installed");
        if (!g_serving)
            LOG_ERR("[mod] the speeder's files are not being served (see the [files] line above), so the speeder does "
                    "not exist this session.");
        if (g_serving && g_broomy) WaitForBroomy();
        const bool granting = g_serving && give && bm::grant::Install();
        // Broomy guards the same equipment copies for every mount.
        if (g_serving && !g_broomy) bm::equipfix::Install();
        bm::analogspeed::SetSlowest(ReadSetting(L"SlowestPush", 15));
        if (g_serving) bm::analogspeed::Install();
        if (g_serving)
            bm::speedersound::Start(g_self, Clamp(ReadSetting(L"EngineVolume", 60), 0, 100),
                                    kWyvernRun * g_groundSpeed / 100.0f);
        if (g_serving && !give) LOG("[grant] GiveSpeeder=0, so no save is given the speeder.");
        while (!g_stop.load())
        {
            Sleep(500);
            if (granting) bm::grant::Tick();
        }
        return 0;
    }
}

namespace bm::Mod
{
    void EarlyInstall(HMODULE module)
    {
        // On the loading thread, before the game's own start-up code runs,
        // so the files it reads at boot are seen too.
        Paths::Init(module);
        g_self = module;
        if (!IsGame()) return;
        g_broomy = GetModuleHandleW(L"Broomy.asi") != nullptr;
        bm::broomchart::Speeds speeds;
        // The speeder never flies, so only the ground speed is a setting.
        speeds.ground = Clamp(ReadSetting(L"GroundSpeed", speeds.ground), 10, 1000);
        g_groundSpeed = speeds.ground;
        bm::broomchart::SetSpeeds(speeds);
        g_serving = bm::broomy::Install(bm::broomy::kChartsPadded);
        if (g_serving) LoadSpeeder(module);
        if (g_serving) bm::gamefile::SupplyLoads(&Supply);
        if (g_serving) bm::riderfix::Install(false);
        // The aim rate is the broom's body bone's, which Broomy sets itself.
        if (g_serving && !g_broomy) bm::aimrate::Install();
    }

    void Initialize(HMODULE module)
    {
        Paths::Init(module);
        if (!IsGame())
        {
            wchar_t name[96];
            _snwprintf_s(name, _countof(name), _TRUNCATE, L"%s.other", BM_FILEBASE);
            Log::ClaimSingle(name);
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            const wchar_t* leaf = wcsrchr(exe, L'\\');
            LOG("[mod] %ls (pid %lu) is not the game, so nothing is done here. The game's own log is %ls.log.",
                leaf ? leaf + 1 : exe, GetCurrentProcessId(), BM_FILEBASE);
            Log::Shutdown();
            return;
        }
        Log::Claim(BM_FILEBASE);
        g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }

    void Shutdown(bool processExiting)
    {
        g_stop.store(true);
        if (processExiting)
        {
            Log::Shutdown();
            return;
        }
        bm::analogspeed::Stop();
        bm::speedersound::Stop();
        if (g_thread)
        {
            WaitForSingleObject(g_thread, 3000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        farhook::RemoveAll();
        Log::Shutdown();
    }
}
