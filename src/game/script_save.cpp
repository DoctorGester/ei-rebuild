// Saving the mission script into a savegame (CScript::Save 004C0F90) and the savers of its parts
// in this range: command blocks (004C1230), the four kinds of variables (004C2EF0, 004C33C0,
// 004C38C0, 004C4330) and the areas (004CAEC0). See script.h and docs/notes/script.md.
//
// Every piece goes through the stream's Write (vtable slot 2) in the original's order and in the
// original's pieces (a verify wrapper compares the pieces as well as the bytes).
#include "script.h"

#include "ei/hook.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// The savers of the other parts (004B / 004BB range), __thiscall with the stream.
// 004B1940 point light (0x20 slot), 004B1A80 particle source, 004B1C50 FX source, 004B1DB0
// lightning (list node data): not rewritten. 004BBFE0 definition / instance (0xC0), 004BB320 run
// (0xB4): rewritten below, called through their addresses (verify wrappers run).
__declspec(naked) void __fastcall PointLightSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004B1940)
__declspec(naked) void __fastcall ParticleSourceSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004B1A80)
__declspec(naked) void __fastcall FxSourceSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004B1C50)
__declspec(naked) void __fastcall LightningSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004B1DB0)
__declspec(naked) void __fastcall DefinitionSave(const ScriptDefinition*, void*, ScriptStream*) EI_ORIGINAL(0x004BBFE0)
__declspec(naked) void __fastcall RunSave(const ScriptRun*, void*, ScriptStream*) EI_ORIGINAL(0x004BB320)
// SSCCmd::Save (rewritten in script_vars.cpp; called through its address so its wrapper runs).
__declspec(naked) void __fastcall CmdSave(const SSCCmd*, void*, ScriptStream*) EI_ORIGINAL(0x004B6360)
// The savers of this file, called from CScript::Save through their addresses as well.
__declspec(naked) void __fastcall DeclSaveOrig(const ScriptDecl*, void*, ScriptStream*) EI_ORIGINAL(0x004C1230)
__declspec(naked) void __fastcall FloatsSaveOrig(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C2EF0)
__declspec(naked) void __fastcall StringsSaveOrig(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C33C0)
__declspec(naked) void __fastcall ObjectsSaveOrig(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C38C0)
__declspec(naked) void __fastcall GroupsSaveOrig(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C4330)
__declspec(naked) void __fastcall AreasSaveOrig(const void*, ScriptStream*) EI_ORIGINAL(0x004CAEC0)
// ScriptObjectRef::Save (rewritten in script_group.cpp; the definition and run savers call it).
__declspec(naked) void __fastcall RefSave(const ScriptObjectRef*, void*, ScriptStream*) EI_ORIGINAL(0x004B25F0)

// The script being saved; the run saver (004BB320) finds a run's instance through it.
CScript*& SavingScript() { return *reinterpret_cast<CScript**>(0x007AA620); }

void WriteInt(ScriptStream* stream, int value) { stream->Write(&value, 4); }

int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// The string with its terminator.
void WriteString(ScriptStream* stream, const mfc::CString& s) { stream->Write(static_cast<const char*>(s), Length(s) + 1); }

// Variable i's name, or a single zero byte when the name array is shorter.
void WriteName(ScriptStream* stream, const mfc::CArray<mfc::CString>& names, int i) {
    if (i < names.size) {
        WriteString(stream, names.data[i]);
    } else {
        int zero = 0;
        stream->Write(&zero, 1);
    }
}

int ObjectId(const ScriptObjectRef& ref) { return ref.object ? ref.object->id_ : -1; }

// A ScriptVars as the definition and run savers write it: floats, strings and objects inline (the
// pieces of their savers; objects through ScriptObjectRef::Save), groups through 004C4330.
void WriteVars(ScriptStream* stream, const ScriptVars* vars) {
    ScriptFloatsSave(reinterpret_cast<const ScriptNamedValues<float>*>(&vars->floats), nullptr, stream);
    ScriptStringsSave(reinterpret_cast<const ScriptNamedValues<mfc::CString>*>(&vars->strings), nullptr, stream);
    WriteInt(stream, vars->objects.size);
    for (int i = 0; i < vars->objects.size; ++i) {
        RefSave(&vars->objects.data[i], nullptr, stream);
        WriteName(stream, vars->objectNames, i);
    }
    GroupsSaveOrig(&vars->groups, nullptr, stream);
}

// A list's count, then each element through `save`.
template <typename List, typename Save>
void WriteList(ScriptStream* stream, const List& list, Save save) {
    WriteInt(stream, list.count);
    for (const auto* node = list.head; node;) {
        const auto* next = node->next;
        save(&node->data, nullptr, stream);
        node = next;
    }
}

} // namespace

void __fastcall ScriptDeclSave(const ScriptDecl* block, void*, ScriptStream* stream) {
    stream->Write(&block->id, 4);
    WriteInt(stream, block->commands.size);
    for (int i = 0; i < block->commands.size; ++i) CmdSave(&block->commands.data[i], nullptr, stream);
}
EI_REPLACE(0x004C1230, ScriptDeclSave);

void __fastcall ScriptFloatsSave(const ScriptNamedValues<float>* vars, void*, ScriptStream* stream) {
    WriteInt(stream, vars->values.size);
    for (int i = 0; i < vars->values.size; ++i) {
        stream->Write(&vars->values.data[i], 4);
        WriteName(stream, vars->names, i);
    }
}
EI_REPLACE(0x004C2EF0, ScriptFloatsSave);

