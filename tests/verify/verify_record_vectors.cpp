// Checks for the vector<T> instantiations over game records (src/game/record_vectors.cpp): ours
// only, the model check of stlp_vector_verify.h over a text of each record (its strings and the
// fields the copies carry).
#include "verify.h"

#include "stlp_vector_verify.h"

#include "../../src/game/record_vectors.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

void* s_unused;

using stlp_verify::ModelCheck;
using namespace recordvec;

std::string Str(const mfc::CString& s) { return static_cast<const char*>(s); }
std::string Str(const char* s) { return s ? s : "(null)"; }

// The item records: the head's strings and string array sizes (a resize(n) record leaves the plain
// fields uninitialised).
struct ItemText {
    static std::string Of(const itemdb::ItemRecordHead& r) {
        char numbers[32];
        std::snprintf(numbers, sizeof(numbers), "|%d|%d", r.list20.GetSize(), r.list50.GetSize());
        return Str(r.text04) + "|" + Str(r.text08) + "|" + Str(r.materialClass) + numbers;
    }
};
struct MaterialText {
    static std::string Of(const itemdb::MaterialRecordHead& r) {
        return Str(r.key) + "|" + Str(r.soundName) + "|" + Str(r.name);
    }
};

// The spell records: their strings (and the lists' sizes).
struct SpellText {
    static std::string Of(const itemdb::SpellRecord& r) {
        char sizes[32];
        std::snprintf(sizes, sizeof(sizes), "|%u|%u", static_cast<unsigned>(r.list48.size()),
                      static_cast<unsigned>(r.list54.size()));
        return Str(r.name) + "|" + Str(r.code) + "|" + Str(r.text08) + sizes;
    }
};
struct SpellModifierText {
    static std::string Of(const itemdb::SpellModifierRecord& r) { return Str(r.name) + "|" + Str(r.text1C); }
};
std::string Text(const stlp::string& s) { return std::string(s.data(), s.size()); }
std::string Texts(const stlp::vector<stlp::string>& v) {
    std::string text = "[";
    for (const stlp::string* p = v.begin(); p != v.end(); ++p) text += Text(*p) + ",";
    return text + "]";
}
struct SpellListText {
    static std::string Of(const itemdb::SpellListRecord& r) {
        return Text(r.name) + "|" + Texts(r.list0C) + "|" + Texts(r.list18) + "|" + Text(r.text24);
    }
};
std::string Texts(const CStringVector& v) {
    std::string text = "[";
    for (const mfc::CString* p = v.begin(); p != v.end(); ++p) text += Str(*p) + ",";
    return text + "]";
}

// The quest records: the strings and the places' count / the lists.
struct QuestText {
    static std::string Of(const QuestRecord& r) {
        char places[16];
        std::snprintf(places, sizeof(places), "|%u|", static_cast<unsigned>(r.places.size()));
        return Text(r.name) + places + Text(r.text);
    }
};
struct BriefingText {
    static std::string Of(const QuestBriefingRecord& r) {
        std::string text = Text(r.name) + "|";
        for (const CStringVector& list : r.lists) text += Texts(list);
        return text + "|" + Text(r.text);
    }
};

// The unit and skill records: their strings and string lists.
struct UnitPrototypeText {
    static std::string Of(const UnitPrototypeRecord& r) {
        return Str(r.name) + "|" + Str(r.modelName) + "|" + Str(r.soundFolder) + "|" + Texts(r.lootItems) +
               Texts(r.spellNames) + Texts(r.armourNames) + "|" + Str(r.weaponName) + "|" + Str(r.secondWeaponName);
    }
};
struct UnitNpcText {
    static std::string Of(const UnitNpcRecord& r) {
        return Str(r.name) + "|" + Texts(r.skillNames) + Texts(r.startWeapons) + Texts(r.startQuickItems) +
               Texts(r.startSpells);
    }
};
struct UnitModelText {
    static std::string Of(const UnitModelRecord& r) {
        std::string text = Str(r.name) + "|";
        for (const UnitModelSlot& slot : r.slots) text += Str(slot.levelName) + ",";
        char counts[16];
        std::snprintf(counts, sizeof(counts), "|%u",
                      static_cast<unsigned>(r.soundCountsEnd - r.soundCounts));
        return text + "|" + Str(r.figureName) + Texts(r.field1F) + Texts(r.field20) + "|" + Str(r.hitMaterial) + "|" +
               Str(r.soundFolder) + Texts(r.soundNames) + counts;
    }
};
struct UnitLevelText {
    static std::string Of(const UnitLevelRecord& r) { return Str(r.name); }
};
struct SkillText {
    static std::string Of(const SkillRecord& r) { return Str(r.name) + "|" + Str(r.field1) + Texts(r.statNames); }
};
struct SkillLevelText {
    static std::string Of(const SkillLevelRecord& r) {
        return Str(r.field0) + "|" + Str(r.name) + "|" + Str(r.requiredLevelName) + "|" + Str(r.requiredSkillName);
    }
};

// A zone's unit looks: the id lists and the texture names.
std::string Ids(const UnitIdList& v) {
    std::string text = "[";
    char id[16];
    for (const std::uint32_t* p = v.begin(); p != v.end(); ++p) {
        std::snprintf(id, sizeof(id), "%u,", *p);
        text += id;
    }
    return text + "]";
}
struct FiguredUnitText {
    static std::string Of(const FiguredUnitRecord& r) {
        return Ids(r.armourIds) + Ids(r.weaponIds) + Texts(FiguredUnitElement::NamesOf(r));
    }
};
struct DressedUnitText {
    static std::string Of(const DressedUnitRecord& r) { return Ids(r.items) + Ids(r.extraWeapons); }
};

