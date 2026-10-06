// Checks for the purge of deleted objects and the zone reset (src/game/script_cleanup.cpp). Both change
// the script's lists and the world's objects, so they run ours only and are checked against what
// they must leave.
#include "verify.h"

#include "../../src/game/script.h"

#include <vector>

namespace {
void* s_unused;

bool Alive(const CEngineObject* object) { return object && !(object->flags_ & 1); }

// A ScriptVars' object variables and group members as pointers, and what the purge must leave of
// them: a deleted object variable becomes null, a group keeps its live members in order.
struct VarsSnapshot {
    explicit VarsSnapshot(const ScriptVars& vars) : vars_(&vars) {
        for (int i = 0; i < vars.objects.size; ++i) {
            CEngineObject* o = vars.objects.data[i].object;
            objects.push_back(Alive(o) ? o : nullptr);
        }
        for (int g = 0; g < vars.groups.size; ++g) {
            std::vector<CEngineObject*> members;
            const ScriptGroup& group = vars.groups.data[g];
            for (int k = 0; k < group.size; ++k)
                if (Alive(group.data[k].object)) members.push_back(group.data[k].object);
            groups.push_back(members);
        }
    }
    bool Holds() const {
        const ScriptVars& vars = *vars_;
        if (vars.objects.size != static_cast<int>(objects.size()) || vars.groups.size != static_cast<int>(groups.size()))
            return false;
        for (int i = 0; i < vars.objects.size; ++i)
            if (vars.objects.data[i].object != objects[i]) return false;
        for (int g = 0; g < vars.groups.size; ++g) {
            const ScriptGroup& group = vars.groups.data[g];
            if (group.size != static_cast<int>(groups[g].size())) return false;
            for (int k = 0; k < group.size; ++k)
                if (group.data[k].object != groups[g][k]) return false;
        }
        return true;
    }
    const ScriptVars* vars_;
    std::vector<CEngineObject*> objects;
    std::vector<std::vector<CEngineObject*>> groups;
};

// 004BED90 (the world's RemoveObject 005935C0, both scripts): every run's, instance's and the global
// variables lose exactly their deleted objects (groups keep the order of the rest), and outside For
// loops the temporaries are down to their one entry each. Reached whenever a world object is removed:
// world_load, unit_create, combat, script_exec.
void __fastcall PurgeVerify(CScript* self) {
    std::vector<VarsSnapshot> snapshots;
    for (auto* node = self->runs_.head; node; node = node->next) snapshots.emplace_back(node->data.vars);
    for (auto* node = self->instances_.head; node; node = node->next) snapshots.emplace_back(node->data.vars);
    snapshots.emplace_back(self->globals_);
    ScriptPurgeDeletedObjects(self);
    int wrong = -1;
    for (std::size_t i = 0; i < snapshots.size() && wrong < 0; ++i)
        if (!snapshots[i].Holds()) wrong = static_cast<int>(i);
    bool temporaries = self->forDepth_ > 0 || (self->tempGroups_.count == 1 && self->tempObjects_.count == 1);
    ei::verify::Check("ScriptPurgeDeletedObjects", true, wrong < 0 && temporaries,
                      "variable set %d of %u wrong (runs, instances, globals); temporaries %d / %d", wrong,
                      unsigned(snapshots.size()), self->tempGroups_.count, self->tempObjects_.count);
}

template <typename T>
bool EmptyList(const mfc::CList<T>& list) {
    return list.count == 0 && !list.head && !list.tail && !list.free && !list.blocks;
}

// 004BE490 (from the zone reset): both lists empty.
void __fastcall DropVerify(CScript* self) {
    ScriptDropInstancesAndRuns(self);
    ei::verify::Check("ScriptDropInstancesAndRuns", true,
                      self->instances_.count == 0 && !self->instances_.head && self->runs_.count == 0 &&
                          !self->runs_.head,
                      "%d instances, %d runs left", self->instances_.count, self->runs_.count);
}

// 004BEB00 (EnterZone: every new game and zone change): no instances; one new run per WorldScript
// block (at the start of its block, without kill flag); For depth 0; every point light slot zero;
// the effect lists empty; every global float 0, string empty, object null and group empty.
void __fastcall ResetZoneVerify(CScript* self) {
    ScriptResetZone(self);
    bool ok = self->instances_.count == 0 && self->forDepth_ == 0 && self->runs_.count == self->worldBlocks_.count &&
              EmptyList(self->fxSources_) && EmptyList(self->particles_) && EmptyList(self->lightning_);
    for (auto* node = self->runs_.head; ok && node; node = node->next)
        ok = node->data.index == 0 && !node->data.killFlag && node->data.ticks == 0;
    const mfc::CArray<ScriptPointLight>& lights = self->pointLights_;
    for (int i = 0; ok && i < lights.size; ++i) {
        const ScriptPointLight& l = lights.data[i];
        ok = !l.object && l.id == 0 && l.removeWhenDone == 0 && l.moving == 0 && l.steps == 0;
    }
    const ScriptVars& g = self->globals_;
    for (int i = 0; ok && i < g.floats.size; ++i) ok = g.floats.data[i] == 0.0f;
    for (int i = 0; ok && i < g.strings.size; ++i) ok = static_cast<const char*>(g.strings.data[i])[0] == 0;
    for (int i = 0; ok && i < g.objects.size; ++i) ok = g.objects.data[i].object == nullptr;
    for (int i = 0; ok && i < g.groups.size; ++i) ok = g.groups.data[i].size == 0;
    ei::verify::Check("ScriptResetZone", true, ok, "%d runs for %d WorldScript blocks, %d instances",
                      self->runs_.count, self->worldBlocks_.count, self->instances_.count);
}

} // namespace

EI_VERIFY(0x004BED90, PurgeVerify, 0, s_unused);
EI_VERIFY(0x004BE490, DropVerify, 0, s_unused);
EI_VERIFY(0x004BEB00, ResetZoneVerify, 0, s_unused);
