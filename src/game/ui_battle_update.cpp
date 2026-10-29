// The battle screen's every-frame update, its left button release,
// its quest variable notice, the selection's refresh and the quick
// item panel's unit. See docs/notes/ui.md ("Battle screen").
#include "camera.h"
#include "game_singletons.h"
#include "item.h"
#include "text_manager.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "player_client.h"
#include "player_messages.h"
#include "text_split.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_widescreen.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "world_glue.h"

#include "lib/nan_audit.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <vector>

// --- the quick item panel's unit -----------------------------------------------------------------

void HudQuickItems::SetUnit(CUnit* newUnit) {
    std::string tip;
    unit = newUnit;
    ClearSceneObjects();
    shownCount = 0;
    selected = -1;
    for (int i = 0; i < 8; ++i) {
        regions[i]->SetTip(0);
        regionItem[i] = -1;
    }
    entries.clear();    // (zeroed new entries)
    if (!unit) return;

    // The entry of the sprite just added.
    auto finish = [&](Item* item, bool own) {
        entries.resize(shownCount);
        Entry& entry = entries[shownCount - 1];
        entry.own = own;
        entry.id = item->id;
        entry.item = item;
    };
    // The sprite just added: placed in its row (x from the panels' right column, 695; Rebirth moves it), its
    // region pointing at it, scaled, opaque.
    auto place = [&](int slot) {
        EngineObject* sprite = sceneObjects[shownCount - 1];
        const float right = widescreen::g_panelRight;
        if (slot < 4)
            AsUiMesh(sprite)->Place(right - (slot * 50), 575.0f, 20.0f);
        else
            AsUiMesh(sprite)->Place(right - (slot * 50 - 200), 525.0f, 20.0f);
        int last = shownCount - 1;
        regionItem[slot] = last;
        if (last >= 0 && last < shownCount) {
            EngineObject* shown = sceneObjects[last];
            AsUiMesh(shown)->SetScale(slot < 4 ? 0.55f : 0.85f);
            SceneMesh(last)->flatSpecular = 0xFF000000;
        }
    };

    // The unit's four quick items in the bottom row.
    for (unsigned slot = 0; slot < 4; ++slot) {
        Item* item = unit->GetQuickItem(slot);
        if (!item) continue;
        {
            std::string image = item->FigureName();
            std::string model = item->PictureName();
            AddItem(model, image);
        }
        finish(item, true);
        regions[slot]->SetTipString(ItemTip(slot), static_cast<int>(slot) + 0x2A);
        place(static_cast<int>(slot));
    }

    // The local player's own unit also shows up to four of the player's ItemKind::Quest items above, from
    // the last of the player's items backwards (node: +4 previous, +8 item).
    CPlayer* player = GameLocalView()->player;
    if (!player->IsLeader(unit)) return;
    player = GameLocalView()->player;
    auto node = player->items.rbegin();
    if (node == player->items.rend()) return;
    int slot = 4;
    while (node != player->items.rend()) {
        if (slot - 4 >= 4) break;
        Item* item = *node;
        ++node;
        if (!item || item->Kind() != ItemKind::Quest)
            continue;
        {
            std::string image = item->FigureName();
            std::string model = item->PictureName();
            AddItem(model, image);
        }
        finish(item, false);
        // The same string serves as the second and third texts.
        tip = g_clientWorld.texts->GetItemTexts(item).name;
        regions[slot]->SetTipString(tip);
        place(slot);
        ++slot;
    }
}

// --- the battle screen's update ------------------------------------------------------------------

