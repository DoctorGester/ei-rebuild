// CEffectSoundSeq's Start and Unpack: the sound sequence handed to the sound manager with a
// chooser that picks the next name. See world_effect.h and docs/notes/effects.md.
#include "world_effect.h"

#include "console.h"
#include "random.h"
#include "sound.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstring>
#include <string>

namespace {

std::uint32_t NextRandom() { return IsaacRandom::Global().Next(); }

// The choosers: called with the previous index and the number of sounds
// started so far; bit 30 of the result loops the sound. Without a first name any of the n names is
// picked; with one, the first call plays it (index 0) and later calls pick among the n after it.
int LoopFirst(int, int) { return 0x40000000; }
int AnyOf2(int, int) { return static_cast<int>(NextRandom() & 1); }
int AnyOf3(int, int) { return static_cast<int>(NextRandom() % 3); }
int AnyOf4(int, int) { return static_cast<int>(NextRandom() & 3); }
int AnyOf5(int, int) { return static_cast<int>(NextRandom() % 5); }
int FirstThenLoop1(int, int played) { return played != 0 ? 0x40000001 : 0; }
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
    BitStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}


} // namespace

// An update moves the sequence through the single-sound call (SetSoundPosition with the sequence id,
// like the original). A new start lists the first name (when not empty) and the others, and starts the
// sequence with the chooser for their count.
void CEffectSoundSeq::Start(int update) {
    CSoundManager* sounds = TheSoundManager();
    if (update != 0) {
        sounds->SetSoundPosition(sound_, &localPosition_.x, volume_, radius_);
        return;
    }
    SoundNameArray list;
    const std::string& first = name_;
    const std::vector<std::string>& others = names_;
    bool withFirst = str::Compare(first, "") != 0;
    if (withFirst) list.push_back(first);
    Chooser chooser = ChooserFor(static_cast<int>(others.size()), withFirst);
    for (const std::string& other : others) list.push_back(other);
    sound_ = sounds->AddSequence(&list, noFade_ != 0 ? 1000 : 0,
                                 &localPosition_.x, volume_, radius_, 0, -1, chooser);
}

int CEffectSoundSeq::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    {
        std::string read;
        field->ReadString(&read);
        name_ = read;
    }
    // The count is read into the stack slot of `field` (ReadDelta always stores it).
    int count = static_cast<int>(reinterpret_cast<std::uintptr_t>(field));
    field->ReadDelta(&count, 4);
    std::vector<std::string>& others = names_;
    others.resize(static_cast<std::size_t>(count));
    for (std::string& other : others) {
        std::string read;   // reuses the name's stack slot, like the original
        field->ReadString(&read);
        other = read.c_str();
    }
    field->ReadFloat16(&volume_);
    field->ReadFloat16(&radius_);
    field->ReadFloat16x3(&velocity_.x);
    field->ReadDelta(&noFade_, 1);
    EI_NAN_AUDIT(velocity_.x, velocity_.y, velocity_.z);
    moving_ = velocity_.x == 0.0f && velocity_.y == 0.0f && velocity_.z == 0.0f ? 0 : 1;
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    Start(update);
    return 1;
}
