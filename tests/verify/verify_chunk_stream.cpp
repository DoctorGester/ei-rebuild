// Differential checks for chunk_stream.cpp.
//
// The chunk functions read and write a real file, so they cannot simply run twice. Ours runs first
// on the real chunks, with the file replaced by a proxy that forwards every call to the real file
// and records it (arguments, results, the bytes read or written). The original then runs on copies
// of the chunk chain (the copies' root returns a replaying proxy): it must make the same calls in
// the same order, with the same arguments and written bytes, and it gets the recorded results and
// bytes. The chains must end equal (id, consumed, size, start of every chunk), and so must the
// outputs. The file is put in place by giving the chain's root a vtable whose slot 1 (the file)
// returns the proxy; the real root's vtable pointer is restored right after ours returns.
//
// Advance (no file) runs on a copy of the chain against ours; the constructor and destructor body
// on scratch copies. ForEachChild calls the chunks' OnChunk handlers (the zone loader's code), so it
// runs ours only. The text readers compare the strings (the original builds its own in a scratch
// slot or a scratch CStringArray); the root's Open and destructor run ours only.
#include "verify.h"

#include "../../src/game/chunk_stream.h"
#include "../../src/game/console.h"   // CStringArray

using console::CStringArray;

#include "ei/vcall.h"

#include <cstring>

