// Checks for the class template's trader stock (class_template_stock.cpp), ours only (the
// generators draw random numbers and make items). Scenario: trader_stock (`screen 5 <n>` opens
// the camp with constructor class n; the first opening per class generates).
#include "verify.h"

#include "../../src/game/class_templates.h"

#include <cstring>

// 004A6DC0: both stocks hold the 8 base and 8 scroll levels given.
static void (__fastcall* s_setLevels)(ClassTemplate*, void*, const float*, const float*);
static void __fastcall SetLevelsVerify(ClassTemplate* t, void*, const float* base, const float* scroll) {
    ClassTemplateSetLevels(t, nullptr, base, scroll);
    bool same = std::memcmp(t->allowed.baseLevels, base, 32) == 0 && std::memcmp(t->allowed.scrollLevels, scroll, 32) == 0 &&
                std::memcmp(t->magic.baseLevels, base, 32) == 0 && std::memcmp(t->magic.scrollLevels, scroll, 32) == 0;
    ei::verify::Check("ClassTemplateSetLevels", true, same, "levels copied to both stocks");
}
EI_VERIFY(0x004A6DC0, SetLevelsVerify, 0, s_setLevels);

// 004A6DF0 / 004A6F60: the stock generated from has the template's class index.
static void (__fastcall* s_generateItems)(ClassTemplate*);
static void __fastcall GenerateItemsVerify(ClassTemplate* t) {
    ClassTemplateGenerateItems(t);
    if (t->flag0C) ei::verify::Check("ClassTemplateGenerateItems class", t->type - 1, t->allowed.classIndex, "%d");
}
EI_VERIFY(0x004A6DF0, GenerateItemsVerify, 0, s_generateItems);

static void (__fastcall* s_generateMagic)(ClassTemplate*);
static void __fastcall GenerateMagicVerify(ClassTemplate* t) {
    ClassTemplateGenerateMagic(t);
    if (t->flag0D) ei::verify::Check("ClassTemplateGenerateMagic class", t->type - 1, t->magic.classIndex, "%d");
}
EI_VERIFY(0x004A6F60, GenerateMagicVerify, 0, s_generateMagic);
