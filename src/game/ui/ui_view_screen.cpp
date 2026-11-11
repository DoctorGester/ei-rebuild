// Interface screen 7 (ui_view_screen.h): its Create, draw and close slots and the picked units'
// input. See docs/notes/ui.md, section 4.
#include "game/ui/ui_camera_input.h"
#include "game/ui/ui_view_screen.h"

#include "engine/scene/camera.h"
#include "lib/vecmath.h"
#include "game/players/player_client.h"
#include "game/ui/ui_cursor.h"
#include "game/world/world.h"

#include <cstdint>
#include "game/app/singletons.h"
#include "lib/containers.h"
#include "platform/platform.h"

#include <vector>

namespace {

constexpr int kAttackHow = 6;

// The view's turn about x: 225 degrees.
constexpr float kTurnAngle = ei::kRadiansPerDegree * 225.0f;

} // namespace

// --- ViewScreen -------------------------------------------------------------

void ViewScreen::CreateScreen() {
    AddInput(std::make_unique<UnitPickInput>());
    AddInput(std::make_unique<CameraInput>());

    Camera* camera = &g_camera;
    camera->flags &= ~Camera::kFree;
    Vector3 focus{30.0f, 30.0f, 0.0f};
    camera->SetFocus(focus);
    Vector3 axis{1.0f, 0.0f, 0.0f};
    Quaternion turn = Quaternion::AxisAngle(axis, kTurnAngle);
    camera->localRotation.PreMultiply(turn);
}

void ViewScreen::DrawSprites(SceneRenderer*) {
    g_clientWorld.Render();
}

void ViewScreen::CloseScreen() { g_camera.flags |= Camera::kFree; }

// --- UnitPickInput ------------------------------------------------------------------------------

void UnitPickInput::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::RightDown: case UiEventKind::RightDouble: RightDown(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::KeyUp: KeyUp(event); break;
    default: break;
    }
}

void UnitPickInput::LeftDown(UiEvent& event) {
    WorldClient* world = &g_clientWorld;
    Unit* unit = world->UnitAt(event.at, HitTest::Parts);
    if (!g_interface.ctrlDown) {
        if (unit) {
            int n = lib::Count(picked);
            picked.resize(n + 1);
            picked[n] = static_cast<std::uint32_t>(unit->id);
            world->Select(unit);
            return;
        }
        Vector3 ground = world->PickGround(event.at.x, event.at.y);
        for (std::uint32_t id : picked) {
            auto* member = g_clientWorld.FindUnit(id);
            if (!member) continue;
            std::vector<Unit*> order{member};
            world->LocalView()->SendWalk(order, ground, 0);
        }
    } else if (unit) {
        for (std::uint32_t id : picked) {
            auto* member = g_clientWorld.FindUnit(id);
            if (!member) continue;
            std::vector<Unit*> order{member};
            world->LocalView()->SendAttack(order, unit, kAttackHow, 0);
        }
    }
}

void UnitPickInput::RightDown(UiEvent&) {
    lib::FreeStorage(picked);
    g_clientWorld.Deselect();
}

void UnitPickInput::KeyDown(UiEvent& event) {
    if (event.key == platform::Key::Control) {
        g_cursor.SetShape(CursorShape::Attack);
    } else if (event.key == platform::Key::Space) {
        g_serverWorld.SetServerPaused(g_serverWorld.paused == 0);
    }
}

void UnitPickInput::KeyUp(UiEvent& event) {
    if (event.key == platform::Key::Control) g_cursor.SetShape(CursorShape::Default);
}