// prints.db's records: the vtable, the name and the values; acks.db's speeches: the strings (and
// the environment flags).
template <typename R>
struct PrintText {
    static std::string Of(const R& r) {
        std::uintptr_t vtable;
        std::memcpy(&vtable, &r, sizeof(vtable));
        std::string text = Text(r.name);
        char value[16];
        std::snprintf(value, sizeof(value), "|%08X", static_cast<unsigned>(vtable));
        text += value;
        const std::uint32_t* values = r.values2;
        for (std::size_t i = 0; i < (sizeof(R) - offsetof(R, values2)) / 4; ++i) {
            std::snprintf(value, sizeof(value), ",%08X", values[i]);
            text += value;
        }
        return text;
    }
};
std::string SpeechOf(const SpeechRecord& r) { return Text(r.sound) + "|" + Text(r.text) + "|" + Text(r.name); }
struct SpeechText {
    static std::string Of(const SpeechRecord& r) { return SpeechOf(r); }
};
struct EnvironmentSpeechText {
    static std::string Of(const acks::EnvironmentSpeech& r) { return SpeechOf(r.speech); }
};

template <std::uintptr_t A> using Prints = ModelCheck<A, PrintVector, PrintText<PrintRecord>>;
template <std::uintptr_t A> using Prints2 = ModelCheck<A, Print2Vector, PrintText<PrintRecord2>>;
template <std::uintptr_t A> using Speeches = ModelCheck<A, SpeechVector, SpeechText>;
template <std::uintptr_t A> using EnvironmentSpeeches = ModelCheck<A, EnvironmentSpeechVector, EnvironmentSpeechText>;
template <std::uintptr_t A> using FiguredUnits = ModelCheck<A, FiguredUnitVector, FiguredUnitText>;
template <std::uintptr_t A> using DressedUnits = ModelCheck<A, DressedUnitVector, DressedUnitText>;
template <std::uintptr_t A> using UnitPrototypes = ModelCheck<A, UnitPrototypeVector, UnitPrototypeText>;
template <std::uintptr_t A> using UnitNpcs = ModelCheck<A, UnitNpcVector, UnitNpcText>;
template <std::uintptr_t A> using UnitModels = ModelCheck<A, UnitModelVector, UnitModelText>;
template <std::uintptr_t A> using UnitLevels = ModelCheck<A, UnitLevelVector, UnitLevelText>;
template <std::uintptr_t A> using Skills = ModelCheck<A, SkillVector, SkillText>;
template <std::uintptr_t A> using SkillLevels = ModelCheck<A, SkillLevelVector, SkillLevelText>;
template <std::uintptr_t A> using Materials =ModelCheck<A, MaterialVector, MaterialText>;
template <std::uintptr_t A> using Weapons = ModelCheck<A, WeaponVector, ItemText>;
template <std::uintptr_t A> using Armors = ModelCheck<A, ArmorVector, ItemText>;
template <std::uintptr_t A> using QuickItems = ModelCheck<A, QuickItemVector, ItemText>;
template <std::uintptr_t A> using QuestItems = ModelCheck<A, QuestItemVector, ItemText>;
template <std::uintptr_t A> using Loot = ModelCheck<A, LootVector, ItemText>;
template <std::uintptr_t A> using Spells = ModelCheck<A, SpellVector, SpellText>;
template <std::uintptr_t A> using SpellModifiers = ModelCheck<A, SpellModifierVector, SpellModifierText>;
template <std::uintptr_t A> using SpellLists = ModelCheck<A, SpellListVector, SpellListText>;
template <std::uintptr_t A> using Quests = ModelCheck<A, QuestVector, QuestText>;
template <std::uintptr_t A> using Briefings = ModelCheck<A, BriefingVector, BriefingText>;

} // namespace

// The item database's resize(n) (reached while the databases load).
EI_VERIFY(0x006A7F00, Materials<0x006A7F00>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006A8110, Weapons<0x006A8110>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006A8290, Armors<0x006A8290>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006A83F0, QuickItems<0x006A83F0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006A85A0, QuestItems<0x006A85A0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006A8790, Loot<0x006A8790>::ResizeDefault, 0, s_unused);

// The spell database's resize(n) (reached while the databases load).
EI_VERIFY(0x00685730, Spells<0x00685730>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006858D0, SpellModifiers<0x006858D0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x00684D60, SpellLists<0x00684D60>::ResizeDefault, 0, s_unused);

// The quest database (loaded at startup; the push_backs while the quest files are read).
EI_VERIFY(0x006B2100, Quests<0x006B2100>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006B2240, Briefings<0x006B2240>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006B0710, Quests<0x006B0710>::PushBack, 0, s_unused);
EI_VERIFY(0x006B0780, Briefings<0x006B0780>::PushBack, 0, s_unused);

// The unit and skill databases (loaded at startup; the clear when the perks reload).
EI_VERIFY(0x0053E2A0, UnitPrototypes<0x0053E2A0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x0053E520, UnitNpcs<0x0053E520>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x0053E730, UnitModels<0x0053E730>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x0053E9B0, UnitLevels<0x0053E9B0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x005209D0, Skills<0x005209D0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x00520B20, SkillLevels<0x00520B20>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x00520C20, SkillLevels<0x00520C20>::Clear, 0, s_unused);

// A zone's unit looks (resize(n) while a zone's units load).
EI_VERIFY(0x0055A400, FiguredUnits<0x0055A400>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x0055A580, DressedUnits<0x0055A580>::ResizeDefault, 0, s_unused);

// prints.db and acks.db (loaded at startup).
EI_VERIFY(0x006D26E0, Prints<0x006D26E0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006D2830, Prints2<0x006D2830>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006E4560, Speeches<0x006E4560>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x006E47F0, EnvironmentSpeeches<0x006E47F0>::ResizeDefault, 0, s_unused);
