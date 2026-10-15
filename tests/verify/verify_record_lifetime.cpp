// Checks for the game records' compiler-written helpers ported in S13 (record_lifetime.h): the
// destructors, copy constructors and assignments of the item, spell, skill and quest records, the
// save list's entries, the music themes, the zone exits, the scene's face groups and the shared
// objects.
//
// All ours only: a destructor frees what the original would free a second time, and the copies
// construct or assign strings in place. A destructor's wrapper checks that the record is well formed
// before (its vectors ordered); a copy's wrapper compares the destination with the source after.
// Reached: the databases' records when they are loaded again (a new game resets them: 0057CBE0 and
// the three after it), the save / load window's list (ui_saveload), the music themes when music.reg
// is read (every start), the zone exits on a zone load, the face groups with every figure scene
// load, the shared objects when a saved game is loaded.
#include "verify.h"

#include "../../src/game/acks_db.h"
#include "../../src/game/ai_shape_cache.h"
#include "../../src/game/core_refcount.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/engine_object.h"
#include "../../src/game/item_db.h"
#include "../../src/game/quest_records.h"
#include "../../src/game/record_lifetime.h"
#include "../../src/game/scene_loader.h"
#include "../../src/game/sound.h"
#include "../../src/game/ui.h"
#include "../../src/game/unit_db.h"
#include "../../src/game/world.h"

#include <cstring>

using namespace itemdb;

