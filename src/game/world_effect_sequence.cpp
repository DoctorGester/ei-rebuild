// EffectSoundSeq's Start and Unpack: the sound sequence handed to the sound manager with a
// chooser that picks the next name. See world_effect.h and docs/notes/effects.md.
#include "world_effect.h"

#include "console.h"
#include "random.h"
#include "sound.h"

#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstring>
#include <string>

namespace {

std::uint32_t NextRandom() { return g_random.Next(); }

// The choosers: called with the previous index and the number of sounds
// started so far; kSequenceLoop in the result loops the sound. Without a first name any of the n names is
// picked; with one, the first call plays it (index 0) and later calls pick among the n after it.
int LoopFirst(int, int) { return kSequenceLoop; }
int AnyOf2(int, int) { return static_cast<int>(NextRandom() & 1); }
int AnyOf3(int, int) { return static_cast<int>(NextRandom() % 3); }
int AnyOf4(int, int) { return static_cast<int>(NextRandom() & 3); }
int AnyOf5(int, int) { return static_cast<int>(NextRandom() % 5); }
int FirstThenLoop1(int, int played) { return played != 0 ? kSequenceLoop | 1 : 0; }
int FirstThenOf2(int, int played) { return played != 0 ? static_cast<int>(NextRandom() & 1) + 1 : 0; }
int FirstThenOf3(int, int played) { return played != 0 ? static_cast<int>(NextRandom() % 3) + 1 : 0; }
int FirstThenOf4(int, int played) { return played != 0 ? static_cast<int>(NextRandom() & 3) + 1 : 0; }
int FirstThenOf5(int, int played) { return played != 0 ? static_cast<int>(NextRandom() % 5) + 1 : 0; }

using Chooser = int(*)(int, int);

Chooser ChooserFor(int count, bool withFirst) {
    switch (count) {
    case 1: return withFirst ? &FirstThenLoop1 : &LoopFirst;
    case 2: return withFirst ? &FirstThenOf2 : &AnyOf2;
    case 3: return withFirst ? &FirstThenOf3 : &AnyOf3;
    case 4: return withFirst ? &FirstThenOf4 : &AnyOf4;
    case 5: return withFirst ? &FirstThenOf5 : &AnyOf5;
    default: return &LoopFirst;
    }
}

bool SnapshotReadToEnd() {
    MemoryStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}


} // namespace

// An update moves the sequence through the single-sound call (SetSoundPosition with the sequence id,
// like the original). A new start lists the first name (when not empty) and the others, and starts the
// sequence with the chooser for their count.
void EffectSoundSeq::Start(int update) {
    SoundManager* sounds = g_clientWorld.sounds;
    if (update != 0) {
        sounds->SetSoundPosition(sound, &localPosition.x, volume, radius);
        return;
    }
    std::vector<std::string> list;
    const std::string& first = name;
    const std::vector<std::string>& others = names;
    bool withFirst = str::Compare(first, "") != 0;
    if (withFirst) list.push_back(first);
    Chooser chooser = ChooserFor(static_cast<int>(others.size()), withFirst);
    for (const std::string& other : others) list.push_back(other);
    sound = sounds->AddSequence(&list, noFade != 0 ? 1000 : 0,
                                 &localPosition.x, volume, radius, 0, -1, chooser);
}

int EffectSoundSeq::Unpack(int update, DeltaField* field) {
    Vector3 position;
    position = field->ReadPosition();
    name = field->ReadString();
    // The count is read into the storage of `field` (ReadDelta always stores it).
    int count = static_cast<int>(reinterpret_cast<std::uintptr_t>(field));
    field->ReadDelta(&count, 4);
    std::vector<std::string>& others = names;
    others.resize(static_cast<std::size_t>(count));
    for (std::string& other : others) other = field->ReadString();
    volume = field->ReadFloat16();
    radius = field->ReadFloat16();
    velocity = field->ReadFloat16x3();
    field->ReadDelta(&noFade, 1);
    EI_NAN_AUDIT(velocity.x, velocity.y, velocity.z);
    moving = velocity.x == 0.0f && velocity.y == 0.0f && velocity.z == 0.0f ? 0 : 1;
    if (!SnapshotReadToEnd()) return 0;
    localPosition = position;
    Start(update);
    return 1;
}
