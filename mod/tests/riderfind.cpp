// Offline check of riderfind.cpp: on the shipped ride_test3_lower.paac,
// ride_upper.paac and m0004_ride_dragon_upper.paac, the plugin's own search
// must find every word speeder/research/rider_words.py found (riderwords.h),
// with the same shipped and new values. Then the same on the charts with a
// node array grown by a record in the middle, so every record past it moves:
// the search must follow.
//
//   riderfind CHARTS [SPLICED]
//
// SPLICED, when given, holds Broomy's ride_test3_lower.paac and
// ride_upper.paac with Damiane's nodes spliced in, which must give the same
// records and values. `build.bat riderfind CHARTS [SPLICED]` builds and runs it.
#include <cstdio>
#include <cstring>
#include <string>

#include "game/chartwalk.h"
#include "game/riderfind.h"
#include "riderwords.h"

namespace
{
    int g_failed = 0;

    void Expect(bool ok, const char* what)
    {
        printf("    %-4s %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++g_failed;
    }

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

    const uint8_t* Bytes(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }

    template <size_t N>
    bool Same(const bm::riderfind::Swap* got, const riderwords::Row (&want)[N], uint32_t shift, uint32_t from)
    {
        for (size_t i = 0; i < N; ++i)
        {
            const uint32_t at = want[i].offset + (want[i].offset >= from ? shift : 0);
            if (got[i].offset != at || got[i].shipped != want[i].shipped || got[i].now != want[i].now)
            {
                printf("         row %zu: +0x%X %08X -> %08X, want +0x%X %08X -> %08X\n", i, got[i].offset,
                       got[i].shipped, got[i].now, at, want[i].shipped, want[i].now);
                return false;
            }
        }
        return true;
    }

    // The chart with a node's play-rate array (16-byte records) grown by a
    // copy of its first record, in a node a third of the way in or later:
    // every byte after it moves on by 16.
    bool Grow(std::string& d, uint32_t& from)
    {
        bm::chartwalk::Chart c;
        const char* why = "";
        if (!bm::chartwalk::Walk(Bytes(d), static_cast<uint32_t>(d.size()), c, why)) return false;
        for (uint32_t n = static_cast<uint32_t>(c.nodes.size() / 3); n < c.nodes.size(); ++n)
        {
            const uint32_t h = c.nodes[n], rate = c.NodeArray(n, 1);
            const uint8_t count = static_cast<uint8_t>(d[h + 0x76]);
            if (!rate || count == 255) continue;
            d.insert(rate, d.substr(rate, 16));
            d[h + 0x76] = static_cast<char>(count + 1);
            for (int i = 2; i < 11; ++i)   // the later arrays' offsets
            {
                uint16_t off;
                memcpy(&off, &d[h + 0x70 + 4 * i], 2);
                off = static_cast<uint16_t>(off + 16);
                memcpy(&d[h + 0x70 + 4 * i], &off, 2);
            }
            from = rate;
            return true;
        }
        return false;
    }

    void Check(const char* label, const std::string& lower, const std::string& upper, const std::string& rideOn,
               uint32_t shiftLower, uint32_t fromLower, uint32_t shiftUpper, uint32_t fromUpper)
    {
        printf("%s\n", label);
        const char* why = "";
        bm::riderfind::Lower lo{};
        const bool l = bm::riderfind::FindLower(Bytes(lower), static_cast<uint32_t>(lower.size()), lo, why);
        if (!l) printf("         lower: %s\n", why);
        Expect(l && Same(lo.swaps, riderwords::kLower, shiftLower, fromLower), "the lower chart's swaps");
        bool ik = l && lo.ikCount == static_cast<int>(sizeof riderwords::kIk / sizeof riderwords::kIk[0]);
        for (int i = 0; ik && i < lo.ikCount; ++i)
            ik = lo.ik[i] == riderwords::kIk[i] + (riderwords::kIk[i] >= fromLower ? shiftLower : 0);
        Expect(ik, "01B8F8D3's hand IK events");
        Expect(l && lo.pushOff == riderwords::kPushOff + (riderwords::kPushOff >= fromLower ? shiftLower : 0),
               "the push-off path");
        if (l)
            printf("         branches %u, %u; slot %u -> %u; slot %u -> %u\n", lo.ground, lo.air, lo.takeoffSlot,
                   lo.loop, lo.loopSlot, lo.exit);

        bm::riderfind::Upper up{};
        const bool u = bm::riderfind::FindUpper(Bytes(upper), static_cast<uint32_t>(upper.size()), up, why);
        if (!u) printf("         upper: %s\n", why);
        const auto moved = [&](uint32_t at) { return at + (at >= fromUpper ? shiftUpper : 0); };
        Expect(u && Same(up.swaps, riderwords::kUpper, shiftUpper, fromUpper) &&
                   Same(up.donor, riderwords::kDonor, shiftUpper, fromUpper),
               "the upper chart's swaps and the donor's times");
        Expect(u && up.idle == moved(riderwords::kIdleRecord) && up.donorRecord == moved(riderwords::kDonorRecord) &&
                   up.dismount == moved(riderwords::kDismount),
               "the broom idle's and EFD5260B's records, the dismount branch");

        uint32_t at[bm::riderfind::kIdleMax];
        int n = 0;
        const bool r = bm::riderfind::FindRideOnIdle(Bytes(rideOn), static_cast<uint32_t>(rideOn.size()), at, n, why);
        bool same = r && n == static_cast<int>(sizeof riderwords::kRideOnIdle / sizeof riderwords::kRideOnIdle[0]);
        for (int i = 0; same && i < n; ++i) same = at[i] == riderwords::kRideOnIdle[i];
        Expect(same, "the RideOn chart's ground idle branches");
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "usage: riderfind CHARTS [SPLICED]\n");
        return 2;
    }
    const std::string in = std::string(argv[1]) + "/";
    std::string lower, upper, rideOn;
    if (!Load(in + "ride_test3_lower.paac", lower) || !Load(in + "ride_upper.paac", upper) ||
        !Load(in + "m0004_ride_dragon_upper.paac", rideOn))
    {
        fprintf(stderr, "ride_test3_lower.paac, ride_upper.paac or m0004_ride_dragon_upper.paac missing in %s\n", argv[1]);
        return 2;
    }
    Expect(lower.size() == riderwords::kLowerSize && upper.size() == riderwords::kUpperSize &&
               rideOn.size() == riderwords::kRideOnSize,
           "the charts are the sizes rider_words.py read");
    Check("shipped", lower, upper, rideOn, 0, 0, 0, 0);

