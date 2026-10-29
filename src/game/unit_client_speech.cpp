// What a unit says on the client: the speech queue of UnitClientSpecific and
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
constexpr int kQueuedDelay = 3;
constexpr std::uint32_t kRepeatAfter = 5000;          // ms before the same speech plays again
constexpr int kSpeechPriority = 1;
constexpr int kSpeechVolume = 100;

// The kinds a queued speech gives way to: (new kind, queued kind).
bool Replaces(AckKind kind, AckKind queued) {
    return (kind == AckKind::CantChangePosition && queued == AckKind::ChangePosition) ||
           (kind == AckKind::NoPath && queued == AckKind::Move) || (kind == AckKind::NoMana && queued == AckKind::Cast) ||
           (kind == AckKind::CantTeleport && queued == AckKind::Cast) ||
           (kind == AckKind::ScienceFailed && (queued == AckKind::UseObject || queued == AckKind::Steal)) ||
           (kind == AckKind::SpellTooComplex && queued == AckKind::Cast);
}

// Kinds that queue behind speeches already waiting.
bool Important(AckKind kind) {
    switch (kind) {
    case AckKind::OutOfStamina:
    case AckKind::ArmCrippled:
    case AckKind::LegCrippled:
    case AckKind::ArmourBroken:
    case AckKind::WeaponBroken:
    case AckKind::ArmourWorn:
    case AckKind::WeaponWorn:
    case AckKind::Scenario: return true;
    default: return false;
    }
}

bool SoundBusy(int handle) {
    if (handle == -1) return false;
    SoundManager* sounds = g_clientWorld.sounds;
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

void SetTalking(int unitId, bool talking) {
    EngineObject* serverUnit = g_serverWorld.FindUnit(unitId);
    if (!serverUnit) return;
    if (talking) serverUnit->flags |= MapObjectFlag::Blocked;
    else serverUnit->flags &= ~MapObjectFlag::Blocked;
}

// Erases the queued speeches `filter` picks.
template <typename Pick>
void EraseWhere(std::list<ClientSpeech>& queue, Pick pick) {
    for (auto it = queue.begin(); it != queue.end();) it = pick(&*it) ? queue.erase(it) : std::next(it);
}
} // namespace

bool SpeechFilter::Replaced(const ClientSpeech* queued) const {
    return Replaces(kind, queued->kind);
}

bool SpeechFilter::Finished(const ClientSpeech* queued) const {
    if (queued->sound == -1 || SoundBusy(queued->sound)) return false;
    if (queued->talking) SetTalking(unitId, false);
    return true;
}

void UnitClientSpecific::Say(const SpeechRecord* record, AckKind kind) {
    if (!record) return;
    SpeechFilter replacing{0, kind};
    EraseWhere(speech, [&](const ClientSpeech* queued) { return replacing.Replaced(queued); });
    SpeechFilter finishing{unit->id, AckKind::Selected};
    EraseWhere(speech, [&](const ClientSpeech* queued) { return finishing.Finished(queued); });
    bool empty = speech.empty();
    if (!empty && !Important(kind)) return;
    ClientSpeech said{};
    said.id = record;
    said.kind = kind;
    said.delay = kQueuedDelay;
    said.sound = -1;
    said.talking = kind == AckKind::ScenarioTalking;
    if (empty) {
        if (said.talking) SetTalking(unit->id, true);
        said.delay = 0;
        if (kind != AckKind::Attack && kind != AckKind::DecidedToAttack) {
            Speak(&said);
        } else if (!SoundBusy(speechSound) && Speak(&said)) {
            speechSound = said.sound;
        }
    }
    speech.push_back(said);
}

void UnitClientSpecific::StopSpeech() {
    for (const ClientSpeech& queued : speech) {
        if (queued.talking) SetTalking(unit->id, false);
        g_clientWorld.sounds->StopSound(queued.sound);
    }
    speech.clear();
}

bool UnitClientSpecific::Speak(ClientSpeech* said) {
    std::uint32_t now = gameapi::timeGetTime();
    if (lastSpeech == said->id && now - lastSpeechTime < kRepeatAfter) return false;
    said->sound = g_clientWorld.sounds->Play2D(said->id->sound.data(), kSpeechPriority, kSpeechVolume, 0, 0, 0, 1, 0);
    lastSpeech = said->id;
    lastSpeechTime = now;
    return true;
}
