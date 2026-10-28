#pragma once

// Element policies (lib/stlp_vector.h) of the game's vector<T> instantiations whose elements are game records
// with their own copy constructor, operator= and destructor, and their VectorAbi types (docs/notes/standalone.md).

#include "engine_object.h"
#include "model.h"
#include "net_classes.h"
#include "record_members.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_screens.h"

#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace recordvec {

// ---- The client's lobby queries (net_classes.h; net_lobby.cpp) -----------------------------

// A query: the copy constructor, the copy loop's assignment (the address, then the reply buffer's), the
// default (a zero address, an empty growing buffer) and the destructor (the reply buffer's).
struct QueryElement {
    using R = net::Query;
    static void Construct(R* to, const R& from) { new (to) R(from); }
    static void Assign(R& to, const R& from) { to = from; }
    static void Default(R* p) { new (p) R(); }
    static void Destroy(R* p) { p->~R(); }
};

using QueryVector = stlp::VectorAbi<net::Query, QueryElement>;

// ---- The session's replicated messages (net_classes.h; net_session_base.cpp) -------------

// {message, priority}: plain bytes; push_back(T()) builds nothing (the constructor is
// empty), the caller stores into the new element.
struct ReplicatedElement : stlp::PlainElement<net::Replicated> {
    static void Default(net::Replicated*) {}
};

using ReplicatedVector = stlp::VectorAbi<net::Replicated, ReplicatedElement>;

// ---- The client party's engine objects (net_client_party.cpp) -----------------------------

// Counted engine object pointers: a copy adds a reference, the destructor releases a
// held object through its slot 4 (CEngineObject::Release). Only _M_insert_overflow is out of line.
struct EngineObjectRefElement {
    static void Construct(CEngineObject** to, CEngineObject* const& from) {
        *to = from;
        if (from) ++from->refCount_;
    }
    static void Destroy(CEngineObject** p) {
        if (*p) (*p)->Release();
    }
};


// ---- The HUD's portrait slots (ui.h; ui_hud_portrait_setup.cpp) ---------------------------

// A slot: member by member (the padding after hasPlayer_ is not copied), the three texts as
// CStrings; push_back(T())'s default has identity orientations and empty texts (the other fields
// left as they are); the destructor: the texts from the last.
struct HudPortraitSlotElement {
    using R = HudPortraitSlot;
    static void Construct(R* to, const R& from) {
        Plain(*to, from);
        std::construct_at(&to->faceName_, from.faceName_);
        std::construct_at(&to->modelPrefix_, from.modelPrefix_);
        std::construct_at(&to->modelSuffix_, from.modelSuffix_);
    }
    static void Assign(R& to, const R& from) {
        Plain(to, from);
        to.faceName_ = from.faceName_;
        to.modelPrefix_ = from.modelPrefix_;
        to.modelSuffix_ = from.modelSuffix_;
    }
    static void Default(R* p) {
        p->restA_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        p->current_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        p->restB_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        std::construct_at(&p->faceName_);
        std::construct_at(&p->modelPrefix_);
        std::construct_at(&p->modelSuffix_);
        p->wobbleTarget_.SetIdentity();
    }
    static void Destroy(R* p) {
        std::destroy_at(&p->modelSuffix_);
        std::destroy_at(&p->modelPrefix_);
        std::destroy_at(&p->faceName_);
    }

private:
    static void Plain(R& to, const R& from) {
        to.lastHitPoints_ = from.lastHitPoints_;
        to.flash_ = from.flash_;
        to.restA_ = from.restA_;
        to.current_ = from.current_;
        to.restB_ = from.restB_;
        to.wobbleKind_ = from.wobbleKind_;
        to.wobbleTime_ = from.wobbleTime_;
        to.faceState_ = from.faceState_;
        to.faceMode_ = from.faceMode_;
        to.faceTimer_ = from.faceTimer_;
        to.build_ = from.build_;
        to.wobbleTarget_ = from.wobbleTarget_;
        to.wobbleSpeed_ = from.wobbleSpeed_;
        to.hasPlayer_ = from.hasPlayer_;
        to.playerId_ = from.playerId_;
    }
};


// ---- The UI's lists (ui.h, ui_camp.h, ui_screens.h) ---------------------------------------------

// The save list's entries (ui_saveload.cpp): the compiler's copy constructor, operator= and destructor.
struct SaveEntryElement {
    static void Construct(SaveEntry* to, const SaveEntry& from) { to->CopyConstruct(&from); }
    static void Assign(SaveEntry& to, const SaveEntry& from) { to.Assign(&from); }
    static void Destroy(SaveEntry* p) { p->Destruct(); }
};

// The multiplayer address list (ui_connect_screen.cpp).
struct MpAddressElement {
    static void Construct(MpAddressEntry* to, const MpAddressEntry& from) { to->CopyConstruct(&from); }
    static void Assign(MpAddressEntry& to, const MpAddressEntry& from) { to.AssignFrom(from); }
    static void Destroy(MpAddressEntry* p) { p->Destroy(); }
};

// The camp's trade rows (ui_trade_panel.cpp): the copy constructor (the texts,
// then the numbers and the flag; the padding is not copied), the destructor.
struct TradeRowElement {
    static void Construct(TradeRow* to, const TradeRow& from) { to->CopyConstruct(&from); }
    static void Destroy(TradeRow* p) { p->Destroy(); }
};

// The options window's key bindings (ui_options.cpp): memberwise,
// the destructor inline (OptionBinding::Destroy).
struct OptionBindingElement : records::Memberwise<OptionBinding, &OptionBinding::action_, &OptionBinding::keys_,
                                                  &OptionBinding::name_, &OptionBinding::keysText_> {
    static void Destroy(OptionBinding* p) { p->Destroy(); }
};

using TradeRecordVector = stlp::VectorAbi<TradeRecord>;   // plain 0x3C-byte records

// ---- The game flow's zone table (world_game.cpp) -------------------------------------

// world_game.cpp's ZoneEntry with its texts typed: two CStrings and an id, copied memberwise and
// destroyed from the last (the compiler's pair).
struct ZoneTableEntry {
    std::string first;
    std::string second;
    int id;
};

using ZoneTableVector = stlp::VectorAbi<ZoneTableEntry, stlp::ObjectElement<ZoneTableEntry>>;

} // namespace recordvec
