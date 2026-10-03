// Differential checks for the class templates (src/game/class_templates.cpp).
//
// Fill (004A7080) runs ours on the real template and the original on a scratch template (allocated
// and constructed like the real ones, with the real one's class, flags and weights); the database
// pointers and every list the fill appends to must end equal. The name maps it reads through
// operator[] gain the same null entries in either order. The scratch is then deleted.
// Build (004A7630) allocates templates: ours only, then the vector holds the expected classes,
// each referenced once.
#include "verify.h"

#include "../../src/game/class_templates.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {
void* s_unused;

__declspec(naked) ClassTemplate* __fastcall TemplateConstruct(void*) EI_ORIGINAL(0x004A6580)

struct PtrVector {
    char* start;
    char* finish;
    char* eos;
};

bool SameVector(const void* a, const void* b) {
    auto* x = static_cast<const PtrVector*>(a);
    auto* y = static_cast<const PtrVector*>(b);
    std::size_t n = static_cast<std::size_t>(x->finish - x->start);
    return n == static_cast<std::size_t>(y->finish - y->start) && (n == 0 || std::memcmp(x->start, y->start, n) == 0);
}

// The vectors of one filter list (+0x11C or +0x1D4); returns the offset of the first difference.
int ListDifference(const std::uint8_t* a, const std::uint8_t* b) {
    const int vectors[] = {0x08, 0x14, 0x20, 0x2C, 0x38, 0x44};
    for (int offset : vectors)
        if (!SameVector(a + offset, b + offset)) return offset;
    return -1;
}

// 004A7080: PUSH -1; PUSH 0x7173C8 (7 bytes).
void(__fastcall* s_fill)(ClassTemplate*);
void __fastcall FillVerify(ClassTemplate* self) {
    void* memory = gamecrt::operator_new(sizeof(ClassTemplate));
    std::memset(memory, 0, 8);
    ClassTemplate* scratch = TemplateConstruct(memory);
    std::memcpy(reinterpret_cast<char*>(scratch) + 8, reinterpret_cast<char*>(self) + 8, 0x34 - 8);

    ClassTemplateFill(self);
    s_fill(scratch);

    bool sameDb = ei::VCall<void*>(scratch->items, 18) == ei::VCall<void*>(self->items, 18) &&
                  ei::VCall<void*>(scratch->spells, 22) == ei::VCall<void*>(self->spells, 22);
    int allowed = ListDifference(scratch->allowed, self->allowed);
    int magic = ListDifference(scratch->magic, self->magic);
    ei::verify::Check("ClassTemplateFill", true, sameDb && allowed < 0 && magic < 0,
                      "class %d: databases %s, all-list vector +0x%X, magic-list vector +0x%X", self->type,
                      sameDb ? "same" : "differ", allowed, magic);
    ei::VCall<void>(scratch, 0, 1);   // scalar deleting destructor
}

// 004A7630: ours only.
void __fastcall BuildVerify(ClassTemplateVector* self, void*, unsigned fourClasses) {
    ClassTemplatesBuild(self, nullptr, fourClasses);
    static const int kFour[] = {1, 3, 4, 5};
    static const int kFive[] = {1, 2, 3, 4, 5};
    bool four = static_cast<std::uint8_t>(fourClasses) != 0;
    const int* types = four ? kFour : kFive;
    int expected = four ? 4 : 5;
    int count = static_cast<int>(self->finish - self->start);
    bool ok = count == expected;
    for (int i = 0; ok && i < count; ++i) {
        const ClassTemplate* t = self->start[i];
        ok = t && t->type == types[i] && t->refCount == 1;
    }
    ei::verify::Check("ClassTemplatesBuild", true, ok, "%d templates (expected %d)", count, expected);
}

} // namespace

EI_VERIFY(0x004A7080, FillVerify, 7, s_fill);
EI_VERIFY(0x004A7630, BuildVerify, 0, s_unused);
