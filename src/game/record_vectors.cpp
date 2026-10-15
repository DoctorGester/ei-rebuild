// The game's STLport vector<T> instantiations over game records (record_vectors.h), registered at
// their own addresses; the forwarders in the area files keep calling these addresses. The element
// size of each was read from its size arithmetic. Checked by tests/verify/verify_record_vectors.cpp.
#include "record_vectors.h"

#include "ei/hook.h"

using namespace recordvec;

// The item database's resize(n) (RET 4): a default record made first and destroyed last; the
// tail erased, or copies of it appended.
EI_REPLACE(0x006A7F00, MaterialVector::ResizeDefault);    // 0x50 bytes
EI_REPLACE(0x006A8110, WeaponVector::ResizeDefault);      // 0xB4
EI_REPLACE(0x006A8290, ArmorVector::ResizeDefault);       // 0xC8
EI_REPLACE(0x006A83F0, QuickItemVector::ResizeDefault);   // 0x8C
EI_REPLACE(0x006A85A0, QuestItemVector::ResizeDefault);   // 0x7C
EI_REPLACE(0x006A8790, LootVector::ResizeDefault);        // 0x78

// The spell database's resize(n) (RET 4).
EI_REPLACE(0x00685730, SpellVector::ResizeDefault);           // 0x84
EI_REPLACE(0x006858D0, SpellModifierVector::ResizeDefault);   // 0x24
EI_REPLACE(0x00684D60, SpellListVector::ResizeDefault);       // 0x34

// The quest database's resize(n) (RET 4) and push_back(x) (RET 4).
EI_REPLACE(0x006B2100, QuestVector::ResizeDefault);      // 0x38
EI_REPLACE(0x006B2240, BriefingVector::ResizeDefault);   // 0x6C
EI_REPLACE(0x006B0710, QuestVector::PushBack);
EI_REPLACE(0x006B0780, BriefingVector::PushBack);
