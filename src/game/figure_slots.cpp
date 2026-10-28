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

// A record whose only member to destroy is a CString, and its scalar deleting destructor.
void __fastcall NamedRecordDestruct(scene::TextureNameRecord* self) { std::destroy_at(&self->name); }
EI_REWRITTEN(0x0044C490, NamedRecordDestruct);
// The {id, name} array's slots and element constructor (scene::TextureNameArrayClass).
EI_REWRITTEN(0x00451A00, scene::TextureNameRecords::Destruct<scene::TextureNameArrayClass>);
EI_REWRITTEN(0x00452A30, mfc::impl::ScalarDeleting<&scene::TextureNameRecords::Destruct<scene::TextureNameArrayClass>>::Delete);
EI_REWRITTEN(0x00451A70, scene::TextureNameRecords::Serialize);
EI_REWRITTEN(0x004553F0, scene::TextureNameRecords::ConstructElements);
EI_REWRITTEN(0x00454360, mfc::impl::ScalarDeleting<&NamedRecordDestruct>::Delete);

} // namespace figure