    uint32_t fromLower = 0, fromUpper = 0;
    std::string grownLower = lower, grownUpper = upper;
    const bool grown = Grow(grownLower, fromLower) && Grow(grownUpper, fromUpper);
    Expect(grown, "a node array grows in each chart");
    if (grown) Check("with a node array grown by 16 bytes", grownLower, grownUpper, rideOn, 16, fromLower, 16, fromUpper);

    // The lower chart after the other mod's load-time edits (Broomy writes
    // the same push-off name and IK times into the same buffer): the search
    // must find the same records, or the second mod to see the chart would
    // leave Kliff in the dragon pose.
    {
        bm::riderfind::Lower a{}, b{};
        const char* why = "";
        std::string edited = lower;
        const bool first = bm::riderfind::FindLower(Bytes(lower), static_cast<uint32_t>(lower.size()), a, why);
        if (first)
        {
            for (int i = 0; i < a.ikCount; ++i) memcpy(&edited[a.ik[i]], &bm::riderfind::kIkNever, 4);
            memset(&edited[a.pushOff], 0, sizeof bm::riderfind::kPushOffOld);
            memcpy(&edited[a.pushOff], bm::riderfind::kPushOffNew, sizeof bm::riderfind::kPushOffNew - 1);
        }
        const bool second = first && bm::riderfind::FindLower(Bytes(edited), static_cast<uint32_t>(edited.size()), b, why);
        if (!second) printf("         edited lower: %s\n", why);
        bool same = second && a.ikCount == b.ikCount && a.pushOff == b.pushOff && a.ground == b.ground &&
                    a.exit == b.exit && memcmp(a.swaps, b.swaps, sizeof a.swaps) == 0;
        for (int i = 0; same && i < a.ikCount; ++i) same = a.ik[i] == b.ik[i];
        Expect(same, "the lower chart with the push-off renamed and the hand IK at 1000 s is found the same");
    }

    // Broomy's charts with Damiane's nodes spliced in: the same records,
    // the same values, wherever the splices moved them.
    if (argc == 3)
    {
        printf("Broomy's charts with Damiane's nodes, from %s\n", argv[2]);
        const std::string sp = std::string(argv[2]) + "/";
        std::string sl, su;
        bm::riderfind::Lower a{}, b{};
        bm::riderfind::Upper c{}, d{};
        const char* why = "";
        const bool read = Load(sp + "ride_test3_lower.paac", sl) && Load(sp + "ride_upper.paac", su);
        Expect(read && sl != lower && su != upper, "the spliced charts are there and differ from the shipped ones");
        const bool l = read && bm::riderfind::FindLower(Bytes(lower), static_cast<uint32_t>(lower.size()), a, why) &&
                       bm::riderfind::FindLower(Bytes(sl), static_cast<uint32_t>(sl.size()), b, why);
        if (!l) printf("         lower: %s\n", why);
        bool same = l && a.ground == b.ground && a.air == b.air && a.takeoffSlot == b.takeoffSlot && a.loop == b.loop &&
                    a.loopSlot == b.loopSlot && a.exit == b.exit && a.ikCount == b.ikCount;
        for (int i = 0; same && i < bm::riderfind::kLowerSwaps; ++i)
            same = a.swaps[i].shipped == b.swaps[i].shipped && a.swaps[i].now == b.swaps[i].now;
        Expect(same, "the lower chart's records and words are found, with the shipped chart's values");
        const bool u = read && bm::riderfind::FindUpper(Bytes(upper), static_cast<uint32_t>(upper.size()), c, why) &&
                       bm::riderfind::FindUpper(Bytes(su), static_cast<uint32_t>(su.size()), d, why);
        if (!u) printf("         upper: %s\n", why);
        same = u && c.loop == d.loop && c.dismountIndex == d.dismountIndex;
        for (int i = 0; same && i < bm::riderfind::kUpperSwaps; ++i)
            same = c.swaps[i].shipped == d.swaps[i].shipped && c.swaps[i].now == d.swaps[i].now;
        for (int i = 0; same && i < 2; ++i)
            same = c.donor[i].shipped == d.donor[i].shipped && c.donor[i].now == d.donor[i].now;
        Expect(same, "the upper chart's records and words are found, with the shipped chart's values");
    }

    std::string cut = lower.substr(0, lower.size() - 1);
    bm::riderfind::Lower lo{};
    const char* why = "";
    Expect(!bm::riderfind::FindLower(Bytes(cut), static_cast<uint32_t>(cut.size()), lo, why),
           "a chart one byte short is refused");
    printf(g_failed ? "%d FAILED\n" : "all ok\n", g_failed);
    return g_failed ? 1 : 0;
}
