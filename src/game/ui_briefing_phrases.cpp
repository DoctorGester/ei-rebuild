// The briefing / base screen's phrases (CBriefingScreen::NextPhrase, ui_briefing.h): a dialogue
// text's commands and phrases, the rewards after the last one, the camera and the speakers'
// animations. See docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"
#include "world_glue.h"
#include "combat.h"
#include "ui_briefing.h"

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
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "game_data.h"
#include "game_singletons.h"

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
    static constexpr const char* kFolders[2] = {"t", "s"};
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
stlp::NameMap& BriefingRecords() { return ei::GameData<stlp::NameMap>(0x007C87D4); }   // hash_map<string, BriefingRecord*>
void* ItemManager() { return ei::GameData<void* const>(0x007AF60C); }

// 006023F0: what a speaker's controller calls when its dialogue animation has finished.
using ActionDone = void(__fastcall*)(CUnit*);
ActionDone SpeakerActionDone() { return ei::Code<ActionDone>(0x006023F0); }

constexpr int kStateRewards = 4;
constexpr int kQuestNameParts = 3;
constexpr int kClassQuestItem = 0x14;
constexpr int kSoundPriority = 0x100;
constexpr int kSoundVolume = 100;
constexpr float kSecondsPerCharacter = 0.05f;   // 0074134C
constexpr double kPi = 3.141592653589793;       // 007410C8
constexpr double kDegree = 1.0 / 180.0;         // 0073BA30
// pi * (1 / 180) as the original's FLD / FMUL leaves it at 24-bit precision (SV7d1a: the folded
// double constant has 29 more bits).
constexpr float kRadiansPerDegree = static_cast<float>(kPi * kDegree);
// Camera presets (ViewPreset) and the speakers they view.
constexpr int kViewBoth = 1;
constexpr int kViewLeader = 2;
constexpr int kViewPerson = 3;
constexpr int kViewThird = 4;

int Compare(const char* a, const char* b) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

CUnitClientSpecific* Model(CUnit* unit) { return reinterpret_cast<CUnitClientSpecific*>(unit->controller); }

// The speaker's eyes: blinking (`blinks`), or open.
void SetBlink(CUnit* unit, std::uint8_t blinks) {
    CUnitClientSpecific* model = Model(unit);
    model->blinks = blinks;
    if (!blinks) model->BlinkOpen(true);
}

} // namespace

// --- 00604C50 -----------------------------------------------------------------------------------

