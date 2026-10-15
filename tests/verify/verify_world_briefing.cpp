// Checks for the briefing variables (src/game/world_briefing.cpp). The speaker and the variable
// only read the briefing texts (each lookup opens its section again), so ours runs, then the
// original into a scratch string (SV2h1: ours allocates first) and the results are compared; MarkBriefing sets a variable, so only ours
// runs and the variable is read back.
#include "verify.h"

#include "../../src/game/world.h"
#include "../../src/game/world_map_files.h"

#include <cstring>

namespace {
void* s_unused;

using StringFn = mfc::CString*(__stdcall*)(mfc::CString*, const char*);
StringFn s_originalSpeaker;
StringFn s_originalVariable;

__declspec(naked) double __fastcall VariableGetAt(void*, void*, const char*) {
    __asm mov eax, 0x00460AF0
    __asm jmp eax
}

mfc::CString* Compare(const char* what, StringFn original, mfc::CString* (__stdcall* ours)(mfc::CString*, const char*),
                      mfc::CString* out, const char* briefing) {
    mfc::CString* result = ours(out, briefing);
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* expected = original(reinterpret_cast<mfc::CString*>(storage), briefing);
    bool ok = result == out && std::strcmp(*result, *expected) == 0;
    ei::verify::Check(what, true, ok, "%s: %s (original %s)", briefing ? briefing : "", static_cast<const char*>(*result),
                      static_cast<const char*>(*expected));
    expected->~CString();
    return result;
}
} // namespace

// 0057FC90.
static mfc::CString* __stdcall BriefingSpeakerVerify(mfc::CString* out, const char* briefing) {
    return Compare("BriefingSpeakerInto", s_originalSpeaker, BriefingSpeakerInto, out, briefing);
}
EI_VERIFY(0x0057FC90, BriefingSpeakerVerify, 7, s_originalSpeaker);

// 0057FDE0.
static mfc::CString* __stdcall BriefingVariableVerify(mfc::CString* out, const char* briefing) {
    return Compare("BriefingVariableInto", s_originalVariable, BriefingVariableInto, out, briefing);
}
EI_VERIFY(0x0057FDE0, BriefingVariableVerify, 7, s_originalVariable);

// 0057FF40: afterwards the briefing's variable is 1 for the local player.
static void __fastcall MarkBriefingVerify(GameFlow* self, void*, const char* briefing) {
    self->MarkBriefing(briefing);
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* variable = BriefingVariableInto(reinterpret_cast<mfc::CString*>(storage), briefing);
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    double value = VariableGetAt(player->variables, nullptr, *variable);
    ei::verify::Check("GameFlow::MarkBriefing", true, value == 1.0, "%s = %g", static_cast<const char*>(*variable), value);
    variable->~CString();
}
EI_VERIFY(0x0057FF40, MarkBriefingVerify, 0, s_unused);

// 00580040: afterwards the briefing's variable reads 0 for the local player.
static void __fastcall ForgetBriefingVerify(GameFlow* self, void*, const char* briefing) {
    self->ForgetBriefing(briefing);
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* variable = BriefingVariableInto(reinterpret_cast<mfc::CString*>(storage), briefing);
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    double value = VariableGetAt(player->variables, nullptr, *variable);
    ei::verify::Check("GameFlow::ForgetBriefing", true, value == 0.0, "%s = %g", static_cast<const char*>(*variable), value);
    variable->~CString();
}
EI_VERIFY(0x00580040, ForgetBriefingVerify, 0, s_unused);

// 00580F10: afterwards no quest text file is listed.
static void UnloadQuestTextsVerify() {
    UnloadQuestTexts();
    auto* files = reinterpret_cast<std::uint8_t* const*>(0x007AFDD0);
    ei::verify::Check("UnloadQuestTexts", true, files[0] == files[1], "%d left", static_cast<int>((files[1] - files[0]) / 12));
}
EI_VERIFY(0x00580F10, UnloadQuestTextsVerify, 0, s_unused);

