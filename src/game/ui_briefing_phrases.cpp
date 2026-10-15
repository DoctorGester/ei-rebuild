// The briefing / base screen's phrases (CBriefingScreen::NextPhrase, ui_briefing.h): a dialogue
// text's commands and phrases, the rewards after the last one, the camera and the speakers'
// animations. See docs/notes/ui.md, "Briefing / base screen".
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
#include "lib/stlp.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"

namespace {

const char* NoLipsCommand() { return reinterpret_cast<const char*>(0x007934C8); }      // "#nolips"
const char* ShowCommand() { return reinterpret_cast<const char*>(0x007906DC); }        // "#show"
const char* WeaponWord() { return reinterpret_cast<const char*>(0x007934C0); }         // "weapon"
const char* HideCommand() { return reinterpret_cast<const char*>(0x007934B8); }        // "#hide"
const char* AnimationCommand() { return reinterpret_cast<const char*>(0x007934AC); }   // "#animation"
const char* CameraCommand() { return reinterpret_cast<const char*>(0x00785738); }      // "#camera"
const char* PhraseCommand() { return reinterpret_cast<const char*>(0x007934A4); }      // "#phrase"
// The phrase: "\x07C<colour> " name, ":\n\x07C<colour> " (light, or grey with "#nolips"), the text, "\n".
const char* PhraseHead() { return reinterpret_cast<const char*>(0x0079349C); }
const char* PhraseBodyLips() { return reinterpret_cast<const char*>(0x00793490); }
const char* PhraseBodyNoLips() { return reinterpret_cast<const char*>(0x00793484); }
const char* LineEnd() { return reinterpret_cast<const char*>(0x00784340); }            // "\n"
const char* VoiceFormat() { return reinterpret_cast<const char*>(0x0079346C); }        // "%s\briefing\%s\%d.mp3"
const char* const* VoiceFolders() { return reinterpret_cast<const char* const*>(0x00793378); }   // "t", "s"
// The reward lines.
const char* ExperienceLabel() { return reinterpret_cast<const char*>(0x0079345C); }    // "brief_get_exp"
const char* GoldLabel() { return reinterpret_cast<const char*>(0x0079343C); }          // "brief_get_money"
const char* ItemLabel() { return reinterpret_cast<const char*>(0x0079342C); }          // "brief_get_item"
const char* LostItemLabel() { return reinterpret_cast<const char*>(0x0079340C); }      // "brief_lost_item"
const char* QuestLabel() { return reinterpret_cast<const char*>(0x007933F8); }         // "brief_get_quest"
const char* QuestDoneLabel() { return reinterpret_cast<const char*>(0x007933E0); }     // "brief_complete_quest"
const char* SpecialsLabel() { return reinterpret_cast<const char*>(0x007933C8); }      // "brief_get_specials_%d"
const char* NumberLine() { return reinterpret_cast<const char*>(0x0079344C); }         // "\x07C... %s %d\n"
const char* NameLine() { return reinterpret_cast<const char*>(0x0079341C); }           // "\x07C... %s %s\n"
const char* TextLine() { return reinterpret_cast<const char*>(0x007933BC); }           // "\x07C... %s\n"
const char* ZonePrefix() { return reinterpret_cast<const char*>(0x00793408); }         // "z"
const char* QuestSection() { return reinterpret_cast<const char*>(0x00790790); }       // "quest "
void* BriefingRecords() { return reinterpret_cast<void*>(0x007C87D4); }   // hash_map<string, BriefingRecord*>
void* ItemManager() { return *reinterpret_cast<void* const*>(0x007AF60C); }
void* UnitGlue() { return reinterpret_cast<void*>(0x007AFEA6); }
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }

// 006023F0: what a speaker's controller calls when its dialogue animation has finished.
using ActionDone = void(__fastcall*)(CUnit*);
ActionDone SpeakerActionDone() { return reinterpret_cast<ActionDone>(0x006023F0); }

constexpr int kStateRewards = 4;
constexpr int kQuestNameParts = 3;
constexpr int kClassQuestItem = 0x14;
constexpr int kSoundPriority = 0x100;
constexpr int kSoundVolume = 100;
constexpr float kSecondsPerCharacter = 0.05f;   // 0074134C
constexpr double kPi = 3.141592653589793;       // 007410C8
constexpr double kDegree = 1.0 / 180.0;         // 0073BA30
// Camera presets (ViewPreset) and the speakers they view.
constexpr int kViewBoth = 1;
constexpr int kViewLeader = 2;
constexpr int kViewPerson = 3;
constexpr int kViewThird = 4;

