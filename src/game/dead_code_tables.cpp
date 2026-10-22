// Tables of game.exe's data that only its dead code read (functions nothing calls), named here so that their bytes
// come from the source (C18o; docs/notes/standalone.md "Named data (C18)"). Nothing of ours reads them:
// tools/named_data.py --check proves that nothing else reaches them either.
namespace {

// The units' model folders by kind (the first, "", for a kind past the table), read by 00532650: the folder of a
// unit's second prototype (CUnit +0x23C; no call, jump or pointer reaches the function, which config/functions.csv
// does not list). "animals\\cow" and "animals\\pig" are one string, as the original's missing comma
// left them.
[[maybe_unused]] constexpr const char* kUnitModelFolders[15] = /*0078F7EC*/ {
    "", "heroes\\human\\male", "heroes\\human\\female", "heroes\\orc\\male", "heroes\\orc\\female", "monsters\\goblin",
    "monsters\\troll", "monsters\\ogre", "animals\\tiger", "monsters\\driad", "monsters\\banshee", "animals\\wolf",
    "animals\\boar", "animals\\cow" "animals\\pig", ""
};

// Three words read by 00602190 (dead: only the demo / autoplay screen's dead functions call it).
[[maybe_unused]] constexpr const char* kDemoWords[3] = /*007932F4*/ {"War", "Magic", "Islands"};

} // namespace
