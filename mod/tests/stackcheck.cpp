// Offline check that the speeder's tables build on top of Broomy's. Broomy.asi
// loads before SpeederBike.asi, so with both installed the game's read goes
// through Broomy's hook first and the speeder's builders get Broomy's tables.
// This runs Broomy's own builders (its src/game/broomrows.cpp, compiled into
// bm::broomyrows) on the shipped files, then the speeder's on the result, and
// checks that both mounts' rows are there and that the speeder's run alone on
// the shipped files too. The chart package list the same way, and the
// speeder's charts from the Wyvern's.
//
//   stackcheck IN
//
// IN holds the shipped NAME.staticinfoheader and NAME.staticinfobody of every
// table the speeder changes, character.paloc, mon_wyvern.xml,
// cd_image_portrait_00.xml, cd_icon_map_03.xml, the map's minimapicon.thtml,
// worldmapicon.thtml, minimapicon.css and worldmapicon.css,
// characteractionpackagedescription.paacdesc and the Wyvern's m0004_dragon_upper.paac, m0004_ride_dragon_lower.paac and
// m0004_ride_dragon_upper.paac. `build.bat stackcheck IN` builds and runs it;
// CMake's BROOMY_SRC names Broomy's mod/src.
#include <cstdio>
#include <cstring>
#include <string>

#define broomrows broomyrows
#include BROOMY_ROWS_H
#undef broomrows
#define broomchart broomychart
#include BROOMY_CHART_H
#undef broomchart
#include "game/broomchart.h"
#include "game/broomrows.h"
#include "game/tablefile.h"

