// Constructor and destructor bodies of the spell objects: the effect record (MapEffect), the spell
// (CSpell) and the cast spell (CSpellEffect). See spell.h.
#include "spell.h"

#include "map_object.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// A counted pointer's destructor and the counted pointer assignment of null: the old object loses a
// reference.
template <class T>
void Drop(T*& field) {
    T* old = field;
    field = nullptr;
    if (old) old->Release();
}

} // namespace

MapEffect* MapEffect::Construct() {
    type = 0;
    power = 0;
    ticksLeft = 0;
    value = 0;
    ending = 0;
    return this;
}
EI_REWRITTEN(0x00688510, MapEffect::Construct);


CSpell::CSpell() {
    ClearFields();
    Effect().Construct();
    speed_ = 0;
    range_ = 0;
    targetCount_ = 0;
    targetFlags_ = 0;
    manaCost_ = 0;
    complexity_ = 0;
    price_ = 0;
    prototypeField14_ = 0;
    skillNeeded_ = 1;
    std::memset(modifiers_, 0, sizeof(modifiers_));
}
EI_REWRITTEN_SPECIAL(0x0068B1F0, CSpell::CSpell);

CSpell::~CSpell() { CSpell::Destroy(); }
EI_REWRITTEN_SPECIAL(0x0068B2C0, CSpell::~CSpell);
EI_REWRITTEN_SPECIAL(0x0068B2A0, CSpell::~CSpell);

const char* CSpell::GetClassName() { return "CSpell"; }
int CSpell::GetClassId() { return 0x4F; }
EI_REWRITTEN(0x0068B280, CSpell::GetClassName);
EI_REWRITTEN(0x0068B290, CSpell::GetClassId);


CSpellEffect::CSpellEffect() {
    target = nullptr;
    caster = nullptr;
    new (Spell()) CSpell();
    visual = nullptr;
    sound = nullptr;
    light = nullptr;
    position = Vector3{0.0f, 0.0f, 0.0f};
    timer = 0;
    world = nullptr;
    flags = 0;
    silentEnd = 0;
    footprintStamped = 0;
}
EI_REWRITTEN_SPECIAL(0x0067E510, CSpellEffect::CSpellEffect);

CSpellEffect::~CSpellEffect() {
    if (footprintStamped) UpdateFootprint(static_cast<int>(Spell()->prototypeId_ >> 16), 0);
    if (visual) world->RemoveTimedObject(static_cast<unsigned>(visual->id_));
    Drop(visual);
    ReleaseSound();
    ReleaseLight();
    // The three counted pointers' destructors (empty by now).
    if (light) light->Release();
    if (sound) sound->Release();
    if (visual) visual->Release();
    Spell()->CSpell::~CSpell();
    if (caster) caster->Release();
    caster = nullptr;
    if (target) target->Release();
    target = nullptr;
}
EI_REWRITTEN_SPECIAL(0x0067E5D0, CSpellEffect::~CSpellEffect);
EI_REWRITTEN_SPECIAL(0x0067E5B0, CSpellEffect::~CSpellEffect);   // deleting

// --- The cast's start values and the object it forgets ------------------------------------

// The caster and the target are counted references (AIRefTo::Assign: nothing when it holds the
// object already; else the held one released, the new one counted).

void CSpellEffect::Init(CWorldServer* newWorld, CSpell* spell, CUnit* newCaster, const Vector3* newPosition,
                        CMapObject* newTarget, const Vector3* newTargetPoint, std::uint8_t flagA4) {
    spell->CopyTo(Spell());
    world = newWorld;
    position = *newPosition;
    caster.Assign(newCaster);
    target.Assign(newTarget);
    targetPoint = *newTargetPoint;
    state = 0;
    silentEnd = flagA4;
}
EI_REWRITTEN(0x0067ECB0, CSpellEffect::Init);

BOOL CSpellEffect::Forget(CEngineObject* object) {
    if (target == object) return 0;
    if (caster == object && caster) {
        caster->Release();
        caster = nullptr;
    }
    return 1;
}
EI_REWRITTEN(0x0067EF00, CSpellEffect::Forget);

// --- An effect's tick --------------------------------------------------------------

int MapEffect::Tick() {
    --ticksLeft;
    return ticksLeft > 0 ? 1 : 0;
}
EI_REWRITTEN(0x00688530, MapEffect::Tick);
