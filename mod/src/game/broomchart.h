#pragma once
#include <string>
#include <vector>

// Broomy's own action charts, made from the Wyvern's each time the game
// reads them. Pure functions, like broomrows: the offline check
// (tests/chartcheck.cpp) runs them against the shipped files.
//
// Under the Wyvern's own charts the broom played the Wyvern's animations on
// its skeleton, which drew it under the ground, and the Wyvern's sounds.
// Broomy's row names the charts of GoldStar, a cut dragon no character uses,
// and the plugin serves those files as the Wyvern's charts with every
// animation and blend they name pointed at the broom's.
//
// A chart names each animation and blend by path: a length byte counting
// the terminating zero, the text, then the zero. The file holds absolute
// offsets, so a path can change only for another of the same length. Each
// animation path becomes an alias of the same length in the same folder, a
// name no pack holds, and Redirect sends the game's loads of that alias (the
// animation, its LOD copy and its metadata) to broom files. Each blend
// becomes an alias that loads the broom's own blend, broom_riding_move.
namespace bm::broomchart
{
    // A file the game reads and the shipped file it is made from.
    struct Made
    {
        const char* path;
        const char* source;
    };

    // Broomy has GoldStar's charts and the armadillo's. The speeder has two
    // cut groups that no table names and stringinfo does not even hold: the
    // minigame's upper group (CD_Mon_Minigame_0001) holds the Wyvern's upper
    // chart and the ghost's lower group (CD_Monster_Common_Ghost_Lower) the
    // Wyvern's lower one, and the Wyvern's riding sub-chart goes over the
    // small tank's upper chart, whose package no table names either. The
    // chart loader takes no new file name; Broomy's first build crashed on
    // one.
    //
    // Up to 1.0.12 the speeder had the cut boat's groups, CD_R0014_Boat and
    // CD_R0014_Boat_Lower. Exe 3392 brought the boat back as the DLC's
    // pirate ship (characterinfo Riding_Big_pirateship_1 names both), so
    // every ship ran the speeder's Wyvern charts: it tilted, would not
    // sail and could not be left (Buxunqingmo, Trollzorsix9, charliev640).
    inline constexpr const char* kUpperGroup = "CD_Mon_Minigame_0001";
    inline constexpr const char* kLowerGroup = "CD_Monster_Common_Ghost_Lower";
    inline constexpr Made kCharts[] = {
        { "actionchart/bin__/upperaction/2_mon/minigame_0001_upper.paac",
          "actionchart/bin__/upperaction/2_mon/m0004_dragon_upper.paac" },
        { "actionchart/bin__/loweraction/2_mon/common_ghost_lower.paac",
          "actionchart/bin__/loweraction/2_mon/m0004_ride_dragon_lower.paac" },
        { "actionchart/bin__/upperaction/2_mon/m0010_tanksmall_upper.paac",
          "actionchart/bin__/upperaction/2_mon/m0004_ride_dragon_upper.paac" },
    };
    inline constexpr int kChartCount = sizeof kCharts / sizeof kCharts[0];
    // The Wyvern's attack tables go with its charts. Neither path ships, so
    // the find and existence checks see the Wyvern's (mod.cpp's aliases).
    inline constexpr Made kAttacks[] = {
        { "actionchart/bin__/attackinfo/upperaction/2_mon/minigame_0001_upper.paatt",
          "actionchart/bin__/attackinfo/upperaction/2_mon/m0004_dragon_upper.paatt" },
        { "actionchart/bin__/attackinfo/upperaction/2_mon/m0010_tanksmall_upper.paatt",
          "actionchart/bin__/attackinfo/upperaction/2_mon/m0004_ride_dragon_upper.paatt" },
    };
    // The package list, where the speeder's upper group gets the Wyvern's
    // riding sub-package.
    inline constexpr const char* kPackageDesc =
        "actionchart/bin__/description/characteractionpackagedescription.paacdesc";

    struct Alias
    {
        std::string leaf;     // the alias's file name without its extension
        std::string wyvern;   // the Wyvern's path it stands for, lower case
        const char* broom;    // the broom animation that plays for it
        bool wyvernMeta;      // keeps the Wyvern's metadata (its timing and events)
    };
    struct Aliases
    {
        std::vector<Alias> anims;
        std::vector<std::string> blends;   // alias file names without the extension
    };

    // The charts from the Wyvern's, in kCharts order. The files come out the
    // same size as the Wyvern's. `padded` writes the broom's own paths over
    // the Wyvern's instead of aliases: the text, then zeros to the old
    // length, which works only if the game reads a chart path up to its
    // first zero. No aliases then.
    // Research: the leaf, after "cd_rd_broom_basic_00_00_", that padded charts
    // give every idle; "nor_std_idle_01" unless set before they are built.
    void SetIdleLeaf(const char* leaf);
    // Research: hoverpatches.h applied (Broomy never in a ground state). Off.
    void SetHoverAlways(bool on);

    // The speeds as percentages (speedpatches.h): ground and flight of the
    // Wyvern's, boost of the flight speed, climb of the Wyvern's vertical
    // speeds. The speeder never flies, so only the ground speed comes from
    // the ini. Set before the charts are built.
    struct Speeds
    {
        int ground = 350;
        int flight = 250;
        int boost = 200;
        int climb = 200;
    };
    void SetSpeeds(const Speeds& speeds);

    bool BuildCharts(const std::string (&wyvern)[kChartCount], std::string (&out)[kChartCount], Aliases& aliases,
                     std::string& report, std::string& why, bool padded = false);

    // The package list with the speeder's upper group given the
    // sub-package the Wyvern's upper group has, pointing at the small tank's
    // chart path, and its lower group left with none, like the Wyvern's.
    // Broomy's own sub-package for GoldStar may already be there.
    bool BuildPackageDesc(const std::string& game, std::string& out, std::string& report, std::string& why);

    // For a path the game loads: the file to load in its place, when the
    // path is one of the aliases. An alias's metadata that is the Wyvern's
    // also names the broom's as `fallback`, for a Wyvern animation that has
    // none.
    bool Redirect(const Aliases& aliases, const char* path, std::string& to, std::string* fallback = nullptr);
}
