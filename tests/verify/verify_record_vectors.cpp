// Checks for the vector<T> instantiations over game records (src/game/record_vectors.cpp): ours
// only, the model check of stlp_vector_verify.h over a text of each record (its strings and the
// fields the copies carry).
#include "verify.h"

#include "stlp_vector_verify.h"

#include "../../src/game/record_vectors.h"

#include <cstdint>
#include <cstdio>
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

template <std::uintptr_t A> using Materials = ModelCheck<A, MaterialVector, MaterialText>;
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
