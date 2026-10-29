// The script runtime around command execution: starting script instances, running an
// instance's if-then pairs, running a command block, freeing evaluation
// temporaries and the per-tick driver. See script.h and docs/notes/script.md.
#include "script.h"

#include "base_manager.h"
#include "random.h"
#include "world.h"
#include "world_effect.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// FatalError formats of ScriptBindCall ("Wrong number of script %s float / string / object / group
// parameters").
const char* const kWrongFloatCount = "Wrong number of script %s float parameters";
const char* const kWrongStringCount = "Wrong number of script %s string parameters";
const char* const kWrongObjectCount = "Wrong number of script %s object parameters";
const char* const kWrongGroupCount = "Wrong number of script %s group parameters";

// Compared with 0.0 (a NaN counts as zero).
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

// An effect object took a new position.
void EffectMoved(EngineObject* object) { static_cast<CEffect*>(object)->MarkChanged(); }

// 1 / (2^32 - 1).
constexpr std::uint64_t kRandomScaleBits = 0x3DF0000000100000ull;

// FX source position: (r - (-r)) * random * (1 / (2^32 - 1)) + (-r), with the random value loaded
// as a 64-bit integer; for x the source's x is added before rounding, for y the rounded offset is
// added to the source's y afterwards (the goldens depend on the order).
float FxOffset(float radius, std::uint32_t random) {
    const float negative = -radius;
    return static_cast<float>(x87::FromUnsigned(random) * (static_cast<double>(radius) - negative) * std::bit_cast<double>(kRandomScaleBits) + negative);
}
float FxOffsetPlus(float radius, std::uint32_t random, float origin) {
    const float negative = -radius;
    return static_cast<float>(x87::FromUnsigned(random) * (static_cast<double>(radius) - negative) * std::bit_cast<double>(kRandomScaleBits) + negative + origin);
}

void RespawnFxSources(Script* script) {
    for (ScriptFxSource& fx : script->fxSources) {
        if (fx.idle) continue;
        if (--fx.countdown > 0) continue;
        int minDelay = fx.minDelay;
        fx.countdown = static_cast<int>(g_random.Next() % static_cast<std::uint32_t>(fx.maxDelay - minDelay + 1)) + minDelay;
        fx.object.reset();
        auto* sound = g_serverWorld.timedObjectManager->CreateObjectAs<EffectSound>(ClassId::EffectSound, -1);
        float yOffset = FxOffset(fx.spread, g_random.Next());
        float x = FxOffsetPlus(fx.spread, g_random.Next(), fx.position.x);
        float y = yOffset + fx.position.y;
        float z = fx.position.z;
        sound->localPosition.x = x;
        sound->localPosition.y = y;
        sound->localPosition.z = z;
        x87::CopyBits(sound->volume, fx.volume);
        x87::CopyBits(sound->radius, fx.radius);
        fx.object = ScriptEffect<EffectSound>(sound);
        const char* name = fx.name.c_str();
        std::memcpy(sound->name, name, std::strlen(name) + 1);
        g_serverWorld.AddTimedObject(sound);
    }
}

void UpdatePointLights(Script* script) {
    for (ScriptPointLight& light : script->pointLights) {
        if (!light.object || !light.moving) continue;
        int count = light.steps;
        light.steps = count - 1;
        if (count <= 0) {
            light.moving = 0;
            if (light.removeWhenDone) light.object.reset();
            continue;
        }
        Vector3& position = light.object->localPosition;
        position.x = position.x + light.step.x;
        position.y = position.y + light.step.y;
        position.z = position.z + light.step.z;
        EffectMoved(light.object.get());
    }
}

void UpdateParticleSources(Script* script) {
    auto& list = script->particles;
    for (auto node = list.begin(); node != list.end();) {
        ScriptParticleSource& source = *node;
        int lifetime = source.lifetime;
        bool remove = lifetime == 0;
        if (lifetime > 0) source.lifetime = lifetime - 1;
        if (source.moving) {
            int count = source.steps;
            source.steps = count - 1;
            if (count > 0) {
                Vector3& position = source.object->localPosition;
                position.x = source.step.x + position.x;
                position.y = source.step.y + position.y;
                position.z = source.step.z + position.z;
                EffectMoved(source.object.get());
            } else {
                source.moving = 0;
                if (source.deleteAfterMove) remove = true;
            }
        }
        // Removing the source drops its effect.
        if (remove) node = list.erase(node);
        else ++node;
    }
}

} // namespace

