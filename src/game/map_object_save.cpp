// Saving and loading the map object part of every map object (CMapObject Load / Save) and its
// server companion's (CMapObjectServer Load / Save). See map_object.h and docs/notes/map_objects.md.
#include "map_object.h"

#include "core_streams.h"
#include "unit.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {
constexpr std::uint32_t kBaseScaleVersion = 0x4E;   // base scale saved after this version

// A string read up to its terminator.
std::string ReadText(CStream* stream) {
    std::string text;
    stream->ReadString(&text);
    return text;
}

} // namespace

void CMapObject::LoadMapObject(CStream* stream) {
    companion->Load(stream);
    stream->Read(&animationSpeed, sizeof(animationSpeed));
    stream->Read(&localPosition_.x, 3 * sizeof(float));
    stream->Read(&localRotation_, sizeof(localRotation_));
    name = ReadText(stream);
    stream->Read(&scale, sizeof(scale));
    int count = 0;
    stream->Read(&count, sizeof(count));
    SetEffectCount(count);
    for (int i = 0; i < effects.size; ++i) effects[i].Load(stream);
    std::uint32_t flags = flags_;   // the original's local starts as stack garbage
    stream->Read(&flags, sizeof(flags));
    flags_ = flags;
    name2 = ReadText(stream);
    if (stream->version_ > kBaseScaleVersion) stream->Read(&baseScale, sizeof(baseScale));
}

void CMapObject::SaveMapObject(CStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->Write(&speed, sizeof(speed));
    stream->Write(&localPosition_.x, 3 * sizeof(float));
    stream->Write(&localRotation_, sizeof(localRotation_));
    stream->WriteString(name);
    stream->Write(&scale, sizeof(scale));
    int count = effects.size;
    stream->Write(&count, sizeof(count));
    for (int i = 0; i < effects.size; ++i) effects[i].Save(stream);
    std::uint32_t flags = flags_;
    stream->Write(&flags, sizeof(flags));
    stream->WriteString(name2);
    stream->Write(&baseScale, sizeof(baseScale));
}

void CMapObjectServer::Load(CStream* stream) {
    stream->Read(&vitals, sizeof(vitals));
    name78 = ReadText(stream);
    name7C = ReadText(stream);
    int count = 0;
    stream->Read(&count, sizeof(count));
    for (int i = 0; i < count; ++i) strings.push_back(ReadText(stream));
}

void CMapObjectServer::Save(CStream* stream) {
    stream->Write(&vitals, sizeof(vitals));
    stream->WriteString(name78);
    stream->WriteString(name7C);
    int count = static_cast<int>(strings.size());
    stream->Write(&count, sizeof(count));
    for (const std::string& text : strings) stream->WriteString(text);
}
