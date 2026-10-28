// Interface screen 7 (ui_view_screen.h): its Create, draw and close slots and the picked units'
// input side. See docs/notes/ui.md, section 4.
#include "ui_camera_keys.h"
#include "ui_view_screen.h"

#include "camera.h"
#include "maths.h"
#include "player_client.h"
#include "ui_cursor.h"
#include "world.h"

#include <cstdint>
#include "world_glue.h"
#include "game_singletons.h"

#include <vector>

namespace {

constexpr int kVkControl = 0x11;
constexpr int kVkSpace = 0x20;
constexpr int kCursorNormal = 0;
constexpr int kCursorCtrl = 1;
constexpr int kAttackHow = 6;

// The view's turn: 225 degrees (pi * 1/180 * 225, formed at run time as the original's FMULs and stored as
// float) about x.
float TurnAngle() {
    volatile double pi = 3.141592653589793;
    volatile double perDegree = 1.0 / 180.0;
    volatile double degrees = 225.0;
    return static_cast<float>(pi * perDegree * degrees);
}

// A one-unit order list as the original builds it on the stack: SetSize(1), the unit stored.
// Freed with Release (the vtable written back, the data deleted).
struct OneUnit {
    explicit OneUnit(CUnit* unit) {
        units.resize(1);
        units[0] = unit;
    }
    ~OneUnit() {
    }
    std::vector<CUnit*> units{};
};
} // namespace

// --- CViewScreen -------------------------------------------------------------

void CViewScreen::CreateScreen() {
    UiInputSide* inert = new UiInertInput(interface_);
    AddInput(inert);
    AddInput(new UnitPickInput(interface_));
    AddInput(new CCameraInput(interface_));

    CCamera* camera = TheCamera();
    camera->flags_ &= ~CCamera::kFree;
    Vector3 focus{30.0f, 30.0f, 0.0f};
    camera->SetFocus(&focus);
    Vector3 axis{1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAngleAxis(TurnAngle(), axis);
    camera->localRotation_.PreMultiply(turn);
}

BOOL CViewScreen::DrawView(CSceneRenderer*) {
    GameRender();
    return TRUE;
}

void CViewScreen::OnClose() { TheCamera()->flags_ |= CCamera::kFree; }

// --- UnitPickInput ------------------------------------------------------------------------------

UnitPickInput::UnitPickInput(CInterface3D* ui) : UiInputSide(ui) {
    picked_ = UiDwordArray{};
}

UnitPickInput::~UnitPickInput() = default;

void UnitPickInput::OnLeftDown(int, int x, int y) {
    CWorldClient* world = TheClientWorld();
    const POINT point = {x, y};
    CUnit* unit = world->UnitAt2(&point);
    if (!interface_->ctrlDown_) {
        if (unit) {
            int n = std::ssize(picked_);
            picked_.resize(n + 1);
            picked_[n] = static_cast<std::uint32_t>(unit->id_);
            world->Select(unit);
            return;
        }
        Vector3 ground;
        world->PickGround(&ground, x, y);
        for (int i = 0; i < std::ssize(picked_); ++i) {
            auto* member = GameFindClientUnit(picked_[i]);
            if (!member) continue;
            OneUnit order(member);
            world->LocalView()->SendWalk(&order.units, &ground, 0);
        }
    } else if (unit) {
        for (int i = 0; i < std::ssize(picked_); ++i) {
            auto* member = GameFindClientUnit(picked_[i]);
            if (!member) continue;
            OneUnit order(member);
            world->LocalView()->SendAttack(&order.units, unit, kAttackHow, 0);
        }
    }
}

void UnitPickInput::OnRightDown(int, int, int) {
    lib::FreeStorage(picked_);
    TheClientWorld()->Deselect(nullptr);
}

void UnitPickInput::OnKeyDown(int key, int, int) {
    if (key == kVkControl) {
        TheCursor()->SetShape(kCursorCtrl);
    } else if (key == kVkSpace) {
        GameSetServerPaused(GameServerPaused() == 0);
    }
}

void UnitPickInput::OnKeyUp(int key, int, int) {
    if (key == kVkControl) TheCursor()->SetShape(kCursorNormal);
}
