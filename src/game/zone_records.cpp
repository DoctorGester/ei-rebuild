// Zone exits (object_tables.h MapExit, 0x3C bytes) and the map database's zone records that hold
// them (MapRecord: eight CStrings, then a CArray<MapExit> at +0x48): their destructors. See
// docs/notes/app.md.
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game/game_flow_data.h"
#include "game/object_tables.h"

#include <cstdint>

namespace zone {

namespace {
// The records keep their CStrings as the bare data pointer.
void DestroyString(char*& text) { reinterpret_cast<mfc::CString*>(&text)->~CString(); }
__declspec(naked) void __fastcall ExitDestructAt(void*) EI_ORIGINAL(0x00460460)
} // namespace

// 00460460: a zone exit's destructor (its CString at +0x28).
void __fastcall ExitDestruct(MapExit* self) { DestroyString(self->target); }
EI_REPLACE(0x00460460, ExitDestruct);

// 004608B0: the zone exit's scalar deleting destructor.
void* __fastcall ExitDelete(void* self, void*, unsigned flags) {
    ExitDestructAt(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004608B0, ExitDelete);

// 00460970: a zone record's destructor: the exits' array (base vtable 0073C3F4; each exit's string
// destroyed, the block freed, the fields left as they are), then the strings from +0x1C down to +0.
void __fastcall ZoneRecordDestruct(MapRecord* self) {
    mfc::CArray<MapExit>& exits = self->exits;
    exits.vtable = reinterpret_cast<void*>(flowdata::kExitArrayBaseVtable);
    if (MapExit* data = exits.data) {
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
EI_REPLACE(0x00460970, ZoneRecordDestruct);

} // namespace zone
