// The battle screen's every-frame update, its left button release,
// its quest variable notice, the selection's refresh and the quick
// item panel's unit. See docs/notes/ui.md ("Battle screen").
#include "camera.h"
#include "item.h"
#include "player_client.h"
#include "player_messages.h"
#include "item_db_text.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "net_classes.h"
#include "net_messages.h"
#include "text_reader.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_widescreen.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "text_split.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <algorithm>
#include <bit>
#include <list>
#include <memory>
#include <vector>
#include "party.h"
#include "world.h"
#include "world_glue.h"
#include "game_singletons.h"

// --- the quick item panel's unit -----------------------------------------------------------------

void CHudQuickItems::SetUnit(CUnit* unit) {
    std::string tip;
    std::string unused;
    unit_ = unit;
    ClearSceneObjects();
    shownCount_ = 0;
    selected_ = -1;
    for (int i = 0; i < 8; ++i) {
        regions_[i]->SetTip(0, 1, 0);
        regionItem_[i] = -1;
    }
    entries_.clear();   // (zeroed new entries)
    if (!unit_) return;

    // The entry of the sprite just added.
    auto finish = [&](CItem* item, bool own) {
        entries_.resize(shownCount_);
        Entry& entry = entries_[shownCount_ - 1];
        entry.own = own;
        entry.id = item->id_;
        entry.item = item;
    };
    // The sprite just added: placed in its row (x from the panels' right column, 695; Rebirth moves it), its
    // region pointing at it, scaled, opaque.
    auto place = [&](int slot) {
        CEngineObject* sprite = sceneObjects_[shownCount_ - 1];
        const float right = widescreen::g_panelRight;
        if (slot < 4)
            AsUiMesh(sprite)->PlaceBody(right - static_cast<float>(slot * 50), 575.0f, 20.0f);
        else
            AsUiMesh(sprite)->PlaceBody(right - static_cast<float>(slot * 50 - 200), 525.0f, 20.0f);
        int last = shownCount_ - 1;
        regionItem_[slot] = last;
        if (last >= 0 && last < shownCount_) {
            CEngineObject* shown = sceneObjects_[last];
            AsUiMesh(shown)->SetScale(slot < 4 ? 0.55f : 0.85f);
            SceneMesh(last)->flatSpecular_ = 0xFF000000;
        }
    };

    // The unit's four quick items in the bottom row.
    for (unsigned slot = 0; slot < 4; ++slot) {
        CItem* item = unit_->GetQuickItem(slot);
        if (!item) continue;
        {
            std::string image = item->FigureName();
            std::string model = item->PictureName();
            AddItem(model.c_str(), image.c_str());
        }
        finish(item, true);
        ItemTip(slot, &tip);
        regions_[slot]->SetTipString(tip, 1, static_cast<int>(slot) + 0x2A);
        place(static_cast<int>(slot));
    }

    // The local player's own unit also shows up to four of the player's 0x3009 items above, from
    // the last of the player's items backwards (node: +4 previous, +8 item).
    CPlayer* player = GameLocalView()->player_;
    if (!player->IsLeader(unit_)) return;
    player = GameLocalView()->player_;
    auto node = player->items_.rbegin();
    if (node == player->items_.rend()) return;
    int slot = 4;
    while (node != player->items_.rend()) {
        if (slot - 4 >= 4) break;
        CItem* item = *node;
        ++node;
        if (!item || item->type_ != kItemQuest)
            continue;
        {
            std::string image = item->FigureName();
            std::string model = item->PictureName();
            AddItem(model.c_str(), image.c_str());
        }
        finish(item, false);
        // The same CString serves as the second and third texts.
        TheTextManager()->GetItemTexts(item, &tip, &unused, &unused);
        regions_[slot]->SetTipString(tip, 1, 0);
        place(slot);
        ++slot;
    }
}

// --- the battle screen's update ------------------------------------------------------------------

