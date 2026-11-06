#include "game/broomrows.h"

#include <cstdio>
#include <cstring>

#include "game/broomchart.h"
#include "game/hash.h"
#include "game/lz4.h"
#include "game/tablefile.h"

// Each new row starts as a copy of a shipped one and changes only what makes
// it Broomy's. Field positions in a characterinfo record move with the
// name's length and the digits in its localized strings, so every change
// finds its field by the value it holds and checks what is around it, and
// refuses when the value is not there exactly once.
//
// Exe 1.0.0.3392 (Steam build 26308950) changed every record this edits:
// each now opens with its name block and then its key, where 2976 had the
// key first (patch-dlc's catalog, master-looter.md), and characterinfo's
// fields were put in a new order. The positions below are 3392's, checked
// against the 2976 values of the same rows by patch-dlc's
// work/speeder-bike/3392/tools/ciflags.py and tablerows.py.

namespace bm::broomrows
{
    namespace
    {
        // Broomy (1900001, 87, 20000) has the keys before these; Broomy's own
        // rows may already be in the tables this builds on.
        constexpr const char* kBroomyName = "Riding_Speeder_1";
        constexpr uint8_t  kMercKey = 88;
        constexpr const char* kMercName = "Vehicle_Speeder";
        constexpr uint16_t kVehicleKey = 20001;
        constexpr const char* kVehicleName = "Speeder";
        constexpr uint8_t  kBroomyMerc = 87;           // Broomy's Vehicle_Broom
        constexpr uint64_t kFaintSeconds = 1;          // the ibex's is 300
        constexpr uint32_t kVanishMs = 1000;           // the ibex's (and every riding row's) is 900000

        // Riding_AlpineIbex_1, a riding row the game no longer uses.
        constexpr uint32_t kIbexKey = 29448;
        constexpr uint16_t kIbexVehicle = 16993;       // AlpineIbex
        constexpr uint8_t  kHorseMerc = 78;            // Vehicle_Horse
        constexpr uint8_t  kSpecialMerc = 81;          // Vehicle_Special, the ibex row's list
        constexpr uint8_t  kVehicleGroup = 65;         // mercenarygroupinfo Vehicle
        constexpr uint32_t kVehicleSlot = 1000006;     // reserveslot VehicleSlot, the saddle wedge
        // interactioninfo Broom_Ride, a cut row that seats the rider on the
        // broom's B_Rider_01 with pivots of its own, whose last field Kliff's
        // riding chart tests to pick the broom's mount. The speeder's mesh
        // hangs from B_Rider_01 too. Broomy's second interaction,
        // Wyvern_RideAir, mounts in the air, which a ground vehicle never
        // does, so the speeder has only Broom_Ride.
        constexpr uint32_t kWyvernRide[2] = { 1000068, 1000264 };   // Broom_Ride, Wyvern_RideAir
        constexpr uint32_t kIbexRide[2] = { 1000098, 1000085 };

        // The cut minigame's upper group and the cut ghost's lower one, which
        // no character uses, served as the speeder's own (broomchart.h); the
        // Wyvern's when they cannot be, or when a shipped row names them
        // (g_claimedBy). Broomy has GoldStar's.
        constexpr const char* kOwnUpper = bm::broomchart::kUpperGroup;
        constexpr const char* kOwnLower = bm::broomchart::kLowerGroup;
        constexpr const char* kWyvernUpper = "CD_M0004_Dragon";
        constexpr const char* kWyvernLower = "CD_M0004_Dragon_Lower";
        bool g_ownCharts = true;
        std::string g_claimedBy;
        constexpr const char* kGameplay = "animal_orca";
        constexpr const char* kSkeleton = "character/model/4_riding/cd_r0032_00_broom/cd_r0032_00_broom.pab";
        constexpr const char* kPortrait = "cd_portraitimage_Speeder";
        constexpr const char* kPortraitFile = "UI/texture/image/portraitimage/cd_mercenary_portrait_riding_speeder_1.dds";
        // The map icon. vehicleinfo names a uimaptextureinfo row, the row
        // names a component of the map's markup (minimapicon.thtml and
        // worldmapicon.thtml), and the component's CSS class draws a white
        // silhouette that a hired mount's class tints #74d4e5. The ibex's
        // row is 5347; the speeder's is a copy under its own key naming its
        // own component, drawn with the speeder's own silhouette, which the
        // map's texture list gains (speeder/make_icons.py).
        constexpr uint32_t kIbexMapTexture = 5347;
        constexpr const char* kIbexMapName = "Actor_Vehicle_AlpineIbex_Hired";
        constexpr const char* kIbexMapComponent = "MapIcon_ActorVehicleAlpineIbex_Hired";
        constexpr const char* kMapName = "Actor_Vehicle_Speeder_Hired";
        constexpr const char* kMapComponent = "MapIcon_ActorVehicleSpeeder_Hired";
        constexpr const char* kIbexMapImage = "textureid(cd_Icon_map_AlpineIbex)";
        constexpr const char* kMapTexture = "cd_Icon_map_Speeder";
        constexpr const char* kMapImage = "textureid(cd_Icon_map_Speeder)";
        constexpr const char* kMapFile = "UI/texture/cd_icon_map_speeder.dds";
        constexpr uint32_t kUnset = 0xEAC5E173;        // the tables' "no string" key

