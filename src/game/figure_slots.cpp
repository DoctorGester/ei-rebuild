// Small members of the figure classes (CFigure, CFakeFigure) and their records around the
// animation loaders: class names and ids (virtuals), destructors, constructors that only
// clear. See docs/notes/objects.md ("Rewritten").
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "scene_loader.h"   // TextureNameRecord

#include <cstdint>
#include <cstring>
#include <memory>

namespace figure {

// CFigure's and CFakeFigure's class name and class id; a virtual that answers 1.
const char* __fastcall FigureClassName(void*) { return "CFigure"; }
EI_REWRITTEN(0x00453450, FigureClassName);
int __fastcall FigureClassId(void*) { return 0x44; }
EI_REWRITTEN(0x00453460, FigureClassId);
const char* __fastcall FakeFigureClassName(void*) { return "CFakeFigure"; }
EI_REWRITTEN(0x004547F0, FakeFigureClassName);
int __fastcall FakeFigureClassId(void*) { return 0x47; }
EI_REWRITTEN(0x00454800, FakeFigureClassId);
int __fastcall FakeFigureAnswerOne(void*) { return 1; }
EI_REWRITTEN(0x004547A0, FakeFigureAnswerOne);

// A record whose only member to destroy is a CString, and its scalar deleting destructor.
void __fastcall NamedRecordDestruct(scene::TextureNameRecord* self) { std::destroy_at(&self->name); }
EI_REWRITTEN(0x0044C490, NamedRecordDestruct);
// The {id, name} array's slots and element constructor (scene::TextureNameArrayClass).
EI_REWRITTEN(0x00451A00, scene::TextureNameRecords::Destruct<scene::TextureNameArrayClass>);
EI_REWRITTEN(0x00452A30, mfc::impl::ScalarDeleting<&scene::TextureNameRecords::Destruct<scene::TextureNameArrayClass>>::Delete);
EI_REWRITTEN(0x00451A70, scene::TextureNameRecords::Serialize);
EI_REWRITTEN(0x004553F0, scene::TextureNameRecords::ConstructElements);
EI_REWRITTEN(0x00454360, mfc::impl::ScalarDeleting<&NamedRecordDestruct>::Delete);

// A 0x38-byte record cleared; an aligned array emptied. Both return this.
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
