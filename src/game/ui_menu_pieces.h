#pragma once

// The pieces the escape menu's windows (save / load, options) are built and slid in with: a region over a layout
// rectangle, a frame and a shade sprite, the stone menu model and the slide's ease.

#include "game/ui.h"
#include "game/ui_mesh.h"

inline UiRegion* AddRect(UiWindow* window, int left, int top, int right, int bottom) {
    UiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

inline void Frame(UiWindow* window, float depth, float left, float top, float right, float bottom) {
    window->AddBlankSprite()->SetFrame(depth, 100.0f, left, top, right, bottom, 5.0f, 2, 0xFE, 0xB4, 0xF9);
}

inline void Shade(UiWindow* window, float depth, float left, float top, float right, float bottom) {
    UiMesh* sprite = window->AddBlankSprite();
    sprite->SetRectangle(depth, 100.0f, left, top, right, bottom, 0, 0, kUiTextureSize, kUiTextureSize);
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = 0xA0000000;
}

inline UiModel* AddMenuModel(UiWindow* window, float angle) {
    UiModel* model = window->AddModel();
    model->Load("unmoco1", "escMenu00", "escMenu00labels");
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn = Quaternion::AxisAngle(axis, angle);
    model->object->localRotation = turn;
    return model;
}

// The windows slide in: the model's y goes from 700 to its resting place with an ease-in-out
// curve over the slide (0..1).
inline float Ease(float slide) {
    if (slide < 0.5f) {
        float s = slide;
        float e = s * s;
        return e + e;
    }
    float r = 1.0f - slide;
    float e = r * r;
    e += e;
    return 1.0f - e;
}