        // The ibex row's twelve string keys from _lowerActionChartPackageGroupName
        // to _appearanceName, in their 3392 order: lower, upper, tribe and
        // gender, skeleton variation, faction, prefab, socket file, skeleton,
        // level icon, projectiles, gameplay data, appearance. A null is the
        // unset key; a field with `check` false is neither checked nor
        // changed. 3392 also renamed the ibex's skeleton folders
        // (cd_r0002_00_fourfeet is r0002_00_fourfeet).
        struct Asset { const char* ibex; bool check; };
        constexpr int kAssetCount = 12;
        constexpr Asset kIbexStrings[kAssetCount] = {
            { nullptr, true }, { "CD_R0001_Horse", true }, { nullptr, false },
            { "character/binary/skeletonvariation/4_riding/r0002_00_fourfeet/r0002_00_alpineibex/"
              "r0002_00_alpineibex_0001.pabc", true },
            { nullptr, false }, { nullptr, true }, { nullptr, true },
            { "character/model/4_riding/r0002_00_fourfeet/r0002_00_horse/r0002_00_horse.pab", true },
            { nullptr, true }, { nullptr, true }, { "animal_horse", true },
            { "character/appearance/4_riding/cd_r0002_00_alpineibex/cd_r0002_00_alpineibex_0001_00001.app_xml", true } };
        constexpr int kUpperString = 1, kAppearanceString = 11;
        constexpr const char* kIbexPortrait = "cd_portraitimage_Riding_AlpineIbex_1";
        // Fixed positions in a 3392 characterinfo record, counted from the
        // end of its key: _isHirable, the 11th of the one-byte flags that
        // start there; _vehicleInfo, a u16; _mercenaryInfo, a u8. Everything
        // from the key to past _mercenaryInfo is fixed in size (the memo,
        // a text on 2976, is now a string key).
        constexpr size_t kHirableAt = 10, kVehicleAt = 0xCF, kMercAt = 0xD7;
        constexpr uint32_t kNameSub = 0x30, kHireSub = 0x32;

        uint32_t Key(const char* text) { return bm::hash::Little(text); }
        const char* ChartUpper() { return g_ownCharts ? kOwnUpper : kWyvernUpper; }
        const char* ChartLower() { return g_ownCharts ? kOwnLower : kWyvernLower; }

        template <typename T> std::string Bytes(T v) { return std::string(reinterpret_cast<const char*>(&v), sizeof v); }
        template <typename T> void Poke(std::string& s, size_t at, T v) { memcpy(&s[at], &v, sizeof v); }
        template <typename T> T Peek(const std::string& s, size_t at)
        {
            T v{};
            if (at + sizeof v <= s.size()) memcpy(&v, s.data() + at, sizeof v);
            return v;
        }

        std::string NameBlock(const char* text) { return Bytes<uint32_t>(static_cast<uint32_t>(strlen(text))) + text; }

        // A localized string field: u8 3, u32 sub, u32 character key, u32
        // length, then the decimal digits of (key << 32) | sub.
        std::string Localized(uint32_t key, uint32_t sub)
        {
            char digits[32];
            snprintf(digits, sizeof digits, "%llu", (static_cast<unsigned long long>(key) << 32) | sub);
            return std::string(1, '\x03') + Bytes<uint32_t>(sub) + Bytes<uint32_t>(key) +
                   Bytes<uint32_t>(static_cast<uint32_t>(strlen(digits))) + digits;
        }

        size_t Count(const std::string& hay, const std::string& needle)
        {
            size_t n = 0;
            for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
            return n;
        }

        // Where `needle` sits, when it sits there exactly once.
        bool Once(const std::string& rec, const std::string& needle, const char* what, size_t& at, std::string& why)
        {
            const size_t n = Count(rec, needle);
            if (n != 1)
            {
                why = std::string(what) + (n ? " is in the record more than once" : " is not in the record");
                return false;
            }
            at = rec.find(needle);
            return true;
        }

        bool Splice(std::string& rec, const std::string& from, const std::string& to, const char* what, std::string& why)
        {
            size_t at;
            if (!Once(rec, from, what, at, why)) return false;
            rec.replace(at, from.size(), to);
            return true;
        }

        // ---- the tables ----------------------------------------------------

        bool Strings(tablefile::Table& t, std::string& report, std::string& why)
        {
            const char* texts[] = { ChartUpper(), ChartLower(), kGameplay, kAppearancePath, kSkeleton, kPortrait,
                                    kMapComponent };
            int added = 0;
            for (const char* text : texts)
            {
                const uint32_t key = Key(text);
                const size_t len = strlen(text);
                if (const std::string* rec = t.Record(key))
                {
                    // four zero bytes, key u32, a flag byte, u32 length, text
                    // (2976 had the key first)
                    if (Peek<uint32_t>(*rec, 9) != len || rec->compare(13, len, text) != 0)
                    {
                        why = std::string("stringinfo's key for \"") + text + "\" holds another string";
                        return false;
                    }
                    continue;
                }
                const std::string rec = Bytes<uint32_t>(0) + Bytes<uint32_t>(key) + std::string(1, '\0') +
                                        Bytes<uint32_t>(static_cast<uint32_t>(len)) + text;
                if (!t.Append(key, rec))
                {
                    why = "a string row could not be added";
                    return false;
                }
                ++added;
            }
            char line[96];
            snprintf(line, sizeof line, "stringinfo: %d new strings", added);
            report = line;
            return true;
        }

        // Where a characterinfo record's key ends: the name block, then the
        // u32 key, which must be the row's. npos when the record is not
        // that shape.
        size_t AfterKey(const std::string& rec, uint64_t key)
        {
            const uint32_t n = Peek<uint32_t>(rec, 0);
            if (rec.size() < 8 || n > rec.size() - 8 || Peek<uint32_t>(rec, 4 + n) != key) return std::string::npos;
            return 8 + n;
        }

        void MercenaryInfoByRow(const tablefile::Table& t, std::vector<int16_t>& out)
        {
            out.assign(t.Rows(), -1);
            for (size_t row = 0; row < t.Rows(); ++row)
            {
                const std::string& rec = t.RecordAt(row);
                const size_t base = AfterKey(rec, t.KeyAt(row).key);
                if (base != std::string::npos && base + kMercAt < rec.size())
                    out[row] = static_cast<uint8_t>(rec[base + kMercAt]);
            }
        }