namespace {

// A unit's prototype record.
static const UnitPrototypeRecord* UnitPrototype(CUnit* unit) {
    return unit->GetPrototype();
}
// The unit panel's model view for the unit it now shows, copied from the prototype as dwords: the
// size from `first`'s, the height from the shown unit's (+0x24; the same unit).
void ShowPanelView(CHudUnitPanel* panel, CUnit* first) {
    std::memcpy(&panel->viewSize_[0], &UnitPrototype(first)->panelViewSize, 4);
    std::memcpy(&panel->viewHeight_[0], &UnitPrototype(panel->shownUnit_)->height, 4);
}

// The children's update (CUiContainer::UpdateBase).

// push_back of `*value`: the constructing helper, or the reallocating insert when full.
void PushId(std::vector<int>* ids, const int* value) {
    ids->push_back(*value);
}
void PushIdInline(std::vector<int>* ids, const int* value) {
    ids->push_back(*value);
}

void SortIds(std::vector<int>* ids) { std::sort(ids->begin(), ids->end()); }

// A multiplayer client: the party is the session's players' units, the camera's player first.
void RebuildClientParty(CBattleScreen* screen) {
    net::ClientSession* session = net::g_gameClient;
    const auto& players = session->players_;
    CBattleScreen::PartyIds ids;
    ids.camera = -1;
    for (auto node = players.begin(); node != players.end(); ++node) {
        net::PlayerMessage* player = node->Get();
        int* unitId = &player->id_;
        int* playerId = &player->serial_;
        if (player->state_ == 0 && *unitId >= 0) {
            if (!g_clientWorld->FindUnit(*unitId)) {
                PushId(&ids.without, playerId);
                continue;
            }
            PushId(&ids.withUnit, unitId);
            net::BindingMessage* camera = session->binding_;
            if (camera && !(camera->flags7_ & CRefCounted::kDetached) &&
                camera->first_.ptr == player)
                ids.camera = *unitId;
        } else {
            PushIdInline(&ids.without, playerId);
        }
    }
    SortIds(&ids.withUnit);
    SortIds(&ids.without);
    if (!ids.Equals(screen->clientParty_)) {
        auto* party = &screen->party_;
        party->clear();
        bool cameraFirst = false;
        for (int* id = ids.withUnit.data(); id != ids.withUnit.data() + ids.withUnit.size(); ++id) {
            int unitId = *id;
            CUnit* unit = g_clientWorld->FindUnit(unitId);
            if (!unit) continue;
            if (unitId == ids.camera) {
                g_clientWorld->Select(unit);
                // Insert at the front (InsertAt).
                int n = std::ssize(*party);
                if (n <= 0) {
                    party->resize(1);
                } else {
                    party->resize(n + 1);
                    std::memmove(party->data() + 1, party->data(), static_cast<std::size_t>(n) * 4);

                }
                (*party)[0] = unit;
                cameraFirst = true;
            } else {
                lib::SetAtGrow(*party, std::ssize(*party), unit);
            }
        }
        screen->portraits_->Rebuild(cameraFirst ? 1 : 0);
        if (screen->clientParty_.camera != ids.camera) {
            g_camera->SetTargetId(ids.camera);
            screen->RefreshSelection();
        }
        screen->clientParty_.withUnit = ids.withUnit;
        screen->clientParty_.camera = ids.camera;
        screen->clientParty_.without = ids.without;
    }
}

bool SameIds(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.end() - a.begin() != b.end() - b.begin()) return false;
    for (const int *x = a.data(), *y = b.data(); x != a.data() + a.size(); ++x, ++y)
        if (*x != *y) return false;
    return true;
}

// FCOMP; TEST AH,0x40: equal.
bool Equal(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}

} // namespace

void CBattleScreen::Update() {
    // The one selected party unit is dead (IsDead): the panels lose their unit and the
    // unit panel shows it.
    auto* party = &party_;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < std::ssize(*party); ++i) {
        auto* unit = (*party)[i];
        if (unit && (unit->flags_ & 0x10)) {
            if (!first) first = unit;
            ++selected;
        }
    }
    if (first) {
        if (first->IsDead() && selected == 1) {
            weaponBar_->SetUnit(nullptr);
            quickItems_->SetUnit(nullptr);
            spellColumn_->SetUnit(nullptr);
            actionColumn_->SetUnit(nullptr, 0);
            CHudUnitPanel* panel = unitPanel_;
            if (first != panel->shownUnit_) {
                panel->shownUnit_ = first;
                ShowPanelView(panel, first);
                panel->owner_->UnitName(panel->shownUnit_, &panel->strings_[0]);
            }
        }
    }

    if (IsMultiplayerClient()) RebuildClientParty(this);

    UpdateBase();
    interface_->ShowMessage(0);

    // A zone exit saved with the game: once it can be taken, the "leave_zone" question.
    if (exitPending_) {
        // The local player's unit records (0x784 bytes each) and the ones in the exit.
        std::vector<PartyUnitRecord*> records = {};
        std::vector<PartyUnitRecord*> second = {};
        CParty* data = LocalPlayerData();
        for (int i = 0; i < std::ssize(data->members_); ++i)
            lib::SetAtGrow(records, std::ssize(records), &data->members_[i]);
        if (GamePartyInExit(&records, pendingExit_, &second)) {
            CMessageBox* box = new CMessageBox(interface_, "leave_zone", "leave_zone_msg", 0x10000, 3, 1);
            interface_->PushWindow(box, 1, 1);
            exitPending_ = 0;
        }
    }

    // The selection box: party units inside it are marked (flag 0x100 and their figure's slot 26
    // with 1), the others unmarked (with 0); dead ones (0x10) are left alone.
    if (boxSelecting_) {
        std::list<CMapObject*> units;
        g_clientWorld->UnitsInBox(&selectionBox_, &units);
        std::list<std::uintptr_t> inside;
        for (CMapObject* unit : units) {
            for (int i = 0; i < std::ssize(*party); ++i) {
                if ((*party)[i] != unit) continue;
                inside.push_back(static_cast<std::uintptr_t>(i));
            }
        }
        auto mark = [](CUnit* unit, int on) { unit->Object3D()->attached->SetLit(on); };
        for (int i = 0; i < std::ssize(*party); ++i) {
            auto* unit = (*party)[i];
            std::uint32_t flags = unit->flags_;
            if (!(flags & 0x10) && (flags & 0x100)) {
                unit->flags_ = flags & ~0x100u;
                mark(unit, 0);
            }
        }
        for (std::list<std::uintptr_t>::iterator node = inside.begin(); node != inside.end(); ++node) {
            auto* unit = (*party)[(*node)];
            std::uint32_t flags = unit->flags_;
            if (!(flags & 0x10) && !(flags & 0x100)) {
                unit->flags_ = flags | 0x100;
                mark(unit, 1);
            }
        }
    }

    // The quest sound asked for.
    if (questSound_ != 0) {
        const char* sound = nullptr;
        switch (questSound_) {
        case 1: sound = "buttons\\quest.wav"; break;
        case 2: sound = "buttons\\subcomplete.wav"; break;
        case 3: sound = "buttons\\complete.wav"; break;
        }
        if (sound) interface_->PlaySound(sound, 0);
        questSound_ = 0;
    }
}