namespace {
void* s_unused;

// A vector's three pointers in order (an empty one may have no storage).
template <typename T>
bool Ordered(const stlp::vector<T>& v) {
    auto& w = const_cast<stlp::vector<T>&>(v);
    return w.Start() <= w.Finish() && w.Finish() <= w.EndOfStorage() && (w.Start() || !w.EndOfStorage());
}

bool SameText(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
bool SameText(const mfc::CString& a, const mfc::CString& b) { return std::strcmp(a, b) == 0; }

bool SameArray(const mfc::CStringArray& a, const mfc::CStringArray& b) {
    if (a.GetSize() != b.GetSize()) return false;
    for (int i = 0; i < a.GetSize(); ++i)
        if (!SameText(a[i], b[i])) return false;
    return true;
}

// A destructor: the record's shape before, the run, the result.
template <typename R, typename F>
void CheckDestruct(const char* name, R* record, bool wellFormed, F destruct) {
    destruct(record);
    ei::verify::Check(name, true, wellFormed, "record %p malformed before", static_cast<void*>(record));
}

// ---- Item and spell records --------------------------------------------------------------------

void __fastcall HeadDestructVerify(ItemRecordHead* record) {
    bool ok = record->list20.size >= 0 && record->list50.size >= 0;
    CheckDestruct("ItemRecordHead::DestructHead", record, ok, [](ItemRecordHead* r) { r->DestructHead(); });
}
void __fastcall QuickDestructVerify(QuickItemRecord* record) {
    CheckDestruct("QuickItemRecord::Destruct", record, record->list20.size >= 0, [](QuickItemRecord* r) { r->Destruct(); });
}
void __fastcall QuestItemDestructVerify(QuestItemRecord* record) {
    bool ok = record->zonesBegin <= record->zonesEnd && record->zonesEnd <= record->zonesCapacity;
    CheckDestruct("QuestItemRecord::Destruct", record, ok, [](QuestItemRecord* r) { r->Destruct(); });
}
void __fastcall MaterialDestructVerify(MaterialRecordHead* record) {
    CheckDestruct("MaterialRecordHead::Destruct", record, record->key && record->name, [](MaterialRecordHead* r) { r->Destruct(); });
}
void __fastcall SpellDestructVerify(SpellRecord* record) {
    bool ok = Ordered(record->list48) && Ordered(record->list54);
    CheckDestruct("SpellRecord::Destruct", record, ok, [](SpellRecord* r) { r->Destruct(); });
}
void __fastcall SpellModifierDestructVerify(SpellModifierRecord* record) {
    CheckDestruct("SpellModifierRecord::Destruct", record, true, [](SpellModifierRecord* r) { r->Destruct(); });
}
void __fastcall SpellListDestructVerify(SpellListRecord* record) {
    bool ok = Ordered(record->list0C) && Ordered(record->list18);
    CheckDestruct("SpellListRecord::Destruct", record, ok, [](SpellListRecord* r) { r->Destruct(); });
}

// ---- Skill and quest records -------------------------------------------------------------------

void __fastcall SkillDestructVerify(SkillRecord* record) {
    CheckDestruct("SkillRecord::Destruct", record, Ordered(record->statNames), [](SkillRecord* r) { r->Destruct(); });
}
void __fastcall SkillLevelDestructVerify(SkillLevelRecord* record) {
    CheckDestruct("SkillLevelRecord::Destruct", record, true, [](SkillLevelRecord* r) { r->Destruct(); });
}
void __fastcall QuestDestructVerify(QuestRecord* record) {
    CheckDestruct("QuestRecord::Destruct", record, Ordered(record->places), [](QuestRecord* r) { r->Destruct(); });
}
void __fastcall BriefingDestructVerify(QuestBriefingRecord* record) {
    bool ok = true;
    for (const CStringVector& list : record->lists) ok = ok && Ordered(list);
    CheckDestruct("QuestBriefingRecord::Destruct", record, ok, [](QuestBriefingRecord* r) { r->Destruct(); });
}

// ---- The save list's entries -------------------------------------------------------------------

bool SameEntry(const SaveEntry& a, const SaveEntry& b) {
    return SameText(a.title_, b.title_) && SameText(a.folder_, b.folder_) && a.savedAt_ == b.savedAt_ &&
           std::memcmp(&a.gameTime_, &b.gameTime_, sizeof(float)) == 0 && SameText(a.map_, b.map_) &&
           SameText(a.zone_, b.zone_) && SameArray(a.stringArrays_[0], b.stringArrays_[0]) &&
           SameArray(a.stringArrays_[1], b.stringArrays_[1]) && a.broken_ == b.broken_;
}

SaveEntry* __fastcall EntryAssignVerify(SaveEntry* self, void*, const SaveEntry* from) {
    SaveEntry* result = self->Assign(from);
    ei::verify::Check("SaveEntry::Assign", true, result == self && SameEntry(*self, *from), "folder %s", from->folder_);
    return result;
}
SaveEntry* __fastcall EntryCopyVerify(SaveEntry* self, void*, const SaveEntry* from) {
    SaveEntry* result = self->CopyConstruct(from);
    ei::verify::Check("SaveEntry::CopyConstruct", true, result == self && SameEntry(*self, *from), "folder %s",
                      from->folder_);
    return result;
}
void __fastcall EntryDestructVerify(SaveEntry* self) {
    bool ok = self->stringArrays_[0].size >= 0 && self->stringArrays_[1].size >= 0;
    CheckDestruct("SaveEntry::Destruct", self, ok, [](SaveEntry* e) { e->Destruct(); });
}

// ---- Music themes, zone exits, face groups, shared objects -------------------------------------

void __fastcall ThemeAssignVerify(MusicTheme* self, void*, const MusicTheme* from) {
    self->Assign(from);
    bool ok = SameText(self->name, from->name) && self->dungeon == from->dungeon &&
              SameText(self->briefing, from->briefing) && SameText(self->constructor, from->constructor) &&
              SameArray(self->calm, from->calm) && SameArray(self->combat, from->combat);
    ei::verify::Check("MusicTheme::Assign", true, ok, "theme %s", static_cast<const char*>(from->name));
}

ZoneExit* __fastcall ExitAssignVerify(ZoneExit* self, void*, const ZoneExit* from) {
    ZoneExit* result = self->Assign(from);
    bool ok = result == self && std::memcmp(self, from, offsetof(ZoneExit, target)) == 0 &&
              SameText(self->target, from->target) && self->entrance == from->entrance && self->view == from->view &&
              self->hasDeployAngle == from->hasDeployAngle && self->deployAngle == from->deployAngle;
    ei::verify::Check("ZoneExit::Assign", true, ok, "target %s", from->target);
    return result;
}

void __fastcall GroupAssignVerify(scene::FaceGroup* self, void*, const scene::FaceGroup* from) {
    self->Assign(from);
    bool ok = SameText(self->name, from->name) && self->faces.size == from->faces.size &&
              (from->faces.size == 0 ||
               std::memcmp(self->faces.data, from->faces.data, sizeof(std::uint32_t) * from->faces.size) == 0);
    ei::verify::Check("FaceGroup::Assign", true, ok, "group %s, %d faces", static_cast<const char*>(from->name),
                      from->faces.size);
}

SharedObject* __fastcall SharedCopyVerify(SharedObject* self, void*, const SharedObject* from) {
    SharedObject* result = self->CopyFrom(from);
    bool ok = result == self && self->objectId == from->objectId && self->player == from->player &&
              self->money == from->money && self->objects.size == from->objects.size;
    for (int i = 0; ok && i < from->objects.size; ++i) ok = self->objects[i] == from->objects[i];
    ei::verify::Check("SharedObject::CopyFrom", true, ok, "object %d, %d objects", from->objectId, from->objects.size);
    return result;
}

// ---- Deleting destructors ----------------------------------------------------------------------

SharedObject* __fastcall SharedDeleteVerify(SharedObject* self, void*, unsigned flags) {
    bool ok = self->objects.size >= 0 && self->objects.size <= self->objects.maxSize;
    for (int i = 0; ok && i < self->objects.size; ++i) ok = !self->objects[i] || self->objects[i]->refCount_ > 0;
    SharedObject* result = self->DeletingDestruct(flags);
    ei::verify::Check("SharedObject::DeletingDestruct", true, ok && result == self, "flags %u", flags);
    return result;
}

stlp::string* __fastcall PairDeleteVerify(stlp::string* self, void*, unsigned flags) {
    auto* words = reinterpret_cast<char* const*>(self);
    bool ok = words[0] <= words[1] && words[1] <= words[2];
    stlp::string* result = records::LeadingStringDelete(self, nullptr, flags);
    ei::verify::Check("records::LeadingStringDelete", true, ok && result == self, "flags %u", flags);
    return result;
}

void* __fastcall ShapeDeleteVerify(void* self, void*, unsigned flags) {
    void* result = RefObjectDeletingDestructor(self, nullptr, flags);
    ei::verify::Check("RefObjectDeletingDestructor (unit shape)", true, result == self, "flags %u", flags);
    return result;
}

CEngineObject* __fastcall PartyBaseDeleteVerify(CEngineObject* self, void*, unsigned flags) {
    CEngineObject* result = self->DeletingDestruct(flags);
    ei::verify::Check("CEngineObject::DeletingDestruct (party base)", true, result == self, "flags %u", flags);
    return result;
}

// ---- Ack records, wave records, the shape map's pairs ------------------------------------------

template <typename Lists>
bool ListsOrdered(const Lists& lists) {
    for (const auto& list : lists)
        if (!Ordered(list)) return false;
    return true;
}
void __fastcall UnitAcksDestructVerify(acks::UnitAcks* self) {
    bool ok = ListsOrdered(self->lists) && Ordered(self->environmentSpeeches) && Ordered(self->namedSpeeches);
    CheckDestruct("UnitAcks::Destruct", self, ok, [](acks::UnitAcks* r) { r->Destruct(); });
}
void __fastcall ModelAcksDestructVerify(acks::ModelAcks* self) {
    CheckDestruct("ModelAcks::Destruct", self, ListsOrdered(self->lists), [](acks::ModelAcks* r) { r->Destruct(); });
}
void __fastcall ExtraAcksDestructVerify(acks::ExtraAcks* self) {
    CheckDestruct("ExtraAcks::Destruct", self, ListsOrdered(self->lists), [](acks::ExtraAcks* r) { r->Destruct(); });
}

WaveRecord* __fastcall WaveAssignVerify(WaveRecord* self, void*, const WaveRecord* from) {
    WaveRecord* result = self->Assign(from);
    const auto& a = *reinterpret_cast<const stlp::string*>(self->name);
    const auto& b = *reinterpret_cast<const stlp::string*>(from->name);
    bool ok = result == self && self->data == from->data && a == b && self->lastUsed == from->lastUsed &&
              self->size == from->size && self->uses == from->uses;
    ei::verify::Check("WaveRecord::Assign", true, ok, "wave %.*s", static_cast<int>(b.size()), b.data());
    return result;
}

using ShapePairT = UnitShapeCache::Map::Pair;
ShapePairT* __fastcall ShapePairConstructVerify(ShapePairT* self, void*, const ShapeCacheHandle* key,
                                                const RefPtr* value) {
    int refsBefore = key->ref ? key->ref->refs_ : 0;
    ShapePairT* result = ConstructShapePair(self, nullptr, key, value);
    CRefObject* shared = self->value.ptr;
    bool ok = result == self && std::memcmp(&self->key, key, sizeof(*key)) == 0 &&
              (!key->ref || key->ref->refs_ == refsBefore + 1) &&
              (shared ? (shared->flags6_ & CRefObject::kCopyOnShare) != 0 && shared->refs_ > 0
                      : !value->ptr || (value->ptr->flags7_ & CRefObject::kDetached) != 0);
    ei::verify::Check("ConstructShapePair", true, ok, "key %08X", key->key);
    return result;
}
void __fastcall ShapePairDestroyVerify(ShapePairT* self) {
    bool ok = (!self->key.ref || self->key.ref->refs_ > 0) && (!self->value.ptr || self->value.ptr->refs_ > 0);
    CheckDestruct("DestroyShapePair", self, ok, [](ShapePairT* p) { DestroyShapePair(p); });
}

} // namespace

EI_VERIFY(0x006A8950, HeadDestructVerify, 0, s_unused);
EI_VERIFY(0x006A89D0, HeadDestructVerify, 0, s_unused);
EI_VERIFY(0x006A8BD0, HeadDestructVerify, 0, s_unused);
EI_VERIFY(0x006A8A50, QuickDestructVerify, 0, s_unused);
EI_VERIFY(0x006A8AE0, QuestItemDestructVerify, 0, s_unused);
EI_VERIFY(0x006A88F0, MaterialDestructVerify, 0, s_unused);
EI_VERIFY(0x00685AA0, SpellDestructVerify, 0, s_unused);
EI_VERIFY(0x00685B70, SpellModifierDestructVerify, 0, s_unused);
EI_VERIFY(0x00589860, SpellListDestructVerify, 0, s_unused);
EI_VERIFY(0x00520D20, SkillDestructVerify, 0, s_unused);
EI_VERIFY(0x00520DE0, SkillLevelDestructVerify, 0, s_unused);
EI_VERIFY(0x006AF4D0, QuestDestructVerify, 0, s_unused);
EI_VERIFY(0x006AF5A0, BriefingDestructVerify, 0, s_unused);
EI_VERIFY(0x006538E0, EntryAssignVerify, 0, s_unused);
EI_VERIFY(0x00656640, EntryCopyVerify, 0, s_unused);
EI_VERIFY(0x00656720, EntryDestructVerify, 0, s_unused);
EI_VERIFY(0x006B4D40, ThemeAssignVerify, 0, s_unused);
EI_VERIFY(0x00583B50, ExitAssignVerify, 0, s_unused);
EI_VERIFY(0x00474C50, GroupAssignVerify, 0, s_unused);
EI_VERIFY(0x0059B510, SharedCopyVerify, 0, s_unused);
EI_VERIFY(0x0059DDE0, SharedDeleteVerify, 0, s_unused);
EI_VERIFY(0x005AE5B0, PairDeleteVerify, 0, s_unused);
EI_VERIFY(0x005AE5D0, PairDeleteVerify, 0, s_unused);
EI_VERIFY(0x005B3570, ShapeDeleteVerify, 0, s_unused);
EI_VERIFY(0x006611B0, PartyBaseDeleteVerify, 0, s_unused);
EI_VERIFY(0x006DC390, UnitAcksDestructVerify, 0, s_unused);
EI_VERIFY(0x006DC920, ModelAcksDestructVerify, 0, s_unused);
EI_VERIFY(0x006DCAF0, ExtraAcksDestructVerify, 0, s_unused);
EI_VERIFY(0x006BD1B0, WaveAssignVerify, 0, s_unused);
EI_VERIFY(0x005BE370, ShapePairConstructVerify, 0, s_unused);
EI_VERIFY(0x005BE280, ShapePairDestroyVerify, 0, s_unused);
