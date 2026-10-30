// Command helpers of the script runtime: areas (AddRectToArea, AddRoundToArea, DeleteArea) and point
// lights (CreatePointLight, MovePointLight, DeletePointLight). See script.h and
// docs/notes/script.md.
#include "script.h"

#include "base_manager.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "script_parse.h"
#include "world.h"
#include "world_effect.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <string>

namespace {

int LightCount(Script* script) { return static_cast<int>(script->pointLights.size()); }

// MovePointLight's step: (target - position) * (1 / steps) per axis. steps is a float rounded to an int, so a
// float holds it.
void LightStep(ScriptPointLight* light, const float* target, const float* position) {
    const float inverse = 1.0f / static_cast<float>(light->steps);
    light->step.x = (target[0] - position[0]) * inverse;
    light->step.y = (target[1] - position[1]) * inverse;
    light->step.z = (target[2] - position[2]) * inverse;
}

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
void Script::DeleteArea(int id) {
    std::erase_if(areas, [id](const ScriptArea& area) { return area.id == id; });
}

// ---- Point lights -------------------------------------------------------------------------------

void Script::CreatePointLight(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data();
    int id = lib::RoundEven(f[0]);
    // The light with this id is replaced, else the first free slot is used (one with the id after
    // a free slot is not found).
    int i = 0;
    for (; i < LightCount(this); ++i) {
        ScriptPointLight* light = &pointLights[i];
        if (!light->object) break;
        if (light->id == id) break;
    }
    ScriptPointLight* slot = &pointLights[i];
    slot->object.reset();
    slot->moving = 0;
    slot->id = id;
    slot->removeWhenDone = 0;
    slot->object = ScriptEffect<EffectPointLight>(g_serverWorld.timedObjectManager->CreateObjectAs<EffectPointLight>(0x4B, -1));
    EffectPointLight* object = slot->object.get();
    const float scale = 0.003921569f;   // 1 / 255
    float red = f[5] * scale;
    float green = f[6] * scale;
    float blue = f[7] * scale;
    object->color.x = red;
    object->color.y = green;
    object->color.z = blue;
    object->localPosition.x = f[1];
    object->localPosition.y = f[2];
    object->localPosition.z = f[3];
    object->range = f[4];    // radius
    g_serverWorld.AddTimedObject(object);
    slot->step.x = 0.0f;
    slot->step.y = 0.0f;
    slot->step.z = 0.0f;
    slot->steps = 0;
}

void Script::MovePointLight(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data();
    int id = lib::RoundEven(f[0]);
    int i = 0;
    for (;; ++i) {
        if (i >= LightCount(this)) return;
        ScriptPointLight* light = &pointLights[i];
        if (light->object && light->id == id) break;
    }
    ScriptPointLight* light = &pointLights[i];
    light->moving = 1;
    light->removeWhenDone = lib::RoundEven(f[5]);
    light->steps = lib::RoundEven(f[4]);
    LightStep(light, &f[1], &light->object->localPosition.x);
}

void Script::DeletePointLight(const SSCCmd* cmd) {
    int id = lib::RoundEven(cmd->floatArgs[0]);
    for (ScriptPointLight& light : pointLights) {
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
void ScriptQuestFinish(Script* script) {
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
        const ScriptQuestObjective& objective = g_scriptQuestObjectives[i];
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

// --- Script text file --------------------------------------------------------------------------------

// The script loader's text file reader: the whole file as one text, its "\r\n" line ends made "\n" (a text-mode
// read's). A file that does not open or read gives a result of 0 and an empty text.

namespace scriptparse {

int ReadTextFile(const std::string& path, std::string* text) {
    text->clear();
    std::string bytes;
    if (!platform::ReadFile(path, &bytes)) return 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (bytes[i] != '\r' || i + 1 == bytes.size() || bytes[i + 1] != '\n') *text += bytes[i];
    return 1;
}

} // namespace scriptparse
