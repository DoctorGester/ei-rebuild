// Command helpers of the script runtime: areas (AddRectToArea, AddRoundToArea, DeleteArea) and point
// lights (CreatePointLight, MovePointLight, DeletePointLight). See script.h and
// docs/notes/script.md.
#include "script.h"
#include "game_strings.h"
#include "base_manager.h"
#include "world.h"
#include "world_effect.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <format>

namespace {

ScriptPointLight* Lights(CScript* script) { return script->pointLights_.data(); }
int LightCount(CScript* script) { return static_cast<int>(script->pointLights_.size()); }

// MovePointLight's step: (target - position) / steps per axis. The x difference stays in the FPU,
// the y and z differences are rounded to float first (the goldens depend on it).
void LightStep(ScriptPointLight* light, const float* target, const float* position) {
    const double dx = static_cast<double>(target[0]) - position[0];
    const float dy = static_cast<float>(static_cast<double>(target[1]) - position[1]);
    const float dz = static_cast<float>(static_cast<double>(target[2]) - position[2]);
    // 1 / steps, rounded to float.
    const float inverse = static_cast<float>(1.0 / light->steps);
    light->step.x = static_cast<float>(dx * inverse);
    light->step.y = static_cast<float>(static_cast<double>(dy) * inverse);
    light->step.z = static_cast<float>(static_cast<double>(dz) * inverse);
}

ScriptQuestObjective* QuestObjectives() { return g_scriptQuestObjectives.data(); }
int QuestObjectiveCount() { return static_cast<int>(g_scriptQuestObjectives.size()); }


} // namespace


void ScriptArea::AddRect(float x1, float y1, float x2, float y2) {
    ScriptAreaPart part = {};
    part.isRect = 1;
    part.x = x1;
    part.y = y1;
    part.x2OrRadiusSquared = x2;
    part.y2 = y2;
    parts.push_back(part);
}

void ScriptArea::AddRound(float x, float y, float radius) {
    ScriptAreaPart part = {};
    part.isRect = 0;
    part.x = x;
    part.y = y;
    part.x2OrRadiusSquared = radius * radius;
    parts.push_back(part);
}

// Every area with the id.
void CScript::DeleteArea(int id) {
    std::erase_if(areas_, [id](const ScriptArea& area) { return area.id == id; });
}

// ---- Point lights -------------------------------------------------------------------------------

void CScript::CreatePointLight(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data();
    int id = x87::fistp(f[0]);
    // The light with this id is replaced, else the first free slot is used (one with the id after
    // a free slot is not found).
    int i = 0;
    for (; i < LightCount(this); ++i) {
        ScriptPointLight* light = &Lights(this)[i];
        if (!light->object) break;
        if (light->id == id) break;
    }
    ScriptPointLight* slot = &Lights(this)[i];
    slot->object.reset();
    slot->moving = 0;
    slot->id = id;
    slot->removeWhenDone = 0;
    slot->object = ScriptEffect<CEffectPointLight>(g_serverWorld.timedObjectManager_->CreateObjectAs<CEffectPointLight>(0x4B, -1));
    CEffectPointLight* object = slot->object.get();
    const float scale = 0.003921569f;   // 1 / 255
    float red = f[5] * scale;
    float green = f[6] * scale;
    float blue = f[7] * scale;
    object->color_.x = red;
    object->color_.y = green;
    object->color_.z = blue;
    object->localPosition_.x = f[1];
    object->localPosition_.y = f[2];
    object->localPosition_.z = f[3];
    x87::CopyBits(object->range_, f[4]);   // radius
    g_serverWorld.AddTimedObject(object);
    slot->step.x = 0.0f;
    slot->step.y = 0.0f;
    slot->step.z = 0.0f;
    slot->steps = 0;
}

void CScript::MovePointLight(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data();
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
    LightStep(light, &f[1], &light->object->localPosition_.x);
}

void CScript::DeletePointLight(const SSCCmd* cmd) {
    int id = x87::fistp(cmd->floatArgs[0]);
    for (ScriptPointLight& light : pointLights_) {
        if (!light.object || light.id != id) continue;
        light.object.reset();
        return;
    }
}