// 005810A0: afterwards the folder is listed at most once.
static void __fastcall RegisterQuestsVerify(GameFlow* self, void*, const char* folder) {
    self->RegisterQuests(folder);
    struct Str {
        const char* start;
        const char* finish;
        const char* end;
    };
    auto* list = reinterpret_cast<Str* const*>(0x007AFDC0);
    int count = 0;
    std::size_t length = folder ? std::strlen(folder) : 0;
    for (Str* s = list[0]; s != list[1]; ++s)
        if (static_cast<std::size_t>(s->finish - s->start) == length && std::memcmp(s->start, folder, length) == 0) ++count;
    ei::verify::Check("GameFlow::RegisterQuests", true, count <= 1, "%s listed %d", folder ? folder : "", count);
}
EI_VERIFY(0x005810A0, RegisterQuestsVerify, 0, s_unused);

namespace {
StringFn s_originalSessionMapName;
mfc::CString* __stdcall SessionMapOriginalAdapter(mfc::CString* out, const char*) {
    return reinterpret_cast<mfc::CString*(__stdcall*)(mfc::CString*)>(s_originalSessionMapName)(out);
}
} // namespace

// 005814C0: the same name as the original's.
static mfc::CString* __stdcall SessionMapNameVerify(mfc::CString* out) {
    mfc::CString* result = SessionMapNameInto(out);
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* expected = SessionMapOriginalAdapter(reinterpret_cast<mfc::CString*>(storage), nullptr);
    bool ok = result == out && std::strcmp(*result, *expected) == 0;
    ei::verify::Check("SessionMapNameInto", true, ok, "%s (original %s)", static_cast<const char*>(*result),
                      static_cast<const char*>(*expected));
    expected->~CString();
    return result;
}
EI_VERIFY(0x005814C0, SessionMapNameVerify, 7, s_originalSessionMapName);

// 00580840: through original code (quest log, client world); logged.
static void __fastcall OnVariableChangedVerify(GameFlow* self, void*, const void* event) {
    const auto* record = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(event) + 8);
    const char* name = *reinterpret_cast<const char* const*>(record + 0x0C);
    self->OnVariableChanged(event);
    ei::verify::Check("GameFlow::OnVariableChanged", true, true, "%s", name ? name : "");
}
EI_VERIFY(0x00580840, OnVariableChangedVerify, 0, s_unused);

// 005809D0 LoadZoneQuests (A-W15; patched by Rebirth, so the vanilla launch only): it lists a file
// with the server and loads a text source, so only ours runs. Checked: false only without the
// texts or the file (the original's own CFileFind test, repeated through MapFileExists); true
// leaves exactly one quest file ending in "\<zone>.mq" (the original compares the paths as
// they are, so a zone named in another case is another entry), and the list
// grows by at most one. Reached by the quest registration (005810A0) at every new game and zone
// start, and by MarkBriefing: newgame and every scenario after it.
static bool __stdcall LoadZoneQuestsVerify(const char* zone) {
    struct Name {
        const char* start;
        const char* finish;
        const char* end;
    };
    auto* files = reinterpret_cast<Name* const*>(0x007AFDD0);
    const int before = static_cast<int>(files[1] - files[0]);
    const bool texts = *reinterpret_cast<void* const*>(0x007AF788) != nullptr;
    mfc::CString name = mfc::CString(zone) + ".mq";
    const bool exists = MapFileExists(name);
    const bool result = LoadZoneQuests(zone);
    const int after = static_cast<int>(files[1] - files[0]);
    int listed = 0;
    const std::size_t length = std::strlen(name);
    for (const Name* file = files[0]; file != files[1]; ++file) {
        const std::size_t size = static_cast<std::size_t>(file->finish - file->start);
        if (size > length && file->start[size - length - 1] == '\\' &&
            std::memcmp(file->start + size - length, static_cast<const char*>(name), length) == 0)
            ++listed;
    }
    bool ok = result == (texts && exists);
    if (result) ok = ok && listed == 1 && (after == before || after == before + 1);
    else ok = ok && after == before;
    ei::verify::Check("LoadZoneQuests", true, ok, "%s: result %d (texts %d, file %d), %d -> %d files, %d listed", zone,
                      result, texts, exists, before, after, listed);
    return result;
}
EI_VERIFY(0x005809D0, LoadZoneQuestsVerify, 0, s_unused);