namespace {

// The unit panel's model view for the unit it now shows, copied from the prototype bit for bit (the size
// from `first`'s, the height from the shown unit's: the same unit), and its name.
void ShowPanelView(HudUnitPanel* panel, CUnit* first) {
    panel->viewSize[0] = first->GetPrototype()->panelViewSize;
    panel->viewHeight[0] = std::bit_cast<float>(panel->shownUnit->GetPrototype()->height);
    panel->strings[0] = panel->owner->UnitName(panel->shownUnit);
}

// A multiplayer client: the party is the session's players' units, the camera's player first.
void RebuildClientParty(BattleScreen* screen) {
    net::ClientSession* session = net::g_gameClient;
    const auto& players = session->players;
    BattleScreen::PartyIds ids;
    ids.camera = -1;
    for (auto& node : players) {
        net::PlayerMessage* player = node.Get();
        const int unitId = player->id;
        const int playerId = player->serial;
        if (player->state == 0 && unitId >= 0) {
            if (!g_clientWorld.FindUnit(unitId)) {
                ids.without.push_back(playerId);
                continue;
            }
            ids.withUnit.push_back(unitId);
            net::BindingMessage* camera = session->binding;
            if (camera && !(camera->flags7 & RefCounted::kDetached) &&
                camera->first.Get() == player)
                ids.camera = unitId;
        } else {
            ids.without.push_back(playerId);
        }
    }
    std::sort(ids.withUnit.begin(), ids.withUnit.end());
    std::sort(ids.without.begin(), ids.without.end());
    if (!ids.Equals(screen->clientParty)) {
        auto* party = &screen->party;
        party->clear();
        bool cameraFirst = false;
        for (auto& id : ids.withUnit) {
            int unitId = id;
            CUnit* unit = g_clientWorld.FindUnit(unitId);
            if (!unit) continue;
            if (unitId == ids.camera) {
                g_clientWorld.Select(unit);
                // Insert at the front.
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
        screen->portraits->Rebuild(cameraFirst);
        if (screen->clientParty.camera != ids.camera) {
            g_camera.SetTargetId(ids.camera);
            screen->RefreshSelection();
        }
        screen->clientParty.withUnit = ids.withUnit;
        screen->clientParty.camera = ids.camera;
        screen->clientParty.without = ids.without;
    }
}

bool SameIds(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.end() - a.begin() != b.end() - b.begin()) return false;
    for (const int *x = a.data(), *y = b.data(); x != a.data() + a.size(); ++x, ++y)
        if (*x != *y) return false;
    return true;
}

// Equal.
bool Equal(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}

} // namespace

void BattleScreen::Update() {
    // The one selected party unit is dead (IsDead): the panels lose their unit and the
    // unit panel shows it.
    auto* currentParty = &party;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* unit = (*currentParty)[i];
        if (unit && (unit->flags & MapObjectFlag::Selected)) {
            if (!first) first = unit;
            ++selected;
        }
    }
    if (first) {
        if (first->IsDead() && selected == 1) {
            weaponBar->SetUnit(nullptr);
            quickItems->SetUnit(nullptr);
            spellColumn->SetUnit(nullptr);
            actionColumn->SetUnit(nullptr, false);
            HudUnitPanel* panel = unitPanel;
            if (first != panel->shownUnit) {
                panel->shownUnit = first;
                ShowPanelView(panel, first);
            }
        }
    }

    if (IsMultiplayerClient()) RebuildClientParty(this);

    UiContainer::Update();
    ui->ShowMessage(false);

    // A zone exit saved with the game: once it can be taken, the "leave_zone" question.
    if (exitPending) {
        // The local player's unit records (0x784 bytes each) and the ones in the exit.
        std::vector<PartyUnitRecord*> records = {};
        std::vector<PartyUnitRecord*> second = {};
        CParty* data = LocalPlayerData();
        for (int i = 0; i < std::ssize(data->members); ++i)
            lib::SetAtGrow(records, std::ssize(records), &data->members[i]);
        if (GamePartyInExit(&records, pendingExit, &second)) {
            MessageWindow* box = new MessageWindow(ui, "leave_zone", "leave_zone_msg", 0x10000, 3, 1);
            ui->PushWindow(box, true, true);
            exitPending = false;
        }
    }

