// The briefing / base screen's topics: a person's topics from the "b." variables and a topic's
// title (CBriefingScreen, ui_briefing.h). See docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstring>
#include <new>

namespace {

const char* TopicPrefix() { return reinterpret_cast<const char*>(0x007906FC); }       // "b."
const char* ConstructorTopic() { return reinterpret_cast<const char*>(0x007933B4); }  // "constr"
const char* BriefingSection() { return reinterpret_cast<const char*>(0x007906E4); }   // "briefing "

constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;               // "b", person, topic
constexpr int kConstructorPrefixLength = 6;

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The STLport sort<string*> pieces a network client's topics go through (not rewritten): the
// introsort loop (RET 0xC: an unused word, the depth limit, the comparator) and the final
// insertion sort (RET 4: the comparator). The comparator is an empty object; the original passes
// the person's id in its slot.
__declspec(naked) void __fastcall TopicIntroSortAt(stlp::string* /*first*/, stlp::string* /*last*/, int, int /*depth*/,
                                                   int /*comparator*/) EI_ORIGINAL(0x0060B330)
__declspec(naked) void __fastcall TopicFinalSortAt(stlp::string* /*first*/, stlp::string* /*last*/, int /*comparator*/)
    EI_ORIGINAL(0x0060B440)

// A vector<string> of the topics, freed as the original's inlined destructor does.
class TopicList {
public:
    TopicList() = default;
    TopicList(const TopicList&) = delete;
    TopicList& operator=(const TopicList&) = delete;
    ~TopicList() {
        list_.clear();
        list_.FreeStorage();
    }
    stlp::vector<stlp::string>& Get() { return list_; }

private:
    stlp::vector<stlp::string> list_;
};

} // namespace

// --- 00607360 -----------------------------------------------------------------------------------

void CBriefingScreen::FindTopics(int personId) {
    BriefingVariables variables(TopicPrefix());
    console::CStringArray parts;
    mfc::CString name;
    mfc::CString constructor;
    TopicList found;
    for (NamedFloatTable::Node* node = variables.First(); node; node = variables.Next(node)) {
        if (Bits(node->value) != kOneBits) continue;
        StringSplit(node->key, &parts, '.');
        if (parts.size != kTopicNameParts || TextKeyFromName(parts[1]) != personId) continue;
        name = parts[2];
        bool isConstructor;
        {
            mfc::CString start = name.Left(kConstructorPrefixLength);
            isConstructor = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(start)),
                                             reinterpret_cast<const unsigned char*>(ConstructorTopic())) == 0;
        }
        if (isConstructor) {
            constructor = name;
            continue;
        }
        const char* text = name;
        found.Get().push_back(stlp::string(text, text + std::strlen(text)));
    }
    stlp::vector<stlp::string>& list = found.Get();
    if (IsNetworkClientOriginal() && list.begin() != list.end()) {
        int depth = 0;
        for (int n = static_cast<int>(list.end() - list.begin()); n != 1; n >>= 1) ++depth;
        TopicIntroSortAt(list.begin(), list.end(), 0, depth * 2, personId);
        TopicFinalSortAt(list.begin(), list.end(), personId);
    }
    topics_.SetSize(0, -1);
    for (stlp::string* topic = list.begin(); topic != list.end(); ++topic) topics_.SetAtGrow(topics_.size, topic->data());
    if (constructor.GetLength() != 0) topics_.SetAtGrow(topics_.size, constructor);
}
EI_REPLACE_METHOD(0x00607360, CBriefingScreen::FindTopics);

// --- 006078C0 -----------------------------------------------------------------------------------

mfc::CString* CBriefingScreen::TopicTitle(mfc::CString* out, const char* topic) {
    mfc::CString title;
    mfc::CString key(topic);
    key.MakeLower();
    bool isConstructor;
    {
        mfc::CString start = key.Left(kConstructorPrefixLength);
        isConstructor = GameMbscmp(start, ConstructorTopic()) == 0;
    }
    if (isConstructor) key = ConstructorTopic();
    CTextManager* texts = GameTexts();
    mfc::CString section(BriefingSection());
    section += key;
    if (TextsOpenSection(texts, nullptr, section)) texts->ReadLine(&title, 1);
    return new (out) mfc::CString(title);
}
EI_REPLACE_METHOD(0x006078C0, CBriefingScreen::TopicTitle);