void __fastcall ScriptStringsSave(const ScriptNamedValues<mfc::CString>* vars, void*, ScriptStream* stream) {
    WriteInt(stream, vars->values.size);
    for (int i = 0; i < vars->values.size; ++i) {
        WriteString(stream, vars->values.data[i]);
        WriteName(stream, vars->names, i);
    }
}
EI_REPLACE(0x004C33C0, ScriptStringsSave);

void __fastcall ScriptObjectsSave(const ScriptNamedValues<ScriptObjectRef>* vars, void*, ScriptStream* stream) {
    WriteInt(stream, vars->values.size);
    for (int i = 0; i < vars->values.size; ++i) {
        WriteInt(stream, ObjectId(vars->values.data[i]));
        WriteName(stream, vars->names, i);
    }
}
EI_REPLACE(0x004C38C0, ScriptObjectsSave);

void __fastcall ScriptGroupsSave(const ScriptNamedValues<ScriptGroup>* vars, void*, ScriptStream* stream) {
    WriteInt(stream, vars->values.size);
    for (int i = 0; i < vars->values.size; ++i) {
        const ScriptGroup& group = vars->values.data[i];
        int size = group.size;   // read once
        WriteInt(stream, size);
        for (int k = 0; k < size; ++k) WriteInt(stream, ObjectId(group.data[k]));
        WriteName(stream, vars->names, i);
    }
}
EI_REPLACE(0x004C4330, ScriptGroupsSave);

void __fastcall ScriptAreasSave(const mfc::CArray<ScriptArea>* areas, ScriptStream* stream) {
    WriteInt(stream, areas->size);
    for (int i = 0; i < areas->size; ++i) {
        const ScriptArea& area = areas->data[i];
        WriteInt(stream, area.id);
        WriteInt(stream, area.parts.size);
        for (int k = 0; k < area.parts.size; ++k) stream->Write(&area.parts.data[k], sizeof(ScriptAreaPart));
    }
}
EI_REPLACE(0x004CAEC0, ScriptAreasSave);

// A definition (CScript::definitions_) or an instance (instances_). The pairs' blocks are not null-checked.
void __fastcall ScriptDefinitionSave(const ScriptDefinition* definition, void*, ScriptStream* stream) {
    stream->Write(&definition->index, 4);
    WriteString(stream, definition->name);
    WriteString(stream, definition->params);
    WriteInt(stream, definition->ifThen.size);
    for (int i = 0; i < definition->ifThen.size; ++i) {
        const ScriptIfThen& pair = definition->ifThen.data[i];
        WriteInt(stream, pair.conditions->id);
        WriteInt(stream, pair.commands->id);
    }
    WriteVars(stream, &definition->vars);
}
EI_REPLACE(0x004BBFE0, ScriptDefinitionSave);

// A run (CScript::runs_). Its instance is found by the address of the killed flag (the node's
// data) in the list of the script being saved; the WorldScript's runs (no flag) write -1.
void __fastcall ScriptRunSave(const ScriptRun* run, void*, ScriptStream* stream) {
    WriteInt(stream, run->block->id);
    stream->Write(&run->index, 4);
    stream->Write(&run->ticks, 4);
    stream->Write(&run->sleepStart, 4);
    int instance = 0;
    const auto* node = SavingScript()->instances_.head;
    for (; node; node = node->next, ++instance) {
        if (static_cast<const void*>(run->killFlag) == &node->data) break;
    }
    WriteInt(stream, node ? instance : -1);
    WriteVars(stream, &run->vars);
}
EI_REPLACE(0x004BB320, ScriptRunSave);

void __fastcall ScriptSave(CScript* script, void*, ScriptStream* stream) {
    SavingScript() = script;

    const auto& lights = script->pointLights_;
    WriteInt(stream, lights.size);
    for (int i = 0; i < lights.size; ++i) PointLightSave(&lights.data[i], nullptr, stream);

    WriteList(stream, script->particles_, ParticleSourceSave);
    WriteList(stream, script->fxSources_, FxSourceSave);
    WriteList(stream, script->lightning_, LightningSave);
    stream->Write(&script->nextParticleId_, 4);
    stream->Write(&script->nextFxId_, 4);
    WriteList(stream, script->blocks_, DeclSaveOrig);
    WriteList(stream, script->worldBlocks_, DeclSaveOrig);

    const auto& scripts = script->definitions_;
    WriteInt(stream, scripts.size);
    for (int i = 0; i < scripts.size; ++i) DefinitionSave(&scripts.data[i], nullptr, stream);

    const ScriptVars& globals = script->globals_;
    FloatsSaveOrig(&globals.floats, nullptr, stream);
    StringsSaveOrig(&globals.strings, nullptr, stream);
    ObjectsSaveOrig(&globals.objects, nullptr, stream);
    GroupsSaveOrig(&globals.groups, nullptr, stream);

    // Instances (copies of definitions) and runs.
    WriteList(stream, script->instances_, DefinitionSave);
    WriteList(stream, script->runs_, RunSave);

    stream->Write(&script->loaded_, 4);
    stream->Write(&script->fileCount_, 4);
    SavingScript() = nullptr;
    AreasSaveOrig(&script->areas_, stream);
}
EI_REPLACE(0x004C0F90, ScriptSave);
