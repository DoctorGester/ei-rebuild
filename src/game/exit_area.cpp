// The zone exit's area test (world.h, ZoneExit). The rectangle is turned by `angle` about
// its centre: the point is turned the other way first. Comparisons keep the game's NaN cases:
// a NaN angle takes the axis-aligned path, a NaN turned x is outside, the other tests let NaN in.
// Also the exit's stream form and the stream's string reader.
#include "world.h"

#include "core_maths.h"
#include "core_streams.h"
#include "record_lifetime.h"

#include "lib/mfc.h"

#include "lib/nan_audit.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

bool ZoneExit::Contains(float x, float y) const {
    EI_NAN_AUDIT(angle, y, y2);
    if (angle == 0.0f) {
        if (x1 > x) return false;
        if (x > x2) return false;
        if (y1 > y) return false;
        return y <= y2;
    }
    double centerX = (x2 + x1) * 0.5f;
    float centerY = static_cast<float>((y2 + y1) * 0.5f);
    float c = static_cast<float>(x87::fcos(static_cast<double>(angle)));
    float s = static_cast<float>(x87::fsin(static_cast<double>(angle)));
    float dy = static_cast<float>(y - centerY);
    double dxExact = x - centerX;
    float dx = static_cast<float>(dxExact);
    double turnedX = dxExact * c + dy * s + centerX;
    float turnedY = static_cast<float>(centerY - dx * s + dy * c);
    EI_NAN_AUDIT(turnedX, x1, turnedY, y2);
    if (turnedX < x1) return false;
    if (turnedX > x2) return false;
    if (y1 > turnedY) return false;
    return turnedY <= y2;
}

ZoneExit* ZoneExit::Assign(const ZoneExit* from) {
    std::memcpy(this, from, offsetof(ZoneExit, target));   // ..+0x24 as dwords
    target = from->target;
    entrance = from->entrance;
    view = from->view;
    hasDeployAngle = from->hasDeployAngle;
    deployAngle = from->deployAngle;
    return this;
}

// ---- Stream form and the string reader ------------------

namespace {
constexpr std::uint32_t kExitExtrasVersion = 0x68;   // +0x38 and the angle saved from here on
} // namespace

void CStream::ReadInto(std::string* out) {
    std::string text;
    ReadString(&text);
    *out = text.c_str();   // up to the first zero byte
}

void ZoneExit::Read(CStream* stream) {
    stream->Read(&passTime, 4);
    for (float& value : deploy) stream->Read(&value, 4);
    stream->Read(&x1, 4);
    stream->Read(&y1, 4);
    stream->Read(&x2, 4);
    stream->Read(&y2, 4);
    stream->ReadInto(&target);
    stream->Read(&entrance, 4);
    stream->Read(&view, 4);
    hasDeployAngle = 0;
    deployAngle = 0;
    std::memset(&angle, 0, sizeof(angle));
    if (static_cast<int>(stream->version_) >= static_cast<int>(kExitExtrasVersion)) {
        stream->Read(&hasDeployAngle, 1);
        stream->Read(&deployAngle, 4);
        stream->Read(&angle, 4);
    }
}

void ZoneExit::Write(CStream* stream) const {
    auto put = [stream](const void* field, std::uint32_t size) {
        std::uint32_t copy = 0;
        std::memcpy(&copy, field, size);
        stream->Write(&copy, size);
    };
    put(&passTime, 4);
    for (const float& value : deploy) put(&value, 4);
    put(&x1, 4);
    put(&y1, 4);
    put(&x2, 4);
    put(&y2, 4);
    stream->Write(target.c_str(), static_cast<std::uint32_t>(std::strlen(target.c_str()) + 1));
    put(&entrance, 4);
    put(&view, 4);
    put(&hasDeployAngle, 1);
    put(&deployAngle, 4);
    put(&angle, 4);
}