        bool Character(tablefile::Table& t, std::string& report, std::string& why, CharacterFacts* facts)
        {
            const std::string* src = t.Record(kIbexKey);
            if (!src)
            {
                why = "characterinfo has no Riding_AlpineIbex_1 row";
                return false;
            }
            std::string rec = *src;
            const std::string oldName = NameBlock("Riding_AlpineIbex_1");
            if (rec.compare(0, oldName.size(), oldName) != 0 || AfterKey(rec, kIbexKey) != oldName.size() + 4)
            {
                why = "characterinfo 29448 is not Riding_AlpineIbex_1";
                return false;
            }
            rec.replace(0, oldName.size(), NameBlock(kBroomyName));
            const size_t base = AfterKey(rec, kIbexKey);
            Poke<uint32_t>(rec, base - 4, kBroomyKey);

            // Its localized strings carry the character key twice, raw and
            // as the decimal digits of (key << 32) | sub.
            bool name = false;
            for (uint32_t sub = 0x30; sub < 0x40; ++sub)
            {
                const std::string old = Localized(kIbexKey, sub);
                const size_t n = Count(rec, old);
                if (n > 1)
                {
                    why = "a localized string is in the ibex row twice";
                    return false;
                }
                if (n == 1)
                {
                    rec.replace(rec.find(old), old.size(), Localized(kBroomyKey, sub));
                    name = name || sub == kNameSub;
                }
            }
            if (!name)
            {
                why = "the ibex row's name string was not found";
                return false;
            }
            if (rec.find(Bytes<uint32_t>(kIbexKey)) != std::string::npos)
            {
                why = "the ibex's key is still somewhere in the record";
                return false;
            }

            // The twelve asset strings, found by the upper chart's key.
            size_t at;
            if (!Once(rec, Bytes<uint32_t>(Key(kIbexStrings[kUpperString].ibex)), "the ibex's upper chart", at, why))
                return false;
            const size_t run = at - 4 * kUpperString;
            if (at < 4 * kUpperString || run + 4 * kAssetCount > rec.size())
            {
                why = "the ibex's asset strings do not fit the record";
                return false;
            }
            // No shipped row may name the speeder's chart groups, or that
            // character runs the speeder's charts. 1.0.12 and earlier had the
            // cut boat's, and exe 3392 gave them to the DLC's pirate ship,
            // which then tilted and would not sail. The two keys sit at the
            // same place after every row's key, so a later game that hands
            // these groups to a character is caught here and the speeder
            // falls back to the Wyvern's charts.
            if (g_ownCharts)
            {
                const size_t off = run - base;
                const uint32_t own[2] = { Key(kOwnLower), Key(kOwnUpper) };
                for (size_t row = 0; row < t.Rows() && g_claimedBy.empty(); ++row)
                {
                    const std::string& other = t.RecordAt(row);
                    const size_t b = AfterKey(other, t.KeyAt(row).key);
                    if (b == std::string::npos || b + off + 8 > other.size()) continue;
                    const uint32_t lower = Peek<uint32_t>(other, b + off), upper = Peek<uint32_t>(other, b + off + 4);
                    for (uint32_t k : own)
                        if (lower == k || upper == k)
                        {
                            const uint32_t n = Peek<uint32_t>(other, 0);
                            g_claimedBy = n && n < 128 ? other.substr(4, n) : std::to_string(t.KeyAt(row).key);
                            break;
                        }
                }
                if (!g_claimedBy.empty()) g_ownCharts = false;
            }
            const char* ours[kAssetCount] = { ChartLower(), ChartUpper(), nullptr, nullptr, nullptr, nullptr, nullptr,
                                              kSkeleton, nullptr, nullptr, kGameplay, kAppearancePath };
            for (int i = 0; i < kAssetCount; ++i)
            {
                const Asset& a = kIbexStrings[i];
                if (!a.check) continue;
                if (Peek<uint32_t>(rec, run + 4 * i) != (a.ibex ? Key(a.ibex) : kUnset))
                {
                    why = "the ibex's asset strings are not the ones it shipped with";
                    return false;
                }
                Poke<uint32_t>(rec, run + 4 * i, ours[i] ? Key(ours[i]) : kUnset);
            }

            // _vehicleInfo and _mercenaryInfo, at their fixed places.
            if (base + kMercAt >= rec.size() || Peek<uint16_t>(rec, base + kVehicleAt) != kIbexVehicle ||
                static_cast<uint8_t>(rec[base + kMercAt]) != kSpecialMerc)
            {
                why = "the ibex row's vehicle or mercenary list is not where 3392 puts it";
                return false;
            }
            Poke<uint16_t>(rec, base + kVehicleAt, kVehicleKey);
            rec[base + kMercAt] = static_cast<char>(kMercKey);
            // _callMercenaryCoolTime, u64 seconds. Under _mercenaryCoolTimeType
            // 1 it is how long a fainted mount cannot be called ("Cannot summon
            // because it's fainted"). One second rather than 0 keeps the timer,
            // and the HP refill at its end, running as for the ibex.
            if (!Once(rec, Bytes<uint64_t>(300), "the ibex's call cooldown (300)", at, why)) return false;
            Poke<uint64_t>(rec, at, kFaintSeconds);

            // _uiPortraitPath.
            if (!Once(rec, Bytes<uint32_t>(Key(kIbexPortrait)), "the ibex's portrait", at, why)) return false;
            Poke<uint32_t>(rec, at, Key(kPortrait));
            // _isHirable.
            if (rec[base + kHirableAt] != 0)
            {
                why = "the ibex row is already hirable, which it was not";
                return false;
            }
            rec[base + kHirableAt] = 1;
            // _vanishTickCount, u32 milliseconds, taken to be how long a
            // fainted body stays in the world. With the ibex's 15 minutes and
            // the 1 second faint timer, a fainted speeder was still refused
            // 15 s after the ride and could be called again only after a
            // reload, which clears every body (Buxunqingmo's 1.0.1 log, 6
            // October). Not yet seen in play.
            if (!Once(rec, Bytes<uint32_t>(900000), "the ibex's vanish time (900000 ms)", at, why)) return false;
            Poke<uint32_t>(rec, at, kVanishMs);

            // _interactionInfoList and, on 3392, _interactionInfoListWithRecursiveChild
            // after it, both holding the ibex's two ride interactions: each
            // becomes Broom_Ride alone.
            const std::string ibexList = Bytes<uint32_t>(2) + Bytes<uint32_t>(kIbexRide[0]) + Bytes<uint32_t>(kIbexRide[1]);
            const std::string wyvernList = Bytes<uint32_t>(1) + Bytes<uint32_t>(kWyvernRide[0]);
            const size_t lists = Count(rec, ibexList);
            if (lists < 1 || lists > 2)
            {
                why = "the ibex's interaction lists are not there once or twice";
                return false;
            }
            for (size_t i = 0; i < lists; ++i) rec.replace(rec.find(ibexList), ibexList.size(), wyvernList);

            const int row = static_cast<int>(t.Rows());
            if (!t.Append(kBroomyKey, rec))
            {
                why = "characterinfo already has key 1900002";
                return false;
            }
            if (facts)
            {
                facts->broomyRow = row;
                MercenaryInfoByRow(t, facts->mercInfo);
            }
            char line[128];
            snprintf(line, sizeof line, "characterinfo: %u %s at row %d, from Riding_AlpineIbex_1", kBroomyKey,
                     kBroomyName, row);
            report = line;
            return true;
        }

