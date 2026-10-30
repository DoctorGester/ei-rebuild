// The briefing / base screen's phrases (BriefingScreen::NextPhrase, ui_briefing.h): a dialogue
// text's commands and phrases, the rewards after the last one, the camera and the speakers'
// animations. See docs/notes/ui.md, "Briefing / base screen".
#include "asset_text.h"
#include "combat.h"
#include "console.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "text_manager.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "quest_records.h"
#include "sound.h"
#include "text_split.h"
#include "ui.h"
#include "ui_briefing.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "world.h"
#include "world_data.h"
#include "world_glue.h"
#include "random.h"

#include "lib/containers.h"
#include "lib/pc24.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kNoLipsCommand = "#nolips";
constexpr const char* kShowCommand = "#show";
constexpr const char* kWeaponWord = "weapon";
constexpr const char* kHideCommand = "#hide";
constexpr const char* kAnimationCommand = "#animation";
constexpr const char* kCameraCommand = "#camera";
constexpr const char* kPhraseCommand = "#phrase";
// The phrase: "\x07C<colour> " name, ":\n\x07C<colour> " (light, or grey with "#nolips"), the text, "\n".
constexpr const char* kPhraseHead = "\007C\377" "\263" "1 ";
constexpr const char* kPhraseBodyLips = ":\n\007C\344\327\247 ";
constexpr const char* kPhraseBodyNoLips = ":\n\007C\250\250\250 ";
constexpr const char* kLineEnd = "\n";
constexpr const char* kVoiceFormat = "%s\\briefing\\%s\\%d.mp3";
constexpr const char* kVoiceFolders[2] = /*00793378*/ {"t", "s"};
// The reward lines.
constexpr const char* kExperienceLabel = "brief_get_exp";
constexpr const char* kGoldLabel = "brief_get_money";
constexpr const char* kItemLabel = "brief_get_item";
constexpr const char* kLostItemLabel = "brief_lost_item";
constexpr const char* kQuestLabel = "brief_get_quest";
constexpr const char* kQuestDoneLabel = "brief_complete_quest";
constexpr const char* kSpecialsLabel = "brief_get_specials_%d";
constexpr const char* kNumberLine = "\007C\377\377\377 %s %d\n";
constexpr const char* kNameLine = "\007C\377\377\377 %s %s\n";
constexpr const char* kTextLine = "\007C\377\377\377 %s\n";
constexpr const char* kZonePrefix = "z";
constexpr const char* kQuestSection = "quest ";

constexpr int kStateRewards = 4;
constexpr int kQuestNameParts = 3;
constexpr int kSoundPriority = 0x100;
constexpr int kSoundVolume = 100;
constexpr float kSecondsPerCharacter = 0.05f;
constexpr double kDegree = 1.0 / 180.0;
// pi * (1 / 180) as the original's arithmetic leaves it, at 24-bit precision (the folded double constant has 29 more bits).
constexpr float kRadiansPerDegree = static_cast<float>(kPiDouble * kDegree);
// Camera presets (ViewPreset) and the speakers they view.
constexpr int kViewBoth = 1;
constexpr int kViewLeader = 2;
constexpr int kViewPerson = 3;
constexpr int kViewThird = 4;

// The speaker's eyes: blinking (`blinks`), or open.
void SetBlink(CUnit* unit, std::uint8_t blinks) {
    UnitClientSpecific* model = unit->ClientController();
    model->blinks = blinks;
    if (!blinks) model->BlinkOpen(true);
}

} // namespace