// ---- Script instances ---------------------------------------------------------------------------

void ScriptDefinition::BindCall(const SSCCmd* call) {
    if (call->floatArgs.size() - 1 != vars.floats.values.size()) {
        game::FatalError(kWrongFloatCount, name);
        return;
    }
    if (call->stringArgs.size() - 1 != vars.strings.values.size()) {
        game::FatalError(kWrongStringCount, name);
        return;
    }
    if (call->objectArgs.size() != vars.objects.values.size()) {
        game::FatalError(kWrongObjectCount, name);
        return;
    }
    if (call->groupArgs.size() != vars.groups.values.size()) {
        game::FatalError(kWrongGroupCount, name);
        return;
    }
    // Float 0 and string 0 name the script; the rest are the parameters.
    for (std::size_t i = 1; i < call->floatArgs.size(); ++i) x87::CopyBits(vars.floats.values[i - 1], call->floatArgs[i]);
    for (std::size_t i = 1; i < call->stringArgs.size(); ++i) vars.strings.values[i - 1] = call->stringArgs[i];
    for (std::size_t i = 0; i < call->objectArgs.size(); ++i) vars.objects.values[i] = *call->objectArgs[i];
    for (std::size_t i = 0; i < call->groupArgs.size(); ++i) vars.groups.values[i] = *call->groupArgs[i];
}

void ScriptIfThen::Run(Script* script, ScriptDefinition* instance) const {
    for (SSCCmd& condition : conditions->commands) {
        float result;
        condition.CalcFloat(script, &instance->vars, &result);
        ScriptFreeTemporaries(script);
        if (IsZero(result)) return;
    }
    script->RunCommands(commands, instance);
}

void Script::RunCommands(ScriptDecl* block, ScriptDefinition* instance) {
    ScriptRun run;
    run.block = block;
    if (instance) {
        // The values only: the run's variables have no names.
        ScriptVars& from = instance->vars;
        run.vars.floats.values = from.floats.values;
        run.vars.strings.values = from.strings.values;
        run.vars.objects.values = from.objects.values;
        run.vars.groups.values = from.groups.values;
        run.killFlag = &instance->index;
    }
    if (run.Execute(this)) {
        // Not finished: it goes to the head of the run list, its kill flag cleared.
        run.killFlag = nullptr;
        runs.push_front(std::move(run));
    }
}

// A new run at the head of the run list, not executed yet.
void Script::RunWorldScript(ScriptDecl* block) {
    ScriptRun& run = runs.emplace_front();
    run.block = block;
}

void ScriptFreeTemporaries(Script* script) {
    if (script->forDepth > 0) return;
    // All but the first of each kind dropped, the first emptied.
    auto& groups = script->tempGroups;
    groups.resize(1);
    groups.front().Clear();
    auto& objects = script->tempObjects;
    objects.resize(1);
    objects.front().object = nullptr;
}

void ScriptTick(Script* script) {
    if (!script->loaded) return;

    // Waiting runs: continue each; a run that had already ended is removed.
    auto& runs = script->runs;
    for (auto run = runs.begin(); run != runs.end();) {
        auto next = std::next(run);
        if (!run->Execute(script)) runs.erase(run);
        run = next;
    }

    // Script instances: every if-then pair until the instance is killed; killed instances are
    // removed.
    auto& instances = script->instances;
    for (auto instance = instances.begin(); instance != instances.end();) {
        auto next = std::next(instance);
        instance->vars.RemoveDeletedObjects();
        for (const ScriptIfThen& pair : instance->ifThen) {
            pair.Run(script, &*instance);
            if (instance->index != 0) break;
        }
        if (instance->index != 0) instances.erase(instance);
        instance = next;
    }

    // Global variables: drop deleted objects.
    script->globals.RemoveDeletedObjects();

    RespawnFxSources(script);
    UpdatePointLights(script);
    UpdateParticleSources(script);
    ScriptFreeTemporaries(script);
}
