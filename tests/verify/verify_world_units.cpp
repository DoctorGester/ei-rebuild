// Checks for CWorldServer::CreateUnits (src/game/world_units.cpp). It creates objects, runs AI
// and script code and queues a script line, so only ours runs and the result is checked.
#include "verify.h"

#include "../../src/game/chunk_stream.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;

struct RecordArray {
    void* vtable;
    std::uint8_t** data;
    int size;
};
} // namespace

// 0058EF20: afterwards every record's id names a unit of the world.
static void __fastcall CreateUnitsVerify(CWorldServer* self, void*, void* player, void* records,
                                         const Vector3* position, Float4 rotation, void* placements) {
    self->CreateUnits(player, records, position, rotation, placements);
    auto* array = static_cast<RecordArray*>(records);
    int created = 0;
    for (int i = 0; i < array->size; ++i) {
        unsigned id = *reinterpret_cast<unsigned*>(array->data[i] + 0x1C);
        CEngineObject* unit = self->FindObject(id);
        if (unit && unit->GetClassId() == world::kClassUnit) ++created;
    }
    ei::verify::Check("CWorldServer::CreateUnits", array->size, created, "%d records, %d units, placements %d, player %d",
                      array->size, created, placements != nullptr, player != nullptr);
}
EI_VERIFY(0x0058EF20, CreateUnitsVerify, 0, s_unused);

// 0058ECE0: the effect object is created (the world has no fewer objects afterwards).
static void __fastcall UseQuickItemVerify(CWorldServer* self, void*, void* item, CEngineObject* unit) {
    std::size_t before = self->objects_.map.Size();
    self->UseQuickItem(item, unit);
    std::size_t after = self->objects_.map.Size();
    ei::verify::Check("CWorldServer::UseQuickItem", true, after >= before, "objects %u -> %u",
                      static_cast<unsigned>(before), static_cast<unsigned>(after));
}
EI_VERIFY(0x0058ECE0, UseQuickItemVerify, 0, s_unused);
