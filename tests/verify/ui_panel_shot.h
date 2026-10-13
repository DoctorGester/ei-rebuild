#pragma once

// A record of what a window's setup made, for the side-by-side checks of the functions that build a
// window's contents (verify_ui_hud_setup.cpp, verify_ui_help_page.cpp): the regions (vertices,
// bounds, flags, offsets, tooltip lines and arguments) and the sprites (position, orientation,
// texture frame, tint, scale, flags, plain colour, texture, array sizes; a rectangle's corners and
// texels). Nothing unwritten is read: an empty tooltip's arguments and the other shapes' arrays
// are left out. Integers are compared exactly, the sprites' floats within a few ULPs.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ui_shot {

struct RegionShot {
    std::vector<UiPoint> points;
    RECT bounds;
    bool rectOnly;
    bool enabled;
    bool tip;
    std::vector<std::string> lines;
    int tipFlag;
    int tipExtra;
    int offset[2];
};

struct SpriteShot {
    Vector3 position;
    Quaternion orientation;
    float frame[2];
    float tint[3];
    float scale;
    int sizes[4];
    int visible;
    std::uint32_t flags;
    std::uint32_t plainColour;
    const void* texture;
    // A rectangle's four corners and texels (every element of both is written); empty for the
    // other shapes, whose arrays may keep unwritten elements.
    std::vector<float> shape;
};

struct PanelShot {
    RECT rect;
    std::vector<RegionShot> regions;
    std::vector<SpriteShot> sprites;
};

inline PanelShot TakePanel(CUiWindow* w) {
    PanelShot shot;
    shot.rect = w->rect_;
    for (int i = 0; i < w->regions_.size; ++i) {
        const CUiRegion* region = w->regions_.data[i];
        RegionShot r = {};
        r.points.assign(region->points_.data, region->points_.data + region->points_.size);
        r.bounds = region->bounds_;
        r.rectOnly = region->rectOnly_;
        r.enabled = region->enabled_;
        r.tip = region->tip_ != nullptr;
        if (const CUiTooltip* tip = region->tip_) {
            for (int line = 0; line < tip->lines_.size; ++line) r.lines.emplace_back(static_cast<const char*>(tip->lines_[line]));
            // An empty tooltip leaves its two arguments unwritten.
            if (tip->lines_.size > 0) {
                r.tipFlag = tip->flag_;
                r.tipExtra = tip->extra_;
            }
        }
        r.offset[0] = region->offsetX_;
        r.offset[1] = region->offsetY_;
        shot.regions.push_back(r);
    }
    for (int i = 0; i < w->sceneObjects_.size; ++i) {
        CUiMesh* sprite = reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[i]);
        SpriteShot s = {};
        s.position = sprite->position_;
        s.frame[0] = sprite->uOffset_;
        s.frame[1] = sprite->vOffset_;
        std::memcpy(s.tint, sprite->tint_, sizeof(s.tint));
        s.scale = sprite->ScaleX();
        s.sizes[0] = sprite->quadCount_;
        s.sizes[1] = sprite->vertexCount_;
        s.sizes[2] = sprite->indexCount_;
        s.sizes[3] = sprite->uvCount_;
        s.visible = sprite->visible_;
        s.orientation = sprite->orientation_;
        s.flags = sprite->flags_;
        s.plainColour = sprite->plainColour_;
        s.texture = sprite->texture_;
        if (sprite->quadCount_ == 1 && sprite->vertexCount_ == 4 && sprite->indexCount_ == 6 && sprite->uvCount_ == 4) {
            const float* corners = reinterpret_cast<const float*>(sprite->quads_);
            s.shape.assign(corners, corners + sizeof(UiQuadBlock) / sizeof(float));
            s.shape.insert(s.shape.end(), sprite->uvs_, sprite->uvs_ + 8);
        }
        shot.sprites.push_back(s);
    }
    return shot;
}

inline bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 4, 1e-5f); }

// The index of the first region that differs (-1: none), and the same for the sprites.
inline int FirstRegionDifference(const PanelShot& a, const PanelShot& b) {
    if (a.regions.size() != b.regions.size()) return static_cast<int>(a.regions.size() < b.regions.size() ? a.regions.size() : b.regions.size());
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        const RegionShot& x = a.regions[i];
        const RegionShot& y = b.regions[i];
        bool same = x.points.size() == y.points.size() &&
                    (x.points.empty() || std::memcmp(x.points.data(), y.points.data(), x.points.size() * sizeof(UiPoint)) == 0) &&
                    std::memcmp(&x.bounds, &y.bounds, sizeof(RECT)) == 0 && x.rectOnly == y.rectOnly && x.enabled == y.enabled &&
                    x.tip == y.tip && x.lines == y.lines && x.tipFlag == y.tipFlag && x.tipExtra == y.tipExtra &&
                    x.offset[0] == y.offset[0] && x.offset[1] == y.offset[1];
        if (!same) return static_cast<int>(i);
    }
    return -1;
}

inline int FirstSpriteDifference(const PanelShot& a, const PanelShot& b) {
    if (a.sprites.size() != b.sprites.size()) return static_cast<int>(a.sprites.size() < b.sprites.size() ? a.sprites.size() : b.sprites.size());
    for (std::size_t i = 0; i < a.sprites.size(); ++i) {
        const SpriteShot& x = a.sprites[i];
        const SpriteShot& y = b.sprites[i];
        bool same = Near(x.position.x, y.position.x) && Near(x.position.y, y.position.y) && Near(x.position.z, y.position.z) &&
                    Near(x.frame[0], y.frame[0]) && Near(x.frame[1], y.frame[1]) && Near(x.tint[0], y.tint[0]) &&
                    Near(x.tint[1], y.tint[1]) && Near(x.tint[2], y.tint[2]) && Near(x.scale, y.scale) &&
                    std::memcmp(x.sizes, y.sizes, sizeof(x.sizes)) == 0 && x.visible == y.visible &&
                    Near(x.orientation.w, y.orientation.w) && Near(x.orientation.x, y.orientation.x) &&
                    Near(x.orientation.y, y.orientation.y) && Near(x.orientation.z, y.orientation.z) && x.flags == y.flags &&
                    x.plainColour == y.plainColour && x.texture == y.texture && x.shape.size() == y.shape.size();
        for (std::size_t k = 0; same && k < x.shape.size(); ++k) same = Near(x.shape[k], y.shape[k]);
        if (!same) return static_cast<int>(i);
    }
    return -1;
}

} // namespace ui_shot
