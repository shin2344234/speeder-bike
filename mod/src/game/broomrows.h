#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The speeder's own rows and files, built from the bytes the game reads each
// time it reads them, so a player's install is the plugin alone. Pure
// functions: no game memory, no hooks, so the offline check
// (mod/tests/stackcheck.cpp) runs them against the shipped files and
// against Broomy's.
//
// The speeder is characterinfo 1900002, Riding_Speeder_1, with a mercenary
// list (88), a vehicle row (20001) and a map row (1900002) of its own.
// Nothing a shipped row uses changes except three lists: the Vehicle
// mercenary group and the saddle wedge's reserve slot (VehicleSlot) gain
// Vehicle_Speeder, and the map's Group_Invisible gains the map row. Broomy
// (1.0.3 on) is in the saddle wedge the same way. Broomy.asi loads first,
// so with it installed these build on Broomy's tables, which already hold
// Broomy's rows.
namespace bm::broomrows
{
    inline constexpr uint32_t kBroomyKey = 1900002;   // the speeder's; Broomy's is 1900001
    // The name the game shows.
    inline constexpr const char* kDisplayName = "Speeder Bike";

    inline constexpr const char* kTableDir = "gamedata/binarystaticinfo__/bin/";
    // Every table the speeder changes, in no particular order.
    inline constexpr const char* kTables[] = { "stringinfo", "characterinfo", "mercenaryinfo", "mercenarygroupinfo",
                                               "reserveslot", "vehicleinfo", "characterappearanceindexinfo",
                                               "interactioninfo", "uimaptextureinfo", "uifiltergroupinfo" };

    // A file the packs do not hold cannot be read, so the speeder's
    // appearance names one that ships and that no table names: a cut boat's
    // (Broomy has the cut Phoenix's). The plugin hands the appearance loader
    // the speeder's text whenever it loads this path.
    inline constexpr const char* kAppearancePath =
        "character/appearance/4_riding/cd_r0014_00_boat/cd_r0014_03_boat/cd_r0014_03_boat_0001_00000.app_xml";
    // Its gameplay data the same way: a character description that ships
    // and that no table names, served as the Wyvern's without its foot IK
    // and climbing. The descriptions are read by listing their folder, so a
    // new file name would never be read at all. Broomy has animal_seal.
    inline constexpr const char* kDescriptionPath = "character/descriptors/characterdescription/animal_orca.xml";
    inline constexpr const char* kDescriptionSource = "character/descriptors/characterdescription/mon_wyvern.xml";
    // The UI finds a portrait by name in this list, not by building a path.
    inline constexpr const char* kPortraitRegistry = "ui/xml/texture/cd_image_portrait_00.xml";
    // The map's silhouettes the same way; the ibex's is in this one.
    inline constexpr const char* kMapRegistry = "ui/xml/texture/cd_icon_map_03.xml";
    // The map's markup and styles, by the end of their path: the speeder's
    // map icon is a component of each markup file and a class of each style
    // sheet. Markup first.
    inline constexpr const char* kMapIconFiles[] = { "/minimapicon.thtml", "/worldmapicon.thtml", "/minimapicon.css",
                                                     "/worldmapicon.css" };
    // Each language's character names; the game reads one.
    inline constexpr const char* kPalocLeaf = "/character.paloc";
    // Whether Broomy's row names its own charts (GoldStar's, served as the
    // broom's) or the Wyvern's. Own unless the plugin cannot redirect the
    // broom's animations; set before the tables are built.
    void UseOwnCharts(bool own);

    // What the characterinfo build learns that the grant needs: Broomy's
    // row, and each row's _mercenaryInfo (-1 where the record does not say).
    struct CharacterFacts
    {
        int broomyRow = -1;
        std::vector<int16_t> mercInfo;
    };

    // A table's new header and body from the game's. `report` is one line
    // for the log. False, with `why`, when the game's rows are not the ones
    // this was written against; the game then keeps its own table.
    bool BuildTable(const char* name, const std::string& header, const std::string& body, std::string& outHeader,
                    std::string& outBody, std::string& report, std::string& why, CharacterFacts* facts = nullptr);

    // character.paloc with the speeder's name added.
    bool BuildPaloc(const std::string& game, std::string& out, std::string& why);
    // The portrait list with an entry for the speeder's portrait, a render
    // of the speeder (speeder/make_icons.py).
    bool BuildPortraits(const std::string& game, std::string& out, std::string& why);
    // The map's silhouette list with an entry for the speeder's.
    bool BuildMapTextures(const std::string& game, std::string& out, std::string& why);
    // A map markup file with a component for the speeder (`markup`), or a
    // map style sheet with its classes, copies of the hired ibex's drawn
    // with the speeder's silhouette.
    bool BuildMapIcons(bool markup, const std::string& game, std::string& out, std::string& why);
    // The broom's description from the Wyvern's.
    bool BuildDescription(const std::string& wyvern, std::string& out, std::string& why);
    // The speeder's appearance file.
    const std::string& AppearanceText();
}
