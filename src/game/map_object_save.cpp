// Saving and loading the map object part of every map object (CMapObject slots 9 / 10) and its
// server companion's (CMapObjectServer slots 9 / 10). See map_object.h and
// docs/notes/map_objects.md.
#include "map_object.h"

#include "core_streams.h"
#include "unit.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstring>
#include <new>

namespace {
constexpr std::uint32_t kBaseScaleVersion = 0x4E;   // base scale saved after this version

mfc::CString& AsString(char*& text) { return *reinterpret_cast<mfc::CString*>(&text); }
mfc::CString& AsString(const char*& text) { return *reinterpret_cast<mfc::CString*>(&text); }

// A string read up to its terminator (CStream::ReadString into a temporary).
struct ReadText {
    explicit ReadText(CStream* stream) : text(stream->ReadString(storage)) {}
    ~ReadText() { text->~CString(); }
    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* text;
};

void WriteText(CStream* stream, const char* text) {
    stream->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
}
} // namespace

void CMapObject::LoadMapObject(CStream* stream) {
    companion->Load(stream);
    stream->Read(&animationSpeed, sizeof(animationSpeed));
    stream->Read(&position.x, 3 * sizeof(float));
    stream->Read(&orientation, sizeof(orientation));
    {
        ReadText text(stream);
        AsString(name) = static_cast<const char*>(*text.text);
    }
    stream->Read(&scale, sizeof(scale));
    // The original reads the count into its argument's stack slot: a failed read leaves the stream
    // pointer there.
    int count = static_cast<int>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&count, sizeof(count));
    SetEffectCount(count);
    for (int i = 0; i < effectCount; ++i) effects[i].Load(stream);
    std::uint32_t flags = flags_;   // the original's local starts as stack garbage
    stream->Read(&flags, sizeof(flags));
    flags_ = flags;
    {
        ReadText text(stream);
        AsString(name2) = *text.text;
    }
    if (stream->version_ > kBaseScaleVersion) stream->Read(&baseScale, sizeof(baseScale));
}
EI_REPLACE_METHOD(0x0050F7A0, CMapObject::LoadMapObject);

void CMapObject::SaveMapObject(CStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->Write(&speed, sizeof(speed));
    stream->Write(&position.x, 3 * sizeof(float));
    stream->Write(&orientation, sizeof(orientation));
    WriteText(stream, name);
    stream->Write(&scale, sizeof(scale));
    int count = effectCount;
    stream->Write(&count, sizeof(count));
    for (int i = 0; i < effectCount; ++i) effects[i].Save(stream);
    std::uint32_t flags = flags_;
    stream->Write(&flags, sizeof(flags));
    WriteText(stream, name2);
    stream->Write(&baseScale, sizeof(baseScale));
}
EI_REPLACE_METHOD(0x0050F900, CMapObject::SaveMapObject);

void CMapObjectServer::LoadServer(CStream* stream) {
    stream->Read(&vitals, sizeof(vitals));
    {
        ReadText text(stream);
        name78 = *text.text;
    }
    {
        ReadText text(stream);
        name7C = *text.text;
    }
    // Read into the argument's stack slot, as LoadMapObject's count.
    int count = static_cast<int>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&count, sizeof(count));
    for (int i = 0; i < count; ++i) {
        ReadText text(stream);
        strings.SetAtGrow(strings.size, *text.text);
    }
}
EI_REPLACE_METHOD(0x005159D0, CMapObjectServer::LoadServer);

void CMapObjectServer::SaveServer(CStream* stream) {
    stream->Write(&vitals, sizeof(vitals));
    WriteText(stream, name78);
    WriteText(stream, name7C);
    int count = strings.size;
    stream->Write(&count, sizeof(count));
    for (int i = 0; i < strings.size; ++i) WriteText(stream, strings[i]);
}
EI_REPLACE_METHOD(0x00515AC0, CMapObjectServer::SaveServer);
