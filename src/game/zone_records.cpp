// Zone exits (object_tables.h ZoneExit) and the map database's zone records that hold them: their destructors.
// See docs/notes/app.md.
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

void __fastcall ExitDestruct(ZoneExit* self) { std::destroy_at(&self->target); }
EI_REWRITTEN(0x00460460, ExitDestruct);

EI_REWRITTEN(0x004608B0, mfc::impl::ScalarDeleting<&ExitDestruct>::Delete);

// The exits' array (base vtable; each exit's string destroyed, the block freed, the fields left as they are),
// then the strings, last first.
void __fastcall ZoneRecordDestruct(MapRecord* self) {
    ZoneExits& exits = self->exits;
    exits.vtable = mfc::impl::ExitArrayClass::Vtable();
    if (ZoneExit* data = exits.data) {
        for (int i = 0, n = exits.size; i < n; ++i) std::destroy_at(&data[i].target);
        gamecrt::operator_delete(exits.data);
    }
    std::destroy_at(&self->camera);
    std::destroy_at(&self->picture);
    std::destroy_at(&self->picture2);
    std::destroy_at(&self->figure);
    std::destroy_at(&self->map);
    std::destroy_at(&self->mob);
    std::destroy_at(&self->allod);
    std::destroy_at(&self->name);
}
EI_REWRITTEN(0x00460970, ZoneRecordDestruct);

} // namespace zone
