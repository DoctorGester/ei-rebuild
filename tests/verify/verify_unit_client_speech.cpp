// Checks for unit_client_speech.cpp (A-U2).
// - SpeechReplaced (00537F60): pure, original and ours compared.
// - SpeechFinished (00537FD0): the original runs first (it may clear the server unit's talking
//   flag, which ours then finds cleared), ours second; results compared.
// - Speak (005380D0): ours only (it plays the sound); played: the speech and its sound are
//   remembered; refused: nothing changed.
// - Say (00537D80): ours only; the speech is the queue's last unless the queue (after the
//   filters) was not empty and the kind does not queue behind others.
// - StopSpeech (00538040): ours only; the queue is empty.
// Reached by: units speaking on the client (slot 51: unit_ai / combat (battle cries, kinds 0x1D-0x2A),
// zone_* (talking heroes); slot 52 when a unit dies or leaves (combat_more)); the filters by Say.
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include <cstdint>

namespace {
void* s_unusedSay;
void* s_unusedStop;
void* s_unusedSpeak;
} // namespace

// 00537F60: MOV EAX,[ECX+4]; MOV ECX,[ESP+4] (7 bytes).
static int(__fastcall* s_speechReplaced)(const SpeechFilter*, void*, const ClientSpeech*);
static int __fastcall SpeechReplacedVerify(const SpeechFilter* filter, void*, const ClientSpeech* queued) {
    int expected = s_speechReplaced(filter, nullptr, queued);
    int actual = SpeechReplaced(filter, nullptr, queued);
    ei::verify::Check("SpeechReplaced", expected, actual, "kind %d over %d", filter->kind, queued->kind);
    return actual;
}
EI_VERIFY(0x00537F60, SpeechReplacedVerify, 7, s_speechReplaced);

// 00537FD0: PUSH EBX; PUSH EBP; MOV EBP,[ESP+0xC] (6 bytes).
static bool(__fastcall* s_speechFinished)(const SpeechFilter*, void*, const ClientSpeech*);
static bool __fastcall SpeechFinishedVerify(const SpeechFilter* filter, void*, const ClientSpeech* queued) {
    bool expected = s_speechFinished(filter, nullptr, queued);
    bool actual = SpeechFinished(filter, nullptr, queued);
    ei::verify::Check("SpeechFinished", expected, actual, "unit %d sound %d", filter->unitId, queued->sound);
    return actual;
}
EI_VERIFY(0x00537FD0, SpeechFinishedVerify, 6, s_speechFinished);

static bool __fastcall SpeakVerify(CUnitClientSpecific* self, void*, ClientSpeech* said) {
    const SpeechRecord* last = self->lastSpeech;
    std::uint32_t time = self->lastSpeechTime;
    int sound = said->sound;
    bool played = self->Speak(said);
    bool ok = played ? self->lastSpeech == said->id
                     : self->lastSpeech == last && self->lastSpeechTime == time && said->sound == sound;
    ei::verify::Check("CUnitClientSpecific::Speak", true, ok, "unit %d played %d", self->unit->id_, played ? 1 : 0);
    return played;
}
EI_VERIFY(0x005380D0, SpeakVerify, 0, s_unusedSpeak);

static void __fastcall SayVerify(CUnitClientSpecific* self, void*, const SpeechRecord* record, int kind) {
    self->Say(record, kind);
    bool ok = true;
    if (record) {
        ClientSpeechNode* last = self->speech->prev;
        bool added = last != self->speech && last->value.id == record && last->value.kind == kind;
        bool important = kind == 0x1D || kind == 0x1E || kind == 0x1F || (kind >= 0x23 && kind <= 0x26) || kind == 0x2A;
        ok = added || (self->speech->next != self->speech && !important);
    }
    ei::verify::Check("CUnitClientSpecific::Say", true, ok, "unit %d kind %d", self->unit->id_, kind);
}
EI_VERIFY(0x00537D80, SayVerify, 0, s_unusedSay);

static void __fastcall StopSpeechVerify(CUnitClientSpecific* self) {
    self->StopSpeech();
    bool ok = self->speech->next == self->speech && self->speech->prev == self->speech;
    ei::verify::Check("CUnitClientSpecific::StopSpeech", true, ok, "unit %d", self->unit->id_);
}
EI_VERIFY(0x00538040, StopSpeechVerify, 0, s_unusedStop);