// --- the party ids compared ----------------------------------------------------------------------

bool CBattleScreen::PartyIds::Equals(const CBattleScreen::PartyIds& other) const {
    return SameIds(withUnit, other.withUnit) && SameIds(without, other.without);
}

// --- the battle screen's left button release -----------------------------------------------------

void CBattleScreen::OnLButtonUp(int buttons, int x, int y) {
    if (portraitMode_ != 0) return;
    if (!viewPressed_) {
        CUiContainer::LeftUp(buttons, x, y);
        return;
    }
    viewPressed_ = false;
    CWorldClient* world = &g_clientWorld.Get();
    if (!boxSelecting_ || IsMultiplayerClient()) {
        // The (x, y) pair is read back through its address.
        POINT point = {x, y};
        CUnit* unit = world->UnitAt(&point);
        if (unit && !(unit->flags_ & 0x400000)) unit = nullptr;
        SelectUnit(unit, point.x, point.y);
        return;
    }

    auto* party = &party_;
    auto unitAt = [&](int i) { return (*party)[i]; };
    int first = -1;
    std::list<CMapObject*> units;
    world->UnitsInBox(&selectionBox_, &units);
    std::list<std::uintptr_t> inside;
    if (!units.empty()) {
        for (CMapObject* unit : units) {
            for (int i = 0; i < std::ssize(*party); ++i) {
                if ((*party)[i] != unit) continue;
                inside.push_back(static_cast<std::uintptr_t>(i));
                if (first < 0) first = i;
            }
        }
        if (first >= 0) {
            world->Deselect(nullptr);
            // Portrait i's selection marker (scene object 5 i + 4) and face state.
            auto marker = [&](int i) { return portraits_->SceneMesh(i * 5 + 4); };
            auto slot = [&](int i) -> HudPortraitSlot& { return portraits_->slots_[i]; };
            for (int i = 0; i < std::ssize(*party); ++i) {
                CUnit* unit = unitAt(i);
                std::uint32_t flags = unit->flags_;
                if (!(flags & 0x10) && (flags & 0x100)) {
                    unit->flags_ = flags & ~0x100u;
                    unit->Object3D()->attached->SetLit(0);
                }
                marker(i)->visible_ = 0;
                slot(i).restB_ = kHudRestRotation;
            }
            for (std::list<std::uintptr_t>::iterator node = inside.begin(); node != inside.end(); ++node) {
                int i = static_cast<int>((*node));
                marker(i)->visible_ = 1;
                const Vector3 axis = {-1.0f, 0.0f, 0.0f};
                const float angle = kHudQuarterTurn *
                                    0.14285715f /*0073CDCC*/;   // 1/7
                Quaternion turn;
                turn.SetAxisAngle(axis, angle);
                slot(i).restB_ = turn;
                world->Select(unitAt(static_cast<int>((*node))));
            }
            int id = unitAt(first)->id_;
            Speech(id, 0, nullptr);
        }
    }
    RefreshSelection();
    boxSelecting_ = false;
}

