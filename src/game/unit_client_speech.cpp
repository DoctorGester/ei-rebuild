// What a unit says on the client: the speech queue of CUnitClientSpecific and
// playing a speech. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "sound.h"
#include "unit.h"
#include "world.h"

#include "lib/gameapi.h"

#include "game_singletons.h"

#include <iterator>
#include <list>

namespace {
constexpr std::uint32_t kFlagTalking = 0x800000;      // the server unit's flags_ while it speaks
constexpr int kKindTalking = 0x2B;
constexpr int kSpeechAfterSpeech = 2;                 // kinds that wait for the last speech sound
constexpr int kSpeechAfterSpeech2 = 0x1C;
constexpr int kQueuedDelay = 3;
constexpr std::uint32_t kRepeatAfter = 5000;          // ms before the same speech plays again
constexpr int kSpeechPriority = 1;
constexpr int kSpeechVolume = 100;

// The kinds a queued speech gives way to: (new kind, queued kind).
bool Replaces(int kind, int queued) {
    return (kind == 0xD && queued == 9) || (kind == 0xE && queued == 1) || (kind == 0xF && queued == 3) ||
           (kind == 0x10 && queued == 3) || (kind == 0x11 && (queued == 5 || queued == 6)) ||
           (kind == 0x13 && queued == 3);
}

// Kinds that queue behind speeches already waiting.
bool Important(int kind) {
    switch (kind) {
    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x2A: return true;
    default: return false;
    }
}

bool SoundBusy(int handle) {
    if (handle == -1) return false;
    CSoundManager* sounds = g_clientWorld->sounds_;
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

void SetTalking(int unitId, bool talking) {
    CEngineObject* serverUnit = g_serverWorld->FindUnit(unitId);
    if (!serverUnit) return;
    if (talking) serverUnit->flags_ |= kFlagTalking;
    else serverUnit->flags_ &= ~kFlagTalking;
}

// Erases the queued speeches `filter` picks.
template <typename Pick>
void EraseWhere(std::list<ClientSpeech>& queue, Pick pick) {
    for (auto it = queue.begin(); it != queue.end();) it = pick(&*it) ? queue.erase(it) : std::next(it);
}
} // namespace

int SpeechFilter::Replaced(const ClientSpeech* queued) const {
    return Replaces(kind, queued->kind) ? 1 : 0;
}

bool SpeechFilter::Finished(const ClientSpeech* queued) const {
    if (queued->sound == -1 || SoundBusy(queued->sound)) return false;
    if (queued->talking) SetTalking(unitId, false);
    return true;
}

void CUnitClientSpecific::Say(const SpeechRecord* record, int kind) {
    if (!record) return;
    SpeechFilter replacing{0, kind};
    EraseWhere(speech, [&](const ClientSpeech* queued) { return replacing.Replaced(queued) & 0xFF; });
    SpeechFilter finishing{unit->id_, 0};
    EraseWhere(speech, [&](const ClientSpeech* queued) { return finishing.Finished(queued); });
    bool empty = speech.empty();
    if (!empty && !Important(kind)) return;
    ClientSpeech said{};
    said.id = record;
    said.kind = kind;
    said.delay = kQueuedDelay;
    said.sound = -1;
    said.talking = kind == kKindTalking;
    if (empty) {
        if (said.talking) SetTalking(unit->id_, true);
        said.delay = 0;
        if (kind != kSpeechAfterSpeech && kind != kSpeechAfterSpeech2) {
            Speak(&said);
        } else if (!SoundBusy(speechSound) && Speak(&said)) {
            speechSound = said.sound;
        }
    }
    speech.push_back(said);
}

void CUnitClientSpecific::StopSpeech() {
    for (const ClientSpeech& queued : speech) {
        if (queued.talking) SetTalking(unit->id_, false);
        g_clientWorld->sounds_->StopSound(queued.sound);
    }
    speech.clear();
}

bool CUnitClientSpecific::Speak(ClientSpeech* said) {
    std::uint32_t now = gameapi::timeGetTime();
    if (lastSpeech == said->id && now - lastSpeechTime < kRepeatAfter) return false;
    said->sound = g_clientWorld->sounds_->Play2D(said->id->sound.data(), kSpeechPriority, kSpeechVolume, 0, 0, 0, 1, 0);
    lastSpeech = said->id;
    lastSpeechTime = now;
    return true;
}
