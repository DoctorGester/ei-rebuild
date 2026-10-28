// Small members of the figure classes (CFigure, CFakeFigure) and their records around the
// animation loaders: class names and ids (virtuals), destructors, constructors that only
// clear. See docs/notes/objects.md ("Rewritten").
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

} // namespace figure