int Compare(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
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
    CTextManager* texts = GameTexts();
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
                BriefingShowItemOriginal(this, nullptr, words[1], GameAtoi(words[2]) - 1, 0);
            if (count > 2)
                BriefingShowItemOriginal(this, nullptr, words[1], GameAtoi(words[2]) - 1, 1);
            else
                BriefingShowItemOriginal(this, nullptr, words[1], -1, 1);
        } else if (Compare(words[0], HideCommand()) == 0) {
            BriefingHideSpeakerOriginal(this, nullptr, words[1], skip, count > 2 ? 1 : 0);
        } else if (Compare(words[0], AnimationCommand()) == 0) {
            animation = GameAtoi(words[1]);
        } else if (Compare(words[0], CameraCommand()) == 0) {
            if (count >= 5) {
                angle = static_cast<float>(kPi * kDegree * GameAtof(words[2]));
                distance = static_cast<float>(GameAtof(words[3]));
                height = static_cast<float>(GameAtof(words[4]));
                view = true;
            }
            preset = GameAtoi(words[1]);
        } else if (Compare(words[0], PhraseCommand()) == 0) {
            spoke = true;
            break;
        }
    }

    if (spoke) {
        // "#phrase <speaker> [voice]": the speaker's name (its unit's name, else its "pers" text,
        // else the word itself), then the text up to the next '#'.
        auto* unit = static_cast<CUnit*>(GameUnitByIdOriginal(ClientGlue(), nullptr, TextKeyFromName(words[1])));
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
        if (voice_ != -1) sound::Manager()->StopSound(voice_);
        voice_ = -1;
        if (!skip) {
            int number;
            if (words.GetSize() > 2 && (number = GameAtoi(words[2])) > 0) {
                mfc::CString file;
                int folder = gamecrt::rand() * 2 / 0x7FFF % 2;
                file.Format(VoiceFormat(), VoiceFolders()[folder], static_cast<const char*>(topic_), number);
                voice_ = sound::Manager()->Play2D(file, kSoundPriority, kSoundVolume, 0, 0, 0, 1, 1);
            }
            phraseTime_ = static_cast<float>(phrase.GetLength()) * kSecondsPerCharacter;
            if (firstPhrase_) phraseTime_ += 1.0f;
            LogAddTextOriginal(text_, nullptr, phrase);
        }
    } else if (!skip) {
        LogAddTextOriginal(text_, nullptr, phrase);
    }

    if (last) {
        // The rewards of the briefing record of topic_ (the lookup adds an empty entry).
        state_ = kStateRewards;
        BriefingRecord* record;
        {
            stlp::string key(topic_);
            record = *static_cast<BriefingRecord**>(MapRegistryFindOriginal(BriefingRecords(), nullptr, &key));
        }
        if (record) {
            console::CStringArray parts;
            mfc::CString name;
            mfc::CString line;
            mfc::CString label;
            auto add = [&] {
                dialogueText_ += line;
                if (!skip) LogAddTextOriginal(text_, nullptr, line);
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
                    StringSplit(*it, &parts, '.');
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
                if (auto* player = static_cast<WorldPlayer*>(WorldGetPlayer(WorldServer(), nullptr, 0))) {
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
        if (leader_) GameSetUnitHeightOriginal(UnitGlue(), nullptr, leader_->id_, 0.0f);
        if (person_) GameSetUnitHeightOriginal(UnitGlue(), nullptr, person_->id_, 0.0f);
        for (std::size_t i = 0; i < static_cast<std::size_t>(speakersEnd_ - speakers_); ++i) {
            if (Compare(speakers_[i].name, named()) != 0) continue;
            if (view) {
                CUnit* target;
                switch (preset) {
                case kViewLeader: target = leader_; break;
                case kViewPerson: target = person_; break;
                case kViewThird: target = third_; break;
                default:
                    BriefingViewSpeakerOriginal(this, nullptr, nullptr, angle, distance, height);
                    continue;
                }
                if (target) BriefingViewSpeakerOriginal(this, nullptr, target, angle, distance, height);
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
                BriefingViewPresetOriginal(this, nullptr, chosen);
            }
        }
    }

    // The speaker talks, the others listen.
    if (!skip && spoke) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(speakersEnd_ - speakers_); ++i) {
            CUnit* unit = speakers_[i].unit;
            if (Compare(speakers_[i].name, named()) != 0) {
                BriefingSpeakerAnimationOriginal(this, nullptr, unit, 0, 0);
                SetBlink(unit, 0);
                continue;
            }
            if (animation > 0) {
                if (auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(WorldServer(), nullptr, unit->id_))) {
                    auto* controller = reinterpret_cast<UnitController*>(serverUnit->controller);
                    ControllerStopActionOriginal(controller);
                    controller->actionDone = SpeakerActionDone();
                    if (FigureAnimation* played = unit->Object3D()->FindSkillAnimation(animation))
                        ControllerPlayAnimationOriginal(controller, nullptr, played);
                }
            } else {
                BriefingSpeakerAnimationOriginal(this, nullptr, unit, 1, 0);
            }
            SetBlink(unit, lips);
            speaking_ = unit;
        }
    }

    firstPhrase_ = false;
    if (skip && last) {
        // Skipped to the end: the whole text at once, scrolled to its end; everyone listens.
        LogSetTextOriginal(text_, nullptr, dialogueText_);
        text_->CallSetPosition(text_->maxPosition_);
        text_->moved_ = true;
        for (BriefingSpeaker* p = speakers_; p != speakersEnd_; ++p) {
            SetBlink(p->unit, 0);
            BriefingSpeakerAnimationOriginal(this, nullptr, p->unit, 0, 0);
        }
        speaking_ = nullptr;
        ClearShownItems();
    }
}
EI_REPLACE_METHOD(0x00604C50, CBriefingScreen::NextPhrase);
