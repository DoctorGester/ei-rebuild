// Saving and loading the map object part of every map object (CMapObject Load / Save) and its
// server companion's (CMapObjectServer Load / Save). See map_object.h and docs/notes/map_objects.md.
#include "map_object.h"

#include "core_streams.h"
#include "unit.h"
#include "save_versions.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

// A string read up to its terminator.
std::string ReadText(CStream* stream) { return stream->ReadString(); }

} // namespace

void CMapObject::LoadMapObject(CStream* stream) {
    companion->Load(stream);
    stream->ReadValue(animationSpeed);
    stream->Read(&localPosition_.x, 3 * sizeof(float));
    stream->ReadValue(localRotation_);
    name = ReadText(stream);
    stream->ReadValue(scale);
    int count = 0;
    stream->ReadValue(count);
    SetEffectCount(count);
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Load(stream);
    std::uint32_t flags = flags_;   // the original's local starts as stack garbage
    stream->ReadValue(flags);
    flags_ = flags;
    name2 = ReadText(stream);
    if (stream->version_ > kVersionBaseScale) stream->ReadValue(baseScale);
}

void CMapObject::SaveMapObject(CStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->WriteValue(speed);
    stream->Write(&localPosition_.x, 3 * sizeof(float));
    stream->WriteValue(localRotation_);
    stream->WriteString(name);
    stream->WriteValue(scale);
    int count = std::ssize(effects);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Save(stream);
    std::uint32_t flags = flags_;
    stream->WriteValue(flags);
    stream->WriteString(name2);
    stream->WriteValue(baseScale);
}

void CMapObjectServer::Load(CStream* stream) {
    stream->ReadValue(vitals);
    parentIname = ReadText(stream);
    skinTexture = ReadText(stream);
    int count = 0;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) strings.push_back(ReadText(stream));
}

void CMapObjectServer::Save(CStream* stream) {
    stream->WriteValue(vitals);
    stream->WriteString(parentIname);
    stream->WriteString(skinTexture);
    int count = static_cast<int>(strings.size());
    stream->WriteValue(count);
    for (const std::string& text : strings) stream->WriteString(text);
}
