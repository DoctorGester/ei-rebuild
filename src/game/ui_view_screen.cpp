// Interface screen 7 (ui_view_screen.h): its Create, draw and close slots and the picked units'
// input side. See docs/notes/ui.md, section 4.
#include "ui_camera_keys.h"
#include "ui_view_screen.h"

#include "camera.h"
#include "maths.h"
#include "player_client.h"
#include "ui_cursor.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>

namespace {

// An extra input side whose slots do nothing (vtable 00741154).
void** InertInputVtable() { return ei::GamePointer<void*>(0x00741154); }
// The orders' unit list (CArray<CUnit*>, vtable 007402AC; SetSize 005EBDB0).
void* OrderUnitsVtable() { return ei::GamePointer<void>(0x007402AC); }
// CDWordArray's vtable (0073C3D0).
void* DwordArrayVtable() { return ei::GamePointer<void>(0x0073C3D0); }

constexpr int kVkControl = 0x11;
constexpr int kVkSpace = 0x20;
constexpr int kCursorNormal = 0;
constexpr int kCursorCtrl = 1;
constexpr int kAttackHow = 6;

// The view's turn: 225 degrees (pi 00741F30 * 1/180 0073BA30 * 225 00741F48, formed at run time
// as the original's FMULs and stored as float) about x.
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
        units.SetSize(1, -1);
        units.data[0] = unit;
    }
    ~OneUnit() {
        units.vtable = OrderUnitsVtable();
        units.FreeData();
    }
    mfc::CArray<CUnit*> units{OrderUnitsVtable(), nullptr, 0, 0, 0};
};

} // namespace

// --- CViewScreen (slots 25, 20, 26) -------------------------------------------------------------

void CViewScreen::Build() {
    static_assert(sizeof(UiInputSide) == 0x0C);
    auto* inert = static_cast<UiInputSide*>(gamecrt::operator_new(sizeof(UiInputSide)));
    if (inert) {
        inert->vtable_ = InertInputVtable();
        inert->owner_ = interface_;
    }
    AddInput(inert);
    void* memory = gamecrt::operator_new(sizeof(UnitPickInput));
    AddInput(memory ? static_cast<UnitPickInput*>(memory)->Construct(interface_) : nullptr);
    memory = gamecrt::operator_new(0x2C);
    AddInput(memory ? reinterpret_cast<UiInputSide*>(static_cast<CCameraInput*>(memory)->Construct(interface_)) : nullptr);

    CCamera* camera = TheCamera();
    camera->flags_ &= ~CCamera::kFree;
    Vector3 focus{30.0f, 30.0f, 0.0f};
    camera->SetFocus(&focus);
    Vector3 axis{1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAngleAxis(TurnAngle(), axis);
    camera->localRotation_.PreMultiply(turn);
}
EI_REPLACE_METHOD(0x0062A830, CViewScreen::Build);

BOOL CViewScreen::DrawView(int) {
    GameRender();
    return TRUE;
}
EI_REPLACE_METHOD(0x0062AA50, CViewScreen::DrawView);

void CViewScreen::OnClose() { TheCamera()->flags_ |= CCamera::kFree; }
EI_REPLACE_METHOD(0x0062AA40, CViewScreen::OnClose);

// --- UnitPickInput ------------------------------------------------------------------------------

UnitPickInput* UnitPickInput::Construct(CInterface3D* ui) {
    owner_ = ui;
    picked_ = UiDwordArray{DwordArrayVtable(), nullptr, 0, 0, 0};
    vtable_ = Vtable();
    return this;
}
EI_REWRITTEN(0x005EB8C0, UnitPickInput::Construct);

void UnitPickInput::Destruct() {
    picked_.vtable = DwordArrayVtable();
    if (picked_.data) gamecrt::operator_delete(picked_.data);
    vtable_ = BaseVtable();
}
EI_REWRITTEN(0x005EB910, UnitPickInput::Destruct);

UnitPickInput* UnitPickInput::DeleteThis(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005EB8F0, UnitPickInput::DeleteThis);

void UnitPickInput::OnLeftDown(int, int x, int y) {
    CWorldClient* world = TheClientWorld();
    const int point[2] = {x, y};
    CEngineObject* unit = world->UnitAt2(reinterpret_cast<const long*>(point));
    if (!static_cast<CInterface3D*>(owner_)->ctrlDown_) {
        if (unit) {
            int n = picked_.size;
            picked_.SetSize(n + 1, -1);
            picked_.data[n] = static_cast<std::uint32_t>(unit->id_);
            world->Select(unit);
            return;
        }
        Vector3 ground;
        world->PickGround(&ground, x, y);
        for (int i = 0; i < picked_.size; ++i) {
            auto* member = GameFindClientUnit(picked_.data[i]);
            if (!member) continue;
            OneUnit order(member);
            world->LocalView()->SendWalk(&order.units, &ground, 0);
        }
    } else if (unit) {
        for (int i = 0; i < picked_.size; ++i) {
            auto* member = GameFindClientUnit(picked_.data[i]);
            if (!member) continue;
            OneUnit order(member);
            world->LocalView()->SendAttack(&order.units, unit, kAttackHow, 0);
        }
    }
}
EI_REPLACE_METHOD(0x005EB940, UnitPickInput::OnLeftDown);

void UnitPickInput::OnRightDown(int, int, int) {
    if (picked_.data) {
        gamecrt::operator_delete(picked_.data);
        picked_.data = nullptr;
    }
    picked_.maxSize = 0;
    picked_.size = 0;
    TheClientWorld()->Deselect(nullptr);
}
EI_REPLACE_METHOD(0x005EBB40, UnitPickInput::OnRightDown);

void UnitPickInput::OnKeyDown(int key, int, int) {
    if (key == kVkControl) {
        TheCursor()->SetShape(kCursorCtrl);
    } else if (key == kVkSpace) {
        GameSetServerPaused(GameServerPaused() == 0);
    }
}
EI_REPLACE_METHOD(0x005EBB80, UnitPickInput::OnKeyDown);

void UnitPickInput::OnKeyUp(int key, int, int) {
    if (key == kVkControl) TheCursor()->SetShape(kCursorNormal);
}
EI_REPLACE_METHOD(0x005EBBC0, UnitPickInput::OnKeyUp);
