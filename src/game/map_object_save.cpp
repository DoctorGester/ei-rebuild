// Saving and loading the map object part of every map object (CMapObject Load / Save) and its
// server companion's (CMapObjectServer Load / Save). See map_object.h and docs/notes/map_objects.md.
#include "map_object.h"

#include "core_streams.h"
#include "unit.h"

#include "ei/hook.h"

#include <cstring>
#include <memory>
#include <new>

namespace {
constexpr std::uint32_t kBaseScaleVersion = 0x4E;   // base scale saved after this version

// A string read up to its terminator (CStream::ReadString into a temporary).
struct ReadText {
    explicit ReadText(CStream* stream) : text(stream->ReadString(storage)) {}
    ~ReadText() { std::destroy_at(text); }
    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* text;
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
        name = text.text->GetString();
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
EI_REWRITTEN(0x0050F7A0, CMapObject::LoadMapObject);

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
EI_REWRITTEN(0x0050F900, CMapObject::SaveMapObject);

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
        strings.push_back(text.text->GetString());
    }
}
EI_REWRITTEN(0x005159D0, CMapObjectServer::Load);

void CMapObjectServer::Save(CStream* stream) {
    stream->Write(&vitals, sizeof(vitals));
    WriteText(stream, name78);
    WriteText(stream, name7C);
    int count = static_cast<int>(strings.size());
    stream->Write(&count, sizeof(count));
    for (const std::string& text : strings) WriteText(stream, text);
}
EI_REWRITTEN(0x00515AC0, CMapObjectServer::Save);