        bool Mercenary(tablefile::Table& t, std::string& report, std::string& why)
        {
            // The name block, then the u8 key.
            const std::string* src = t.Record(kHorseMerc);
            const std::string old = NameBlock("Vehicle_Horse");
            if (!src || src->size() <= old.size() || src->compare(0, old.size(), old) != 0 ||
                static_cast<uint8_t>((*src)[old.size()]) != kHorseMerc)
            {
                why = "mercenaryinfo 78 is not Vehicle_Horse";
                return false;
            }
            std::string rec = *src;
            rec[old.size()] = static_cast<char>(kMercKey);
            rec.replace(0, old.size(), NameBlock(kMercName));
            const int row = static_cast<int>(t.Rows());
            if (!t.Append(kMercKey, rec))
            {
                why = "mercenaryinfo already has key 88";
                return false;
            }
            char line[96];
            snprintf(line, sizeof line, "mercenaryinfo: %u %s at index %d, from Vehicle_Horse", kMercKey, kMercName, row);
            report = line;
            return true;
        }

        // The Vehicle group's list as shipped is six lists; Broomy adds its
        // own (87) at the end. Vehicle_Speeder goes after whatever is there.
        bool Group(tablefile::Table& t, std::string& report, std::string& why)
        {
            const std::string* src = t.Record(kVehicleGroup);
            if (!src)
            {
                why = "mercenarygroupinfo has no Vehicle group";
                return false;
            }
            std::string rec = *src;
            const std::string shipped("NOPRQA", 6);
            const std::string withBroomy = shipped + static_cast<char>(kBroomyMerc);
            const bool broomy = Count(rec, Bytes<uint32_t>(7) + withBroomy) == 1;
            const std::string oldList = broomy ? Bytes<uint32_t>(7) + withBroomy : Bytes<uint32_t>(6) + shipped;
            const std::string newList = Bytes<uint32_t>(broomy ? 8 : 7) + oldList.substr(4) + static_cast<char>(kMercKey);
            if (!Splice(rec, oldList, newList, "the Vehicle group's list", why)) return false;
            t.Set(kVehicleGroup, rec);
            report = broomy ? "mercenarygroupinfo: Vehicle lists Vehicle_Speeder after Broomy's Vehicle_Broom"
                            : "mercenarygroupinfo: Vehicle lists Vehicle_Speeder";
            return true;
        }

        // The saddle wedge is VehicleSlot, whose mercenary lists ship as [78
        // horses, 81 Vehicle_Special]; Broomy adds its 87. The speeder's list
        // goes after whatever is there, so it is one more mount the saddle
        // wedge scrolls to. A list in two slots crashed the game at boot
        // (Broomy, 28 September), so the speeder has no slot of its own.
        bool Slot(tablefile::Table& t, std::string& report, std::string& why)
        {
            const std::string* src = t.Record(kVehicleSlot);
            const std::string name = NameBlock("VehicleSlot");
            if (!src || src->compare(0, name.size(), name) != 0 || Peek<uint32_t>(*src, name.size()) != kVehicleSlot)
            {
                why = "reserveslot 1000006 is not VehicleSlot";
                return false;
            }
            std::string rec = *src;
            const std::string shipped = std::string(1, static_cast<char>(kHorseMerc)) + static_cast<char>(kSpecialMerc);
            const std::string withBroomy = shipped + static_cast<char>(kBroomyMerc);
            const bool broomy = Count(rec, Bytes<uint32_t>(3) + withBroomy) == 1;
            const std::string oldList = broomy ? Bytes<uint32_t>(3) + withBroomy : Bytes<uint32_t>(2) + shipped;
            const std::string newList = Bytes<uint32_t>(broomy ? 4 : 3) + oldList.substr(4) + static_cast<char>(kMercKey);
            if (!Splice(rec, oldList, newList, "VehicleSlot's mercenary lists", why)) return false;
            t.Set(kVehicleSlot, rec);
            report = broomy ? "reserveslot: VehicleSlot takes Vehicle_Speeder after Broomy's Vehicle_Broom"
                            : "reserveslot: VehicleSlot takes Vehicle_Speeder after the horses and Vehicle_Special";
            return true;
        }

