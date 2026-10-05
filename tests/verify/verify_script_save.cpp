// Differential checks for the script savers (src/game/script_save.cpp). Saving only reads the
// script (CScript::Save also sets and clears 007AA620), so both versions run: each writes into a
// capture stream, the bytes and the pieces (the size of every Write) must match, and the
// original's pieces are then written to the real stream. Nested savers run through their own
// wrappers in both runs.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/script.h"

#include <cstdio>
#include <vector>

namespace {

class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(ScriptStream* stream) const {
        std::size_t pos = 0;
        for (unsigned size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    std::vector<unsigned char> bytes;
    std::vector<unsigned> chunks;
};

void Compare(const char* name, const CaptureStream& expected, const CaptureStream& actual) {
    bool same = expected.bytes == actual.bytes && expected.chunks == actual.chunks;
    char detail[160] = "";
    if (!same) {
        std::size_t i = 0;
        while (i < expected.bytes.size() && i < actual.bytes.size() && expected.bytes[i] == actual.bytes[i]) ++i;
        std::snprintf(detail, sizeof(detail), "%u vs %u bytes in %u vs %u writes, first difference at byte %u",
                      unsigned(expected.bytes.size()), unsigned(actual.bytes.size()), unsigned(expected.chunks.size()),
                      unsigned(actual.chunks.size()), unsigned(i));
    }
    ei::verify::Check(name, true, same, "%s", detail);
}

// Runs `original` and `ours` (both __thiscall with the stream) into capture streams.
template <typename T>
void SaveVerify(const char* name, void(__fastcall* original)(const T*, void*, ScriptStream*),
                void(__fastcall* ours)(const T*, void*, ScriptStream*), const T* self, ScriptStream* stream) {
    CaptureStream expected, actual;
    original(self, nullptr, &expected);
    ours(self, nullptr, &actual);
    Compare(name, expected, actual);
    expected.Forward(stream);
}

// 004C0F90: PUSH ECX; PUSH EBX; PUSH ESI; MOV ESI,[ESP+0x10] (7 bytes).
void(__fastcall* s_save)(CScript*, void*, ScriptStream*);
void __fastcall SaveVerifyScript(CScript* self, void*, ScriptStream* stream) {
    CaptureStream expected, actual;
    s_save(self, nullptr, &expected);
    ScriptSave(self, nullptr, &actual);
    Compare("CScript::Save", expected, actual);
    expected.Forward(stream);
}

// 004C1230: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESP+0xC] (8 bytes).
void(__fastcall* s_declSave)(const ScriptDecl*, void*, ScriptStream*);
void __fastcall DeclSaveVerify(const ScriptDecl* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptDeclSave", s_declSave, &ScriptDeclSave, self, stream);
}

// 004C2EF0 / 004C33C0: SUB ESP,8; PUSH ESI; MOV ESI,[ESP+0x10] (8 bytes).
void(__fastcall* s_floatsSave)(const ScriptNamedValues<float>*, void*, ScriptStream*);
void __fastcall FloatsSaveVerify(const ScriptNamedValues<float>* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptFloatsSave", s_floatsSave, &ScriptFloatsSave, self, stream);
}
void(__fastcall* s_stringsSave)(const ScriptNamedValues<mfc::CString>*, void*, ScriptStream*);
void __fastcall StringsSaveVerify(const ScriptNamedValues<mfc::CString>* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptStringsSave", s_stringsSave, &ScriptStringsSave, self, stream);
}
// 004C38C0: SUB ESP,8; PUSH ESI; MOV ESI,ECX (6 bytes).
void(__fastcall* s_objectsSave)(const ScriptNamedValues<ScriptObjectRef>*, void*, ScriptStream*);
void __fastcall ObjectsSaveVerify(const ScriptNamedValues<ScriptObjectRef>* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptObjectsSave", s_objectsSave, &ScriptObjectsSave, self, stream);
}
// 004C4330: SUB ESP,0xC; PUSH EBP; MOV EBP,ECX (6 bytes).
void(__fastcall* s_groupsSave)(const ScriptNamedValues<ScriptGroup>*, void*, ScriptStream*);
void __fastcall GroupsSaveVerify(const ScriptNamedValues<ScriptGroup>* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptGroupsSave", s_groupsSave, &ScriptGroupsSave, self, stream);
}

// 004CAEC0 (__fastcall): SUB ESP,8; PUSH EBX; MOV EBX,ECX (6 bytes).
void(__fastcall* s_areasSave)(const MfcArray<ScriptArea>*, ScriptStream*);
void __fastcall AreasSaveVerify(const MfcArray<ScriptArea>* self, ScriptStream* stream) {
    CaptureStream expected, actual;
    s_areasSave(self, &expected);
    ScriptAreasSave(self, &actual);
    Compare("ScriptAreasSave", expected, actual);
    expected.Forward(stream);
}

// 004BBFE0: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes). Reached by every save of a script
// with definitions or instances (the new game's autosave, script_copy's save).
void(__fastcall* s_definitionSave)(const ScriptDefinition*, void*, ScriptStream*);
void __fastcall DefinitionSaveVerify(const ScriptDefinition* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptDefinitionSave", s_definitionSave, &ScriptDefinitionSave, self, stream);
}

// 004BB320: SUB ESP,8; PUSH EBX; PUSH ESI (5 bytes). Reached by saves with waiting runs.
void(__fastcall* s_runSave)(const ScriptRun*, void*, ScriptStream*);
void __fastcall RunSaveVerify(const ScriptRun* self, void*, ScriptStream* stream) {
    SaveVerify("ScriptRunSave", s_runSave, &ScriptRunSave, self, stream);
}

} // namespace

EI_VERIFY(0x004BBFE0, DefinitionSaveVerify, 6, s_definitionSave);
EI_VERIFY(0x004BB320, RunSaveVerify, 5, s_runSave);
EI_VERIFY(0x004C0F90, SaveVerifyScript, 7, s_save);
EI_VERIFY(0x004C1230, DeclSaveVerify, 8, s_declSave);
EI_VERIFY(0x004C2EF0, FloatsSaveVerify, 8, s_floatsSave);
EI_VERIFY(0x004C33C0, StringsSaveVerify, 8, s_stringsSave);
EI_VERIFY(0x004C38C0, ObjectsSaveVerify, 6, s_objectsSave);
EI_VERIFY(0x004C4330, GroupsSaveVerify, 6, s_groupsSave);
EI_VERIFY(0x004CAEC0, AreasSaveVerify, 6, s_areasSave);