namespace
{
    bool Load(const std::string& path, std::string& out)
    {
        out.clear();
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
        char chunk[65536];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) out.append(chunk, n);
        fclose(f);
        return true;
    }

    int g_failed = 0;

    void Expect(bool ok, const char* what)
    {
        printf("    %-4s %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++g_failed;
    }

    bool Has(const std::string& h, const std::string& b, uint64_t key, int32_t tag = 0)
    {
        bm::tablefile::Table t;
        std::string why;
        return t.Parse(h, b, why) && t.Record(key, tag) != nullptr;
    }

    // The u32 list count and bytes at the end of a mercenarygroupinfo or
    // reserveslot list holding `tail`.
    bool ListHas(const std::string& h, const std::string& b, uint64_t key, const std::string& needle)
    {
        bm::tablefile::Table t;
        std::string why;
        if (!t.Parse(h, b, why)) return false;
        const std::string* rec = t.Record(key);
        return rec && rec->find(needle) != std::string::npos;
    }

    std::string U32(uint32_t v) { return std::string(reinterpret_cast<const char*>(&v), 4); }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: stackcheck IN\n");
        return 2;
    }
    const std::string in = std::string(argv[1]) + "/";
    for (const char* name : bm::broomrows::kTables)
    {
        std::string h, b;
        if (!Load(in + name + ".staticinfoheader", h) || !Load(in + name + ".staticinfobody", b))
        {
            printf("%-30s files missing\n", name);
            ++g_failed;
            continue;
        }
        // The speeder alone.
        std::string sh, sb, report, why;
        if (!bm::broomrows::BuildTable(name, h, b, sh, sb, report, why))
        {
            printf("%-30s FAILED alone: %s\n", name, why.c_str());
            ++g_failed;
        }
        else
            printf("%-30s alone:      %s\n", name, report.c_str());
        if (!strcmp(name, "reserveslot"))
            Expect(ListHas(sh, sb, 1000006, U32(3) + "\x4E\x51\x58"), "alone, VehicleSlot takes 78, 81 and 88");

        // Broomy, then the speeder.
        std::string bh = h, bb = b;
        bool broomy = false;
        for (const char* other : bm::broomyrows::kTables)
            broomy = broomy || !strcmp(other, name);
        if (broomy)
        {
            bm::broomyrows::CharacterFacts facts;
            if (!bm::broomyrows::BuildTable(name, h, b, bh, bb, report, why, &facts))
            {
                printf("%-30s FAILED in Broomy's builder: %s\n", name, why.c_str());
                ++g_failed;
                continue;
            }
        }
        std::string th, tb;
        bm::broomrows::CharacterFacts facts;
        if (!bm::broomrows::BuildTable(name, bh, bb, th, tb, report, why, &facts))
        {
            printf("%-30s FAILED on Broomy's: %s\n", name, why.c_str());
            ++g_failed;
            continue;
        }
        printf("%-30s on Broomy's: %s\n", name, report.c_str());
        if (!strcmp(name, "characterinfo"))
        {
            Expect(Has(th, tb, 1900001), "Broomy's row is still there");
            Expect(Has(th, tb, 1900002), "the speeder's row is there");
            Expect(facts.broomyRow >= 0, "the speeder's row number is known for the grant");
        }
        else if (!strcmp(name, "mercenaryinfo"))
        {
            Expect(Has(th, tb, 87), "Vehicle_Broom is still there");
            Expect(Has(th, tb, 88), "Vehicle_Speeder is there");
        }
        else if (!strcmp(name, "mercenarygroupinfo"))
            Expect(ListHas(th, tb, 65, U32(8) + std::string("\x4E\x4F\x50\x52\x51\x41\x57\x58", 8)),
                   "the Vehicle group lists Broomy's and the speeder's");
        else if (!strcmp(name, "reserveslot"))
            Expect(ListHas(th, tb, 1000006, U32(4) + "\x4E\x51\x57\x58"),
                   "VehicleSlot takes 78, 81, Broomy's 87 and the speeder's 88");
        else if (!strcmp(name, "vehicleinfo"))
        {
            Expect(ListHas(th, tb, 20000, U32(1900001)), "Broomy's vehicle row still names its map row");
            Expect(ListHas(th, tb, 20001, U32(1900002)), "the speeder's vehicle row names its map row");
        }
        else if (!strcmp(name, "uimaptextureinfo"))
        {
            Expect(Has(th, tb, 1900001), "Broomy's map row is still there");
            Expect(Has(th, tb, 1900002), "the speeder's map row is there");
        }
        else if (!strcmp(name, "uifiltergroupinfo"))
            Expect(ListHas(th, tb, 1000015, U32(1900001)) && ListHas(th, tb, 1000015, U32(1900002)),
                   "Group_Invisible lists both map rows");
        else if (!strcmp(name, "characterappearanceindexinfo"))
        {
            Expect(Has(th, tb, 1900001, -2), "Broomy's appearance row is still there");
            Expect(Has(th, tb, 1900002, -2), "the speeder's appearance row is there");
        }
    }

    std::string paloc, wyvern, portraits, out, why;
    if (Load(in + "character.paloc", paloc))
    {
        std::string withBroomy;
        const bool a = bm::broomyrows::BuildPaloc(paloc, withBroomy, why);
        Expect(a && bm::broomrows::BuildPaloc(withBroomy, out, why), "character.paloc names both");
    }
    if (Load(in + "cd_image_portrait_00.xml", portraits))
    {
        std::string withBroomy;
        const bool a = bm::broomyrows::BuildPortraits(portraits, withBroomy, why);
        Expect(a && bm::broomrows::BuildPortraits(withBroomy, out, why) &&
                   out.find("cd_portraitimage_Broomy") != std::string::npos &&
                   out.find("cd_portraitimage_Speeder") != std::string::npos,
               "the portrait list names both");
    }
    std::string icons;
    if (Load(in + "cd_icon_map_03.xml", icons))
        Expect(bm::broomrows::BuildMapTextures(icons, out, why) &&
                   out.find("Name=\"cd_Icon_map_Speeder\" Filename=\"UI/texture/cd_icon_map_speeder.dds\"") !=
                       std::string::npos,
               "the map's silhouette list names the speeder's");
    for (int m = 0; m < 4; ++m)
    {
        const char* file = bm::broomrows::kMapIconFiles[m] + 1;
        std::string game, withBroomy, alone;
        if (!Load(in + file, game))
        {
            printf("%s missing\n", file);
            ++g_failed;
            continue;
        }
        const bool a = bm::broomyrows::BuildMapIcons(m < 2, game, withBroomy, why);
        const bool b = a && bm::broomrows::BuildMapIcons(m < 2, withBroomy, out, why);
        const std::string drawn = m < 2 ? "MapIcon_ActorVehicleSpeeder_Hired" : "textureid(cd_Icon_map_Speeder)";
        char what[128];
        snprintf(what, sizeof what, "%s draws Broomy and the speeder", file);
        Expect(b && out.find("Broomy") != std::string::npos && out.find(drawn) != std::string::npos, what);
        snprintf(what, sizeof what, "%s draws the speeder alone", file);
        Expect(bm::broomrows::BuildMapIcons(m < 2, game, alone, why) && alone.find(drawn) != std::string::npos, what);
    }
    if (Load(in + "mon_wyvern.xml", wyvern))
        Expect(bm::broomrows::BuildDescription(wyvern, out, why), "the speeder's description builds");

    std::string desc, report;
    if (Load(in + "characteractionpackagedescription.paacdesc", desc))
    {
        std::string withBroomy, alone;
        const bool a = bm::broomychart::BuildPackageDesc(desc, withBroomy, report, why);
        printf("package list, Broomy's: %s\n", a ? report.c_str() : why.c_str());
        const bool b = a && bm::broomchart::BuildPackageDesc(withBroomy, out, report, why);
        printf("package list, the speeder's on Broomy's: %s\n", b ? report.c_str() : why.c_str());
        Expect(b && out.size() == withBroomy.size() + 4, "the package list gains one sub-package on Broomy's");
        Expect(bm::broomchart::BuildPackageDesc(desc, alone, report, why) && alone.size() == desc.size() + 4,
               "the package list gains one sub-package alone");
    }
    const char* sources[] = { "m0004_dragon_upper.paac", "m0004_ride_dragon_lower.paac", "m0004_ride_dragon_upper.paac" };
    std::string charts[bm::broomchart::kChartCount], made[bm::broomchart::kChartCount];
    bool read = true;
    for (int c = 0; c < bm::broomchart::kChartCount; ++c) read = Load(in + sources[c], charts[c]) && read;
    if (read)
    {
        bm::broomchart::Aliases aliases;
        const bool ok = bm::broomchart::BuildCharts(charts, made, aliases, report, why, true);
        printf("charts: %s\n", ok ? report.c_str() : why.c_str());
        Expect(ok && report.find("25 of 25 branches into the air cut") != std::string::npos,
               "every branch into the air is cut");
        Expect(ok && report.find("110 of 110 cameras the horse's") != std::string::npos,
               "every riding camera but the aim is the horse's");
        // Broomy serves the broom's own clips and blend at the broom's paths,
        // so the speeder's charts must name none of them.
        bool own = ok;
        for (const std::string& m : made)
            own = own && m.find("cd_rd_broom_basic_") == std::string::npos &&
                  m.find("broom_riding_move") == std::string::npos;
        Expect(own && made[0].find("cd_rd_speed_basic_") != std::string::npos &&
                   made[0].find("speed_riding_move.motionblending") != std::string::npos,
               "the charts name the speeder's own clips and blend, none of the broom's");
    }
    printf(g_failed ? "%d FAILED\n" : "all ok\n", g_failed);
    return g_failed ? 1 : 0;
}
