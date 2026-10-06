// Checks for the briefing / base screen's dialogue (src/game/ui_briefing_dialogue.cpp).
//
// ViewSpeaker only writes message 0x0F to the local player's connection: side by side (ours, the
// bytes it wrote kept, the stream put back, the original, its bytes compared: the two points as
// floats with CheckNear, the type and flag bytes exactly). StartPhrases and PlaySpeakerAnimation
// start the phrases / an animation (sound, server orders, random numbers): ours only, with the
// state they leave checked. Scenario ui_base_dialogue (a topic of Smith's, then its phrases).
#include "player_out_mark.h"
#include "verify.h"

#include "../../src/game/camera.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"
#include "../../src/game/world.h"
#include "../../src/game/originals_world.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;
void* s_viewSpeaker;

void* SpeakerActionDone() { return reinterpret_cast<void*>(0x006023F0); }

void* LocalServer() {
    auto* player = static_cast<WorldPlayer*>(WorldGetPlayer(WorldServer(), nullptr, 0));
    return player ? player->server : nullptr;
}

float FloatAt(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    float v;
    std::memcpy(&v, bytes.data() + offset, 4);
    return v;
}
} // namespace

// StartPhrases: state 0 (no text for the topic) or the camera kept: savedCamera_ is the camera's
// position before the call, lookAt_ that plus the z axis of its rotation.
static void __fastcall StartPhrasesVerify(CBriefingScreen* self, void*) {
    const CCamera* camera = camera::TheCamera();
    Vector3 position = camera->localPosition_;
    Quaternion q = camera->localRotation_;
    self->StartPhrases();
    if (self->state_ == 0) {
        ei::verify::Check("CBriefingScreen::StartPhrases", true, true, "");
        return;
    }
    bool same = std::memcmp(&position, &self->savedCamera_, sizeof(Vector3)) == 0;
    ei::verify::Check("CBriefingScreen::StartPhrases", true, same, "saved camera %g %g %g", self->savedCamera_.x,
                      self->savedCamera_.y, self->savedCamera_.z);
    float x = position.x + 2.0f * (q.x * q.z + q.w * q.y);
    float y = position.y + 2.0f * (q.y * q.z - q.w * q.x);
    float z = position.z + (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z);
    ei::verify::CheckNear("CBriefingScreen::StartPhrases lookAt.x", x, self->lookAt_.x, "");
    ei::verify::CheckNear("CBriefingScreen::StartPhrases lookAt.y", y, self->lookAt_.y, "");
    ei::verify::CheckNear("CBriefingScreen::StartPhrases lookAt.z", z, self->lookAt_.z, "");
}
EI_VERIFY(0x00608210, StartPhrasesVerify, 0, s_unused);

// PlaySpeakerAnimation: the server unit's controller calls 006023F0 when the animation ends.
static void __fastcall SpeakerAnimationVerify(CBriefingScreen* self, void*, CUnit* unit, bool talking, bool third) {
    self->PlaySpeakerAnimation(unit, talking, third);
    auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(WorldServer(), nullptr, unit->id_));
    bool ok = true;
    if (serverUnit) {
        auto* controller = reinterpret_cast<UnitController*>(serverUnit->controller);
        ok = reinterpret_cast<void*>(controller->actionDone) == SpeakerActionDone();
    }
    ei::verify::Check("CBriefingScreen::PlaySpeakerAnimation", true, ok, "unit %d, talking %d", unit->id_, talking);
}
EI_VERIFY(0x00607A60, SpeakerAnimationVerify, 0, s_unused);

static void __fastcall ViewSpeakerVerify(CBriefingScreen* self, void*, CUnit* unit, float angle, float distance,
                                         float height) {
    using Fn = void(__fastcall*)(CBriefingScreen*, void*, CUnit*, float, float, float);
    void* server = LocalServer();
    if (!server) {
        self->ViewSpeaker(unit, angle, distance, height);
        return;
    }
    PlayerOutMark mark = PlayerOutTake(server);
    self->ViewSpeaker(unit, angle, distance, height);
    std::vector<std::uint8_t> actual;
    if (!PlayerOutSince(server, mark, &actual)) return;   // ours grew the buffer: no second run
    PlayerOutRestore(server, mark);
    reinterpret_cast<Fn>(s_viewSpeaker)(self, nullptr, unit, angle, distance, height);
    std::vector<std::uint8_t> expected;
    if (!PlayerOutSince(server, mark, &expected)) return;
    constexpr std::size_t kMessageSize = 1 + 12 + 12 + 1;
    bool shape = expected.size() == kMessageSize && actual.size() == kMessageSize && expected[0] == actual[0] &&
                 expected[kMessageSize - 1] == actual[kMessageSize - 1];
    ei::verify::Check("CBriefingScreen::ViewSpeaker", true, shape, "unit %p: %u / %u bytes", unit,
                      static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
    if (!shape) return;
    for (int i = 0; i < 6; ++i)
        ei::verify::CheckNear("CBriefingScreen::ViewSpeaker point", FloatAt(expected, 1 + 4 * i), FloatAt(actual, 1 + 4 * i),
                              "unit %p, angle %g, value %d", unit, angle, i);
}
// 00607BB0: SUB ESP,0x4C; FLD dword [ESP+0x54] (3 + 4 bytes).
EI_VERIFY(0x00607BB0, ViewSpeakerVerify, 7, s_viewSpeaker);
