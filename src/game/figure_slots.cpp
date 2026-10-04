// Small members of the figure classes (CFigure, CFakeFigure) and their records around the
// animation loaders: class names and ids (vtable slots), destructors, constructors that only
// clear. See docs/notes/objects.md ("Sixth round").
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace figure {

namespace {
__declspec(naked) void __fastcall FigureDestructBody(void*) EI_ORIGINAL(0x0059F030)
__declspec(naked) void __fastcall RecordDestructAt(void*) EI_ORIGINAL(0x0044C490)
} // namespace

// 00453450 / 00453460: CFigure's class name and class id; 004547F0 / 00454800: CFakeFigure's;
// 004547A0: a slot that answers 1.
const char* __fastcall FigureClassName(void*) { return reinterpret_cast<const char*>(0x00784714); }
EI_REPLACE(0x00453450, FigureClassName);
int __fastcall FigureClassId(void*) { return 0x44; }
EI_REPLACE(0x00453460, FigureClassId);
const char* __fastcall FakeFigureClassName(void*) { return reinterpret_cast<const char*>(0x0078471C); }
EI_REPLACE(0x004547F0, FakeFigureClassName);
int __fastcall FakeFigureClassId(void*) { return 0x47; }
EI_REPLACE(0x00454800, FakeFigureClassId);
int __fastcall FakeFigureAnswerOne(void*) { return 1; }
EI_REPLACE(0x004547A0, FakeFigureAnswerOne);

// 00453470: CFigure's scalar deleting destructor (its body 0059F030).
void* __fastcall FigureDelete(void* self, void*, unsigned flags) {
    FigureDestructBody(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00453470, FigureDelete);

// 0044C490 / 00454360: a record whose only member to destroy is the CString at +4, and its scalar
// deleting destructor.
void __fastcall NamedRecordDestruct(std::uint8_t* self) { reinterpret_cast<mfc::CString*>(self + 4)->~CString(); }
EI_REPLACE(0x0044C490, NamedRecordDestruct);
void* __fastcall NamedRecordDelete(std::uint8_t* self, void*, unsigned flags) {
    RecordDestructAt(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00454360, NamedRecordDelete);

// 004533E0: a 0x38-byte record cleared; 00453490: an aligned array emptied. Both return this.
void* __fastcall ClearRecord38(void* self) {
    std::memset(self, 0, 0x38);
    return self;
}
EI_REPLACE(0x004533E0, ClearRecord38);
void* __fastcall ClearArray(std::uint32_t* self) {
    self[0] = 0;
    self[3] = 0;
    self[2] = 0;
    self[1] = 0;
    return self;
}
EI_REPLACE(0x00453490, ClearArray);

// 0044C480: the address of the member at +0x8C.
void* __fastcall MemberAt8C(std::uint8_t* self) { return self + 0x8C; }
EI_REPLACE(0x0044C480, MemberAt8C);

} // namespace figure
