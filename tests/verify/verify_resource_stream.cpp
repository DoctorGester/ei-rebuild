// Checks for resource_stream.cpp: the wildcard match side by side, the archive listing into a
// scratch CStringArray, CResourceBase's constructor into scratch memory; the file reader, the
// constructors and destructors run ours only (they open files, allocate and free), with checks of
// the state they leave.
#include "verify.h"

#include "../../src/game/resource_manager.h"
#include "../../src/game/resource_stream.h"

#include <cstring>

namespace {
void* s_unused;

// MFC CStringArray (0x14 bytes: vtable, data, size, max, grow by).
struct StringArray {
    void* vtable;
    char** data;
    int size;
    int maxSize;
    int growBy;
};
__declspec(naked) void __fastcall StringArrayConstruct(StringArray* /*self*/) EI_ORIGINAL(0x006FE925)
__declspec(naked) void __fastcall StringArrayDestruct(StringArray* /*self*/) EI_ORIGINAL(0x006FE958)
} // namespace

// 00674CE0: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,EDX (5 bytes).
static int(__fastcall* s_wildcard)(const char*, const char*, int);
static int __fastcall WildcardVerify(const char* name, const char* pattern, int emptyResult) {
    int expected = s_wildcard(name, pattern, emptyResult);
    int actual = WildcardMatch(name, pattern, emptyResult);
    ei::verify::Check("WildcardMatch", expected, actual, "\"%s\" \"%s\" %d", name, pattern, emptyResult);
    return expected;
}
EI_VERIFY(0x00674CE0, WildcardVerify, 5, s_wildcard);

// 00674AC0: SUB ESP,0A14h (6 bytes). The original lists into a scratch array, ours into the real
// one; the names must be the same, in the same order.
static void(__fastcall* s_listFiles)(const CMappedResource*, void*, void*, const char*);
static void __fastcall ListFilesVerify(const CMappedResource* self, void*, void* out, const char* pattern) {
    StringArray scratch;
    StringArrayConstruct(&scratch);
    s_listFiles(self, nullptr, &scratch, pattern);
    self->ListFiles(out, pattern);
    auto* actual = static_cast<StringArray*>(out);
    bool same = scratch.size == actual->size;
    for (int i = 0; same && i < scratch.size; ++i) same = std::strcmp(scratch.data[i], actual->data[i]) == 0;
    ei::verify::Check("CMappedResource::ListFiles", true, same, "\"%s\": %d vs %d names", pattern ? pattern : "(all)",
                      scratch.size, actual->size);
    StringArrayDestruct(&scratch);
}
EI_VERIFY(0x00674AC0, ListFilesVerify, 6, s_listFiles);

// 00674790: MOV EAX,ECX; XOR ECX,ECX; LEA EDX,[EAX+4] (7 bytes). Writes its object only: the
// original into scratch memory, ours into the real object.
static CResourceBase*(__fastcall* s_baseConstruct)(CResourceBase*);
static CResourceBase* __fastcall BaseConstructVerify(CResourceBase* self) {
    alignas(4) unsigned char scratch[sizeof(CResourceBase)];
    std::memset(scratch, 0xCD, sizeof(scratch));
    auto* expected = s_baseConstruct(reinterpret_cast<CResourceBase*>(scratch));
    CResourceBase* actual = self->Construct();
    bool same = expected == reinterpret_cast<CResourceBase*>(scratch) && actual == self &&
                std::memcmp(scratch, self, sizeof(CResourceBase)) == 0;
    ei::verify::Check("CResourceBase::Construct", true, same, "object %p", self);
    return actual;
}
EI_VERIFY(0x00674790, BaseConstructVerify, 7, s_baseConstruct);

static void __fastcall BaseDestructVerify(CResourceBase* self) {
    self->Destruct();
    ei::verify::Check("CResourceBase::Destruct", true, self->accelTable_ == nullptr && self->entries_ == nullptr,
                      "object %p", self);
}
EI_VERIFY(0x00674810, BaseDestructVerify, 0, s_unused);
static CResourceBase* __fastcall BaseDeletingVerify(CResourceBase* self, void*, unsigned flags) {
    return self->DeletingDestruct(flags);
}
EI_VERIFY(0x006747C0, BaseDeletingVerify, 0, s_unused);
static void __fastcall FreeAccelVerify(CResourceBase* self) {
    self->FreeAccelTable();
    ei::verify::Check("CResourceBase::FreeAccelTable", true, self->accelTable_ == nullptr && self->accelCount_ == 0,
                      "object %p", self);
}
EI_VERIFY(0x00674A90, FreeAccelVerify, 0, s_unused);

// --- The sound system's file reader -------------------------------------------------------------

