// The armour model name by record indices (006A3480; unit skins and models build the texture of a
// worn armour piece with it). See item.h; the item classes' own name builders are item_names.cpp.
#include "item.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstring>

using namespace itemdb;

namespace {
// The two-letter armour type codes (00796534..0079654C), by type.
constexpr const char* kArmorCodes[7] = {"hl", "pl", "lg", "sh", "pt", "bt", "gl"};
} // namespace

void __fastcall ArmorModelName(mfc::CString* out, std::uint32_t armorIndex, std::uint32_t materialIndex) {
    auto* material = reinterpret_cast<const MaterialRecordHead*>(Materials().At(materialIndex, kMaterialSize));
    auto* armor = reinterpret_cast<const ItemRecordHead*>(Armors().At(armorIndex, kArmorSize));
    out->Empty();
    if (!material || !armor) return;
    // The code goes into the stack slot of `materialIndex` (4 bytes): another type keeps the
    // index's bytes there.
    char code[4];
    std::memcpy(code, &materialIndex, sizeof(code));
    if (static_cast<std::uint32_t>(armor->type) <= 6) std::memcpy(code, kArmorCodes[armor->type], 3);
    out->Format("%s_%.2d.%s.%d", code, armor->number, material->name, armor->variant);
}
EI_REPLACE(0x006A3480, ArmorModelName);
