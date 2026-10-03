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

// The savers of the other parts (004B / 004BB range, not rewritten), __thiscall with the stream.
// 004B1940 point light (0x20 slot), 004B1A80 particle source, 004B1C50 FX source, 004B1DB0
// lightning (list node data), 004BBFE0 definition / instance (0xC0), 004BB320 run (0xB4).
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

// The script being saved; the run saver (004BB320) finds a run's instance through it.
CScript*& SavingScript() { return *reinterpret_cast<CScript**>(0x007AA620); }

template <typename T>
const T& Field(const CScript* script, std::size_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(script) + offset);
}

// MFC CList (0x1C): vtable, head, tail, count, free list, blocks, block size. Nodes: next, prev,
// then the element.
struct ListNode {
    ListNode* next;
    ListNode* prev;
};
struct List {
    void* vtable;
    ListNode* head;     // +0x04
    ListNode* tail;     // +0x08
    int count;          // +0x0C
};
const void* NodeData(const ListNode* node) { return node + 1; }

void WriteInt(ScriptStream* stream, int value) { stream->Write(&value, 4); }

int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// The string with its terminator.
void WriteString(ScriptStream* stream, const mfc::CString& s) { stream->Write(static_cast<const char*>(s), Length(s) + 1); }

// Variable i's name, or a single zero byte when the name array is shorter.
void WriteName(ScriptStream* stream, const MfcArray<mfc::CString>& names, int i) {
    if (i < names.size) {
        WriteString(stream, names.data[i]);
    } else {
        int zero = 0;
        stream->Write(&zero, 1);
    }
}

int ObjectId(const ScriptObjectRef& ref) { return ref.object ? ref.object->id_ : -1; }

// A list's count, then each element through `save`.
template <typename Save>
void WriteList(ScriptStream* stream, const CScript* script, std::size_t offset, Save save) {
    const List& list = Field<List>(script, offset);
    WriteInt(stream, list.count);
    for (const ListNode* node = list.head; node;) {
        const ListNode* next = node->next;
        save(NodeData(node), nullptr, stream);
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

void __fastcall ScriptAreasSave(const MfcArray<ScriptArea>* areas, ScriptStream* stream) {
    WriteInt(stream, areas->size);
    for (int i = 0; i < areas->size; ++i) {
        const ScriptArea& area = areas->data[i];
        WriteInt(stream, area.id);
        WriteInt(stream, area.parts.size);
        for (int k = 0; k < area.parts.size; ++k) stream->Write(&area.parts.data[k], sizeof(ScriptAreaPart));
    }
}
EI_REPLACE(0x004CAEC0, ScriptAreasSave);

void __fastcall ScriptSave(CScript* script, void*, ScriptStream* stream) {
    SavingScript() = script;

    // Point lights (+0x04: CArray of 0x20-byte slots).
    const auto& lights = Field<MfcArray<std::uint8_t[0x20]>>(script, 0x04);
    WriteInt(stream, lights.size);
    for (int i = 0; i < lights.size; ++i) PointLightSave(lights.data[i], nullptr, stream);

    WriteList(stream, script, 0x14, ParticleSourceSave);
    WriteList(stream, script, 0x30, FxSourceSave);
    WriteList(stream, script, 0x4C, LightningSave);
    stream->Write(&Field<int>(script, 0x68), 4);
    stream->Write(&Field<int>(script, 0x6C), 4);
    WriteList(stream, script, 0x70, [](const void* block, void*, ScriptStream* s) {
        DeclSaveOrig(static_cast<const ScriptDecl*>(block), nullptr, s);
    });
    WriteList(stream, script, 0x178, [](const void* block, void*, ScriptStream* s) {
        DeclSaveOrig(static_cast<const ScriptDecl*>(block), nullptr, s);
    });

    // Definitions (+0x8C).
    const auto& scripts = Field<MfcArray<ScriptDefinition>>(script, 0x8C);
    WriteInt(stream, scripts.size);
    for (int i = 0; i < scripts.size; ++i) DefinitionSave(&scripts.data[i], nullptr, stream);

    // Globals (+0xA0).
    const char* globals = reinterpret_cast<const char*>(script) + 0xA0;
    FloatsSaveOrig(globals + 0x00, nullptr, stream);
    StringsSaveOrig(globals + 0x28, nullptr, stream);
    ObjectsSaveOrig(globals + 0x50, nullptr, stream);
    GroupsSaveOrig(globals + 0x78, nullptr, stream);

    // Instances (+0x140, copies of definitions) and runs (+0x15C).
    WriteList(stream, script, 0x140, [](const void* instance, void*, ScriptStream* s) {
        DefinitionSave(static_cast<const ScriptDefinition*>(instance), nullptr, s);
    });
    WriteList(stream, script, 0x15C, [](const void* run, void*, ScriptStream* s) {
        RunSave(static_cast<const ScriptRun*>(run), nullptr, s);
    });

    stream->Write(&Field<int>(script, 0x1A8), 4);
    stream->Write(&Field<int>(script, 0x1AC), 4);
    SavingScript() = nullptr;
    AreasSaveOrig(&Field<MfcArray<ScriptArea>>(script, 0x194), stream);
}
EI_REPLACE(0x004C0F90, ScriptSave);