static CResourceStream* __fastcall StreamConstructVerify(CResourceStream* self) {
    CResourceStream* result = self->Construct();
    bool ok = result == self && *reinterpret_cast<std::uintptr_t*>(self) == 0x00743320 && self->archive_ == nullptr &&
              self->file_ == nullptr && self->offset_ == 0 && self->entries_ == nullptr;
    ei::verify::Check("CResourceStream::Construct", true, ok, "object %p", self);
    return result;
}
EI_VERIFY(0x00674280, StreamConstructVerify, 0, s_unused);
static void __fastcall StreamDestructVerify(CResourceStream* self) { self->Destruct(); }
EI_VERIFY(0x006742C0, StreamDestructVerify, 0, s_unused);
static CResourceStream* __fastcall StreamDeletingVerify(CResourceStream* self, void*, unsigned flags) {
    return self->DeletingDestruct(flags);
}
EI_VERIFY(0x006742A0, StreamDeletingVerify, 0, s_unused);

static void __fastcall StreamCloseVerify(CResourceStream* self) {
    self->Close();
    bool ok = self->offset_ == 0 && self->file_ == nullptr && self->archive_ == nullptr && self->entries_ == nullptr &&
              self->names_ == nullptr && self->header_.count == 0;
    ei::verify::Check("CResourceStream::Close", true, ok, "object %p", self);
}
EI_VERIFY(0x00674430, StreamCloseVerify, 0, s_unused);

static BOOL __fastcall StreamOpenVerify(CResourceStream* self, void*, const char* name, std::uint32_t* size) {
    BOOL result = self->OpenFile(name, size);
    bool ok = result == 0 ? self->offset_ == 0 : self->offset_ != 0 && (self->file_ != nullptr || self->offset_ != 1);
    ei::verify::Check("CResourceStream::OpenFile", true, ok, "\"%s\" result %d offset %d", name, result, self->offset_);
    return result;
}
EI_VERIFY(0x006744C0, StreamOpenVerify, 0, s_unused);

static long __fastcall StreamReadVerify(CResourceStream* self, void*, char* buffer, long size) {
    long result = self->Read(buffer, size);
    ei::verify::Check("CResourceStream::Read", true, result <= size, "object %p: %ld of %ld", self, result, size);
    return result;
}
EI_VERIFY(0x006746B0, StreamReadVerify, 0, s_unused);

static long __fastcall StreamSeekVerify(CResourceStream* self, void*, long offset, int origin) {
    long result = self->Seek(offset, origin);
    ei::verify::Check("CResourceStream::Seek", true, self->offset_ != 0 || result == -1, "object %p", self);
    return result;
}
EI_VERIFY(0x006746F0, StreamSeekVerify, 0, s_unused);

// --- Constructors and destructors -----------------------------------------------------------------

static CMMIOResource* __fastcall MmioConstructVerify(CMMIOResource* self) {
    CMMIOResource* result = self->Construct();
    bool ok = result == self && *reinterpret_cast<std::uintptr_t*>(self) == 0x00743300 && self->fd_ == -1 &&
              self->file_.file_ == reinterpret_cast<void*>(-1) && self->mappedImage_ == nullptr && self->image_ == nullptr;
    ei::verify::Check("CMMIOResource::Construct", true, ok, "object %p", self);
    return result;
}
EI_VERIFY(0x006721D0, MmioConstructVerify, 0, s_unused);
static void __fastcall MmioDestroyVerify(CMMIOResource* self) { self->Destroy(); }
EI_VERIFY(0x006722F0, MmioDestroyVerify, 0, s_unused);
static CMMIOResource* __fastcall MmioDeletingVerify(CMMIOResource* self, void*, unsigned flags) {
    return self->DeletingDestroy(flags);
}
EI_VERIFY(0x006722D0, MmioDeletingVerify, 0, s_unused);

// CResourceManager: ours only (they close, delete and free).
static CResourceManager* __fastcall ManagerConstructVerify(CResourceManager* self) {
    CResourceManager* result = self->Construct();
    bool ok = result == self && *reinterpret_cast<const std::uintptr_t*>(self) == CResourceManager::kVtableAddress &&
              self->archives_.size() == 0;
    ei::verify::Check("CResourceManager::Construct", true, ok, "object %p", self);
    return result;
}
EI_VERIFY(0x00674D90, ManagerConstructVerify, 0, s_unused);
static void __fastcall ManagerDestructVerify(CResourceManager* self) { self->Destruct(); }
EI_VERIFY(0x00674DF0, ManagerDestructVerify, 0, s_unused);
static CResourceManager* __fastcall ManagerDeletingVerify(CResourceManager* self, void*, unsigned flags) {
    return self->DeletingDestruct(flags);
}
EI_VERIFY(0x00674DD0, ManagerDeletingVerify, 0, s_unused);
static void __fastcall ManagerDestroyVerify(CResourceManager* self) {
    self->CResourceManager::Destroy();
    ei::verify::Check("CResourceManager::Destroy", true, self->archives_.size() == 0, "object %p", self);
}
EI_VERIFY(0x00674F40, ManagerDestroyVerify, 0, s_unused);