    // The selection box: party units inside it are marked (MapObjectFlag::BoxMarked and their figure's select virtual
    // with 1), the others unmarked (with 0); selected ones are left alone.
    if (boxSelecting) {
        std::list<CMapObject*> units = g_clientWorld.UnitsInBox(&selectionBox);
        std::list<std::uintptr_t> inside;
        for (CMapObject* unit : units) {
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                if ((*currentParty)[i] != unit) continue;
                inside.push_back(static_cast<std::uintptr_t>(i));
            }
        }
        auto mark = [](CUnit* unit, bool on) { unit->Object3D()->attached->SetLit(on); };
        for (int i = 0; i < std::ssize(*currentParty); ++i) {
            auto* unit = (*currentParty)[i];
            std::uint32_t unitFlags = unit->flags;
            if (!(unitFlags & MapObjectFlag::Selected) && (unitFlags & MapObjectFlag::BoxMarked)) {
                unit->flags = unitFlags & ~MapObjectFlag::BoxMarked;
                mark(unit, 0);
            }
        }
        for (auto& node : inside) {
            auto* unit = (*currentParty)[node];
            std::uint32_t unitFlags = unit->flags;
            if (!(unitFlags & MapObjectFlag::Selected) && !(unitFlags & MapObjectFlag::BoxMarked)) {
                unit->flags = unitFlags | MapObjectFlag::BoxMarked;
                mark(unit, 1);
            }
        }
    }

    // The quest sound asked for.
    if (questSound != 0) {
        const char* sound = nullptr;
        switch (questSound) {
        case 1: sound = "buttons\\quest.wav"; break;
        case 2: sound = "buttons\\subcomplete.wav"; break;
        case 3: sound = "buttons\\complete.wav"; break;
        }
        if (sound) ui->PlaySound(sound);
        questSound = 0;
    }
}

// --- the party ids compared ----------------------------------------------------------------------

bool BattleScreen::PartyIds::Equals(const BattleScreen::PartyIds& other) const {
    return SameIds(withUnit, other.withUnit) && SameIds(without, other.without);
}

// --- the battle screen's left button release -----------------------------------------------------

void BattleScreen::LeftUp(int buttons, int x, int y) {
    if (portraitMode != 0) return;
    if (!viewPressed) {
        UiContainer::LeftUp(buttons, x, y);
        return;
    }
    viewPressed = false;
    WorldClient* world = &g_clientWorld;
    if (!boxSelecting || IsMultiplayerClient()) {
        // The (x, y) pair is read back through its address.
        POINT point = {x, y};
        CUnit* unit = world->UnitAt(&point);
        if (unit && !(unit->flags & MapObjectFlag::InParty)) unit = nullptr;
        SelectUnit(unit, point.x, point.y);
        return;
    }

    auto* currentParty = &party;
    auto unitAt = [&](int i) { return (*currentParty)[i]; };
    int first = -1;
    std::list<CMapObject*> units = world->UnitsInBox(&selectionBox);
    std::list<std::uintptr_t> inside;
    if (!units.empty()) {
        for (CMapObject* unit : units) {
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                if ((*currentParty)[i] != unit) continue;
                inside.push_back(static_cast<std::uintptr_t>(i));
                if (first < 0) first = i;
            }
        }
        if (first >= 0) {
            world->Deselect();
            // Portrait i's selection marker (scene object 5 i + 4) and face state.
            auto marker = [&](int i) { return portraits->SceneMesh(i * 5 + 4); };
            auto slot = [&](int i) -> HudPortraitSlot& { return portraits->slots[i]; };
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                CUnit* unit = unitAt(i);
                std::uint32_t unitFlags = unit->flags;
                if (!(unitFlags & MapObjectFlag::Selected) && (unitFlags & MapObjectFlag::BoxMarked)) {
                    unit->flags = unitFlags & ~MapObjectFlag::BoxMarked;
                    unit->Object3D()->attached->SetLit(false);
                }
                marker(i)->visible = false;
                slot(i).restB = kHudRestRotation;
            }
            for (auto& node : inside) {
                int i = static_cast<int>(node);
                marker(i)->visible = true;
                const Vector3 axis = {-1.0f, 0.0f, 0.0f};
                const float angle = kHudQuarterTurn *
                                    0.14285715f;   // 1/7
                Quaternion turn;
                turn.SetAxisAngle(axis, angle);
                slot(i).restB = turn;
                world->Select(unitAt(static_cast<int>(node)));
            }
            int unitId = unitAt(first)->id;
            Speech(unitId, AckKind::Selected, nullptr);
        }
    }
    RefreshSelection();
    boxSelecting = false;
}

