// Check for the face plane (face_normal.cpp): the original on a copy of the face, ours on the
// face; normal and distance compared within a few ULPs. Scenario: any that loads the fake unit
// scene (the interface's unit previews; ui_hero_screen, ui_camp).
#include "verify.h"

#include "../../src/game/scene_loader.h"

// 0049AEC0: SUB ESP,0x18; MOV EAX,[ECX+4] (6 bytes).
static void (__fastcall* s_faceNormal)(scene::MeshFace*, void*, const scene::PointBuffer*);
static void __fastcall FaceNormalVerify(scene::MeshFace* face, void*, const scene::PointBuffer* positions) {
    scene::MeshFace expected = *face;
    s_faceNormal(&expected, nullptr, positions);
    scene::ComputeFaceNormal(face, nullptr, positions);
    for (int i = 0; i < 3; ++i) ei::verify::CheckNear("ComputeFaceNormal normal", expected.normal[i], face->normal[i], "%d", i);
    ei::verify::CheckNear("ComputeFaceNormal distance", expected.distance, face->distance, "d");
}
EI_VERIFY(0x0049AEC0, FaceNormalVerify, 6, s_faceNormal);
