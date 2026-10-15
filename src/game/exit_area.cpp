// 004E59F0: the zone exit's area test (world.h, ZoneExit). The rectangle is turned by `angle` about
// its centre: the point is turned the other way first. Comparisons keep the original's NaN cases:
// a NaN angle takes the axis-aligned path, a NaN turned x is outside, the other tests let NaN in.
// Also the exit's stream form (004E5750 / 004E58A0) and the stream's CString reader 004E5840.
#include "world.h"

#include "core_maths.h"
#include "core_streams.h"
#include "record_lifetime.h"

#include "lib/mfc.h"

#include "ei/hook.h"

#include <cmath>
#include <cstdint>
#include <cstring>

bool ZoneExit::Contains(float x, float y) const {
    if (!(angle < 0.0f) && !(angle > 0.0f)) {
        if (x1 > x) return false;
        if (x > x2) return false;
        if (y1 > y) return false;
        return !(y > y2);
    }
    double centerX = (x2 + x1) * 0.5f;
    float centerY = StoreFloat(static_cast<float>((y2 + y1) * 0.5f));
    float c = StoreFloat(static_cast<float>(x87::fcos(static_cast<double>(angle))));
    float s = StoreFloat(static_cast<float>(x87::fsin(static_cast<double>(angle))));
    float dy = StoreFloat(y - centerY);
    double dxExact = x - centerX;
    float dx = StoreFloat(static_cast<float>(dxExact));
    double turnedX = dxExact * c + dy * s + centerX;
    float turnedY = StoreFloat(static_cast<float>(centerY - dx * s + dy * c));
    if (!(turnedX >= x1)) return false;
    if (turnedX > x2) return false;
    if (y1 > turnedY) return false;
    return !(turnedY > y2);
}
EI_REPLACE_METHOD(0x004E59F0, ZoneExit::Contains);

ZoneExit* ZoneExit::Assign(const ZoneExit* from) {
    for (int i = 0; i < 5; ++i) passTimeDeploy[i] = from->passTimeDeploy[i];
    x1 = from->x1;
    y1 = from->y1;
    x2 = from->x2;
    y2 = from->y2;
    angle = from->angle;
    records::AsCString(target) = records::AsCString(from->target);
    entrance = from->entrance;
    view = from->view;
    hasDeployAngle = from->hasDeployAngle;
    deployAngle = from->deployAngle;
    return this;
}
EI_REPLACE_METHOD(0x00583B50, ZoneExit::Assign);

// ---- Stream form (004E5750 / 004E58A0) and the CString reader 004E5840 (A-S10) ------------------

namespace {
constexpr std::uint32_t kExitExtrasVersion = 0x68;   // +0x34, +0x38 and the angle saved from here on
} // namespace

void CStream::ReadInto(mfc::CString* out) {
    alignas(mfc::CString) unsigned char slot[sizeof(mfc::CString)];
    auto* text = ReadString(slot);
    *out = *text;
    text->~CString();
}
EI_REPLACE_METHOD(0x004E5840, CStream::ReadInto);

void ZoneExit::Read(CStream* stream) {
    for (std::uint32_t& word : passTimeDeploy) stream->Read(&word, 4);
    stream->Read(&x1, 4);
    stream->Read(&y1, 4);
    stream->Read(&x2, 4);
    stream->Read(&y2, 4);
    stream->ReadInto(reinterpret_cast<mfc::CString*>(&target));
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
EI_REPLACE_METHOD(0x004E5750, ZoneExit::Read);

// Each field goes through a copy on the stack, as in the original.
void ZoneExit::Write(CStream* stream) const {
    auto put = [stream](const void* field, std::uint32_t size) {
        std::uint32_t copy = 0;
        std::memcpy(&copy, field, size);
        stream->Write(&copy, size);
    };
    for (const std::uint32_t& word : passTimeDeploy) put(&word, 4);
    put(&x1, 4);
    put(&y1, 4);
    put(&x2, 4);
    put(&y2, 4);
    stream->Write(target, static_cast<std::uint32_t>(std::strlen(target) + 1));
    put(&entrance, 4);
    put(&view, 4);
    put(&hasDeployAngle, 1);
    put(&deployAngle, 4);
    put(&angle, 4);
}
EI_REPLACE_METHOD(0x004E58A0, ZoneExit::Write);
