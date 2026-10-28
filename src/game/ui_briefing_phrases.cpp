// The briefing / base screen's phrases (CBriefingScreen::NextPhrase, ui_briefing.h): a dialogue
// text's commands and phrases, the rewards after the last one, the camera and the speakers'
// animations. See docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"
#include "world_glue.h"
#include "combat.h"
#include "ui_briefing.h"
#include "quest_records.h"
#include "asset_text.h"

#include "console.h"
#include "item.h"
#include "item_db_text.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "sound.h"
#include "text_reader.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "world_data.h"
#include "world.h"

#include "lib/crt.h"
#include "text_split.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

const char* NoLipsCommand() { return "#nolips"; }
const char* ShowCommand() { return "#show"; }
const char* WeaponWord() { return "weapon"; }
const char* HideCommand() { return "#hide"; }
const char* AnimationCommand() { return "#animation"; }
const char* CameraCommand() { return "#camera"; }
const char* PhraseCommand() { return "#phrase"; }
// The phrase: "\x07C<colour> " name, ":\n\x07C<colour> " (light, or grey with "#nolips"), the text, "\n".
const char* PhraseHead() { return "\007C\377" "\263" "1 "; }
const char* PhraseBodyLips() { return ":\n\007C\344\327\247 "; }
const char* PhraseBodyNoLips() { return ":\n\007C\250\250\250 "; }
const char* LineEnd() { return "\n"; }
const char* VoiceFormat() { return "%s\\briefing\\%s\\%d.mp3"; }
const char* const* VoiceFolders() {
    static constexpr const char* kFolders[2] = /*00793378*/ {"t", "s"};
    return kFolders;
}
// The reward lines.
const char* ExperienceLabel() { return "brief_get_exp"; }
const char* GoldLabel() { return "brief_get_money"; }
const char* ItemLabel() { return "brief_get_item"; }
const char* LostItemLabel() { return "brief_lost_item"; }
const char* QuestLabel() { return "brief_get_quest"; }
const char* QuestDoneLabel() { return "brief_complete_quest"; }
const char* SpecialsLabel() { return "brief_get_specials_%d"; }
const char* NumberLine() { return "\007C\377\377\377 %s %d\n"; }
const char* NameLine() { return "\007C\377\377\377 %s %s\n"; }
const char* TextLine() { return "\007C\377\377\377 %s\n"; }
const char* ZonePrefix() { return "z"; }
const char* QuestSection() { return "quest "; }
QuestBriefingTable::Index& BriefingRecords() { return worlddata::QuestBriefings(); }

constexpr int kStateRewards = 4;
constexpr int kQuestNameParts = 3;
constexpr int kClassQuestItem = 0x14;
constexpr int kSoundPriority = 0x100;
constexpr int kSoundVolume = 100;
constexpr float kSecondsPerCharacter = 0.05f;
constexpr double kPi = 3.141592653589793;
constexpr double kDegree = 1.0 / 180.0;
// pi * (1 / 180) as FLD / FMUL leaves it, at 24-bit precision (the folded double constant has 29 more bits).
constexpr float kRadiansPerDegree = static_cast<float>(kPi * kDegree);
// Camera presets (ViewPreset) and the speakers they view.
constexpr int kViewBoth = 1;
constexpr int kViewLeader = 2;
constexpr int kViewPerson = 3;
constexpr int kViewThird = 4;

int Compare(std::string_view a, std::string_view b) {
    return gamecrt::mbsicmp(a, b);
}

CUnitClientSpecific* Model(CUnit* unit) { return unit->ClientController(); }

// The speaker's eyes: blinking (`blinks`), or open.
void SetBlink(CUnit* unit, std::uint8_t blinks) {
    CUnitClientSpecific* model = Model(unit);
    model->blinks = blinks;
    if (!blinks) model->BlinkOpen(true);
}

} // namespace