// --- a quest variable's notice -------------------------------------------------------------------

void CBattleScreen::OnQuestVariable(std::string_view name, float value) {
    CTextManager* texts = GameClientTexts();
    std::vector<std::string> parts;
    std::string text;
    std::string title;
    text::Split(name, &parts, '.');
    const float complete = 2.0f /*0073CDB8*/;
    const float failed = 3.0f /*0073BAA4*/;
    if (parts.size() == 3) {
        const char* key;
        if (Equal(value, complete)) {
            if (questSound_ <= 3) questSound_ = 3;
            key = "combat_complete_quest";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_quest";
        } else {
            key = "combat_get_quest";
        }
        texts->CTextManager::GetString(key, &title);
        text.clear();
        BOOL found;
        {
            std::string quest("quest ");
            std::string textKey = quest + parts[2];
            found = texts->Open(textKey.c_str());
        }
        if (found) texts->ReadLine(&text, 1);
        std::string colour("\007C\356\343" "1 ");   // colour code 0x31E3EE43 and a space
        text = colour + title + " " + text;
    } else if (parts.size() == 4) {
        if (!IsMultiplayerClient()) {
            const char* dot = ".";
            std::string variable = parts[0] + dot + parts[1] + dot + parts[2];
            double current = LocalPlayerVariable(variable);
            // FCOMP of the unrounded result with 0; TEST AH,0x40.
            EI_NAN_AUDIT(current);
            if (current == static_cast<double>(0.0f /*0073B858*/)) return;
        }
        const char* key;
        if (Equal(value, complete)) {
            key = "combat_complete_subobj";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_subobj";
        } else {
            key = "combat_get_subobj";
        }
        texts->CTextManager::GetString(key, &title);
        text.clear();
        bool found = false;
        {
            std::string quest("quest ");
            std::string textKey = quest + parts[2];
            if (texts->Open(textKey.c_str())) {
                std::string sub("#subobj ");
                std::string lineKey = sub + parts[3];
                found = texts->FindLine(lineKey.c_str()) != 0;
            }
        }
        if (found) {
            TextNextLine(texts);
            texts->ReadLine(&text, 1);
        }
        std::string colour("\007C\377\263" "1 ");   // colour code 0x31B3FF43 and a space
        text = colour + title + " " + text;
    }
    text = text + "\007C\344\327\247\n";   // colour code and a line end

    CHudTopPanel* top = topPanel_;
    const char* line = text.c_str();
    top->pageTexts_[0] += line;
    if (top->page_ == 0) {
        CBattleScreen* screen = top->owner_;
        screen->MessageLog()->AddText(line);
    }
    clock_->highlight_ = true;
    int sound = static_cast<int>(value);
    if (questSound_ > sound) sound = questSound_;
    questSound_ = sound;
}

// --- the selection's refresh ---------------------------------------------------------------------

void CBattleScreen::RefreshSelection() {
    auto* party = &party_;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < std::ssize(*party); ++i) {
        auto* unit = (*party)[i];
        if (!unit || !(unit->flags_ & 0x10)) continue;
        if (!first) first = unit;
        ++selected;
    }
    // Exactly one living unit selected: the panels show it; otherwise they are emptied. The action
    // column's extra button stays while anyone is selected (the flag is pushed with
    // upper bytes left over; the callee reads the low byte only).
    CUnit* shown = (first && first->IsDead() == 0 && selected == 1) ? first : nullptr;
    weaponBar_->SetUnit(shown);
    quickItems_->SetUnit(shown);
    spellColumn_->SetUnit(shown);
    actionColumn_->SetUnit(shown, selected > 0 ? 1 : 0);

    // The unit panel shows the first selected unit (also a dead one, or the first of several).
    CHudUnitPanel* panel = unitPanel_;
    if (first != panel->shownUnit_) {
        panel->shownUnit_ = first;
        if (first) {
            ShowPanelView(panel, first);
            panel->owner_->UnitName(panel->shownUnit_, &panel->strings_[0]);
        }
    }

    // No pending action; every highlight cleared.
    pendingSlot_ = -1;
    pendingAction_ = 0;
    pendingItem_ = -1;
    CHudQuickItems* quick = quickItems_;
    for (int i = 0; i < 8; ++i) quick->Highlight(i, 0);
    quick->selected_ = -1;
    CHudSpellColumn* spells = spellColumn_;
    for (int i = 0; i < spells->count_; ++i) spells->Highlight(i, 0);
    spells->selected_ = -1;
    CHudActionColumn* actions = actionColumn_;
    for (int i = 0; i < actions->count_; ++i) actions->Highlight(i, 0);
    actions->selected_ = -1;
}
