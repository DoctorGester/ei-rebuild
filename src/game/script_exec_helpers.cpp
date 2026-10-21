// Command helpers of the script runtime: areas (AddRectToArea, AddRoundToArea, DeleteArea) and point
// lights (CreatePointLight, MovePointLight, DeletePointLight). See script.h and
// docs/notes/script.md.
#include "script.h"
#include "game_strings.h"
#include "base_manager.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_script.h"
#include "game_arrays.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // inline assembly: no optimisation across it

namespace {


struct World {
    void AddTimedObject(void* object);    // 0058E520
};
void World::AddTimedObject(void* object) { WorldAddTimed(this, nullptr, static_cast<CEngineObject*>(object)); }
World* TheWorld() { return ei::GamePointer<World>(0x007AFF60); }

// The world's manager of timed objects (spells and effects; CWorld::manager48_, 007AFFA8).
CBaseManager* EffectManager() { return static_cast<CBaseManager*>(TheServerWorld()->manager48_); }

// CArray<ScriptAreaPart>::SetSize(newSize, -1) as the area functions inline it.
void SetPartCount(mfc::CArray<ScriptAreaPart>* parts, int newSize) {
    constexpr int kSize = sizeof(ScriptAreaPart);
    if (newSize == 0) {
        if (parts->data) {
            gamecrt::operator_delete(parts->data);
            parts->data = nullptr;
        }
        parts->maxSize = 0;
        parts->size = 0;
        return;
    }
    if (!parts->data) {
        parts->data = static_cast<ScriptAreaPart*>(gamecrt::operator_new(newSize * kSize));
        std::memset(parts->data, 0, newSize * kSize);
        parts->maxSize = newSize;
        parts->size = newSize;
        return;
    }
    if (newSize <= parts->maxSize) {
        if (newSize > parts->size) std::memset(&parts->data[parts->size], 0, (newSize - parts->size) * kSize);
        parts->size = newSize;
        return;
    }
    int grow = parts->growBy;
    if (grow == 0) {
        grow = parts->size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 1024) grow = 1024;
    }
    int newMax = parts->maxSize + grow;
    if (newSize >= newMax) newMax = newSize;
    auto* data = static_cast<ScriptAreaPart*>(gamecrt::operator_new(newMax * kSize));
    std::memcpy(data, parts->data, parts->size * kSize);
    std::memset(&data[parts->size], 0, (newSize - parts->size) * kSize);
    gamecrt::operator_delete(parts->data);
    parts->data = data;
    parts->size = newSize;
    parts->maxSize = newMax;
}

void AddPart(mfc::CArray<ScriptAreaPart>* parts, const ScriptAreaPart& part) {
    int at = parts->size;
    SetPartCount(parts, at + 1);
    parts->data[at] = part;
}

ScriptPointLight* Lights(CScript* script) { return script->pointLights_.data; }
int LightCount(CScript* script) { return script->pointLights_.size; }

// MovePointLight's step: (target - position) / steps per axis. The x difference stays in the FPU,
// the y and z differences are rounded to float first, as the original computes them.
void LightStep(ScriptPointLight* light, const float* target, const float* position) {
    const double dx = static_cast<double>(target[0]) - position[0];
    const float dy = static_cast<float>(static_cast<double>(target[1]) - position[1]);
    const float dz = static_cast<float>(static_cast<double>(target[2]) - position[2]);
    // FILD steps; FDIVR 1.0f (0073B84C), rounded to float.
    const float inverse = static_cast<float>(static_cast<double>(1.0f /*0073B84C*/) / light->steps);
    light->step.x = static_cast<float>(dx * inverse);
    light->step.y = static_cast<float>(static_cast<double>(dy) * inverse);
    light->step.z = static_cast<float>(static_cast<double>(dz) * inverse);
}

} // namespace

// ---- Areas --------------------------------------------------------------------------------------

void __fastcall ScriptAreaAddRect(mfc::CArray<ScriptAreaPart>* area, void*, float x1, float y1, float x2, float y2) {
    ScriptAreaPart part = {};
    part.isRect = 1;
    part.x = x1;
    part.y = y1;
    part.x2OrRadiusSquared = x2;
    part.y2 = y2;
    AddPart(area, part);
}
EI_REPLACE(0x004B1ED0, ScriptAreaAddRect);

void __fastcall ScriptAreaAddRound(mfc::CArray<ScriptAreaPart>* area, void*, float x, float y, float radius) {
    ScriptAreaPart part = {};
    part.isRect = 0;
    part.x = x;
    part.y = y;
    part.x2OrRadiusSquared = radius * radius;
    AddPart(area, part);
}
EI_REPLACE(0x004B2070, ScriptAreaAddRound);

