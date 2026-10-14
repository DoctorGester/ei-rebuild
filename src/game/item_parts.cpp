// Weapon model parts: the names of the figure parts a weapon is drawn with (006A0FF0, 006A1780).
#include "item.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstring>

using namespace itemdb;

namespace {

// One part name: prefix + the weapon's two-digit number + suffix, or a fixed name.
struct PartName {
    const char* prefix;
    bool numbered;
    const char* suffix;
};

// The strings at 00796454..00796508 and 0078FAF8.
constexpr PartName kSword[] = {{"rh3.sword", true, ""}};
constexpr PartName kAxe[] = {{"rh3.axe", true, ""}};
constexpr PartName kDagger[] = {{"rh3.dagger", true, ""}};
constexpr PartName kPike[] = {{"rh3.pike", true, ""}};
constexpr PartName kClub[] = {{"rh3.club", true, ""}};
constexpr PartName kBow[] = {
    {"lh3.bwpartb", true, ""}, {"bwparta", true, ""}, {"bwtetivaa", true, ""}, {"bwtetivab", true, ""},
    {"quiver", false, ""},     {"arrows", false, ""}, {"rh3.arrow00", false, ""},
};
constexpr PartName kCrossbow[] = {
    {"rh3.crbow", true, "main"},   {"crbow", true, "part01"}, {"crbow", true, "part02"},
    {"crbow", true, "tetiva01"},   {"crbow", true, "tetiva02"}, {"quiver", false, ""},
    {"arrows", false, ""},
};

struct PartSpan {
    const PartName* parts;
    int count;
};

template <int N> constexpr PartSpan List(const PartName (&parts)[N]) { return {parts, N}; }

// By weapon type (prototype +0x0C); other types have no parts.
constexpr PartSpan kPartsByType[] = {List(kSword), List(kAxe),  List(kDagger),  List(kPike),
                                     List(kClub),  List(kBow),  List(kCrossbow)};

// Calls add(name) for each part of the weapon prototype `record` (not null), in the original's order.
template <typename Add> void ForEachPart(const char* record, Add add) {
    char number[8];
    auto* head = reinterpret_cast<const ItemRecordHead*>(record);
    gamecrt::sprintf(number, "%.2d", head->number);
    std::uint32_t type = static_cast<std::uint32_t>(head->type);
    if (type >= sizeof(kPartsByType) / sizeof(kPartsByType[0])) return;
    const PartSpan& list = kPartsByType[type];
    for (int i = 0; i < list.count; ++i) {
        const PartName& part = list.parts[i];
        if (!part.numbered) {
            add(part.prefix);
            continue;
        }
        char name[64];
        char* end = name;
        const char* pieces[] = {part.prefix, number, part.suffix};
        for (const char* piece : pieces) {
            std::size_t n = std::strlen(piece);
            std::memcpy(end, piece, n);
            end += n;
        }
        *end = '\0';
        add(static_cast<const char*>(name));
    }
}

} // namespace

void __fastcall WeaponModelParts(std::uint32_t index, console::CStringArray* out) {
    out->SetSize(0, -1);
    const char* record = Weapons().At(index, kWeaponSize);
    if (!record) return;
    ForEachPart(record, [out](const char* name) { out->SetAtGrow(out->GetSize(), name); });
}
EI_REPLACE(0x006A0FF0, WeaponModelParts);

void __fastcall AppendWeaponModelParts(std::uint32_t packedId, stlp::vector<stlp::string>* out) {
    const char* record = Weapons().At(PrototypeIndex(packedId), kWeaponSize);
    if (!record) return;
    ForEachPart(record, [out](const char* name) {
        stlp::string s(name);
        out->push_back(s);
    });
}
EI_REPLACE(0x006A1780, AppendWeaponModelParts);
