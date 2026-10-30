// Saving and loading the map object part of every map object (MapObject Load / Save) and its
// server companion's (MapObjectServer Load / Save). See map_object.h and docs/notes/map_objects.md.
#include "map_object.h"

#include "core_streams.h"
#include "unit.h"
#include "save_versions.h"
#include "lib/containers.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

} // namespace

void MapObject::LoadMapObject(ByteStream* stream) {
    companion->Load(stream);
    stream->ReadValue(animationSpeed);
    stream->Read(&localPosition.x, 3 * sizeof(float));
    stream->ReadValue(localRotation);
    name = stream->ReadString();
    stream->ReadValue(scale);
    int count = 0;
    stream->ReadValue(count);
    SetEffectCount(count);
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Load(stream);
    std::uint32_t currentFlags = flags;    // the original's local starts as stack garbage
    stream->ReadValue(currentFlags);
    flags = currentFlags;
    name2 = stream->ReadString();
    if (stream->version > kVersionBaseScale) stream->ReadValue(baseScale);
}

void MapObject::SaveMapObject(ByteStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->WriteValue(speed);
    stream->Write(&localPosition.x, 3 * sizeof(float));
    stream->WriteValue(localRotation);
    stream->WriteString(name);
    stream->WriteValue(scale);
    int count = lib::Count(effects);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Save(stream);
    std::uint32_t currentFlags = flags;
    stream->WriteValue(currentFlags);
    stream->WriteString(name2);
    stream->WriteValue(baseScale);
}

void MapObjectServer::Load(ByteStream* stream) {
    stream->ReadValue(vitals);
    parentIname = stream->ReadString();
    skinTexture = stream->ReadString();
    int count = 0;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) strings.push_back(stream->ReadString());
}

void MapObjectServer::Save(ByteStream* stream) {
    stream->WriteValue(vitals);
    stream->WriteString(parentIname);
    stream->WriteString(skinTexture);
    int count = static_cast<int>(strings.size());
    stream->WriteValue(count);
    for (const std::string& text : strings) stream->WriteString(text);
}
