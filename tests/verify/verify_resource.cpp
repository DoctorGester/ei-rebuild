// Differential check for CMappedResource::FindEntry (resource archive name lookup).
#include "verify.h"

#include "../../src/game/resource.h"

#include <string>

// 00674850 starts with PUSH ECX; PUSH EBX; PUSH EBP; MOV EBP,ECX; PUSH ESI (6 bytes).
static int(__fastcall* s_original)(const CMappedResource*, void*, const char*);

static int __fastcall FindEntryVerify(const CMappedResource* self, void*, const char* name) {
    int expected = s_original(self, nullptr, name);
    int actual = self->FindEntry(name);
    ei::verify::Check("CMappedResource::FindEntry", expected, actual, "name=\"%s\" -> %d vs %d", name,
                      expected, actual);
    return expected;
}
EI_VERIFY(0x00674850, FindEntryVerify, 6, s_original);

// GetFile and CloseFile change shared state (allocations, the loaded-file maps), so they cannot run
// twice. Instead, after our version runs, the original game's own map lookups must see the result:
// this checks that the STLport-compatible containers match what original code expects.
namespace {
struct MapIterator {
    void* node;
    void* map;
};
// Original hashtable::find for the name map (0x44) and the data map (0x30), both __thiscall.
__declspec(naked) void __fastcall OriginalFindByName(void*, void*, MapIterator*, const stlp::string*) EI_ORIGINAL(0x00672910)
__declspec(naked) void __fastcall OriginalFindByData(void*, void*, MapIterator*, void* const*) EI_ORIGINAL(0x00672860)

LoadedFile* NodeValue(void* node, std::size_t valueOffset) {
    return node ? *reinterpret_cast<LoadedFile**>(static_cast<char*>(node) + valueOffset) : nullptr;
}
LoadedFile* OriginalLookupByName(CMappedResource* self, const stlp::string& name) {
    MapIterator it{};
    OriginalFindByName(&self->filesByName_, nullptr, &it, &name);
    return NodeValue(it.node, 0x10);
}
LoadedFile* OriginalLookupByData(CMappedResource* self, void* data) {
    MapIterator it{};
    OriginalFindByData(&self->filesByData_, nullptr, &it, &data);
    return NodeValue(it.node, 0x08);
}
void* s_unused;
} // namespace

static void* __fastcall GetFileVerify(CMappedResource* self, void*, const char* name, std::uint32_t* size) {
    void* data = self->CMappedResource::GetFileImpl(name, size);
    if (data && !self->image_) {
        LoadedFile* byData = OriginalLookupByData(self, data);
        stlp::string key(name);
        LoadedFile* byName = OriginalLookupByName(self, key);
        bool ok = byData && byData == byName && byData->data == data;
        ei::verify::Check("CMappedResource::GetFile maps", true, ok, "\"%s\" byData %p byName %p", name, byData, byName);
    }
    return data;
}
EI_VERIFY(0x00671B40, GetFileVerify, 0, s_unused);

static bool __fastcall CloseFileVerify(CMappedResource* self, void*, void* data) {
    LoadedFile* before = (data && !self->image_) ? OriginalLookupByData(self, data) : nullptr;
    int refs = before ? before->refCount : 0;
    std::string name = before ? std::string(before->name.data(), before->name.size()) : std::string();
    bool result = self->CMappedResource::CloseFileImpl(data);
    if (before) {
        LoadedFile* byData = OriginalLookupByData(self, data);
        stlp::string key(name.data(), name.data() + name.size());
        LoadedFile* byName = OriginalLookupByName(self, key);
        bool ok = refs == 1 ? (!byData && !byName) : (byData == before && byName == before && before->refCount == refs - 1);
        ei::verify::Check("CMappedResource::CloseFile maps", true, ok, "refs %d byData %p byName %p", refs, byData, byName);
    }
    return result;
}
EI_VERIFY(0x00671E00, CloseFileVerify, 0, s_unused);

// CMMIOResource::GetFile has no side effects: compare against the original directly.
// 006725E0 starts with PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+0x14] (6 bytes).
static void*(__fastcall* s_mmioGetFile)(CMMIOResource*, void*, const char*, std::uint32_t*);

static void* __fastcall MmioGetFileVerify(CMMIOResource* self, void*, const char* name, std::uint32_t* size) {
    std::uint32_t expectedSize = 0xDEADBEEF, actualSize = 0xDEADBEEF;
    void* expected = s_mmioGetFile(self, nullptr, name, &expectedSize);
    void* actual = self->CMMIOResource::GetFileImpl(name, &actualSize);
    bool same = expected == actual && expectedSize == actualSize;
    ei::verify::Check("CMMIOResource::GetFile", true, same, "\"%s\" %p/%u vs %p/%u", name, expected, expectedSize,
                      actual, actualSize);
    if (size && expected) *size = expectedSize;
    return expected;
}
EI_VERIFY(0x006725E0, MmioGetFileVerify, 6, s_mmioGetFile);

// OpenResource opens a file and allocates, so it cannot run twice. Check the state it leaves.
// Only installs in the vanilla launch (Rebirth patches the original).
static BOOL __fastcall OpenResourceVerify(CMappedResource* self, void*, const char* path, BOOL writable) {
    BOOL result = self->CMappedResource::OpenResourceImpl(path, writable);
    bool ok = !result || (self->header_.magic == kResMagic && self->entries_ && self->names_ && self->fd_ >= 0 &&
                          self->writable_ == static_cast<std::uint32_t>(writable));
    ei::verify::Check("CMappedResource::OpenResource", true, ok, "\"%s\" result %d", path, result);
    return result;
}
EI_VERIFY(0x00671730, OpenResourceVerify, 0, s_unused);
