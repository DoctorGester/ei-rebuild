// The briefing / base screen's topics: a person's topics from the "b." variables and a topic's
// title (BriefingScreen, ui_briefing.h). See docs/notes/ui.md, "Briefing / base screen".
#include "game_singletons.h"
#include "text_manager.h"
#include "text_split.h"
#include "ui_briefing.h"
#include "world_glue.h"

#include "lib/intro_sort.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <bit>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr const char* kTopicPrefix = "b.";
constexpr const char* kConstructorTopic = "constr";
constexpr const char* kBriefingSection = "briefing ";

constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;               // "b", person, topic
constexpr int kConstructorPrefixLength = 6;

} // namespace


void BriefingScreen::FindTopics(int personId) {
    NamedFloatTable variables;
    PlayerZeroVariables(kTopicPrefix, &variables);
    std::vector<std::string> parts;
    std::string name;
    std::string constructor;
    std::vector<std::string> found;
    for (auto& node : variables) {
        if (std::bit_cast<std::uint32_t>(node.second) != kOneBits) continue;
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts || TextKeyFromName(parts[1]) != personId) continue;
        name = parts[2];
        bool isConstructor;
        {
            std::string start = str::Left(name, kConstructorPrefixLength);
            isConstructor = text::CompareIgnoringCase(start,
                                            kConstructorTopic) == 0;
        }
        if (isConstructor) {
            constructor = name;
            continue;
        }
        found.push_back(name);
    }
    std::vector<std::string>& list = found;
    if (IsMultiplayerClient()) {
        // By the text ignoring case: topics that differ only in case (or reached through two spellings of the
        // person's name) keep the game's sort's order.
        lib::IntroSort(list.data(), list.data() + list.size(), [](const std::string& a, const std::string& b) {
            return text::CompareIgnoringCase(a.data(), b.data()) < 0;
        });
    }
    topics.clear();
    for (auto& entry : list) topics.push_back(entry);
    if (constructor.size() != 0) topics.push_back(constructor);
}

std::string BriefingScreen::TopicTitle(std::string_view topicKey) {
    std::string title;
    std::string key(topicKey);
    str::MakeLower(key);
    bool isConstructor;
    {
        std::string start = str::Left(key, kConstructorPrefixLength);
        isConstructor = str::Compare(start, kConstructorTopic) == 0;
    }
    if (isConstructor) key = kConstructorTopic;
    TextManager* texts = g_clientWorld.texts;
    std::string section = kBriefingSection;
    section += key;
    if (texts->TextManager::Open(section)) title = texts->ReadLine(true);
    return title;
}
