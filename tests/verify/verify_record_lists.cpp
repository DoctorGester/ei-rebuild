// Checks for the list<T> instantiations over game records (src/game/record_lists.cpp, S9a).
//
// The chunk stack's insert(pos) (plain records) compares with the original: the original's node is
// taken out again (unlinked, back to the pool), then ours must take the same node, link it the same
// way and hold the same Ref (the three unused bytes are the stack's). The rest run ours only: the
// links must be consistent, each copy as its policy says (members, texts, counted textures, the
// streams' and prints' vtables), an erased node or cleared nodes on top of their free list.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/record_lists.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::LinkedBetween;
using stlp_verify::Name;
using namespace recordlist;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}
template <typename T>
bool SameBits(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

// --- The chunk stack ------------------------------------------------------------------------------

using RefNode = ChunkStack::Node;
bool SameRef(const ChunkRef& a, const ChunkRef& b) {
    return a.id == b.id && a.offset == b.offset && a.size == b.size && a.occurrence == b.occurrence;
}
bool IsResetRef(const ChunkRef& r) { return r.id == 0xFF && r.offset == 0 && r.size == 0 && r.occurrence == 1; }

void* s_chunkInsertDefault = nullptr;
RefNode** __fastcall ChunkInsertDefaultVerify(ChunkStack::List* self, void*, RefNode** out, RefNode* pos) {
    using Fn = RefNode** (__fastcall*)(ChunkStack::List*, void*, RefNode**, RefNode*);
    RefNode* prev = pos->prev;
    RefNode* theirs = nullptr;
    reinterpret_cast<Fn>(s_chunkInsertDefault)(self, nullptr, &theirs, pos);
    bool theirsLinked = LinkedBetween(theirs, prev, pos);
    ChunkRef expected = theirs->data;
    prev->next = pos;
    pos->prev = prev;
    stlp::deallocate(theirs, sizeof(RefNode));
    ChunkStack::InsertDefault(self, nullptr, out, pos);
    RefNode* ours = *out;
    bool ok = theirsLinked && LinkedBetween(ours, prev, pos) && ours == theirs && SameRef(ours->data, expected) &&
              IsResetRef(ours->data);
    char detail[96] = "";
    if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p, pos %p", ours, theirs, pos);
    ei::verify::Report("stlp::list::insert 0040A8D0", ok, detail);
    return out;
}

void __fastcall ChunkPushBackVerify(ChunkStack::List* self) {
    RefNode* prev = self->head->prev;
    ChunkStack::PushBackDefault(self);
    RefNode* node = self->head->prev;
    bool ok = node != prev && LinkedBetween(node, prev, self->head) && IsResetRef(node->data);
    ei::verify::Report("stlp::list::push_back 0040A6B0", ok, ok ? "" : "the new last node is not a reset Ref");
}

void __fastcall ChunkPopBackVerify(ChunkStack::List* self) {
    RefNode* last = self->head->prev;
    RefNode* prev = last->prev;
    ChunkStack::PopBack(self);
    void** list = FreeList(sizeof(RefNode));
    bool ok = self->head->prev == prev && prev->next == self->head && *list == last;
    ei::verify::Report("stlp::list::pop_back 0040A6D0", ok, ok ? "" : "the last node is not unlinked and freed");
}

// --- Erase / clear / ~list of the record lists --------------------------------------------------

template <std::uintptr_t A, typename Abi>
struct EraseCheck {
    using List = typename Abi::List;
    using Node = typename List::Node;
    static constexpr std::size_t kNode = sizeof(Node);

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos) {
        Node* prev = pos->prev;
        Node* next = pos->next;
        Abi::Erase(self, nullptr, out, pos);
        void** list = FreeList(kNode);
        bool ok = *out == next && prev->next == next && next->prev == prev && (!list || *list == pos);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "pos %p, returned %p, next %p", pos, *out, next);
        ei::verify::Report(Name<A>("stlp::list::erase"), ok, detail);
        return out;
    }
    static void __fastcall Clear(List* self) {
        Node* last = self->head->prev;
        Abi::Clear(self);
        void** list = FreeList(kNode);
        bool ok = self->head->next == self->head && self->head->prev == self->head &&
                  (!list || last == self->head || *list == last);
        ei::verify::Report(Name<A>("stlp::list::clear"), ok, ok ? "" : "the list is not empty or the nodes not freed");
    }
    static void __fastcall Destruct(List* self) {
        Node* sentinel = self->head;
        Abi::Destruct(self);
        void** list = FreeList(kNode);
        bool ok = !list || *list == sentinel;
        ei::verify::Report(Name<A>("stlp::list::~list"), ok, ok ? "" : "the sentinel is not on top of its free list");
    }
};

using TradesDestruct = EraseCheck<0x00424A40, Trades>;
using TradesErase = EraseCheck<0x00429770, Trades>;
using QueueErase = EraseCheck<0x004790F0, StreamQueue>;
using QueueClear = EraseCheck<0x00479570, StreamQueue>;
using ChunkErase = EraseCheck<0x0040A920, ChunkStack>;
using ChunkClear = EraseCheck<0x0040B290, ChunkStack>;

