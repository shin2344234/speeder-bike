// Offline check that the speeder's tables build on top of Broomy's. Broomy.asi
// loads before SpeederBike.asi, so with both installed the game's read goes
// through Broomy's hook first and the speeder's builders get Broomy's tables.
// This runs Broomy's own builders (its src/game/broomrows.cpp, compiled into
// bm::broomyrows) on the shipped files, then the speeder's on the result, and
// checks that both mounts' rows are there and that the speeder's run alone on
// the shipped files too. The chart package list the same way, and the
// speeder's charts from the Wyvern's.
//
//   stackcheck IN [alone]
//
// IN holds the shipped NAME.staticinfoheader and NAME.staticinfobody of every
// table the speeder changes, character.paloc, mon_wyvern.xml,
// cd_image_portrait_00.xml, cd_icon_map_03.xml, the map's minimapicon.thtml,
// worldmapicon.thtml, minimapicon.css and worldmapicon.css,
// characteractionpackagedescription.paacdesc and the Wyvern's m0004_dragon_upper.paac, m0004_ride_dragon_lower.paac and
// m0004_ride_dragon_upper.paac. With `alone` the tables are built without
// Broomy's builders first, for a build Broomy is not yet ported to.
// `build.bat stackcheck IN [alone]` builds and runs it;
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
#include "game/groundpatches.h"
#include "game/hash.h"
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

    size_t Count(const std::string& hay, const std::string& needle)
    {
        size_t n = 0;
        for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
        return n;
    }

    // The speeder's characterinfo row as built, field by field, at the
    // places exe 1.0.0.3392 reads them (broomrows.cpp says how each was
    // found).
    void CheckSpeederRow(const std::string& h, const std::string& b, const bm::broomrows::CharacterFacts& facts)
    {
        bm::tablefile::Table t;
        std::string why;
        const std::string* rec = t.Parse(h, b, why) ? t.Record(bm::broomrows::kBroomyKey) : nullptr;
        const std::string name = U32(16) + "Riding_Speeder_1";
        const bool head = rec && rec->compare(0, name.size(), name) == 0 &&
                          rec->compare(name.size(), 4, U32(bm::broomrows::kBroomyKey)) == 0;
        Expect(head, "the speeder's row opens with its name, then its key");
        if (!head) return;
        const std::string& r = *rec;
        const size_t base = name.size() + 4;
        uint16_t vehicle = 0;
        memcpy(&vehicle, r.data() + base + 0xCF, 2);
        Expect(vehicle == 20001 && static_cast<uint8_t>(r[base + 0xD7]) == 88 && r[base + 10] == 1,
               "its vehicle row is 20001, its mercenary list 88, and it is hirable");
        const auto key = [](const char* s) { return U32(bm::hash::Little(s)); };
        Expect(Count(r, key("CD_R0014_Boat_Lower") + key("CD_R0014_Boat")) == 1 &&
                   Count(r, key(bm::broomrows::kAppearancePath)) == 1 && Count(r, key("animal_orca")) == 1 &&
                   Count(r, key("character/model/4_riding/cd_r0032_00_broom/cd_r0032_00_broom.pab")) == 1,
               "it names the boat's charts, the speeder's appearance, the orca's gameplay data and the broom's skeleton");
        Expect(Count(r, key("cd_portraitimage_Speeder")) == 1 && Count(r, key("cd_portraitimage_Riding_AlpineIbex_1")) == 0,
               "it names the speeder's portrait, not the ibex's");
        uint64_t ibexCool = 300;
        Expect(Count(r, std::string(reinterpret_cast<const char*>(&ibexCool), 8)) == 0 && Count(r, U32(900000)) == 0 &&
                   Count(r, U32(1000)) >= 1,
               "its faint cooldown and vanish time are the speeder's");
        Expect(Count(r, U32(1) + U32(1000068)) == 2 && Count(r, U32(1000098)) == 0,
               "both interaction lists hold Broom_Ride alone");
        Expect(Count(r, U32(29448)) == 0, "the ibex's key is nowhere in it");
        size_t horses = 0;
        for (int16_t m : facts.mercInfo) horses += m == 78;
        Expect(facts.broomyRow >= 0 && facts.broomyRow < static_cast<int>(facts.mercInfo.size()) &&
                   facts.mercInfo[facts.broomyRow] == 88 && horses > 50,
               "the grant reads each row's mercenary list: 88 for the speeder, 78 for the horses");
        printf("         %zu rows on the horse list\n", horses);
    }
}