        // Broom_Ride's two pivots are name blocks "TempPivotKey_Broom_Ride_1"
        // and "_2", each followed by the same length of data: the seat bone,
        // the position and turn, and last the key of the mount action. The
        // mount's riding chart (the Wyvern's) has one mount action; Broomy's
        // has it under both keys (broomchart.cpp and rightmountpatches.h),
        // and Broomy enters its riding chart only from a pivot whose key it
        // has. Until 0.12.24 the first pivot became a copy of the second and
        // Kliff got on from one side only. Both now stay as shipped; this
        // only checks they still end with the keys the chart is given.
        bool Interaction(tablefile::Table& t, std::string& report, std::string& why)
        {
            const std::string* src = t.Record(kWyvernRide[0]);
            const std::string name = NameBlock("Broom_Ride");
            const std::string first = NameBlock("TempPivotKey_Broom_Ride_1");
            const std::string second = NameBlock("TempPivotKey_Broom_Ride_2");
            const size_t a = src ? src->find(first) : std::string::npos;
            const size_t b = src ? src->find(second) : std::string::npos;
            if (!src || src->compare(0, name.size(), name) != 0 || a == std::string::npos || b == std::string::npos ||
                b < a + first.size())
            {
                why = "interactioninfo 1000068 is not Broom_Ride with its two pivots in order";
                return false;
            }
            const size_t from = a + first.size(), to = b + second.size(), n = b - from;
            constexpr char kKey1[4] = { '\xD3', '\x63', '\x2C', '\x26' }, kKey2[4] = { '\x4B', '\x68', '\x72', '\xA2' };
            if (to + n > src->size() || src->compare(from + n - 0x38, 4, kKey1, 4) != 0 ||
                src->compare(to + n - 0x38, 4, kKey2, 4) != 0)
            {
                why = "Broom_Ride's pivots do not end with their mount action keys where expected";
                return false;
            }
            report = "interactioninfo: Broom_Ride keeps both pivots, mount keys 262C63D3 and A272684B";
            return true;
        }

        bool Vehicle(tablefile::Table& t, std::string& report, std::string& why)
        {
            const std::string* src = t.Record(kIbexVehicle);
            const std::string old = NameBlock("AlpineIbex");
            if (!src || src->compare(0, old.size(), old) != 0 || Peek<uint16_t>(*src, old.size()) != kIbexVehicle)
            {
                why = "vehicleinfo 16993 is not AlpineIbex";
                return false;
            }
            std::string rec = *src;
            Poke<uint16_t>(rec, old.size(), kVehicleKey);
            rec.replace(0, old.size(), NameBlock(kVehicleName));
            // _uiMapTextureInfo, a u32 key: the speeder's map row, not the ibex's.
            if (!Splice(rec, Bytes<uint32_t>(kIbexMapTexture), Bytes<uint32_t>(kBroomyKey), "the ibex's map icon", why))
                return false;
            if (!t.Append(kVehicleKey, rec))
            {
                why = "vehicleinfo already has key 20001";
                return false;
            }
            report = "vehicleinfo: 20001 Speeder, from AlpineIbex";
            return true;
        }

        // u32 0, key, tag, u8 0, appearance string key at +0x11, f32 scale
        // at +0x15, key at +0x19 (2976: key, tag, then the u32 0).
        bool AppearanceIndex(tablefile::Table& t, std::string& report, std::string& why)
        {
            const std::string* src = t.Record(kIbexKey, -2);
            if (!src || src->size() != 29 || Peek<uint32_t>(*src, 0) != 0 || Peek<uint32_t>(*src, 4) != kIbexKey ||
                Peek<uint32_t>(*src, 25) != kIbexKey || Peek<uint32_t>(*src, 0x11) != Key(kIbexStrings[kAppearanceString].ibex))
            {
                why = "the ibex's appearance index row is not the 29-byte shape naming its appearance";
                return false;
            }
            std::string rec = *src;
            Poke<uint32_t>(rec, 4, kBroomyKey);
            Poke<uint32_t>(rec, 0x11, Key(kAppearancePath));
            Poke<float>(rec, 0x15, 1.0f);
            Poke<uint32_t>(rec, 25, kBroomyKey);
            if (!t.Append(kBroomyKey, rec, -2))
            {
                why = "the appearance index already has (1900002, -2)";
                return false;
            }
            report = "characterappearanceindexinfo: (1900002, -2), the speeder's appearance at scale 1";
            return true;
        }

        // The name, u32 key, the component's stringinfo key, then two
        // localized strings: u8 0x19, u32 sub (0x190, 0x191), u32 key, u32
        // length, the digits of (key << 32) | sub.
        bool MapTexture(tablefile::Table& t, std::string& report, std::string& why)
        {
            const std::string* src = t.Record(kIbexMapTexture);
            const std::string old = NameBlock(kIbexMapName);
            if (!src || src->compare(0, old.size(), old) != 0 || Peek<uint32_t>(*src, old.size()) != kIbexMapTexture)
            {
                why = "uimaptextureinfo 5347 is not Actor_Vehicle_AlpineIbex_Hired";
                return false;
            }
            std::string rec = *src;
            Poke<uint32_t>(rec, old.size(), kBroomyKey);
            rec.replace(0, old.size(), NameBlock(kMapName));
            if (!Splice(rec, Bytes<uint32_t>(Key(kIbexMapComponent)), Bytes<uint32_t>(Key(kMapComponent)),
                        "the ibex's map component", why))
                return false;
            int strings = 0;
            for (uint32_t sub = 0x190; sub < 0x1A0; ++sub)
            {
                // Localized() without its leading type byte, which is 0x19 here.
                const std::string from = Localized(kIbexMapTexture, sub).substr(1);
                const size_t n = Count(rec, from);
                if (n > 1)
                {
                    why = "a localized string is in the ibex's map row twice";
                    return false;
                }
                if (n == 1)
                {
                    rec.replace(rec.find(from), from.size(), Localized(kBroomyKey, sub).substr(1));
                    ++strings;
                }
            }
            if (!strings || rec.find(Bytes<uint32_t>(kIbexMapTexture)) != std::string::npos)
            {
                why = "the ibex's map row does not carry its key where it was";
                return false;
            }
            if (!t.Append(kBroomyKey, rec))
            {
                why = "uimaptextureinfo already has key 1900002";
                return false;
            }
            report = "uimaptextureinfo: 1900002 Actor_Vehicle_Speeder_Hired, from the ibex's";
            return true;
        }