// --- The trade records ----------------------------------------------------------------------------

using TradeNode = Trades::Node;
TradeNode** __fastcall TradesInsertDefaultVerify(Trades::List* self, void*, TradeNode** out, TradeNode* pos) {
    TradeNode* prev = pos->prev;
    Trades::InsertDefault(self, nullptr, out, pos);
    TradeNode* node = *out;
    bool ok = LinkedBetween(node, prev, pos);
    for (net::PartyStorage& party : node->data.parties) ok = ok && VtableOf(&party) == CParty::kVtableAddress;
    ei::verify::Report("stlp::list::insert 00424900", ok, ok ? "" : "not linked, or a party is not constructed");
    return out;
}

// --- The link queue -------------------------------------------------------------------------------

using QueueListNode = StreamQueue::Node;
bool IsQueueStream(const CMemoryStream& s) { return VtableOf(&s) == netlink::kNodeStreamVtable; }

QueueListNode** __fastcall QueueInsertDefaultVerify(StreamQueue::List* self, void*, QueueListNode** out,
                                                    QueueListNode* pos) {
    QueueListNode* prev = pos->prev;
    StreamQueue::InsertDefault(self, nullptr, out, pos);
    const CMemoryStream& s = (*out)->data;
    bool ok = LinkedBetween(*out, prev, pos) && IsQueueStream(s) && s.size_ == 0 && s.position_ == 0 &&
              s.data_ == nullptr && s.bitsIndex_ == -1 && s.bitMask_ == 0x80;
    ei::verify::Report("stlp::list::insert 00478FF0", ok, ok ? "" : "not linked, or the stream is not an empty queue stream");
    return out;
}

void __fastcall QueueInsertRangeVerify(StreamQueue::List* self, void*, QueueListNode* pos, const QueueListNode* first,
                                       const QueueListNode* last) {
    QueueListNode* prev = pos->prev;
    int count = 0;
    stlp::list<CMemoryStream>::Distance(first, last, count);
    StreamQueue::InsertRange(self, nullptr, pos, first, last);
    bool ok = true;
    const QueueListNode* source = first;
    QueueListNode* node = prev->next;
    for (int i = 0; i < count && ok; ++i, source = source->next, node = node->next) {
        ok = node != pos && node->prev->next == node && IsQueueStream(node->data) && node->data.size_ == source->data.size_ &&
             node->data.position_ == node->data.size_;
    }
    ok = ok && node == pos && pos->prev->next == pos;
    ei::verify::Report("stlp::list::insert 004795B0", ok, ok ? "" : "the copies are not linked in order or not sized");
}

// --- The link archives ----------------------------------------------------------------------------

using ArchiveNode = LinkArchives::Node;
ArchiveNode** __fastcall ArchivesInsertDefaultVerify(LinkArchives::List* self, void*, ArchiveNode** out, ArchiveNode* pos) {
    ArchiveNode* prev = pos->prev;
    LinkArchives::InsertDefault(self, nullptr, out, pos);
    const stlp::string& name = (*out)->data.name;
    bool ok = LinkedBetween(*out, prev, pos) && name.empty() && name.data() != nullptr && *name.data() == '\0';
    ei::verify::Report("stlp::list::insert 004FB2E0", ok, ok ? "" : "not linked, or the name is not an empty string");
    return out;
}

// --- The ground prints ----------------------------------------------------------------------------

// GroundPrint's members after CopyConstruct: the copy's own name block with the same text, the
// texture counted once more (`refs` before), everything else bit for bit.
bool CopiedPrint(const GroundPrint& copy, const GroundPrint& x, int refs) {
    bool texture = copy.texture_.texture == x.texture_.texture &&
                   (!x.texture_.texture || x.texture_.texture->refCount_ == refs + 1);
    bool name = copy.texture_.name == x.texture_.name && (x.texture_.name.data() == nullptr ||
                                                         copy.texture_.name.data() != x.texture_.name.data());
    return texture && name && copy.built_ == x.built_ && copy.visible_ == x.visible_ && SameBits(copy.position_, x.position_) &&
           SameBits(copy.halfWidth_, x.halfWidth_) && SameBits(copy.halfLength_, x.halfLength_) &&
           SameBits(copy.growth_, x.growth_) && copy.life_ == x.life_ && SameBits(copy.uvOffset_, x.uvOffset_) &&
           SameBits(copy.uvScale_, x.uvScale_) && SameBits(copy.uvAngle_, x.uvAngle_) && SameBits(copy.angle_, x.angle_) &&
           copy.lit_ == x.lit_ && SameBits(copy.alpha_, x.alpha_) && SameBits(copy.alphaScale_, x.alphaScale_) &&
           copy.colour_ == x.colour_ && copy.lifetime_ == x.lifetime_ && SameBits(copy.positions_, x.positions_) &&
           SameBits(copy.texCoords_, x.texCoords_) && SameBits(copy.indices_, x.indices_);
}
int TextureRefs(const GroundPrint& x) { return x.texture_.texture ? x.texture_.texture->refCount_ : 0; }

