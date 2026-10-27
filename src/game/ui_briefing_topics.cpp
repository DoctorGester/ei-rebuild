// The briefing / base screen's topics: a person's topics from the "b." variables and a topic's
// title (CBriefingScreen, ui_briefing.h). See docs/notes/ui.md, "Briefing / base screen".
#include "stl_sorts.h"
#include "ui_briefing.h"

#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/stlp.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

const char* TopicPrefix() { return "b."; }
const char* ConstructorTopic() { return "constr"; }
const char* BriefingSection() { return "briefing "; }

constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;               // "b", person, topic
constexpr int kConstructorPrefixLength = 6;

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// A vector<string> of the topics, freed.
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


void CBriefingScreen::FindTopics(int personId) {
    BriefingVariables variables(TopicPrefix());
    console::CStringArray parts;
    std::string name;
    std::string constructor;
    TopicList found;
    for (NamedFloatTable::Node* node = variables.First(); node; node = variables.Next(node)) {
        if (Bits(node->value.mapped) != kOneBits) continue;
        text::Split(node->value.key, &parts, '.');
        if (parts.size != kTopicNameParts || TextKeyFromName(parts[1]) != personId) continue;
        name = parts[2];
        bool isConstructor;
        {
            std::string start = str::Left(name, kConstructorPrefixLength);
            isConstructor = gamecrt::mbsicmp(start,
                                            ConstructorTopic()) == 0;
        }
        if (isConstructor) {
            constructor = name;
            continue;
        }
        const char* text = name.c_str();
        found.Get().push_back(stlp::string(text, text + std::strlen(text)));
    }
    stlp::vector<stlp::string>& list = found.Get();
    if (IsMultiplayerClient() && list.begin() != list.end()) {
        int depth = 0;
        for (int n = static_cast<int>(list.end() - list.begin()); n != 1; n >>= 1) ++depth;
        stl_sorts::TopicIntroSort(list.begin(), list.end(), 0, depth * 2, personId);
        stl_sorts::TopicFinalSort(list.begin(), list.end(), personId);
    }
    topics_.SetSize(0, -1);
    for (stlp::string* topic = list.begin(); topic != list.end(); ++topic) topics_.SetAtGrow(topics_.size, topic->data());
    if (constructor.size() != 0) topics_.SetAtGrow(topics_.size, constructor);
}
EI_REWRITTEN(0x00607360, CBriefingScreen::FindTopics);


mfc::CString* CBriefingScreen::TopicTitle(mfc::CString* out, const char* topic) {
    mfc::CString title;
    std::string key = topic;
    str::MakeLower(key);
    bool isConstructor;
    {
        std::string start = str::Left(key, kConstructorPrefixLength);
        isConstructor = gamecrt::mbscmp(start, ConstructorTopic()) == 0;
    }
    if (isConstructor) key = ConstructorTopic();
    CTextManager* texts = TheTextManager();
    std::string section = BriefingSection();
    section += key;
    if (texts->CTextManager::Open(section.c_str())) texts->ReadLine(&title, 1);
    return std::construct_at(out, title);
}
EI_REWRITTEN(0x006078C0, CBriefingScreen::TopicTitle);