void CBriefingScreen::NextPhrase(bool skip) {
    std::vector<std::string> words;
    CTextManager* texts = TheTextManager();
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
    while (texts->FindLine(nullptr)) {
        texts->Split(&words, 1);
        if (Compare(words[0], NoLipsCommand()) == 0) {
            lips = false;
            continue;
        }
        int count = words.size();
        if (count < 2) continue;
        if (Compare(words[0], ShowCommand()) == 0) {
            if (count > 3 && Compare(words[3], WeaponWord()) == 0)
                ShowItem(words[1].c_str(), std::atoi(words[2].c_str()) - 1, 0);
            if (count > 2)
                ShowItem(words[1].c_str(), std::atoi(words[2].c_str()) - 1, 1);
            else
                ShowItem(words[1].c_str(), -1, 1);
        } else if (Compare(words[0], HideCommand()) == 0) {
            HideSpeaker(words[1].c_str(), skip, count > 2 ? 1 : 0);
        } else if (Compare(words[0], AnimationCommand()) == 0) {
            animation = std::atoi(words[1].c_str());
        } else if (Compare(words[0], CameraCommand()) == 0) {
            if (count >= 5) {
                angle = static_cast<float>(kRadiansPerDegree * std::atof(words[2].c_str()));
                distance = static_cast<float>(std::atof(words[3].c_str()));
                height = static_cast<float>(std::atof(words[4].c_str()));
                view = true;
            }
            preset = std::atoi(words[1].c_str());
        } else if (Compare(words[0], PhraseCommand()) == 0) {
            spoke = true;
            break;
        }
    }

    if (spoke) {
        // "#phrase <speaker> [voice]": the speaker's name (its unit's name, else its "pers" text,
        // else the word itself), then the text up to the next '#'.
        auto* unit = GameFindClientUnit(TextKeyFromName(words[1].c_str()));
        if (!unit) {
            speaker = words[1];
        } else {
            speaker = unit->GetName();
            if (speaker.size() == 0) {
                const PersonTextNode* node = personTexts_.find(unit->id_);
                speaker = node == personTexts_.end() ? words[1] : node->second;
            }
        }
        texts->ReadToHash(&phrase, 1);
        phrase = std::string(PhraseHead()) + speaker + (lips ? PhraseBodyLips() : PhraseBodyNoLips()) + phrase + LineEnd();
        dialogueText_ += phrase;
        const char* cursor = texts->cursor_;
        if (texts->FindLine(PhraseCommand())) last = false;
        texts->cursor_ = cursor;
        if (voice_ != -1) TheSoundManager()->StopSound(voice_);
        voice_ = -1;
        if (!skip) {
            int number;
            if (words.size() > 2 && (number = std::atoi(words[2].c_str())) > 0) {
                std::string file;
                int folder = gamecrt::rand() * 2 / 0x7FFF % 2;
                file = str::Format(VoiceFormat(), VoiceFolders()[folder], topic_.c_str(), number);
                voice_ = TheSoundManager()->Play2D(file.c_str(), kSoundPriority, kSoundVolume, 0, 0, 0, 1, 1);
            }
            phraseTime_ = static_cast<float>(text::CharacterCount(std::string_view(phrase))) *
                          kSecondsPerCharacter;
            if (firstPhrase_) phraseTime_ += 1.0f;
            text_->AddText(phrase.c_str());
        }
    } else if (!skip) {
        text_->AddText(phrase.c_str());
    }

    if (last) {
        // The rewards of the briefing record of topic_ (the lookup adds an empty entry).
        state_ = kStateRewards;
        QuestBriefingRecord* record;
        {
            std::string key(topic_.c_str());
            record = stlp::NamedSlot<QuestBriefingRecord>(BriefingRecords(), key);
        }
        if (record) {
            std::vector<std::string> parts;
            std::string name;
            std::string line;
            std::string label;
            auto add = [&] {
                dialogueText_ += line;
                if (!skip) text_->AddText(line.c_str());
            };
            auto addItems = [&](const std::vector<std::string>& list) {
                for (const std::string* it = list.data(); it != list.data() + list.size(); ++it) {
                    CItem* item = TheClientWorld()->itemManager_->CreateItem(it->c_str());
                    if (!item) continue;
                    ++item->refCount_;
                    texts->FormatItemName(item, &name);
                    line = str::Format(NameLine(), label.c_str(), name.c_str());
                    add();
                    item->Release();
                }
            };
            auto addQuests = [&](const std::vector<std::string>& list) {
                for (const std::string* it = list.data(); it != list.data() + list.size(); ++it) {
                    text::Split(it->c_str(), &parts, '.');
                    if (parts.size() != kQuestNameParts) continue;
                    if (!texts->Open((std::string(QuestSection()) + parts[2]).c_str())) continue;
                    texts->ReadLine(&name, 1);
                    line = str::Format(NameLine(), label.c_str(), name.c_str());
                    add();
                }
            };
            if (record->exp > 0.0f) {
                texts->GetString(ExperienceLabel(), &label);
                line = str::Format(NumberLine(), label.c_str(), static_cast<int>(record->exp));
                add();
            }
            if (record->money > 0.0f) {
                texts->GetString(GoldLabel(), &label);
                line = str::Format(NumberLine(), label.c_str(), static_cast<int>(record->money));
                add();
            }
            texts->GetString(ItemLabel(), &label);
            addItems(record->lists[QuestBriefingRecord::kGiveItems]);
            texts->GetString(LostItemLabel(), &label);
            // A network client lists the quest items it holds after a zone topic ("z...").
            if (IsMultiplayerClient() && !topic_.empty() && Compare(str::Left(topic_, 1), ZonePrefix()) == 0) {
                if (auto* player = TheServerWorld()->GetPlayer(0)) {
                    for (auto node = player->items_.begin(); node != player->items_.end(); ++node) {
                        CItem* item = *node;
                        if (item->GetClassId() != kClassQuestItem) continue;
                        texts->FormatItemName(item, &name);
                        line = str::Format(NameLine(), label.c_str(), name.c_str());
                        add();
                    }
                }
            }
            addItems(record->lists[QuestBriefingRecord::kRemoveItems]);
            texts->GetString(QuestLabel(), &label);
            addQuests(record->lists[QuestBriefingRecord::kGiveQuests]);
            texts->GetString(QuestDoneLabel(), &label);
            addQuests(record->lists[QuestBriefingRecord::kCompleteQuests]);
            if (record->specials > 0) {
                label = str::Format(SpecialsLabel(), record->specials);
                texts->GetString(label.c_str(), &label);
                line = str::Format(TextLine(), label.c_str());
                add();
            }
        }
    }

    // The phrase's speaker: the last line's second word (the original reads it unchecked; empty here when
    // the line has one word).
    auto named = [&words] { return words.size() > 1 ? words[1].c_str() : ""; };
    if (!skip || last) {
        if (leader_) GameSetUnitHeight(leader_->id_, 0.0f);
        if (person_) GameSetUnitHeight(person_->id_, 0.0f);
        for (std::size_t i = 0; i < speakers_.size(); ++i) {
            if (Compare(speakers_[i].name, named()) != 0) continue;
            if (view) {
                CUnit* target;
                switch (preset) {
                case kViewLeader: target = leader_; break;
                case kViewPerson: target = person_; break;
                case kViewThird: target = third_; break;
                default:
                    ViewSpeaker(nullptr, angle, distance, height);
                    continue;
                }
                if (target) ViewSpeaker(target, angle, distance, height);
            } else {
                int chosen = preset;
                if (chosen < 1) {
                    CUnit* unit = speakers_[i].unit;
                    if (firstPhrase_)
                        chosen = kViewBoth;
                    else if (unit == leader_)
                        chosen = kViewLeader;
                    else if (unit == third_)
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
        for (std::size_t i = 0; i < speakers_.size(); ++i) {
            CUnit* unit = speakers_[i].unit;
            if (Compare(speakers_[i].name, named()) != 0) {
                PlaySpeakerAnimation(unit, 0, 0);
                SetBlink(unit, 0);
                continue;
            }
            if (animation > 0) {
                if (auto* serverUnit = TheServerWorld()->FindUnit(unit->id_)) {
                    auto* controller = serverUnit->ServerController();
                    controller->StopAction();
                    controller->actionDone = SpeakerAnimationDone;
                    if (FigureAnimation* played = unit->Object3D()->FindSkillAnimation(animation))
                        controller->PlayAnimation(played->name);
                }
            } else {
                PlaySpeakerAnimation(unit, 1, 0);
            }
            SetBlink(unit, lips);
            speaking_ = unit;
        }
    }

    firstPhrase_ = false;
    if (skip && last) {
        // Skipped to the end: the whole text at once, scrolled to its end; everyone listens.
        text_->SetText(dialogueText_.c_str());
        text_->SetPosition(text_->maxPosition_);
        text_->moved_ = true;
        for (BriefingSpeaker* p = speakers_.begin(); p != speakers_.end(); ++p) {
            SetBlink(p->unit, 0);
            PlaySpeakerAnimation(p->unit, 0, 0);
        }
        speaking_ = nullptr;
        ClearShownItems();
    }
}
