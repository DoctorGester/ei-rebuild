// The zone exit's area test (world.h, ZoneExit). The rectangle is turned by `angle` about
// its centre: the point is turned the other way first. Comparisons keep the game's NaN cases:
// a NaN angle takes the axis-aligned path, a NaN turned x is outside, the other tests let NaN in.
// Also the exit's stream form and the stream's string reader.
#include "world.h"

#include "core_maths.h"
#include "core_streams.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "lib/nan_audit.h"
#include "save_versions.h"

#include <cmath>
#include <memory>

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

// ---- Stream form and the string reader ------------------

namespace {
} // namespace

void CStream::ReadInto(std::string* out) {
    std::string text;
    ReadString(&text);
    *out = text.c_str();   // up to the first zero byte
}

void ZoneExit::Read(CStream* stream) {
    stream->ReadValue(passTime);
    for (float& value : deploy) stream->ReadValue(value);
    stream->ReadValue(x1);
    stream->ReadValue(y1);
    stream->ReadValue(x2);
    stream->ReadValue(y2);
    stream->ReadInto(&target);
    stream->ReadValue(entrance);
    stream->ReadValue(view);
    hasDeployAngle = 0;
    deployAngle = 0;
    angle = 0;
    if (static_cast<int>(stream->version_) >= static_cast<int>(kVersionExitExtras)) {
        stream->ReadValue(hasDeployAngle);
        stream->ReadValue(deployAngle);
        stream->ReadValue(angle);
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
    stream->WriteString(target);
    put(&entrance, 4);
    put(&view, 4);
    put(&hasDeployAngle, 1);
    put(&deployAngle, 4);
    put(&angle, 4);
}
