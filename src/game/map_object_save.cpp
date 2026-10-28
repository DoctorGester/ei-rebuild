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

// A string read up to its terminator (CStream::ReadString into a temporary).
struct ReadText {
    explicit ReadText(CStream* stream) : text(stream->ReadString(storage.c_str())) {}
    ~ReadText() { std::destroy_at(text); }
    std::string storage;
    std::string* text;
};

void WriteText(CStream* stream, std::string_view text) {
    const gamecrt::TerminatedText terminated(text);
    stream->Write(terminated.c_str(), static_cast<std::uint32_t>(terminated.size() + 1));
}
} // namespace

void CMapObject::LoadMapObject(CStream* stream) {
    companion->Load(stream);
    stream->Read(&animationSpeed, sizeof(animationSpeed));
    stream->Read(&localPosition_.x, 3 * sizeof(float));
    stream->Read(&localRotation_, sizeof(localRotation_));
    {
        ReadText text(stream);
        name = text.text->c_str();
    }
    stream->Read(&scale, sizeof(scale));
    int count = 0;
    stream->Read(&count, sizeof(count));
    SetEffectCount(count);
    for (int i = 0; i < effects.size; ++i) effects[i].Load(stream);
    std::uint32_t flags = flags_;   // the original's local starts as stack garbage
    stream->Read(&flags, sizeof(flags));
    flags_ = flags;
    {
        ReadText text(stream);
        name2 = *text.text;
    }
    if (stream->version_ > kBaseScaleVersion) stream->Read(&baseScale, sizeof(baseScale));
}

void CMapObject::SaveMapObject(CStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->Write(&speed, sizeof(speed));
    stream->Write(&localPosition_.x, 3 * sizeof(float));
    stream->Write(&localRotation_, sizeof(localRotation_));
    WriteText(stream, name);
    stream->Write(&scale, sizeof(scale));
    int count = effects.size;
    stream->Write(&count, sizeof(count));
    for (int i = 0; i < effects.size; ++i) effects[i].Save(stream);
    std::uint32_t flags = flags_;
    stream->Write(&flags, sizeof(flags));
    WriteText(stream, name2);
    stream->Write(&baseScale, sizeof(baseScale));
}

void CMapObjectServer::Load(CStream* stream) {
    stream->Read(&vitals, sizeof(vitals));
    {
        ReadText text(stream);
        name78 = *text.text;
    }
    {
        ReadText text(stream);
        name7C = *text.text;
    }
    int count = 0;
    stream->Read(&count, sizeof(count));
    for (int i = 0; i < count; ++i) {
        ReadText text(stream);
        strings.push_back(text.text->c_str());
    }
}

void CMapObjectServer::Save(CStream* stream) {
    stream->Write(&vitals, sizeof(vitals));
    WriteText(stream, name78);
    WriteText(stream, name7C);
    int count = static_cast<int>(strings.size());
    stream->Write(&count, sizeof(count));
    for (const std::string& text : strings) WriteText(stream, text);
}