// --- a quest variable's notice -------------------------------------------------------------------

void BattleScreen::QuestVariable(std::string_view name, float value) {
    TextManager* texts = g_clientWorld.texts;
    std::vector<std::string> parts;
    std::string text;
    std::string title;
    parts = text::Split(name, '.');
    const float complete = 2.0f;
    const float failed = 3.0f;
    if (parts.size() == 3) {
        const char* key;
        if (Equal(value, complete)) {
            if (questSound <= 3) questSound = 3;
            key = "combat_complete_quest";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_quest";
        } else {
            key = "combat_get_quest";
        }
        title = texts->TextManager::GetString(key);
        text.clear();
        if (texts->Open(std::string("quest ") + parts[2])) text = texts->ReadLine(true);
        std::string colour("\007C\356\343" "1 ");   // colour code 0x31E3EE43 and a space
        text = colour + title + " " + text;
    } else if (parts.size() == 4) {
        if (!IsMultiplayerClient()) {
            const char* dot = ".";
            std::string variable = parts[0] + dot + parts[1] + dot + parts[2];
            float current = LocalPlayerVariable(variable);
            // The unrounded result compared with 0.
            EI_NAN_AUDIT(current);
            if (current == 0.0) return;
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
        title = texts->TextManager::GetString(key);
        text.clear();
        bool found = false;
        {
            std::string quest("quest ");
            std::string textKey = quest + parts[2];
            if (texts->Open(textKey)) {
                std::string sub("#subobj ");
                std::string lineKey = sub + parts[3];
                found = texts->FindLine(lineKey) != 0;
            }
        }
        if (found) {
            texts->NextLine();
            text = texts->ReadLine(true);
        }
        std::string colour("\007C\377\263" "1 ");   // colour code 0x31B3FF43 and a space
        text = colour + title + " " + text;
    }
    text = text + "\007C\344\327\247\n";   // colour code and a line end

    topPanel->pageTexts[0] += text;
    if (topPanel->page == 0) messageLog->AddText(text);
    clock->highlight = true;
    int sound = static_cast<int>(value);
    if (questSound > sound) sound = questSound;
    questSound = sound;
}

// --- the selection's refresh ---------------------------------------------------------------------

void BattleScreen::RefreshSelection() {
    auto* currentParty = &party;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* unit = (*currentParty)[i];
        if (!unit || !(unit->flags & MapObjectFlag::Selected)) continue;
        if (!first) first = unit;
        ++selected;
    }
    // Exactly one living unit selected: the panels show it; otherwise they are emptied. The action
    // column's extra button stays while anyone is selected (the flag is pushed with
    // upper bytes left over; the callee reads the low byte only).
    CUnit* shown = (first && first->IsDead() == 0 && selected == 1) ? first : nullptr;
    weaponBar->SetUnit(shown);
    quickItems->SetUnit(shown);
    spellColumn->SetUnit(shown);
    actionColumn->SetUnit(shown, selected > 0);

    // The unit panel shows the first selected unit (also a dead one, or the first of several).
    HudUnitPanel* panel = unitPanel;
    if (first != panel->shownUnit) {
        panel->shownUnit = first;
        if (first) ShowPanelView(panel, first);
    }

    // No pending action; every highlight cleared.
    SetPendingAction(kPendingNone, -1, -1);
}