        // The game checks at boot that every uimaptextureinfo row is in a
        // filter group, and stops loading when one is not (Broomy, 4
        // October). The ibex's is in Group_Invisible (1000015): the name,
        // u32 key, u8 0, five bytes 3392 added (02 DF 0F 93 75 in every
        // group), u32 1, u32 count, then entries of a u32 map key and five
        // zero bytes. The speeder's goes in after the ibex's.
        bool FilterGroup(tablefile::Table& t, std::string& report, std::string& why)
        {
            constexpr uint32_t kGroup = 1000015;
            const std::string* src = t.Record(kGroup);
            const std::string name = NameBlock("Group_Invisible");
            const size_t countAt = name.size() + 4 + 10, list = countAt + 4;
            if (!src || src->size() < list || src->compare(0, name.size(), name) != 0 ||
                Peek<uint32_t>(*src, name.size()) != kGroup || (*src)[name.size() + 4] != 0 ||
                Peek<uint32_t>(*src, countAt - 4) != 1)
            {
                why = "uifiltergroupinfo 1000015 is not Group_Invisible";
                return false;
            }
            std::string rec = *src;
            const uint32_t count = Peek<uint32_t>(rec, countAt);
            const std::string zeros(5, '\0');
            size_t ibex = std::string::npos;
            for (uint32_t i = 0; i < count; ++i)
            {
                const size_t at = list + i * 9;
                if (at + 9 > rec.size() || rec.compare(at + 4, 5, zeros) != 0)
                {
                    why = "Group_Invisible's list is not the shape it was";
                    return false;
                }
                const uint32_t key = Peek<uint32_t>(rec, at);
                if (key == kBroomyKey)
                {
                    why = "Group_Invisible already lists 1900002";
                    return false;
                }
                if (key == kIbexMapTexture) ibex = at;
            }
            if (ibex == std::string::npos)
            {
                why = "Group_Invisible does not list the ibex's map row";
                return false;
            }
            rec.insert(ibex + 9, Bytes<uint32_t>(kBroomyKey) + zeros);
            Poke<uint32_t>(rec, countAt, count + 1);
            t.Set(kGroup, rec);
            report = "uifiltergroupinfo: Group_Invisible lists the speeder's map row next to the ibex's";
            return true;
        }

        // Every occurrence of `from` in `s` replaced by `to`.
        std::string Replaced(std::string s, const std::string& from, const std::string& to)
        {
            for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
                s.replace(at, from.size(), to);
            return s;
        }

        // A texture list with one more entry, refused when it already has
        // one by that name.
        bool AddTexture(const std::string& game, const char* name, const char* file, const char* rect,
                        std::string& out, std::string& why)
        {
            if (game.find(std::string("Name=\"") + name + "\"") != std::string::npos)
            {
                why = std::string("the list already names ") + name;
                return false;
            }
            if (game.find("<Texture ") == std::string::npos)
            {
                why = "the list has no Texture entries";
                return false;
            }
            size_t end = game.size();
            while (end && game[end - 1] == '\n') --end;
            out.assign(game, 0, end);
            out += std::string("\n<Texture Name=\"") + name + "\" Filename=\"" + file + "\" Type=\"Image\" GetRect=\"" +
                   rect + "\"/>\n";
            return true;
        }
    }

    void UseOwnCharts(bool own) { g_ownCharts = own; }
    const std::string& ChartsClaimedBy() { return g_claimedBy; }

    bool BuildTable(const char* name, const std::string& header, const std::string& body, std::string& outHeader,
                    std::string& outBody, std::string& report, std::string& why, CharacterFacts* facts)
    {
        tablefile::Table t;
        std::string parse;
        if (!t.Parse(header, body, parse))
        {
            why = std::string("the shipped table does not parse: ") + parse;
            return false;
        }
        bool ok = false;
        if (!strcmp(name, "stringinfo")) ok = Strings(t, report, why);
        else if (!strcmp(name, "characterinfo")) ok = Character(t, report, why, facts);
        else if (!strcmp(name, "mercenaryinfo")) ok = Mercenary(t, report, why);
        else if (!strcmp(name, "mercenarygroupinfo")) ok = Group(t, report, why);
        else if (!strcmp(name, "reserveslot")) ok = Slot(t, report, why);
        else if (!strcmp(name, "vehicleinfo")) ok = Vehicle(t, report, why);
        else if (!strcmp(name, "characterappearanceindexinfo")) ok = AppearanceIndex(t, report, why);
        else if (!strcmp(name, "interactioninfo")) ok = Interaction(t, report, why);
        else if (!strcmp(name, "uimaptextureinfo")) ok = MapTexture(t, report, why);
        else if (!strcmp(name, "uifiltergroupinfo")) ok = FilterGroup(t, report, why);
        else why = "not a table the speeder changes";
        return ok && t.Build(outHeader, outBody, why);
    }