void BriefingScreen::NextPhrase(bool skip) {
    std::vector<std::string> words;
    TextManager* texts = g_clientWorld.texts;
    std::string speaker;
    std::string phrase;
    bool last = true;       // no "#phrase" follows this one
    bool spoke = false;     // a phrase was found
    int animation = -1;
    int preset = -1;
    bool lips = true;
    bool view = false;      // "#camera" gave its own view
    float angle = 0.0f;
    float distance = 0.0f;
    float height = 0.0f;

    // The lines up to the next phrase (the cursor stands after the previous one).
    while (texts->FindHeader()) {
        words = texts->Split(true);
        if (text::CompareIgnoringCase(words[0], kNoLipsCommand) == 0) {
            lips = false;
            continue;
        }
        int count = lib::Count(words);
        if (count < 2) continue;
        if (text::CompareIgnoringCase(words[0], kShowCommand) == 0) {
            if (count > 3 && text::CompareIgnoringCase(words[3], kWeaponWord) == 0)
                ShowItem(words[1], str::ToInt(words[2]) - 1);
            if (count > 2)
                ShowItem(words[1], str::ToInt(words[2]) - 1);
            else
                ShowItem(words[1], -1);
        } else if (text::CompareIgnoringCase(words[0], kHideCommand) == 0) {
            HideSpeaker(words[1], skip, count > 2);
        } else if (text::CompareIgnoringCase(words[0], kAnimationCommand) == 0) {
            animation = str::ToInt(words[1]);
        } else if (text::CompareIgnoringCase(words[0], kCameraCommand) == 0) {
            if (count >= 5) {
                angle = pc24::Mul(kRadiansPerDegree, str::ToDouble(words[2]));   // the text's double: one rounding
                distance = static_cast<float>(str::ToDouble(words[3]));
                height = static_cast<float>(str::ToDouble(words[4]));
                view = true;
            }
            preset = str::ToInt(words[1]);
        } else if (text::CompareIgnoringCase(words[0], kPhraseCommand) == 0) {
            spoke = true;
            break;
        }
    }

    if (spoke) {
        // "#phrase <speaker> [voice]": the speaker's name (its unit's name, else its "pers" text,
        // else the word itself), then the text up to the next '#'.
        auto* unit = GameFindClientUnit(TextKeyFromName(words[1]));
        if (!unit) {
            speaker = words[1];
        } else {
            speaker = unit->GetName();
            if (speaker.size() == 0) {
                auto node = personTexts.find(unit->id);
                speaker = node == personTexts.end() ? words[1] : node->second;
            }
        }
        phrase = texts->ReadToHash();
        phrase = std::string(kPhraseHead) + speaker + (lips ? kPhraseBodyLips : kPhraseBodyNoLips) + phrase + kLineEnd;
        dialogueText += phrase;
        const char* cursor = texts->cursor;
        if (texts->FindLine(kPhraseCommand)) last = false;
        texts->cursor = cursor;
        if (voice != -1) g_clientWorld.sounds->StopSound(voice);
        voice = -1;
        if (!skip) {
            int number;
            if (words.size() > 2 && (number = str::ToInt(words[2])) > 0) {
                std::string file;
                int folder = ClientRandBelow(2);
                file = str::Format(kVoiceFormat, kVoiceFolders[folder], topic, number);
                voice = g_clientWorld.sounds->Play2D(file, kSoundPriority, kSoundVolume, 0, 0, 0, 1, 1);
            }
            phraseTime = static_cast<float>(text::CharacterCount(std::string_view(phrase))) *
                          kSecondsPerCharacter;
            if (firstPhrase) phraseTime += 1.0f;
            text->AddText(phrase);
        }
    } else if (!skip) {
        text->AddText(phrase);
    }

    if (last) {
        // The rewards of the briefing record of topic (the lookup adds an empty entry).
        state = kStateRewards;
        QuestBriefingRecord* record;
        {
            std::string key(topic);
            record = worlddata::QuestBriefings()[key];
        }
        if (record) {
            std::vector<std::string> parts;
            std::string name;
            std::string line;
            std::string label;
            auto add = [&] {
                dialogueText += line;
                if (!skip) text->AddText(line);
            };
            auto addItems = [&](const std::vector<std::string>& list) {
                for (const auto& it : list) {
                    Item* item = g_clientWorld.itemManager->CreateItem(it);
                    if (!item) continue;
                    item->AddRef();
                    name = texts->FormatItemName(item);
                    line = str::Format(kNameLine, label, name);
                    add();
                    item->Release();
                }
            };
            auto addQuests = [&](const std::vector<std::string>& list) {
                for (const auto& it : list) {
                    parts = text::Split(it, '.');
                    if (parts.size() != kQuestNameParts) continue;
                    if (!texts->Open(std::string(kQuestSection) + parts[2])) continue;
                    name = texts->ReadLine(true);
                    line = str::Format(kNameLine, label, name);
                    add();
                }
            };
            if (record->exp > 0.0f) {
                label = texts->GetString(kExperienceLabel);
                line = str::Format(kNumberLine, label, static_cast<int>(static_cast<std::int64_t>(record->exp)));
                add();
            }
            if (record->money > 0.0f) {
                label = texts->GetString(kGoldLabel);
                line = str::Format(kNumberLine, label, static_cast<int>(static_cast<std::int64_t>(record->money)));
                add();
            }
            label = texts->GetString(kItemLabel);
            addItems(record->lists[QuestBriefingRecord::kGiveItems]);
            label = texts->GetString(kLostItemLabel);
            // A network client lists the quest items it holds after a zone topic ("z...").
            if (IsMultiplayerClient() && !topic.empty() && text::CompareIgnoringCase(str::Left(topic, 1), kZonePrefix) == 0) {
                if (auto* player = g_serverWorld.GetPlayer(0)) {
                    for (auto& node : player->items) {
                        Item* item = node;
                        if (item->GetClassId() != ClassId::ItemQuest) continue;
                        name = texts->FormatItemName(item);
                        line = str::Format(kNameLine, label, name);
                        add();
                    }
                }
            }
            addItems(record->lists[QuestBriefingRecord::kRemoveItems]);
            label = texts->GetString(kQuestLabel);
            addQuests(record->lists[QuestBriefingRecord::kGiveQuests]);
            label = texts->GetString(kQuestDoneLabel);
            addQuests(record->lists[QuestBriefingRecord::kCompleteQuests]);
            if (record->specials > 0) {
                label = str::Format(kSpecialsLabel, record->specials);
                label = texts->GetString(label);
                line = str::Format(kTextLine, label);
                add();
            }
        }
    }

    // The phrase's speaker: the last line's second word (the original reads it unchecked; empty here when
    // the line has one word).
    auto named = [&words] { return words.size() > 1 ? words[1].c_str() : ""; };
    if (!skip || last) {
        if (leader) GameSetUnitHeight(leader->id, 0.0f);
        if (person) GameSetUnitHeight(person->id, 0.0f);
        for (std::size_t i = 0; i < speakers.size(); ++i) {
            if (text::CompareIgnoringCase(speakers[i].name, named()) != 0) continue;
            if (view) {
                CUnit* target;
                switch (preset) {
                case kViewLeader: target = leader; break;
                case kViewPerson: target = person; break;
                case kViewThird: target = third; break;
                default:
                    ViewSpeaker(nullptr, angle, distance, height);
                    continue;
                }
                if (target) ViewSpeaker(target, angle, distance, height);
            } else {
                int chosen = preset;
                if (chosen < 1) {
                    CUnit* unit = speakers[i].unit;
                    if (firstPhrase)
                        chosen = kViewBoth;
                    else if (unit == leader)
                        chosen = kViewLeader;
                    else if (unit == third)
                        chosen = kViewThird;
                    else
                        chosen = kViewPerson;
                }
                ViewPreset(chosen);
            }
        }
    }

    // The speaker talks, the others listen.
    if (!skip && spoke) {
        for (std::size_t i = 0; i < speakers.size(); ++i) {
            CUnit* unit = speakers[i].unit;
            if (text::CompareIgnoringCase(speakers[i].name, named()) != 0) {
                PlaySpeakerAnimation(unit, false, false);
                SetBlink(unit, 0);
                continue;
            }
            if (animation > 0) {
                if (auto* serverUnit = g_serverWorld.FindUnit(unit->id)) {
                    auto* controller = serverUnit->ServerController();
                    controller->StopAction();
                    controller->actionDone = SpeakerAnimationDone;
                    if (FigureAnimation* played = unit->Object3D()->FindSkillAnimation(static_cast<SkillAnimation>(animation)))
                        controller->PlayAnimation(played->name);
                }
            } else {
                PlaySpeakerAnimation(unit, true, false);
            }
            SetBlink(unit, lips);
            speaking = unit;
        }
    }

    firstPhrase = false;
    if (skip && last) {
        // Skipped to the end: the whole text at once, scrolled to its end; everyone listens.
        text->SetText(dialogueText);
        text->SetPosition(text->maxPosition);
        text->moved = true;
        for (const BriefingSpeaker& p : speakers) {
            SetBlink(p.unit, 0);
            PlaySpeakerAnimation(p.unit, false, false);
        }
        speaking = nullptr;
        ClearShownItems();
    }
}