void CBriefingScreen::NextPhrase(bool skip) {
    console::CStringArray words;
    CTextManager* texts = TheTextManager();
    mfc::CString speaker;
    mfc::CString phrase;
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
        int count = words.GetSize();
        if (count < 2) continue;
        if (Compare(words[0], ShowCommand()) == 0) {
            if (count > 3 && Compare(words[3], WeaponWord()) == 0)
                ShowItem(words[1], libcrt::Atoi(words[2]) - 1, 0);
            if (count > 2)
                ShowItem(words[1], libcrt::Atoi(words[2]) - 1, 1);
            else
                ShowItem(words[1], -1, 1);
        } else if (Compare(words[0], HideCommand()) == 0) {
            HideSpeaker(words[1], skip, count > 2 ? 1 : 0);
        } else if (Compare(words[0], AnimationCommand()) == 0) {
            animation = libcrt::Atoi(words[1]);
        } else if (Compare(words[0], CameraCommand()) == 0) {
            if (count >= 5) {
                angle = static_cast<float>(kRadiansPerDegree * GameAtof(words[2]));
                distance = static_cast<float>(GameAtof(words[3]));
                height = static_cast<float>(GameAtof(words[4]));
                view = true;
            }
            preset = libcrt::Atoi(words[1]);
        } else if (Compare(words[0], PhraseCommand()) == 0) {
            spoke = true;
            break;
        }
    }

    if (spoke) {
        // "#phrase <speaker> [voice]": the speaker's name (its unit's name, else its "pers" text,
        // else the word itself), then the text up to the next '#'.
        auto* unit = static_cast<CUnit*>(GameFindClientUnit(TextKeyFromName(words[1])));
        if (!unit) {
            speaker = words[1];
        } else {
            speaker = unit->GetName();
            if (speaker.GetLength() == 0) {
                const PersonTextNode* node = personTexts_.find(unit->id_);
                speaker = node == personTexts_.end() ? words[1] : node->second;
            }
        }
        texts->ReadToHash(&phrase, 1);
        phrase = mfc::CString(PhraseHead()) + speaker + (lips ? PhraseBodyLips() : PhraseBodyNoLips()) + phrase + LineEnd();
        dialogueText_ += phrase;
        const char* cursor = texts->cursor_;
        if (texts->FindLine(PhraseCommand())) last = false;
        texts->cursor_ = cursor;
        if (voice_ != -1) TheSoundManager()->StopSound(voice_);
        voice_ = -1;
        if (!skip) {
            int number;
            if (words.GetSize() > 2 && (number = libcrt::Atoi(words[2])) > 0) {
                mfc::CString file;
                int folder = gamecrt::rand() * 2 / 0x7FFF % 2;
                file.Format(VoiceFormat(), VoiceFolders()[folder], static_cast<const char*>(topic_), number);
                voice_ = TheSoundManager()->Play2D(file, kSoundPriority, kSoundVolume, 0, 0, 0, 1, 1);
            }
            phraseTime_ = static_cast<float>(phrase.GetLength()) * kSecondsPerCharacter;
            if (firstPhrase_) phraseTime_ += 1.0f;
            static_cast<CUiScrollText*>(text_)->AddText(phrase);
        }
    } else if (!skip) {
        static_cast<CUiScrollText*>(text_)->AddText(phrase);
    }

    if (last) {
        // The rewards of the briefing record of topic_ (the lookup adds an empty entry).
        state_ = kStateRewards;
        BriefingRecord* record;
        {
            stlp::string key(topic_);
            record = stlp::NamedSlot<BriefingRecord>(BriefingRecords(), key);
        }
        if (record) {
            console::CStringArray parts;
            mfc::CString name;
            mfc::CString line;
            mfc::CString label;
            auto add = [&] {
                dialogueText_ += line;
                if (!skip) static_cast<CUiScrollText*>(text_)->AddText(line);
            };
            auto addItems = [&](const BriefingRecord::NameList& list) {
                for (const mfc::CString* it = list.begin(); it != list.end(); ++it) {
                    CEngineObject* item = ItemManagerCreateItemOriginal(ItemManager(), nullptr, *it);
                    if (!item) continue;
                    ++item->refCount_;
                    texts->FormatItemName(static_cast<CItem*>(item), &name);
                    line.Format(NameLine(), static_cast<const char*>(label), static_cast<const char*>(name));
                    add();
                    item->Release();
                }
            };
            auto addQuests = [&](const BriefingRecord::NameList& list) {
                for (const mfc::CString* it = list.begin(); it != list.end(); ++it) {
                    text::Split(*it, &parts, '.');
                    if (parts.GetSize() != kQuestNameParts) continue;
                    if (!texts->Open(mfc::CString(QuestSection()) + parts[2])) continue;
                    texts->ReadLine(&name, 1);
                    line.Format(NameLine(), static_cast<const char*>(label), static_cast<const char*>(name));
                    add();
                }
            };
            if (record->experience > 0.0f) {
                texts->GetString(ExperienceLabel(), &label);
                line.Format(NumberLine(), static_cast<const char*>(label), static_cast<int>(record->experience));
                add();
            }
            if (record->gold > 0.0f) {
                texts->GetString(GoldLabel(), &label);
                line.Format(NumberLine(), static_cast<const char*>(label), static_cast<int>(record->gold));
                add();
            }
            texts->GetString(ItemLabel(), &label);
            addItems(record->giveItems);
            texts->GetString(LostItemLabel(), &label);
            // A network client lists the quest items it holds after a zone topic ("z...").
            if (IsMultiplayerClient() && topic_.GetLength() > 0 && Compare(topic_.Left(1), ZonePrefix()) == 0) {
                if (auto* player = static_cast<WorldPlayer*>(static_cast<CWorldServer*>(TheServerWorld())->GetPlayer(0))) {
                    for (auto* node = player->items.head; node; node = node->next) {
                        CEngineObject* item = node->data;
                        if (item->GetClassId() != kClassQuestItem) continue;
                        texts->FormatItemName(static_cast<CItem*>(item), &name);
                        line.Format(NameLine(), static_cast<const char*>(label), static_cast<const char*>(name));
                        add();
                    }
                }
            }
            addItems(record->removeItems);
            texts->GetString(QuestLabel(), &label);
            addQuests(record->giveQuests);
            texts->GetString(QuestDoneLabel(), &label);
            addQuests(record->completeQuests);
            if (record->specials > 0) {
                label.Format(SpecialsLabel(), record->specials);
                texts->GetString(label, &label);
                line.Format(TextLine(), static_cast<const char*>(label));
                add();
            }
        }
    }

    // The phrase's speaker: the last line's second word (read unchecked, as the original does,
    // only while comparing with a speaker).
    auto named = [&words] { return static_cast<const char*>(words.data[1]); };
    if (!skip || last) {
        if (leader_) GameSetUnitHeight(leader_->id_, 0.0f);
        if (person_) GameSetUnitHeight(person_->id_, 0.0f);
        for (std::size_t i = 0; i < static_cast<std::size_t>(speakersEnd_ - speakers_); ++i) {
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
        for (std::size_t i = 0; i < static_cast<std::size_t>(speakersEnd_ - speakers_); ++i) {
            CUnit* unit = speakers_[i].unit;
            if (Compare(speakers_[i].name, named()) != 0) {
                PlaySpeakerAnimation(unit, 0, 0);
                SetBlink(unit, 0);
                continue;
            }
            if (animation > 0) {
                if (auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(TheServerWorld(), nullptr, unit->id_))) {
                    auto* controller = reinterpret_cast<CUnitServer*>(serverUnit->controller);
                    controller->StopAction();
                    controller->actionDone = SpeakerActionDone();
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
        static_cast<CUiScrollText*>(text_)->SetText(dialogueText_);
        text_->CallSetPosition(text_->maxPosition_);
        text_->moved_ = true;
        for (BriefingSpeaker* p = speakers_; p != speakersEnd_; ++p) {
            SetBlink(p->unit, 0);
            PlaySpeakerAnimation(p->unit, 0, 0);
        }
        speaking_ = nullptr;
        ClearShownItems();
    }
}
EI_REPLACE_METHOD(0x00604C50, CBriefingScreen::NextPhrase);
