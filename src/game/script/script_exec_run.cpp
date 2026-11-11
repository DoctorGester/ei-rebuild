// The script runtime around the scheduler: the per-tick driver, the timed effects, freeing evaluation temporaries,
// the purges and the zone reset. See script.h and docs/notes/script.md.
#include "game/script/script.h"
#include "game/script/script_lua_scheduler.h"

#include "game/app/singletons.h"
#include "engine/core/base_manager.h"
#include "engine/core/random.h"
#include "game/effects/world_effect.h"
#include "game/world/world.h"
#include "lib/str.h"

#include <cstdint>

namespace {

// An FX source's offset on one axis: a random word to [-radius, radius).
float FxOffset(float radius, std::uint32_t random) { return RangeOfWord(random, -radius, radius); }

void RespawnFxSources(Script* script) {
    for (ScriptFxSource& fx : script->fxSources) {
        if (fx.idle) continue;
        if (--fx.countdown > 0) continue;
        int minDelay = fx.minDelay;
        fx.countdown = static_cast<int>(g_random.Next() % static_cast<std::uint32_t>(fx.maxDelay - minDelay + 1)) + minDelay;
        fx.object.reset();
        float yOffset = FxOffset(fx.spread, g_random.Next());
        float x = FxOffset(fx.spread, g_random.Next()) + fx.position.x;
        float y = yOffset + fx.position.y;
        fx.object = ScriptEffect<EffectSound>(ScriptNewSound({x, y, fx.position.z}, fx.volume, fx.radius, fx.name, true));
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
        position += light.step;
        light.object->MarkChanged();
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
                position += source.step;
                source.object->MarkChanged();
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

// --- The tick ----------------------------------------------------------------------------------

void Script::FreeTemporaries() {
    // All but the first of each kind dropped, the first emptied.
    auto& groups = tempGroups;
    groups.resize(1);
    groups.front().Clear();
    auto& objects = tempObjects;
    objects.resize(1);
    objects.front().object = nullptr;
}

void Script::Tick() {
    if (!loaded) return;
    // A reset inside the tick drops the scheduler after it (DropRunner).
    runnerTicking = true;
    Runner().Tick();
    runnerTicking = false;
    retired.reset();
}

void Script::TickEffects() {
    RespawnFxSources(this);
    UpdatePointLights(this);
    UpdateParticleSources(this);
    FreeTemporaries();
}

// --- Clearing parts of a Script ---------------------------------------------------------------
// Dropping references to deleted world objects (after every object removal), and the zone reset when
// the party enters a zone. See script.h and docs/notes/script.md.

// Called after every removal of a world object: the scheduler's values drop deleted objects (object values
// released, group members removed), then the evaluation temporaries are freed.
void Script::PurgeDeletedObjects() {
    if (runner) runner->lua.RemoveDeletedObjects();
    FreeTemporaries();
}

// When the party enters a zone: the scheduler's reset (ScriptLuaScheduler::ResetZone).
void Script::ResetZone() { Runner().ResetZone(); }

void Script::ResetEffects() {
    FreeTemporaries();
    for (ScriptPointLight& light : pointLights) light = {};
    fxSources.clear();
    particles.clear();
    lightning.clear();
}
