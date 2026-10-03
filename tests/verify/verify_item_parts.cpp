// Differential checks for the weapon model part lists (item_parts.cpp). Both only write their output
// container: the original fills a scratch container, ours the real one, and the names are compared.
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/item.h"
#include "../../src/lib/stlp.h"

#include <cstring>

// 006A0FF0: SUB ESP,0x48; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (7 bytes). The original clears
// its scratch array first, as ours does the real one.
static void(__fastcall* s_weaponModelParts)(std::uint32_t, console::CStringArray*);
static void __fastcall WeaponModelPartsVerify(std::uint32_t index, console::CStringArray* out) {
    console::CStringArray scratch;
    s_weaponModelParts(index, &scratch);
    WeaponModelParts(index, out);
    bool same = scratch.GetSize() == out->GetSize();
    for (int i = 0; same && i < scratch.GetSize(); ++i)
        same = std::strcmp(scratch[i], (*out)[i]) == 0;
    ei::verify::Check("WeaponModelParts", true, same, "index %u: %d names vs %d (first %s / %s)", index,
                      scratch.GetSize(), out->GetSize(), scratch.GetSize() ? static_cast<const char*>(scratch[0]) : "",
                      out->GetSize() ? static_cast<const char*>((*out)[0]) : "");
}
EI_VERIFY(0x006A0FF0, WeaponModelPartsVerify, 7, s_weaponModelParts);

// 006A1780: MOV EAX,FS:[0] (6 bytes). The original appends to an empty scratch vector; ours appends
// to the real one; the names ours appended are compared with the scratch's.
static void(__fastcall* s_appendWeaponModelParts)(std::uint32_t, stlp::vector<stlp::string>*);
static void __fastcall AppendWeaponModelPartsVerify(std::uint32_t packedId, stlp::vector<stlp::string>* out) {
    stlp::vector<stlp::string> scratch;
    s_appendWeaponModelParts(packedId, &scratch);
    std::size_t before = out->size();
    AppendWeaponModelParts(packedId, out);
    bool same = out->size() - before == scratch.size();
    for (std::size_t i = 0; same && i < scratch.size(); ++i)
        same = out->begin()[before + i] == scratch.begin()[i];
    ei::verify::Check("AppendWeaponModelParts", true, same, "id %08X: %u names vs %u", packedId,
                      static_cast<unsigned>(scratch.size()), static_cast<unsigned>(out->size() - before));
    scratch.clear();
    scratch.FreeStorage();
}
EI_VERIFY(0x006A1780, AppendWeaponModelPartsVerify, 6, s_appendWeaponModelParts);