void CScript::DeleteArea(int id) {
    // CArray of areas at +0x194 (data +0x198, size +0x19C): {parts (CArray), id}, 0x18 each.
    constexpr int kAreaSize = sizeof(ScriptArea);
    for (int i = 0; i < areas_.size; ++i) {
        ScriptArea* areas = areas_.data;
        if (areas[i].id != id) continue;
        int after = areas_.size - i - 1;
        gamearrays::DestroyArea(&areas[i]);   // 004BCF10: the parts freed
        if (after) {
            areas = areas_.data;
            libcrt::Memmove(&areas[i], &areas[i + 1], after * kAreaSize);
        }
        --areas_.size;
        --i;   // the next area moved into this slot
    }
}
EI_REPLACE_METHOD(0x004BCF30, CScript::DeleteArea);

// ---- Point lights -------------------------------------------------------------------------------

void CScript::CreatePointLight(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = x87::fistp(f[0]);
    // The light with this id is replaced, else the first free slot is used (one with the id after
    // a free slot is not found).
    int i = 0;
    for (; i < LightCount(this); ++i) {
        ScriptPointLight* light = &Lights(this)[i];
        if (!light->object) break;
        if (light->id == id) {
            TheServerWorld()->RemoveTimedObject(Lights(this)[i].object->id_);
            Lights(this)[i].object->Release();
            break;
        }
    }
    Lights(this)[i].moving = 0;
    Lights(this)[i].id = id;
    Lights(this)[i].removeWhenDone = 0;
    ScriptPointLight* slot = &Lights(this)[i];
    slot->object = EffectManager()->CreateObject(0x4B, -1);
    CEffectPointLight* object = static_cast<CEffectPointLight*>(Lights(this)[i].object);
    ++object->refCount_;
    f = cmd->floatArgs.data;
    const float scale = 0.003921569f /*0073C36C*/;   // 1 / 255
    float red = f[5] * scale;
    float green = f[6] * scale;
    float blue = f[7] * scale;
    object->color_.x = red;
    object->color_.y = green;
    object->color_.z = blue;
    object->localPosition_.x = f[1];
    object->localPosition_.y = f[2];
    object->localPosition_.z = f[3];
    std::memcpy(&object->range_, &f[4], 4);   // radius
    TheWorld()->AddTimedObject(object);
    slot = &Lights(this)[i];
    slot->step.x = 0.0f;
    slot->step.y = 0.0f;
    slot->step.z = 0.0f;
    slot->steps = 0;
}
EI_REPLACE_METHOD(0x004BD6A0, CScript::CreatePointLight);

void CScript::MovePointLight(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = x87::fistp(f[0]);
    int i = 0;
    for (;; ++i) {
        if (i >= LightCount(this)) return;
        ScriptPointLight* light = &Lights(this)[i];
        if (light->object && light->id == id) break;
    }
    ScriptPointLight* light = &Lights(this)[i];
    light->moving = 1;
    light->removeWhenDone = x87::fistp(f[5]);
    light->steps = x87::fistp(f[4]);
    LightStep(light, &f[1], &static_cast<CEffectPointLight*>(light->object)->localPosition_.x);
}
EI_REPLACE_METHOD(0x004BD820, CScript::MovePointLight);

void CScript::DeletePointLight(const SSCCmd* cmd) {
    int id = x87::fistp(cmd->floatArgs.data[0]);
    for (int i = 0; i < LightCount(this); ++i) {
        ScriptPointLight* light = &Lights(this)[i];
        if (!light->object || light->id != id) continue;
        TheServerWorld()->RemoveTimedObject(light->object->id_);
        Lights(this)[i].object->Release();
        Lights(this)[i].object = nullptr;
        return;
    }
}
EI_REPLACE_METHOD(0x004BD920, CScript::DeletePointLight);

// ---- QFinish ------------------------------------------------------------------------------------

namespace {

// One objective added by the QObj* commands (004BAE10; CArray at 007AA608, data 007AA60C,
// count 007AA610): the command id (0xD7-0xDD), its string argument, its float argument.
struct QuestObjective {
    int type;
    mfc::CString text;
    float value;
};
static_assert(sizeof(QuestObjective) == 12);

QuestObjective* QuestObjectives() { return ei::GameData<QuestObjective*>(0x007AA60C); }
int QuestObjectiveCount() { return ei::GameData<int>(0x007AA610); }
mfc::CString& QuestName() { return ei::GameData<mfc::CString>(0x007AA61C); }   // QStart's name

// 004D23A0 CScript::Load (rewritten; through its address so its verify wrapper runs).
BOOL __fastcall LoadOrig(CScript* a0, void*, mfc::CString* a2, BOOL a3) { return (BOOL)ScriptLoadLine((void*)a0, nullptr, (const mfc::CString*)a2, (int)a3); }
// 00700B9F operator+(const CString&, const CString&), constructing the result in `out`.
mfc::CString* __stdcall ConcatString(mfc::CString* a0, const mfc::CString* a1, const mfc::CString* a2) { return CStringConcatOriginal(a0, a1, a2); }

// operator+'s result, destroyed with the holder.
class Sum {
public:
    Sum(const mfc::CString& a, const mfc::CString& b) { ConcatString(Get(), &a, &b); }
    ~Sum() { Get()->~CString(); }
    Sum(const Sum&) = delete;
    Sum& operator=(const Sum&) = delete;
    mfc::CString* Get() { return reinterpret_cast<mfc::CString*>(raw_); }

private:
    alignas(mfc::CString) unsigned char raw_[sizeof(mfc::CString)];
};

} // namespace