    // "paloc", four zero bytes, u32 packed size at +9, u32 plain size at
    // +13, zeros to +0x200, then one LZ4 block. The plain list is entries of
    // u64 3, u32 length and the key's decimal digits, u32 length and the
    // text, then a u32 entry count. A character's name is keyed
    // (characterKey << 32) | 0x30.
    bool BuildPaloc(const std::string& game, std::string& out, std::string& why)
    {
        constexpr size_t kWrap = 0x200;
        if (game.size() < kWrap || game.compare(0, 5, "paloc") != 0)
        {
            why = "character.paloc is not wrapped the way this reader expects";
            return false;
        }
        const uint32_t packed = Peek<uint32_t>(game, 9), plainSize = Peek<uint32_t>(game, 13);
        if (game.size() != kWrap + packed)
        {
            why = "character.paloc's packed size does not match the file";
            return false;
        }
        std::string plain;
        if (!lz4::Decompress(reinterpret_cast<const uint8_t*>(game.data()) + kWrap, packed, plain, plainSize))
        {
            why = "character.paloc's block does not decompress";
            return false;
        }
        // Walk the entries: the count must come out right, and Broomy must
        // not be in it yet.
        char key[32];
        snprintf(key, sizeof key, "%llu", (static_cast<unsigned long long>(kBroomyKey) << 32) | kNameSub);
        size_t at = 0;
        uint32_t entries = 0;
        uint64_t lastKind = 3;
        while (plain.size() - at > 4)
        {
            if (plain.size() - at < 16)
            {
                why = "character.paloc's list ends inside an entry";
                return false;
            }
            lastKind = Peek<uint64_t>(plain, at);
            const uint32_t klen = Peek<uint32_t>(plain, at + 8);
            if (klen > plain.size() - at - 12)
            {
                why = "character.paloc's list ends inside a key";
                return false;
            }
            const bool ours = klen == strlen(key) && plain.compare(at + 12, klen, key) == 0;
            at += 12 + klen;
            if (plain.size() - at < 4)
            {
                why = "character.paloc's list ends inside an entry";
                return false;
            }
            const uint32_t vlen = Peek<uint32_t>(plain, at);
            if (vlen > plain.size() - at - 4)
            {
                why = "character.paloc's list ends inside a text";
                return false;
            }
            at += 4 + vlen;
            ++entries;
            if (ours)
            {
                why = "character.paloc already names the speeder";
                return false;
            }
        }
        if (plain.size() - at != 4 || Peek<uint32_t>(plain, at) != entries)
        {
            why = "character.paloc's entry count does not match its entries";
            return false;
        }
        std::string entry = Bytes<uint64_t>(lastKind) + Bytes<uint32_t>(static_cast<uint32_t>(strlen(key))) + key +
                            Bytes<uint32_t>(static_cast<uint32_t>(strlen(kDisplayName))) + kDisplayName;
        plain.replace(at, 4, entry + Bytes<uint32_t>(entries + 1));

        const std::string block = lz4::StoreLiterals(plain);
        out.assign(game, 0, kWrap);
        Poke<uint32_t>(out, 9, static_cast<uint32_t>(block.size()));
        Poke<uint32_t>(out, 13, static_cast<uint32_t>(plain.size()));
        out += block;
        return true;
    }

    bool BuildPortraits(const std::string& game, std::string& out, std::string& why)
    {
        return AddTexture(game, kPortrait, kPortraitFile, "0,0,256,256", out, why);
    }

    bool BuildMapTextures(const std::string& game, std::string& out, std::string& why)
    {
        return AddTexture(game, kMapTexture, kMapFile, "0,0,100,100", out, why);
    }

    bool BuildMapIcons(bool markup, const std::string& game, std::string& out, std::string& why)
    {
        // Broomy's copies, when Broomy is installed, are already in it.
        if (game.find("Speeder") != std::string::npos)
        {
            why = "it already names the speeder";
            return false;
        }
        if (markup)
        {
            // The ibex's component, copied after itself as the speeder's.
            const size_t start = game.find(std::string("<component name=\"") + kIbexMapComponent + "\"");
            const size_t close = start == std::string::npos ? start : game.find("</component>", start);
            if (close == std::string::npos)
            {
                why = "it has no component for the hired ibex";
                return false;
            }
            const size_t end = close + strlen("</component>");
            out.assign(game, 0, end);
            out += "\n\t" + Replaced(game.substr(start, end - start), "AlpineIbex", "Speeder");
            out.append(game, end, std::string::npos);
            return true;
        }
        // Each line of the ibex's classes, copied after itself as the
        // speeder's, drawn with the speeder.
        size_t copied = 0, from = 0;
        out.clear();
        while (from < game.size())
        {
            size_t eol = game.find('\n', from);
            eol = eol == std::string::npos ? game.size() : eol + 1;
            const std::string line = game.substr(from, eol - from);
            out += line;
            if (line.find("-AlpineIbex") != std::string::npos)
            {
                std::string ours = Replaced(Replaced(line, kIbexMapImage, kMapImage), "AlpineIbex", "Speeder");
                if (ours.empty() || ours.back() != '\n') ours += '\n';
                out += ours;
                ++copied;
            }
            from = eol;
        }
        if (!copied || out.find(kMapImage) == std::string::npos)
        {
            why = "it has no class drawing the ibex";
            return false;
        }
        return true;
    }

    namespace
    {
        // A u32-length string of printable bytes at `p`; its text and end.
        bool StringAt(const std::string& d, size_t p, std::string& text, size_t& end)
        {
            if (p + 4 > d.size()) return false;
            const uint32_t n = Peek<uint32_t>(d, p);
            if (n > 4096 || p + 4 + n > d.size()) return false;
            for (size_t i = p + 4; i < p + 4 + n; ++i)
                if (static_cast<uint8_t>(d[i]) < 0x20 || static_cast<uint8_t>(d[i]) >= 0x7F) return false;
            text.assign(d, p + 4, n);
            end = p + 4 + n;
            return true;
        }

        // The end of the compiled component header at `p`: a name starting
        // MapIcon_, its classes, its script and a byte, its attribute pairs.
        bool HeaderEnd(const std::string& d, size_t p, size_t& end)
        {
            std::string s;
            if (!StringAt(d, p, s, p) || s.compare(0, 8, "MapIcon_") != 0) return false;
            for (int lists : { 1, 0, 2 })
            {
                if (lists == 0)
                {
                    if (!StringAt(d, p, s, p) || p + 1 > d.size()) return false;
                    p += 1;
                    continue;
                }
                if (p + 4 > d.size()) return false;
                const uint32_t n = Peek<uint32_t>(d, p);
                if (n > 64) return false;
                p += 4;
                for (uint32_t i = 0; i < n * lists; ++i)
                    if (!StringAt(d, p, s, p)) return false;
            }
            end = p;
            return true;
        }
    }

