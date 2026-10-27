// Zone exits (object_tables.h ZoneExit, 0x3C bytes) and the map database's zone records that hold
// them (MapRecord: eight CStrings, then a CArray<ZoneExit> at +0x48): their destructors. See
// docs/notes/app.md.
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "game/game_flow_data.h"
#include "game/object_tables.h"
#include "game/game_arrays.h"
#include "game_data.h"

#include <cstdint>
#include <memory>

namespace zone {

namespace {
void DestroyString(mfc::CString& text) { std::destroy_at(&text); }
} // namespace

// A zone exit's destructor (its CString at +0x28).
void __fastcall ExitDestruct(ZoneExit* self) { DestroyString(self->target); }
EI_REWRITTEN(0x00460460, ExitDestruct);

// The zone exit's scalar deleting destructor.
EI_REWRITTEN(0x004608B0, mfc::impl::ScalarDeleting<&ExitDestruct>::Delete);

// A zone record's destructor: the exits' array (base vtable; each exit's string
// destroyed, the block freed, the fields left as they are), then the strings from +0x1C down to +0.
void __fastcall ZoneRecordDestruct(MapRecord* self) {
    ZoneExits& exits = self->exits;
    exits.vtable = mfc::impl::ExitArrayClass::Vtable();
    if (ZoneExit* data = exits.data) {
        for (int i = 0, n = exits.size; i < n; ++i) DestroyString(data[i].target);
        gamecrt::operator_delete(exits.data);
    }
    DestroyString(self->camera);
    DestroyString(self->picture);
    DestroyString(self->picture2);
    DestroyString(self->figure);
    DestroyString(self->map);
    DestroyString(self->mob);
    DestroyString(self->allod);
    DestroyString(self->name);
}
EI_REWRITTEN(0x00460970, ZoneRecordDestruct);

} // namespace zone