// The text, per objective i (0-based; the quest name is q, N objectives):
//   DeclareScript Su0() .. SuN()                      (one line each)
//   Script Sui(if(\n<condition>\n)then(\nKillScript()\nSu<i+1>()\nGSSetVar(0,"q.q.q.<i+1>",2)\n
//     then GSSetVar(0,"q.q.q.<i+2>",1)\n)\n)\n, or )\n)\n for the last objective
//   Script SuN(if()then(KillScript()\nQuestComplete(0,"q")\nGSSetVar(0,"q.q.q", 2)\n)\n)\n
//   WorldScript( Su0() )
// The conditions by objective (%f of the float, %s of the string): QObjArea, QObjSeeObject,
// QObjSeeUnit, QObjKillUnit, QObjKillGroup, QObjUse, QObjGetItem; another id leaves it empty.
void __fastcall ScriptQuestFinish(CScript* script) {
    mfc::CString text;
    for (int i = 0; i < QuestObjectiveCount() + 1; ++i) {
        mfc::CString line;
        line.Format("DeclareScript Su%i()\n", i);   // "DeclareScript Su%i()\n"
        text += line;
    }
    for (int i = 0; i < QuestObjectiveCount(); ++i) {
        mfc::CString head;
        mfc::CString condition;
        mfc::CString then;
        mfc::CString tail;
        const char* quest = QuestName();
        head.Format("Script Su%i(if(\n", i);                              // "Script Su%i(if(\n"
        then.Format("\n)then(\nKillScript()\nSu%i()\nGSSetVar(0,\"q.%s.%s.%i\",2)\n", i + 1, quest, quest, i + 1);     // "\n)then(\nKillScript()..."
        if (i == QuestObjectiveCount() - 1) {
            tail = ")\n)\n";                                   // ")\n)\n"
        } else {
            quest = QuestName();
            tail.Format("GSSetVar(0,\"q.%s.%s.%i\",1)\n)\n)\n", quest, quest, i + 2);        // "GSSetVar(0,\"q.%s.%s.%i\",1)..."
        }
        const QuestObjective& objective = QuestObjectives()[i];
        const char* argument = objective.text;
        switch (objective.type) {
        case 0xD7:   // QObjArea
            condition.Format("any( i, Heroes, IsInArea( %f, GetX(i), GetY(i) ) )", static_cast<double>(objective.value));
            break;
        case 0xD8:   // QObjSeeObject
            condition.Format("any( i, Heroes, IsLess( DistanceUnitUnit( i, %s ), 7 ) )", argument);
            break;
        case 0xD9:   // QObjSeeUnit
            condition.Format("IsUnitVisible( %s )", argument);
            break;
        case 0xDA:   // QObjKillUnit
            condition.Format("Not( IsAlive( %s ) )", argument);
            break;
        case 0xDB:   // QObjKillGroup
            condition.Format("Not( any( i, %s, IsAlive(i) ) )", argument);
            break;
        case 0xDC:   // QObjUse
            condition.Format("IsEqual( GetLeverState(%s), %f )", argument, static_cast<double>(objective.value));
            break;
        case 0xDD:   // QObjGetItem
            condition.Format("HaveItem( 0, %f )", static_cast<double>(objective.value));
            break;
        default:
            break;
        }
        Sum first(head, condition);
        Sum second(*first.Get(), then);
        Sum third(*second.Get(), tail);
        text += *third.Get();
    }
    {
        mfc::CString last;
        const char* quest = QuestName();
        last.Format("Script Su%i(if()then(KillScript()\nQuestComplete(0,\"%s\")\nGSSetVar(0,\"q.%s.%s\", 2)\n)\n)\n", QuestObjectiveCount(), quest, quest, quest);
        text += last;
    }
    text += "WorldScript( Su0() )\n";   // "WorldScript( Su0() )\n"
    LoadOrig(script, nullptr, &text, 1);
    QuestName() = game::EmptyText();   // the empty string constant
}
EI_REPLACE(0x004BAAB0, ScriptQuestFinish);