    // The file (the Broomy session's reading, 29 October): u8 0, u32 n and n
    // style sheet paths, u32 0, u32 component count, then the components. A
    // component is its children first, then its own header (HeaderEnd). The
    // styles stay text, so the speeder's served .css still draws it.
    bool BuildMapBinary(const std::string& game, std::string& out, std::string& why)
    {
        // Broomy's copy, when Broomy is installed, is already in it.
        if (game.find("Speeder") != std::string::npos)
        {
            why = "it already names the speeder";
            return false;
        }
        size_t p = 1;
        std::string s;
        if (game.size() < 5)
        {
            why = "it is too short";
            return false;
        }
        const uint32_t sheets = Peek<uint32_t>(game, p);
        p += 4;
        for (uint32_t i = 0; i < sheets; ++i)
            if (sheets > 64 || !StringAt(game, p, s, p))
            {
                why = "its style sheet list does not read";
                return false;
            }
        const size_t countAt = p + 4;
        if (countAt + 4 > game.size() || Peek<uint32_t>(game, p) != 0)
        {
            why = "it has no component count where it was";
            return false;
        }
        const std::string needle = Bytes<uint32_t>(static_cast<uint32_t>(strlen(kIbexMapComponent))) + kIbexMapComponent;
        size_t nameAt;
        if (!Once(game, needle, "the hired ibex's component", nameAt, why)) return false;
        size_t end = 0, start = 0;
        if (!HeaderEnd(game, nameAt, end))
        {
            why = "the hired ibex's component header does not read";
            return false;
        }
        // Children come before a component's header, so the ibex's record
        // starts where the header before it ends.
        bool found = false;
        for (size_t q = nameAt; !found && q > countAt + 4; --q)
        {
            size_t e;
            if (HeaderEnd(game, q - 1, e) && e <= nameAt) start = e, found = true;
        }
        if (!found)
        {
            why = "no component comes before the hired ibex's";
            return false;
        }
        const std::string rec = game.substr(start, end - start);
        // Every string naming AlpineIbex, renamed with its length.
        std::string ours;
        size_t from = 0;
        int renamed = 0;
        for (size_t at = rec.find("AlpineIbex"); at != std::string::npos; at = rec.find("AlpineIbex", from))
        {
            bool inString = false;
            size_t q = at >= 4 ? at - 4 : 0, strEnd = 0;
            for (int back = 0; back < 256 && q >= from; ++back, --q)
            {
                if (StringAt(rec, q, s, strEnd) && q + 4 <= at && strEnd >= at + 10)
                {
                    inString = true;
                    break;
                }
                if (q == 0) break;
            }
            if (!inString)
            {
                why = "the hired ibex's record names AlpineIbex outside a string";
                return false;
            }
            const std::string renamedText = Replaced(s, "AlpineIbex", "Speeder");
            ours.append(rec, from, q - from);
            ours += Bytes<uint32_t>(static_cast<uint32_t>(renamedText.size())) + renamedText;
            from = strEnd;
            ++renamed;
        }
        ours.append(rec, from, std::string::npos);
        if (renamed < 2)
        {
            why = "the hired ibex's record names AlpineIbex in fewer than two strings";
            return false;
        }
        out.assign(game, 0, end);
        out += ours;
        out.append(game, end, std::string::npos);
        Poke<uint32_t>(out, countAt, Peek<uint32_t>(game, countAt) + 1);
        return true;
    }

    // The Wyvern's description with the parts that fit only the Wyvern cut:
    // under it, quadruped foot IK looked for feet the broom does not have and
    // pulled it half under the ground whenever it moved on the ground.
    bool BuildDescription(const std::string& wyvern, std::string& out, std::string& why)
    {
        struct Cut { const char* open; const char* close; };
        const Cut cuts[] = { { "<PoseModifierDesc>", "</PoseModifierDesc>" },
                             { "<FreeClimbPartInfo ", "/>" },
                             { "<ClimbSocketInfo>", "</ClimbSocketInfo>" },
                             { "<HitPartGroupInfo>", "</HitPartGroupInfo>" } };
        out = wyvern;
        for (const Cut& c : cuts)
        {
            const size_t a = out.find(c.open);
            if (a == std::string::npos || out.find(c.open, a + 1) != std::string::npos)
            {
                why = std::string("the Wyvern's description does not have exactly one ") + c.open;
                return false;
            }
            size_t b = out.find(c.close, a);
            if (b == std::string::npos)
            {
                why = std::string("the Wyvern's description does not close ") + c.open;
                return false;
            }
            b += strlen(c.close);
            // and the whitespace after it, as the old build's regex took it
            while (b < out.size() && (out[b] == ' ' || out[b] == '\t' || out[b] == '\r' || out[b] == '\n')) ++b;
            out.erase(a, b - a);
        }
        if (out.find("FootIK") != std::string::npos || out.find("<PhysicsData") == std::string::npos)
        {
            why = "the broom's description did not come out as intended";
            return false;
        }
        return true;
    }

    // The broom's appearance with the cut Phoenix's prefab, whose mesh,
    // material and skeleton the plugin serves as the speeder's
    // (speederfiles.h).
    const std::string& AppearanceText()
    {
        static const std::string text =
            "\xEF\xBB\xBF\r\n<Appearance>\r\n\t<Customization CustomizationFile=\"\" DecorationParamFile=\"\"/>\r\n"
            "\t<Nude>\r\n\t\t<Prefab Name=\"CD_M0004_00_Phoenix_00_0001\"/>\r\n\t</Nude>\r\n</Appearance>";
        return text;
    }
}