namespace {

void* s_unused;

// --- Recorded file -----------------------------------------------------------------------------------

struct FileOp {
    int slot;
    std::uint32_t a, b, result;
    std::uint32_t dataAt, dataSize;
};

struct FileLog {
    static constexpr int kOps = 256;
    static constexpr std::uint32_t kData = 256 * 1024;
    FileOp ops[kOps];
    int count;
    std::uint8_t data[kData];
    std::uint32_t used;
    bool overflow;
};

struct ProxyFile {
    void* const* vtable;
    FileLog* log;
    void* real;      // recording: the real file; replaying: null
    int next;        // replay position
    bool mismatch;
};

FileOp* Record(ProxyFile* f, int slot, std::uint32_t a, std::uint32_t b, std::uint32_t result) {
    FileLog& log = *f->log;
    if (log.count >= FileLog::kOps) {
        log.overflow = true;
        return nullptr;
    }
    FileOp& op = log.ops[log.count++];
    op = {slot, a, b, result, log.used, 0};
    return &op;
}

void RecordData(ProxyFile* f, FileOp* op, const void* p, std::uint32_t n) {
    FileLog& log = *f->log;
    if (!op || n > FileLog::kData - log.used) {
        log.overflow = true;
        return;
    }
    std::memcpy(log.data + log.used, p, n);
    op->dataAt = log.used;
    op->dataSize = n;
    log.used += n;
}

const FileOp* Expect(ProxyFile* f, int slot, std::uint32_t a, std::uint32_t b) {
    if (f->next >= f->log->count) {
        f->mismatch = true;
        return nullptr;
    }
    const FileOp& op = f->log->ops[f->next++];
    if (op.slot != slot || op.a != a || op.b != b) {
        f->mismatch = true;
        return nullptr;
    }
    return &op;
}

std::uint32_t __fastcall ProxyGetPosition(ProxyFile* f) {
    if (f->real) {
        std::uint32_t r = ei::VCall<std::uint32_t>(f->real, 3);
        Record(f, 3, 0, 0, r);
        return r;
    }
    const FileOp* op = Expect(f, 3, 0, 0);
    return op ? op->result : 0;
}

std::uint32_t __fastcall ProxySeek(ProxyFile* f, void*, std::int32_t offset, std::uint32_t from) {
    if (f->real) {
        std::uint32_t r = ei::VCall<std::uint32_t>(f->real, 10, offset, from);
        Record(f, 10, static_cast<std::uint32_t>(offset), from, r);
        return r;
    }
    const FileOp* op = Expect(f, 10, static_cast<std::uint32_t>(offset), from);
    return op ? op->result : 0;
}

std::uint32_t __fastcall ProxyRead(ProxyFile* f, void*, void* p, std::uint32_t n) {
    if (f->real) {
        std::uint32_t r = ei::VCall<std::uint32_t>(f->real, 13, p, n);
        RecordData(f, Record(f, 13, n, 0, r), p, r <= n ? r : n);
        return r;
    }
    const FileOp* op = Expect(f, 13, n, 0);
    if (!op) return 0;
    std::memcpy(p, f->log->data + op->dataAt, op->dataSize);
    return op->result;
}

void __fastcall ProxyWrite(ProxyFile* f, void*, const void* p, std::uint32_t n) {
    if (f->real) {
        ei::VCall<void>(f->real, 14, p, n);
        RecordData(f, Record(f, 14, n, 0, 0), p, n);
        return;
    }
    const FileOp* op = Expect(f, 14, n, 0);
    if (op && (op->dataSize != n || std::memcmp(f->log->data + op->dataAt, p, n) != 0)) f->mismatch = true;
}

void* const* ProxyVtable() {
    static void* vtable[16] = {};
    vtable[3] = reinterpret_cast<void*>(&ProxyGetPosition);
    vtable[10] = reinterpret_cast<void*>(&ProxySeek);
    vtable[13] = reinterpret_cast<void*>(&ProxyRead);
    vtable[14] = reinterpret_cast<void*>(&ProxyWrite);
    return vtable;
}

// The proxy slot 1 of the roots returns.
ProxyFile* g_file;
void* __fastcall RootFile(void*) { return g_file; }

// --- Chunk chains ------------------------------------------------------------------------------------

constexpr int kMaxDepth = 32;

std::uintptr_t VtableOf(const Chunk* c) {
    std::uintptr_t v;
    std::memcpy(&v, c, sizeof(v));
    return v;
}
void SetVtable(Chunk* c, const void* vtable) { std::memcpy(c, &vtable, sizeof(vtable)); }

// The chain from a chunk up to its root.
// With `toFileOwner` the chain ends at the first chunk whose slot 1 is not the base one (0042BF40):
// that chunk owns the file. Without an owner (a base chunk without parent at the end) it is empty.
constexpr std::uintptr_t kBaseFileSlot = 0x0042BF40;

struct Chain {
    Chunk* links[kMaxDepth];
    int depth = 0;
    explicit Chain(Chunk* self, bool toFileOwner = false) {
        for (Chunk* c = self; c; c = c->parent_) {
            if (depth == kMaxDepth) {
                depth = 0;
                return;
            }
            links[depth++] = c;
            if (toFileOwner && reinterpret_cast<std::uintptr_t const*>(VtableOf(c))[1] != kBaseFileSlot) return;
        }
        if (toFileOwner) depth = 0;
    }
    Chunk* Root() const { return links[depth - 1]; }
};

// A copy of a chain; the copy's root answers slot 1 with g_file.
struct ChainCopy {
    Chunk chunks[kMaxDepth];
    void* rootVtable[2];
    int depth;
    explicit ChainCopy(const Chain& chain) : depth(chain.depth) {
        for (int i = 0; i < depth; ++i) {
            std::memcpy(&chunks[i], chain.links[i], sizeof(Chunk));
            chunks[i].parent_ = i + 1 < depth ? &chunks[i + 1] : nullptr;
        }
        if (depth) {
            auto* real = reinterpret_cast<void* const*>(VtableOf(chain.Root()));
            rootVtable[0] = real[0];
            rootVtable[1] = reinterpret_cast<void*>(&RootFile);
            SetVtable(&chunks[depth - 1], rootVtable);
        }
    }
    // id, consumed, size and start of every chunk.
    bool Same(const Chain& chain) const {
        for (int i = 0; i < depth; ++i) {
            if (std::memcmp(&chunks[i].id_, &chain.links[i]->id_, offsetof(Chunk, parent_) - offsetof(Chunk, id_)) != 0)
                return false;
        }
        return true;
    }
};

bool g_busy;   // file checks do not nest (ForEachChild runs ours only)

// Runs ours on the real chain through a recording proxy, then the original on a copy through a
// replaying one. Returns false when the check could not run (ours has run then).
template <typename Ours, typename Original>
bool RunRecorded(const char* name, Chunk* self, Ours ours, Original original) {
    Chain chain(self, true);
    void* file = chain.depth && !g_busy ? chain.Root()->File() : nullptr;
    if (!file) {
        ours();
        return false;
    }
    g_busy = true;
    static FileLog log;
    log.count = 0;
    log.used = 0;
    log.overflow = false;
    auto* copy = new ChainCopy(chain);

    // Ours, with the real root's slot 1 pointing at the recorder.
    ProxyFile recorder{ProxyVtable(), &log, file, 0, false};
    Chunk* root = chain.Root();
    std::uintptr_t realVtable = VtableOf(root);
    void* fake[2] = {reinterpret_cast<void* const*>(realVtable)[0], reinterpret_cast<void*>(&RootFile)};
    ProxyFile* outer = g_file;
    g_file = &recorder;
    SetVtable(root, fake);
    ours();
    SetVtable(root, reinterpret_cast<void*>(realVtable));

    bool ran = !log.overflow;
    if (ran) {
        ProxyFile replay{ProxyVtable(), &log, nullptr, 0, false};
        g_file = &replay;
        original(&copy->chunks[0]);
        bool same = !replay.mismatch && replay.next == log.count && copy->Same(chain);
        ei::verify::Check(name, true, same, "calls %d / %d%s", replay.next, log.count, replay.mismatch ? ", mismatch" : "");
    }
    g_file = outer;
    g_busy = false;
    delete copy;
    return ran;
}

// --- Object only -------------------------------------------------------------------------------------

// 0042BEC0: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX+8],ECX (3).
Chunk*(__fastcall* s_construct)(Chunk*, void*, Chunk*);
Chunk* __fastcall ConstructVerify(Chunk* self, void*, Chunk* parent) {
    Chunk expected;
    std::memcpy(&expected, self, sizeof(Chunk));
    Chunk* expectedRet = s_construct(&expected, nullptr, parent);
    Chunk* actualRet = self->Construct(parent);
    ei::verify::Check("Chunk::Construct", true,
                      expectedRet == &expected && actualRet == self && std::memcmp(&expected, self, sizeof(Chunk)) == 0, "");
    return actualRet;
}

// 0042BEF0: MOV [ECX],imm32 (6).
void(__fastcall* s_destruct)(Chunk*);
void __fastcall DestructVerify(Chunk* self) {
    Chunk expected;
    std::memcpy(&expected, self, sizeof(Chunk));
    s_destruct(&expected);
    self->Destruct();
    ei::verify::Check("Chunk::Destruct", true, std::memcmp(&expected, self, sizeof(Chunk)) == 0, "");
}

// 0042BF00: PUSH ESI (1), MOV ESI,ECX (2), MOV ECX,[ESI+0x14] (3).
void(__fastcall* s_advance)(Chunk*, void*, int);
void __fastcall AdvanceVerify(Chunk* self, void*, int bytes) {
    Chain chain(self);
    if (!chain.depth) {
        self->Advance(bytes);
        return;
    }
    auto* copy = new ChainCopy(chain);
    s_advance(&copy->chunks[0], nullptr, bytes);
    self->Advance(bytes);
    ei::verify::Check("Chunk::Advance", true, copy->Same(chain), "%d bytes, depth %d", bytes, chain.depth);
    delete copy;
}

// 0042BF40 (slot 1): MOV ECX,[ECX+0x14] (3), TEST ECX,ECX (2). Pure. Ours is called qualified (a
// virtual call would come back here).
void*(__fastcall* s_file)(Chunk*);
void* __fastcall FileVerify(Chunk* self) {
    void* expected = s_file(self);
    void* actual = self->Chunk::File();
    ei::verify::Check("Chunk::File", expected, actual, "");
    return actual;
}

// --- With the file -------------------------------------------------------------------------------------


// 0042BF50: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI] (2).
void(__fastcall* s_beginWrite)(Chunk*, void*, std::uint32_t);
void __fastcall BeginWriteVerify(Chunk* self, void*, std::uint32_t id) {
    RunRecorded("Chunk::BeginWrite", self, [&] { self->BeginWrite(id); }, [&](Chunk* c) { s_beginWrite(c, nullptr, id); });
}

// 0042BFB0: PUSH ESI (1), MOV ESI,ECX (2), PUSH EDI (1), MOV EAX,[ESI] (2).
void(__fastcall* s_endWrite)(Chunk*);
void __fastcall EndWriteVerify(Chunk* self) {
    RunRecorded("Chunk::EndWrite", self, [&] { self->EndWrite(); }, [&](Chunk* c) { s_endWrite(c); });
}

// 0042C000: MOV EDX,[ESP+8] (4), PUSH ESI (1).
void(__fastcall* s_writeHeader)(Chunk*, void*, std::uint32_t, int);
void __fastcall WriteHeaderVerify(Chunk* self, void*, std::uint32_t id, int payload) {
    RunRecorded("Chunk::WriteHeader", self, [&] { self->WriteHeader(id, payload); },
                [&](Chunk* c) { s_writeHeader(c, nullptr, id, payload); });
}

// 0042C060: PUSH ESI (1), MOV ESI,ECX (2), CMP [ESI+8],8 (4). Only AL is the result.
int(__fastcall* s_skip)(Chunk*);
bool __fastcall SkipVerify(Chunk* self) {
    bool actual = false, expected = false;
    if (RunRecorded("Chunk::SkipContent", self, [&] { actual = self->SkipContent(); },
                    [&](Chunk* c) { expected = (s_skip(c) & 0xFF) != 0; }))
        ei::verify::Check("Chunk::SkipContent result", expected, actual, "");
    return actual;
}

// 0042C0B0: PUSH EBX (1), PUSH ESI (1), MOV ESI,ECX (2), MOV EBX,8 (5).
void(__fastcall* s_readHeader)(Chunk*);
void __fastcall ReadHeaderVerify(Chunk* self) {
    RunRecorded("Chunk::ReadHeader", self, [&] { self->ReadHeader(); }, [&](Chunk* c) { s_readHeader(c); });
}

// 0042C140: ours only (it calls the chunks' handlers).
void __fastcall ForEachChildVerify(Chunk* self) {
    self->ForEachChild();
    ei::verify::Check("Chunk::ForEachChild", true, !self->parent_ || self->parent_->size_ == self->parent_->consumed_, "");
}

// 0042C280 / 0042C340: PUSH ECX (1), PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI] (2).
float(__fastcall* s_readFloat)(Chunk*);
float __fastcall ReadFloatVerify(Chunk* self) {
    float actual = 0, expected = 0;
    if (RunRecorded("Chunk::ReadFloat", self, [&] { actual = self->ReadFloat(); }, [&](Chunk* c) { expected = s_readFloat(c); }))
        ei::verify::Check("Chunk::ReadFloat value", expected, actual, "");
    return actual;
}
int(__fastcall* s_readInt)(Chunk*);
int __fastcall ReadIntVerify(Chunk* self) {
    int actual = 0, expected = 0;
    if (RunRecorded("Chunk::ReadInt", self, [&] { actual = self->ReadInt(); }, [&](Chunk* c) { expected = s_readInt(c); }))
        ei::verify::Check("Chunk::ReadInt value", expected, actual, "");
    return actual;
}
// 0042C4C0: same prologue. Only AL is the result.
int(__fastcall* s_readByte)(Chunk*);
std::uint8_t __fastcall ReadByteVerify(Chunk* self) {
    std::uint8_t actual = 0, expected = 0;
    if (RunRecorded("Chunk::ReadByte", self, [&] { actual = self->ReadByte(); },
                    [&](Chunk* c) { expected = static_cast<std::uint8_t>(s_readByte(c)); }))
        ei::verify::Check("Chunk::ReadByte value", expected, actual, "");
    return actual;
}

// 0042C2C0 / 0042C380 / 0042C500 / 0042CD70 / 0042CFA0 / 0042D0E0: PUSH ECX (1), MOV EAX,[ESP+8] (4).
void(__fastcall* s_writeFloat)(Chunk*, void*, std::uint32_t, std::uint32_t);
void __fastcall WriteFloatVerify(Chunk* self, void*, std::uint32_t id, std::uint32_t bits) {
    RunRecorded("Chunk::WriteFloat", self, [&] { self->WriteFloat(id, bits); }, [&](Chunk* c) { s_writeFloat(c, nullptr, id, bits); });
}
void(__fastcall* s_writeInt)(Chunk*, void*, std::uint32_t, int);
void __fastcall WriteIntVerify(Chunk* self, void*, std::uint32_t id, int value) {
    RunRecorded("Chunk::WriteInt", self, [&] { self->WriteInt(id, value); }, [&](Chunk* c) { s_writeInt(c, nullptr, id, value); });
}
void(__fastcall* s_writeByte)(Chunk*, void*, std::uint32_t, std::uint32_t);
void __fastcall WriteByteVerify(Chunk* self, void*, std::uint32_t id, std::uint32_t value) {
    RunRecorded("Chunk::WriteByte", self, [&] { self->WriteByte(id, static_cast<std::uint8_t>(value)); },
                [&](Chunk* c) { s_writeByte(c, nullptr, id, value); });
}
void(__fastcall* s_writeVector3)(Chunk*, void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
void __fastcall WriteVector3Verify(Chunk* self, void*, std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    RunRecorded("Chunk::WriteVector3", self, [&] { self->WriteVector3(id, x, y, z); },
                [&](Chunk* c) { s_writeVector3(c, nullptr, id, x, y, z); });
}
void(__fastcall* s_writeBlob)(Chunk*, void*, std::uint32_t, const void*, int);
void __fastcall WriteBlobVerify(Chunk* self, void*, std::uint32_t id, const void* data, int size) {
    RunRecorded("Chunk::WriteBlob", self, [&] { self->WriteBlob(id, data, size); },
                [&](Chunk* c) { s_writeBlob(c, nullptr, id, data, size); });
}
void(__fastcall* s_writeQuaternion)(Chunk*, void*, std::uint32_t, Float4);
void __fastcall WriteQuaternionVerify(Chunk* self, void*, std::uint32_t id, Float4 value) {
    RunRecorded("Chunk::WriteQuaternion", self, [&] { self->WriteQuaternion(id, value); },
                [&](Chunk* c) { s_writeQuaternion(c, nullptr, id, value); });
}

// Readers into an output: the original fills a scratch copy of it.
// 0042CCC0: SUB ESP,0xC (3), PUSH ESI (1), MOV ESI,ECX (2).
Vector3*(__fastcall* s_readVector3)(Chunk*, void*, Vector3*);
Vector3* __fastcall ReadVector3Verify(Chunk* self, void*, Vector3* out) {
    Vector3 scratch;
    std::memcpy(&scratch, out, sizeof(scratch));
    Vector3* ret = nullptr;
    Vector3* expectedRet = nullptr;
    if (RunRecorded("Chunk::ReadVector3", self, [&] { ret = self->ReadVector3(out); },
                    [&](Chunk* c) { expectedRet = s_readVector3(c, nullptr, &scratch); }))
        ei::verify::Check("Chunk::ReadVector3 value", true,
                          expectedRet == &scratch && ret == out && std::memcmp(&scratch, out, sizeof(scratch)) == 0, "");
    return ret;
}
// 0042CEC0: SUB ESP,8 (3), PUSH EBX (1), PUSH EBP (1).
float*(__fastcall* s_readQuad)(Chunk*, void*, float*);
float* __fastcall ReadQuadVerify(Chunk* self, void*, float* out) {
    float scratch[4];
    std::memcpy(scratch, out, sizeof(scratch));
    float* ret = nullptr;
    float* expectedRet = nullptr;
    if (RunRecorded("Chunk::ReadQuad", self, [&] { ret = self->ReadQuad(out); },
                    [&](Chunk* c) { expectedRet = s_readQuad(c, nullptr, scratch); }))
        ei::verify::Check("Chunk::ReadQuad value", true,
                          expectedRet == scratch && ret == out && std::memcmp(scratch, out, sizeof(scratch)) == 0, "");
    return ret;
}
// 0042D060: SUB ESP,0x10 (3), PUSH ESI (1), MOV ESI,ECX (2).
float*(__fastcall* s_readQuaternion)(Chunk*, void*, float*);
float* __fastcall ReadQuaternionVerify(Chunk* self, void*, float* out) {
    float scratch[4];
    std::memcpy(scratch, out, sizeof(scratch));
    float* ret = nullptr;
    float* expectedRet = nullptr;
    if (RunRecorded("Chunk::ReadQuaternion", self, [&] { ret = self->ReadQuaternion(out); },
                    [&](Chunk* c) { expectedRet = s_readQuaternion(c, nullptr, scratch); }))
        ei::verify::Check("Chunk::ReadQuaternion value", true,
                          expectedRet == scratch && ret == out && std::memcmp(scratch, out, sizeof(scratch)) == 0, "");
    return ret;
}
// 0042D020: PUSH ESI (1), MOV ESI,ECX (2), PUSH EDI (1), MOV EAX,[ESI] (2).
void(__fastcall* s_readBytes)(Chunk*, void*, void*, int);
void __fastcall ReadBytesVerify(Chunk* self, void*, void* data, int size) {
    if (size < 0 || size > 0x10000) {
        self->ReadBytes(data, size);
        return;
    }
    auto* scratch = new std::uint8_t[size ? size : 1];
    std::memcpy(scratch, data, size);
    if (RunRecorded("Chunk::ReadBytes", self, [&] { self->ReadBytes(data, size); },
                    [&](Chunk* c) { s_readBytes(c, nullptr, scratch, size); }))
        ei::verify::Check("Chunk::ReadBytes data", true, std::memcmp(scratch, data, size) == 0, "%d bytes", size);
    delete[] scratch;
}

// --- Text ----------------------------------------------------------------------------------------

// 0042C580 / 0042C650: PUSH -1 (2), PUSH imm32 (5). The original builds its string in a scratch
// slot; the texts must be equal (the scratch is destroyed afterwards).
using ReadText = mfc::CString*(__fastcall*)(Chunk*, void*, mfc::CString*);
template <typename Ours>
mfc::CString* ReadTextChecked(const char* name, Chunk* self, mfc::CString* out, Ours ours, ReadText original) {
    alignas(mfc::CString) std::uint8_t slot[sizeof(mfc::CString)];
    auto* scratch = reinterpret_cast<mfc::CString*>(slot);
    mfc::CString* ret = nullptr;
    mfc::CString* expectedRet = nullptr;
    if (RunRecorded(name, self, [&] { ret = ours(); }, [&](Chunk* c) { expectedRet = original(c, nullptr, scratch); })) {
        bool same = expectedRet == scratch && ret == out && scratch->GetLength() == out->GetLength() &&
                    std::memcmp(static_cast<const char*>(*scratch), static_cast<const char*>(*out), out->GetLength()) == 0;
        ei::verify::Check(name, true, same, "text");
        scratch->~CString();
    }
    return ret;
}
ReadText s_readString;
mfc::CString* __fastcall ReadStringVerify(Chunk* self, void*, mfc::CString* out) {
    return ReadTextChecked("Chunk::ReadString", self, out, [&] { return self->ReadString(out); }, s_readString);
}
ReadText s_readEncodedString;
mfc::CString* __fastcall ReadEncodedStringVerify(Chunk* self, void*, mfc::CString* out) {
    return ReadTextChecked("Chunk::ReadEncodedString", self, out, [&] { return self->ReadEncodedString(out); },
                           s_readEncodedString);
}

// 0042C820: PUSH -1 (2), PUSH imm32 (5). Each run gets its own copy of the text (and destroys it).
void(__fastcall* s_writeString)(Chunk*, void*, std::uint32_t, mfc::CString);
void __fastcall WriteStringVerify(Chunk* self, void*, std::uint32_t id, mfc::CString text) {
    RunRecorded("Chunk::WriteString", self, [&] { self->WriteString(id, text); },
                [&](Chunk* c) { s_writeString(c, nullptr, id, text); });
}

// 0042CA20: MOV EAX,FS:[0] (6). The original fills an empty scratch array (same vtable and grow-by,
// freed again with SetSize(0, -1)); the elements must match.
void(__fastcall* s_readStringArray)(Chunk*, void*, CStringArray*);
void __fastcall ReadStringArrayVerify(Chunk* self, void*, CStringArray* array) {
    alignas(CStringArray) std::uint8_t raw[sizeof(CStringArray)];
    auto* scratch = reinterpret_cast<CStringArray*>(raw);
    scratch->vtable_ = array->vtable_;
    scratch->data_ = nullptr;
    scratch->size_ = 0;
    scratch->maxSize_ = 0;
    scratch->growBy_ = array->growBy_;
    if (RunRecorded("Chunk::ReadStringArray", self, [&] { self->ReadStringArray(array); },
                    [&](Chunk* c) { s_readStringArray(c, nullptr, scratch); })) {
        bool same = scratch->GetSize() == array->GetSize();
        for (int i = 0; same && i < array->GetSize(); ++i)
            same = (*scratch)[i].GetLength() == (*array)[i].GetLength() &&
                   std::memcmp(static_cast<const char*>((*scratch)[i]), static_cast<const char*>((*array)[i]),
                               (*array)[i].GetLength()) == 0;
        ei::verify::Check("Chunk::ReadStringArray elements", true, same, "%d / %d", scratch->GetSize(), array->GetSize());
        scratch->SetSize(0, -1);
    }
}

// 0042CB80: SUB ESP,0xC (3), PUSH EBX (1), PUSH EBP (1). Both write the same array.
void(__fastcall* s_writeStringArray)(Chunk*, void*, std::uint32_t, CStringArray*);
void __fastcall WriteStringArrayVerify(Chunk* self, void*, std::uint32_t id, CStringArray* array) {
    RunRecorded("Chunk::WriteStringArray", self, [&] { self->WriteStringArray(id, array); },
                [&](Chunk* c) { s_writeStringArray(c, nullptr, id, array); });
}

// The root: ours only (opening a file and destroying it cannot run twice).
int __fastcall RootOpenVerify(RootChunk* self, void*, mfc::CString path, unsigned flags) {
    int result = self->Open(path, flags);
    ei::verify::Check("RootChunk::Open", true, true, "");
    return result;
}
void __fastcall RootDestructVerify(RootChunk* self) {
    self->Destruct();
    ei::verify::Check("RootChunk::Destruct", Chunk::kVtableAddress, VtableOf(self), "");
}

} // namespace

EI_VERIFY(0x0042BEC0, ConstructVerify, 7, s_construct);
EI_VERIFY(0x0042BEF0, DestructVerify, 6, s_destruct);
EI_VERIFY(0x0042BF00, AdvanceVerify, 6, s_advance);
EI_VERIFY(0x0042BF40, FileVerify, 5, s_file);
EI_VERIFY(0x0042BF50, BeginWriteVerify, 5, s_beginWrite);
EI_VERIFY(0x0042BFB0, EndWriteVerify, 6, s_endWrite);
EI_VERIFY(0x0042C000, WriteHeaderVerify, 5, s_writeHeader);
EI_VERIFY(0x0042C060, SkipVerify, 7, s_skip);
EI_VERIFY(0x0042C0B0, ReadHeaderVerify, 9, s_readHeader);
EI_VERIFY(0x0042C140, ForEachChildVerify, 0, s_unused);
EI_VERIFY(0x0042C280, ReadFloatVerify, 6, s_readFloat);
EI_VERIFY(0x0042C340, ReadIntVerify, 6, s_readInt);
EI_VERIFY(0x0042C4C0, ReadByteVerify, 6, s_readByte);
EI_VERIFY(0x0042C2C0, WriteFloatVerify, 5, s_writeFloat);
EI_VERIFY(0x0042C380, WriteIntVerify, 5, s_writeInt);
EI_VERIFY(0x0042C500, WriteByteVerify, 5, s_writeByte);
EI_VERIFY(0x0042CD70, WriteVector3Verify, 5, s_writeVector3);
EI_VERIFY(0x0042CFA0, WriteBlobVerify, 5, s_writeBlob);
EI_VERIFY(0x0042D0E0, WriteQuaternionVerify, 5, s_writeQuaternion);
EI_VERIFY(0x0042CCC0, ReadVector3Verify, 6, s_readVector3);
EI_VERIFY(0x0042CEC0, ReadQuadVerify, 5, s_readQuad);
EI_VERIFY(0x0042D060, ReadQuaternionVerify, 6, s_readQuaternion);
EI_VERIFY(0x0042D020, ReadBytesVerify, 6, s_readBytes);
EI_VERIFY(0x0042C580, ReadStringVerify, 7, s_readString);
EI_VERIFY(0x0042C650, ReadEncodedStringVerify, 7, s_readEncodedString);
EI_VERIFY(0x0042C820, WriteStringVerify, 7, s_writeString);
EI_VERIFY(0x0042CA20, ReadStringArrayVerify, 6, s_readStringArray);
EI_VERIFY(0x0042CB80, WriteStringArrayVerify, 5, s_writeStringArray);
EI_VERIFY(0x0042D160, RootOpenVerify, 0, s_unused);
EI_VERIFY(0x0042D270, RootDestructVerify, 0, s_unused);
