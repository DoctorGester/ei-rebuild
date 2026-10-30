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
#include "lib/containers.h"
#include "platform/keys.h"

#include <vector>

namespace {

constexpr int kCursorNormal = 0;
constexpr int kCursorCtrl = 1;
constexpr int kAttackHow = 6;

// The view's turn about x: 225 degrees, the original's pi * (1 / 180) * 225 in doubles at 24-bit precision (one
// degree as angles.h has it, times 225).
constexpr float kTurnAngle = ei::kRadiansPerDegree * 225.0f;

} // namespace

// --- ViewScreen -------------------------------------------------------------

void ViewScreen::CreateScreen() {
    UiInputSide* inert = new UiInertInput(ui);
    AddInput(inert);
    AddInput(new UnitPickInput(ui));
    AddInput(new CameraInput(ui));

    Camera* camera = &g_camera;
    camera->flags &= ~Camera::kFree;
    Vector3 focus{30.0f, 30.0f, 0.0f};
    camera->SetFocus(focus);
    Vector3 axis{1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAngleAxis(kTurnAngle, axis);
    camera->localRotation.PreMultiply(turn);
}

void ViewScreen::Draw(SceneRenderer*) {
    g_clientWorld.Render();
}

void ViewScreen::CloseScreen() { g_camera.flags |= Camera::kFree; }

// --- UnitPickInput ------------------------------------------------------------------------------

void UnitPickInput::LeftDown(int, int x, int y) {
    WorldClient* world = &g_clientWorld;
    const Point point = {x, y};
    Unit* unit = world->UnitAt2(&point);
    if (!ui->ctrlDown) {
        if (unit) {
            int n = lib::Count(picked);
            picked.resize(n + 1);
            picked[n] = static_cast<std::uint32_t>(unit->id);
            world->Select(unit);
            return;
        }
        Vector3 ground;
        world->PickGround(&ground, x, y);
        for (int i = 0; i < std::ssize(picked); ++i) {
            auto* member = g_clientWorld.FindUnit(picked[i]);
            if (!member) continue;
            std::vector<Unit*> order{member};
            world->LocalView()->SendWalk(&order, ground, 0);
        }
    } else if (unit) {
        for (int i = 0; i < std::ssize(picked); ++i) {
            auto* member = g_clientWorld.FindUnit(picked[i]);
            if (!member) continue;
            std::vector<Unit*> order{member};
            world->LocalView()->SendAttack(&order, unit, kAttackHow, 0);
        }
    }
}

void UnitPickInput::RightDown(int, int, int) {
    lib::FreeStorage(picked);
    g_clientWorld.Deselect();
}

void UnitPickInput::KeyDown(int key, int, int) {
    if (key == platform::kKeyControl) {
        g_cursor.SetShape(kCursorCtrl);
    } else if (key == platform::kKeySpace) {
        GameSetServerPaused(g_serverWorld.paused == 0);
    }
}

void UnitPickInput::KeyUp(int key, int, int) {
    if (key == platform::kKeyControl) g_cursor.SetShape(kCursorNormal);
}
