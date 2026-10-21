// CEffectSoundSeq's Start and Unpack: the sound sequence handed to the sound manager with a
// chooser that picks the next name. See world_effect.h and docs/notes/effects.md.
#include "world_effect.h"

#include "console.h"
#include "random.h"
#include "sound.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstring>

namespace {

// The sound manager (*007AF784).
CSoundManager* SoundManager() { return ei::GameData<CSoundManager*>(0x007AF784); }

std::uint32_t NextRandom() { return IsaacRandom::Global().Next(); }

// The choosers (00676DD0..00676F90): called with the previous index and the number of sounds
// started so far; bit 30 of the result loops the sound. Without a first name any of the n names is
// picked; with one, the first call plays it (index 0) and later calls pick among the n after it.
int __fastcall LoopFirst(int, int) { return 0x40000000; }                        // 00676DD0
int __fastcall AnyOf2(int, int) { return static_cast<int>(NextRandom() & 1); }    // 00676DE0
int __fastcall AnyOf3(int, int) { return static_cast<int>(NextRandom() % 3); }    // 00676E10
int __fastcall AnyOf4(int, int) { return static_cast<int>(NextRandom() & 3); }    // 00676E50
int __fastcall AnyOf5(int, int) { return static_cast<int>(NextRandom() % 5); }    // 00676E80
int __fastcall FirstThenLoop1(int, int played) { return played != 0 ? 0x40000001 : 0; }                      // 00676EC0
int __fastcall FirstThenOf2(int, int played) { return played != 0 ? static_cast<int>(NextRandom() & 1) + 1 : 0; }  // 00676ED0
int __fastcall FirstThenOf3(int, int played) { return played != 0 ? static_cast<int>(NextRandom() % 3) + 1 : 0; }  // 00676F10
int __fastcall FirstThenOf4(int, int played) { return played != 0 ? static_cast<int>(NextRandom() & 3) + 1 : 0; }  // 00676F50
int __fastcall FirstThenOf5(int, int played) { return played != 0 ? static_cast<int>(NextRandom() % 5) + 1 : 0; }  // 00676F90

using Chooser = int(__fastcall*)(int, int);

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
    BitStream* snapshot = delta::ReadSnapshot();
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}


} // namespace

// An update moves the sequence through the single-sound call (SetSoundPosition with the sequence
// id, as the original does). A new start lists the first name (when not empty) and the others,
// and starts the sequence with the chooser for their count.
void CEffectSoundSeq::Start(int update) {
    CSoundManager* sounds = SoundManager();
    if (update != 0) {
        sounds->SetSoundPosition(sound_, &localPosition_.x, volume_, radius_);
        return;
    }
    console::CStringArray list;
    mfc::CString& first = name_;
    const mfc::CStringArray& others = names_;
    bool withFirst = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(first)),
                                     reinterpret_cast<const unsigned char*>("")) != 0;
    if (withFirst) list.SetAtGrow(list.GetSize(), first);
    Chooser chooser = ChooserFor(others.GetSize(), withFirst);
    for (int i = 0; i < others.GetSize(); ++i) list.SetAtGrow(list.GetSize(), others[i]);
    sound_ = sounds->AddSequence(reinterpret_cast<const SoundNameArray*>(&list), noFade_ != 0 ? 1000 : 0,
                                 &localPosition_.x, volume_, radius_, 0, -1, chooser);
}

int CEffectSoundSeq::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    alignas(4) unsigned char text[sizeof(mfc::CString)];
    field->ReadString(text);
    auto* read = reinterpret_cast<mfc::CString*>(text);
    name_ = *read;
    read->~CString();
    // The count is read into the stack slot of `field` (ReadDelta always stores it).
    int count = static_cast<int>(reinterpret_cast<std::uintptr_t>(field));
    field->ReadDelta(&count, 4);
    mfc::CStringArray& others = names_;
    others.SetSize(count, -1);
    for (int i = 0; i < count; ++i) {
        field->ReadString(text);
        others[i] = *read;
        read->~CString();
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

EI_REPLACE_METHOD(0x00676C00, CEffectSoundSeq::Start);
EI_REPLACE_METHOD(0x00676FD0, CEffectSoundSeq::Unpack);
