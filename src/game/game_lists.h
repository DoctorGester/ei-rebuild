#pragma once

// The CList<T> instantiations whose elements the game destroys through its own code: lib/mfc_list.h's templates
// over the game's element types.

#include "lib/mfc_list.h"
#include "game/core_refcount.h"
#include "game/game_arrays.h"
#include "game/sound.h"
#include "game/world.h"
#include "game_data.h"

#include <memory>
#include <type_traits>

void RefPtrRelease(RefPtr* self);
void ObjectHolderRelease(CEngineObject** self);

namespace gamelists {

using mfc::impl::Bytes;
using mfc::impl::EachElements;
using mfc::impl::ListAbi;

using ObjectRefElements = EachElements<ScriptObjectRef, nullptr, gamearrays::ReleaseAndClear>;
using ObjectRefs = ListAbi<ScriptObjectRef, ObjectRefElements>;

// The script's effects (each leaves the world's timed objects and is released; the sound's name
// destroyed after), its tokens (the text destroyed), its definitions and runs.
inline void DestroyParticleSource(ScriptParticleSource* p) { p->ScriptParticleSourceDestruct(); }
inline void DestroyFxSource(ScriptFxSource* p) { p->ScriptFxSourceDestruct(); }
inline void DestroyLightning(ScriptLightning* p) { p->ScriptLightningDestruct(); }
inline void DestroyToken(ScriptToken* p) { std::destroy_at(&p->text); }
inline void DestroyDefinition(ScriptDefinition* p) { ScriptDefinitionDestruct(p); }
inline void DestroyRun(ScriptRun* p) { ScriptRunDestruct(p); }
// A command block: the body of ScriptDeclDestruct.
inline void DestroyDecl(ScriptDecl* p) { ScriptDeclDestruct(p); }
// After the zero fill (ConstructElements): a definition's name and parameters empty, its if-then array
// (ScriptIfThenPairsClass) and variables constructed, index 0; a run's variables.
void InitDefinition(ScriptDefinition* p);
void InitRun(ScriptRun* p);
using DefinitionElements = EachElements<ScriptDefinition, InitDefinition, DestroyDefinition>;
using RunElements = EachElements<ScriptRun, InitRun, DestroyRun>;
using DeclElements = EachElements<ScriptDecl, nullptr, DestroyDecl>;
using GroupElements = EachElements<ScriptGroup, gamearrays::InitGroup, gamearrays::DestroyGroup>;

using ParticleSources = ListAbi<ScriptParticleSource, EachElements<ScriptParticleSource, nullptr, DestroyParticleSource>>;
using FxSources = ListAbi<ScriptFxSource, EachElements<ScriptFxSource, nullptr, DestroyFxSource>>;
using Lightnings = ListAbi<ScriptLightning, EachElements<ScriptLightning, nullptr, DestroyLightning>>;
using Tokens = ListAbi<ScriptToken, EachElements<ScriptToken, nullptr, DestroyToken>>;
using Definitions = ListAbi<ScriptDefinition, DefinitionElements>;
using Runs = ListAbi<ScriptRun, RunElements>;
using Decls = ListAbi<ScriptDecl, DeclElements>;
using Groups = ListAbi<ScriptGroup, GroupElements>;

} // namespace gamelists