int main(int argc, char** argv)
{
    const bool alone = argc == 3 && !strcmp(argv[2], "alone");
    if (argc != 2 && !alone)
    {
        fprintf(stderr, "usage: stackcheck IN [alone]\n");
        return 2;
    }
    const std::string in = std::string(argv[1]) + "/";
    if (alone) printf("the tables without Broomy's builders\n");
    // A header offset at the end of the body is an empty last record, not an
    // overrun (GitHub issue 1).
    {
        std::string h("\x02\x00", 2), b("AAAABBBB");
        const uint32_t keys[2] = { 1, 2 }, offs[2] = { 0, 8 };
        for (int i = 0; i < 2; ++i)
        {
            h.append(reinterpret_cast<const char*>(&keys[i]), 4);
            h.append(reinterpret_cast<const char*>(&offs[i]), 4);
        }
        bm::tablefile::Table t;
        std::string why;
        const bool ok = t.Parse(h, b, why);
        Expect(ok && t.Rows() == 2 && t.RecordAt(0).size() == 8 && t.RecordAt(1).empty(),
               "a header offset at the end of the body is an empty last record");
        h.replace(10 + 4, 4, "\x09\x00\x00\x00", 4);
        Expect(!t.Parse(h, b, why), "a header offset past the end of the body is refused");
    }
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
        bm::broomrows::CharacterFacts own;
        if (!bm::broomrows::BuildTable(name, h, b, sh, sb, report, why, &own))
        {
            printf("%-30s FAILED alone: %s\n", name, why.c_str());
            ++g_failed;
        }
        else
            printf("%-30s alone:      %s\n", name, report.c_str());
        if (!strcmp(name, "reserveslot"))
            Expect(ListHas(sh, sb, 1000006, U32(3) + "\x4E\x51\x58"), "alone, VehicleSlot takes 78, 81 and 88");
        if (!strcmp(name, "characterinfo")) CheckSpeederRow(sh, sb, own);
        // `alone`: Broomy's builders are left out (they read another
        // build's records until Broomy is ported to it).
        if (alone) continue;

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
            CheckSpeederRow(th, tb, facts);
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
    for (int m = 0; m < static_cast<int>(sizeof bm::broomrows::kMapIconFiles / sizeof bm::broomrows::kMapIconFiles[0]); ++m)
    {
        const char* file = bm::broomrows::kMapIconFiles[m] + 1;
        std::string game, withBroomy, alone;
        if (!Load(in + file, game))
        {
            printf("%s missing\n", file);
            ++g_failed;
            continue;
        }
        const bool binary = m >= bm::broomrows::kMapBinaryFirst;
        const auto build = [&](bool speeder, const std::string& from, std::string& to) {
            if (binary)
                return speeder ? bm::broomrows::BuildMapBinary(from, to, why) : bm::broomyrows::BuildMapBinary(from, to, why);
            return speeder ? bm::broomrows::BuildMapIcons(m < 2, from, to, why)
                           : bm::broomyrows::BuildMapIcons(m < 2, from, to, why);
        };
        const bool a = build(false, game, withBroomy);
        const bool b = a && build(true, withBroomy, out);
        const std::string drawn = m < 2 || binary ? "MapIcon_ActorVehicleSpeeder_Hired" : "textureid(cd_Icon_map_Speeder)";
        char what[160];
        snprintf(what, sizeof what, "%s draws Broomy and the speeder", file);
        if (!b) printf("         %s\n", why.c_str());
        Expect(b && out.find("Broomy") != std::string::npos && out.find(drawn) != std::string::npos, what);
        snprintf(what, sizeof what, "%s draws the speeder alone", file);
        const bool c = build(true, game, alone);
        if (!c) printf("         %s\n", why.c_str());
        Expect(c && alone.find(drawn) != std::string::npos, what);
        if (binary && c)
        {
            // The record's own check: the copy is the ibex's with AlpineIbex
            // renamed and each string's length fixed, and the count is one up.
            uint32_t sheets, count, was;
            size_t p = 1;
            memcpy(&sheets, &game[p], 4);
            p += 4;
            for (uint32_t i = 0; i < sheets; ++i)
            {
                uint32_t n;
                memcpy(&n, &game[p], 4);
                p += 4 + n;
            }
            memcpy(&was, &game[p + 4], 4);
            memcpy(&count, &alone[p + 4], 4);
            const size_t grew = alone.size() - game.size();
            const std::string ibexName = std::string("\x24\0\0\0", 4) + "MapIcon_ActorVehicleAlpineIbex_Hired";
            const std::string ourName = std::string("\x21\0\0\0", 4) + "MapIcon_ActorVehicleSpeeder_Hired";
            snprintf(what, sizeof what, "%s: one component more, its name length-fixed, the file %zu bytes longer", file,
                     grew);
            Expect(count == was + 1 && alone.find(ourName) != std::string::npos &&
                       alone.find(ibexName) != std::string::npos && alone.compare(0, p, game, 0, p) == 0,
                   what);
        }
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
        Expect(ok && report.find("26 of 26 branches into the air cut") != std::string::npos,
               "every branch into the air is cut");
        // groundpatches.h's first row: the body chart's entry, by its offset
        // on this build.
        const bm::broomchart::HoverPatch& entry = bm::broomchart::kGroundPatches[0];
        uint32_t ends = 0;
        if (ok && entry.chart == 0 && made[0].size() >= entry.at + 4) memcpy(&ends, made[0].data() + entry.at, 4);
        Expect(ends == 0x622E5BEC && entry.wyvern == 0xA9D16F76,
               "the body chart's entry ends in the ground idle, not the fly idle");
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
