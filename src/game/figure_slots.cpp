// Small members of the figure classes (CFigure, CFakeFigure) and their records around the
// animation loaders: class names and ids (vtable slots), destructors, constructors that only
// clear. See docs/notes/objects.md ("Rewritten").
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_core.h"

#include <cstdint>
#include <cstring>

namespace figure {

// 00453450 / 00453460: CFigure's class name and class id; 004547F0 / 00454800: CFakeFigure's;
// 004547A0: a slot that answers 1.
const char* __fastcall FigureClassName(void*) { return "CFigure"; }
EI_REPLACE(0x00453450, FigureClassName);
int __fastcall FigureClassId(void*) { return 0x44; }
EI_REPLACE(0x00453460, FigureClassId);
const char* __fastcall FakeFigureClassName(void*) { return "CFakeFigure"; }
EI_REWRITTEN(0x004547F0, FakeFigureClassName);
int __fastcall FakeFigureClassId(void*) { return 0x47; }
EI_REWRITTEN(0x00454800, FakeFigureClassId);
int __fastcall FakeFigureAnswerOne(void*) { return 1; }
EI_REWRITTEN(0x004547A0, FakeFigureAnswerOne);

// 00453470: CFigure's scalar deleting destructor (its body 0059F030).
void* __fastcall FigureDelete(void* self, void*, unsigned flags) {
    FigureDestructBodyOriginal(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00453470, FigureDelete);

// 0044C490 / 00454360: a record whose only member to destroy is the CString at +4, and its scalar
// deleting destructor.
void __fastcall NamedRecordDestruct(std::uint8_t* self) { reinterpret_cast<mfc::CString*>(self + 4)->~CString(); }
EI_REWRITTEN(0x0044C490, NamedRecordDestruct);
void* __fastcall NamedRecordDelete(std::uint8_t* self, void*, unsigned flags) {
    NamedRecordDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REWRITTEN(0x00454360, NamedRecordDelete);

// 004533E0: a 0x38-byte record cleared; 00453490: an aligned array emptied. Both return this.
void* __fastcall ClearArray(std::uint32_t* self) {
    self[0] = 0;
    self[3] = 0;
    self[2] = 0;
    self[1] = 0;
    return self;
}
EI_REWRITTEN(0x00453490, ClearArray);
EI_REWRITTEN(0x005AD870, ClearArray);

} // namespace figure
