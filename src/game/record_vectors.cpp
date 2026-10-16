// The game's STLport vector<T> instantiations over game records (record_vectors.h), registered at
// their own addresses; the forwarders in the area files keep calling these addresses. The element
// size of each was read from its size arithmetic. Checked in game by verify_record_vectors.cpp until S21d.
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

// The unit database's resize(n) (RET 4).
EI_REPLACE(0x0053E2A0, UnitPrototypeVector::ResizeDefault);   // 0x120
EI_REPLACE(0x0053E520, UnitNpcVector::ResizeDefault);         // 0x60
EI_REPLACE(0x0053E730, UnitModelVector::ResizeDefault);       // 0x168
EI_REPLACE(0x0053E9B0, UnitLevelVector::ResizeDefault);       // 0x44

// The skill database's resize(n) (RET 4) and the level records' clear().
EI_REPLACE(0x005209D0, SkillVector::ResizeDefault);           // 0x18
EI_REPLACE(0x00520B20, SkillLevelVector::ResizeDefault);      // 0x44
EI_REPLACE(0x00520C20, SkillLevelVector::Clear);

// A zone's unit looks (world_unit_set.cpp's lists; resize(n), RET 4).
EI_REPLACE(0x0055A400, FiguredUnitVector::ResizeDefault);     // 0x3C
EI_REPLACE(0x0055A580, DressedUnitVector::ResizeDefault);     // 0x24

// prints.db's tables and acks.db's speech lists (resize(n), RET 4; loaded at startup).
EI_REPLACE(0x006D26E0, PrintVector::ResizeDefault);           // 0x28
EI_REPLACE(0x006D2830, Print2Vector::ResizeDefault);          // 0x34
EI_REPLACE(0x006E4560, SpeechVector::ResizeDefault);          // 0x28
EI_REPLACE(0x006E47F0, EnvironmentSpeechVector::ResizeDefault);   // 0x2C

// acks.db's tables (resize(n), RET 4; loaded at startup).
EI_REPLACE(0x006DB9C0, UnitAcksVector::ResizeDefault);    // 0x1C4
EI_REPLACE(0x006DBBD0, ModelAcksVector::ResizeDefault);   // 0x5C
EI_REPLACE(0x006DBE20, ExtraAcksVector::ResizeDefault);   // 0x38

// The client's lobby queries (net_lobby.cpp).
EI_REPLACE(0x004439C0, QueryVector::InsertOverflow);   // 0x2C
EI_REPLACE(0x00443BF0, QueryVector::Assign);
EI_REPLACE(0x004442B0, QueryVector::EraseRange);
EI_REPLACE(0x00444090, QueryVector::ResizeDefault);
EI_REPLACE(0x0041F7F0, QueryVector::Destruct);   // ~vector (the client's queries)
// S12g: _Construct(p) (a default query) and the copy loop of erase (__copy, RET 0xC).
EI_REPLACE(0x00443E30, (stlp::ConstructDefaultAt<net::Query, QueryElement>));
EI_REPLACE(0x00443FF0, (stlp::CopyAssignTagged<net::Query, QueryElement>));

// The party manager's entries (resize(n), RET 4).
EI_REPLACE(0x004ADAA0, PartyEntryVector::ResizeDefault);   // 0x14

// The lever table's records (resize(n), RET 4; Levers.ldb at startup).
EI_REPLACE(0x00465180, LeverVector::ResizeDefault);   // 0x2C

// The session's replicated messages: push_back(T()) (plain RET) and _M_insert_overflow (RET 0xC).
EI_REPLACE(0x0041F790, ReplicatedVector::PushDefault);    // 8
EI_REPLACE(0x00426B10, ReplicatedVector::InsertOverflow);

// The HUD's portrait slots: push_back(T()) (plain RET) and erase(first, last) (RET 8).
EI_REPLACE(0x0064AB20, HudPortraitSlotVector::PushDefault);   // 0x78
EI_REPLACE(0x0064AD40, HudPortraitSlotVector::EraseRange);

// The client party's engine objects: _M_insert_overflow (RET 0xC).
EI_REPLACE(0x00428A20, EngineObjectRefVector::InsertOverflow);   // 4

// The UI's lists: the save list (erase RET 8, _M_insert_overflow RET 0xC), the multiplayer
// addresses, the camp's trade rows and records, the options' key bindings (_M_insert_overflow).
EI_REPLACE(0x006578F0, SaveEntryVector::EraseRange);
EI_REPLACE(0x006579C0, SaveEntryVector::InsertOverflow);       // 0x44
EI_REPLACE(0x0061EC40, MpAddressVector::InsertOverflow);       // 0x2C
EI_REPLACE(0x00629E60, TradeRowVector::InsertOverflow);        // 0x18
// S12g: the save list's _Construct(p, x) (ECX p, EDX x).
EI_REPLACE(0x00657C60, (stlp::ConstructCopyAt<SaveEntry, SaveEntryElement>));
EI_REPLACE(0x0062A0B0, TradeRecordVector::InsertOverflow);     // 0x3C
EI_REPLACE(0x00652930, OptionBindingVector::InsertOverflow);   // 0x18

// The game flow's zone table: _M_insert_overflow (RET 0xC).
EI_REPLACE(0x00583920, ZoneTableVector::Destruct);   // S3c: the zone table 007AFDE0 at exit (0057F4A0)
EI_REPLACE(0x00587480, ZoneTableVector::InsertOverflow);       // 0xC