// The text, per objective i (0-based; the quest name is q, N objectives):
//   DeclareScript Su0() .. SuN()                      (one line each)
//   Script Sui(if(\n<condition>\n)then(\nKillScript()\nSu<i+1>()\nGSSetVar(0,"q.q.q.<i+1>",2)\n
//     then GSSetVar(0,"q.q.q.<i+2>",1)\n)\n)\n, or )\n)\n for the last objective
//   Script SuN(if()then(KillScript()\nQuestComplete(0,"q")\nGSSetVar(0,"q.q.q", 2)\n)\n)\n
//   WorldScript( Su0() )
// The conditions by objective (%f of the float, %s of the string): QObjArea, QObjSeeObject,
// QObjSeeUnit, QObjKillUnit, QObjKillGroup, QObjUse, QObjGetItem; another id leaves it empty.
void ScriptQuestFinish(CScript* script) {
    std::string text;
    for (int i = 0; i < QuestObjectiveCount() + 1; ++i) {
        std::string line;
        line = std::format("DeclareScript Su{}()\n", i);
        text += line;
    }
    for (int i = 0; i < QuestObjectiveCount(); ++i) {
        std::string head;
        std::string condition;
        std::string then;
        std::string tail;
        const std::string& quest = g_scriptQuestName;
        head = std::format("Script Su{}(if(\n", i);
        then = std::format("\n)then(\nKillScript()\nSu{}()\nGSSetVar(0,\"q.{}.{}.{}\",2)\n", i + 1, quest, quest, i + 1);
        if (i == QuestObjectiveCount() - 1) {
            tail = ")\n)\n";                                   // ")\n)\n"
        } else {
            tail = std::format("GSSetVar(0,\"q.{}.{}.{}\",1)\n)\n)\n", quest, quest, i + 2);
        }
        const ScriptQuestObjective& objective = QuestObjectives()[i];
        const std::string& argument = objective.text;
        switch (static_cast<ScriptFunction>(objective.type)) {
        case ScriptFunction::QObjArea:
            condition = str::Format("any( i, Heroes, IsInArea( %f, GetX(i), GetY(i) ) )", static_cast<double>(objective.value));
            break;
        case ScriptFunction::QObjSeeObject:
            condition = std::format("any( i, Heroes, IsLess( DistanceUnitUnit( i, {} ), 7 ) )", argument);
            break;
        case ScriptFunction::QObjSeeUnit:
            condition = std::format("IsUnitVisible( {} )", argument);
            break;
        case ScriptFunction::QObjKillUnit:
            condition = std::format("Not( IsAlive( {} ) )", argument);
            break;
        case ScriptFunction::QObjKillGroup:
            condition = std::format("Not( any( i, {}, IsAlive(i) ) )", argument);
            break;
        case ScriptFunction::QObjUse:
            condition = str::Format("IsEqual( GetLeverState(%s), %f )", argument, static_cast<double>(objective.value));
            break;
        case ScriptFunction::QObjGetItem:
            condition = str::Format("HaveItem( 0, %f )", static_cast<double>(objective.value));
            break;
        default:
            break;
        }
        text += head + condition + then + tail;
    }
    {
        std::string last;
        const std::string& quest = g_scriptQuestName;
        last = std::format("Script Su{}(if()then(KillScript()\nQuestComplete(0,\"{}\")\nGSSetVar(0,\"q.{}.{}\", 2)\n)\n)\n", QuestObjectiveCount(), quest, quest, quest);
        text += last;
    }
    text += "WorldScript( Su0() )\n";   // "WorldScript( Su0() )\n"
    script->Load(&text, 1);
    g_scriptQuestName = game::kEmptyText;   // the empty string constant
}

// ---- The quest's objectives and name (script.h) --------------------------------------------------------------------

std::vector<ScriptQuestObjective> g_scriptQuestObjectives;
std::string g_scriptQuestName;