GroundPrint* __fastcall PrintCopyVerify(GroundPrint* self, void*, const GroundPrint* other) {
    int refs = TextureRefs(*other);
    GroundPrint* result = self->CopyConstruct(other);
    bool ok = result == self && CopiedPrint(*self, *other, refs) && VtableOf(self) == GroundPrint::kCopyVtableAddress;
    ei::verify::Report("GroundPrint::CopyConstruct 00571FC0", ok, ok ? "" : "a member differs from the source");
    return result;
}

template <std::uintptr_t A, typename Abi, std::uintptr_t Vtable, typename Own>
struct PrintInsertCheck {
    using List = typename Abi::List;
    using Node = typename List::Node;
    using T = decltype(Node::data);

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos, const T* x) {
        Node* prev = pos->prev;
        int refs = TextureRefs(*x);
        Abi::Insert(self, nullptr, out, pos, x);
        const T& copy = (*out)->data;
        bool ok = LinkedBetween(*out, prev, pos) && CopiedPrint(copy, *x, refs) && VtableOf(&copy) == Vtable &&
                  Own::Same(copy, *x);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "node %p, pos %p", *out, pos);
        ei::verify::Report(Name<A>("stlp::list::insert"), ok, detail);
        return out;
    }
};
struct BloodOwn {
    static bool Same(const BloodPrint& copy, const BloodPrint& x) {
        return SameBits(copy.spin_, x.spin_) && copy.spreadTicks_ == x.spreadTicks_ && copy.kind_ == x.kind_ &&
               copy.fresh_ == x.fresh_;
    }
};
struct FootprintOwn {
    static bool Same(const Footprint& copy, const Footprint& x) { return SameBits(copy.scale_, x.scale_) && copy.kind_ == x.kind_; }
};
struct ScorchOwn {
    static bool Same(const GroundPrint&, const GroundPrint&) { return true; }
};
using BloodInsert = PrintInsertCheck<0x0056ED90, BloodPrints, BloodPrint::kVtableAddress, BloodOwn>;
using FootprintInsert = PrintInsertCheck<0x0056EED0, Footprints, Footprint::kVtableAddress, FootprintOwn>;
using ScorchInsert = PrintInsertCheck<0x0056F010, ScorchMarks, GroundPrint::kVtableAddress, ScorchOwn>;

// --- The titles' lines ----------------------------------------------------------------------------

using LineNode = TitleLines::Node;
LineNode** __fastcall TitleInsertVerify(TitleLines::List* self, void*, LineNode** out, LineNode* pos, const TitleLine* x) {
    LineNode* prev = pos->prev;
    TitleLines::Insert(self, nullptr, out, pos, x);
    const TitleLine& copy = (*out)->data;
    bool ok = LinkedBetween(*out, prev, pos) && copy.scroll == x->scroll && SameBits(copy.speed, x->speed) &&
              std::strcmp(copy.text, x->text) == 0 && std::strcmp(copy.style, x->style) == 0;
    ei::verify::Report("stlp::list::insert 0062C090", ok, ok ? "" : "not linked, or a member differs");
    return out;
}

} // namespace

// The chunk stack: insert(pos) against the original (sub esp,0x10; xor eax,eax: 5 bytes); push_back(),
// pop_back(), erase(pos) and clear() ours only.
EI_VERIFY(0x0040A8D0, ChunkInsertDefaultVerify, 5, s_chunkInsertDefault);
EI_VERIFY(0x0040A6B0, ChunkPushBackVerify, 0, s_unused);
EI_VERIFY(0x0040A6D0, ChunkPopBackVerify, 0, s_unused);
EI_VERIFY(0x0040A920, ChunkErase::Verify, 0, s_unused);
EI_VERIFY(0x0040B290, ChunkClear::Clear, 0, s_unused);

// The rest: ours only.
EI_VERIFY(0x00424900, TradesInsertDefaultVerify, 0, s_unused);
EI_VERIFY(0x00424A40, TradesDestruct::Destruct, 0, s_unused);
EI_VERIFY(0x00429770, TradesErase::Verify, 0, s_unused);
EI_VERIFY(0x00478FF0, QueueInsertDefaultVerify, 0, s_unused);
EI_VERIFY(0x004790F0, QueueErase::Verify, 0, s_unused);
EI_VERIFY(0x00479570, QueueClear::Clear, 0, s_unused);
EI_VERIFY(0x004795B0, QueueInsertRangeVerify, 0, s_unused);
EI_VERIFY(0x004FB2E0, ArchivesInsertDefaultVerify, 0, s_unused);
EI_VERIFY(0x00571FC0, PrintCopyVerify, 0, s_unused);
EI_VERIFY(0x0056ED90, BloodInsert::Verify, 0, s_unused);
EI_VERIFY(0x0056EED0, FootprintInsert::Verify, 0, s_unused);
EI_VERIFY(0x0056F010, ScorchInsert::Verify, 0, s_unused);
EI_VERIFY(0x0062C090, TitleInsertVerify, 0, s_unused);
