// game.exe's per-file copies of a header's `const char* const` sector file extension (".SEC"): MSVC 6 gave each
// of the original's translation units that included the header its own copy in .rdata. Our ports name the copy
// they read where they read it (terrain_sector.cpp 00744164, ai_map_load.cpp 0073FD20); these are the rest:
// nothing of ours reads them (007441D8's only readers are the dead 006C67E0 and 006CC8D0), and they are named so
// that their bytes come from the source (C18o; docs/notes/standalone.md "Named data (C18)").
namespace {

[[maybe_unused]] constexpr const char* kSectorExtension1 = /*0073B9C4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension2 = /*0073BB14*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension3 = /*0073BEB0*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension4 = /*0073BFCC*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension5 = /*0073C2D4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension6 = /*0073C330*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension7 = /*0073C418*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension8 = /*0073C5A4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension9 = /*0073C7F4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension10 = /*0073C884*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension11 = /*0073CAC4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension12 = /*0073CB0C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension13 = /*0073CFC4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension14 = /*0073D090*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension15 = /*0073D0F4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension16 = /*0073D4E8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension17 = /*0073D6A8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension18 = /*0073D930*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension19 = /*0073DBC4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension20 = /*0073DCCC*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension21 = /*0073DD4C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension22 = /*0073DE84*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension23 = /*0073DFAC*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension24 = /*0073E038*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension25 = /*0073E164*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension26 = /*0073E94C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension27 = /*0073E9BC*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension28 = /*0073EBF4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension29 = /*0073EEE8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension30 = /*0073F158*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension31 = /*0073F180*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension32 = /*0073F408*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension33 = /*0073F7B8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension34 = /*0073F908*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension35 = /*0073FCB4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension36 = /*0073FCE0*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension37 = /*0073FDD4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension38 = /*0073FE04*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension39 = /*0073FE50*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension40 = /*0073FE84*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension41 = /*0073FECC*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension42 = /*0074001C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension43 = /*0074007C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension44 = /*00740190*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension45 = /*007402D0*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension46 = /*0074055C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension47 = /*00740EE0*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension48 = /*00740F0C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension49 = /*007410F0*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension50 = /*007414E8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension51 = /*00741BD8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension52 = /*00741D94*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension53 = /*00741E94*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension54 = /*00741F5C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension55 = /*0074207C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension56 = /*007420B4*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension57 = /*007420F0*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension58 = /*0074240C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension59 = /*00742B60*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension60 = /*00742BA8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension61 = /*007430F8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension62 = /*00743170*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension63 = /*00743270*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension64 = /*007433CC*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension65 = /*00743990*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension66 = /*00743A88*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension67 = /*00743D98*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension68 = /*00744148*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension69 = /*007441D8*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension70 = /*0074425C*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension71 = /*00744280*/ ".SEC";
[[maybe_unused]] constexpr const char* kSectorExtension72 = /*007442A0*/ ".SEC";

} // namespace
