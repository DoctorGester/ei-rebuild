// A network client's player list on the base screen (CChatWindow, 00608D20-00609900). See
// ui_briefing.h and docs/notes/ui.md, section 3. Multiplayer only.
#include "ui_briefing.h"
#include "world_glue.h"
#include "game_strings.h"
#include "plugin_fixes.h"

#include "item_db_text.h"
#include "net_classes.h"
#include "net_messages.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "lib/stlp_vector_refs.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>

namespace {


void** MainVtable() { return ei::GamePointer<void*>(0x00741478); }
void** InputVtable() { return ei::GamePointer<void*>(0x00741444); }

// The status texts' keys, in texts_ order.
const char* TextKey(int i) {
    const char* const keys[] = {
        "lmp_status_zone",
        "lmp_status_base",
        "lmp_status_camp",
        "lmp_status_connect",
        "lmp_exch_we_offer",
        "lmp_exch_they_offer",
    };
    return keys[i];
}
constexpr int kTextCount = 6;
constexpr int kWeOffer = 4, kTheyOffer = 5;

const char* OnOffSound() { return "buttons\\battle\\on_off.wav"; }

constexpr int kSurfaceFormat = 5;
constexpr int kTextureKind = 0x20;
constexpr int kFaceWidth = 0x38;
constexpr int kFirstFaceLeft = 0x2E8;
constexpr int kFirstStatusLeft = 0x118;   // with the window at 0x1D0; 0x2E8 with EI_Plugin's NPlayers (006095FB)
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kPickCommandA = 4, kPickCommandB = 5;   // the client's command kinds for picks 1 / 2

// 0060A120: vector<counted pointer>::_M_insert_overflow(pos, x, n) (stlp::CountedRefVector's): a new
// block with the elements before `pos`, `n` counted copies of `x`, the rest; the old elements released.

// The counted pointers' inlined helpers (16-bit count; released through CRefObject::Release).
void AddRef(CRefObject* object) {
    if (object) ++object->refs_;
}
void Assign(CRefObject** slot, CRefObject* object) {
    CRefObject* old = *slot;
    *slot = object;
    AddRef(object);
    if (old) old->Release();
}

net::PlayerMessage* PlayerOf(CRefObject* object) { return static_cast<net::PlayerMessage*>(object); }
bool Live(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }

// The inlined strcmp(a, b) == 0.
bool SameText(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

} // namespace

// --- 00608D20: the constructor ------------------------------------------------------------------

CChatWindow* CChatWindow::Construct(CInterface3D* ui) {
    CUiWindow::Construct(ui);
    players_.Start() = nullptr;
    players_.Finish() = nullptr;
    players_.EndOfStorage() = nullptr;
    texts_.Construct();
    vtable_ = MainVtable();
    inputVtable_ = InputVtable();
    // EI_Plugin's NPlayers (00608D9D, 00608DE4): the window and its surface start at x 0, not 0x1D0.
    const int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? 0 : 0x1D0;
    rect_ = RECT{left, 0, 800, 0xA0};
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, left, 0x4B, 800, 0x69, kSurfaceFormat, 1)
                      : nullptr);
    portraits_ = 0;
    mfc::CString text;
    for (int i = 0; i < kTextCount; ++i) {
        TheTextManager()->GetString(TextKey(i), &text);
        texts_.SetAtGrow(texts_.size, text);
    }
    return this;
}
EI_REPLACE_METHOD(0x00608D20, CChatWindow::Construct);
// EI_Plugin.asi patches the constructor and Update (NPlayers: the window's left edge and the first status
// column) and ShowPlayers (Fixes: the faces' sex and the prototype fallback) in a Rebirth launch.

// --- 00608F90: a face ---------------------------------------------------------------------------

void CChatWindow::AddPortrait(const char* model, const char* texture) {
    // Four faces share a 256 x 256 texture, one quarter each.
    int u = 0, v = 0;
    switch (portraits_ % 4) {
    case 0: CreateTexture(0x100, 0x100, kTextureKind); break;
    case 1: u = 0x80; break;
    case 2: v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: break;   // a negative count (the original then uses an unset u and `texture` as v)
    }
    CopyImage(objects_.data[objects_.size - 1], texture, 0x80, 0x80, 0x80, 0x80, u, v, 0, 0);

    CUiMesh* sprite = AddSprite(model);
    int left = kFirstFaceLeft - portraits_ * kFaceWidth;
    int right = left + kFaceWidth;
    if (sprite) {
        sprite->Place(static_cast<float>(left + right) * 0.5f /*0073B86C*/, 32.0f, 7.0f);   // * 0.5
        sprite->SetScale(0.3f);
        Vector3 axis{1.0f, 0.0f, 0.0f};
        Quaternion turn;
        // pi (007410D0) * 1.05 (0073CEE0), passed as a float.
        sprite->localRotation_ =
            *turn.SetAxisAngle(axis, 3.1415927f /*007410D0*/ * 1.05f /*0073CEE0*/);
    } else {
        sprite = AddSprite(nullptr);
        sprite->SetRectangle(7.0f, 100.0f, static_cast<float>(left), 0.0f, static_cast<float>(right), 60.0f, 0, 0, 0x80,
                             0x80);
    }
    sprite->textureOffsetU_ = static_cast<float>(u) * 0.00390625f /*0073EEB8*/;          // / 256
    sprite->textureOffsetV_ = static_cast<float>(v + 0x80) * 0.00390625f /*0073EEB8*/;
    sprite->texture_ = objects_.data[objects_.size - 1];
    ++portraits_;
    AddRegion()->CallSetRect(left, 0, right, 0x50);
}
EI_REWRITTEN(0x00608F90, CChatWindow::AddPortrait);

// --- 006091C0: the players' faces ---------------------------------------------------------------

void CChatWindow::ShowPlayers() {
    ClearSceneObjects();
    ClearRegions();
    ClearObjects();
    portraits_ = 0;
    listed_ = 0;

    // The vector emptied (each player released, front to back).
    for (CRefObject** p = players_.Start(); p != players_.Finish(); ++p)
        if (*p) (*p)->Release();
    players_.Finish() = players_.Start();

    // Our own player first, the others after it; none while the binding is gone.
    net::ClientSession* client = net::GameClient();
    const stlp::CountedRefList& players = client->players_;
    for (auto* node = players.First(); node != players.End(); node = node->next) {
        if (!Live(client->binding_)) continue;
        CRefObject* player = node->data.ptr;
        bool own = static_cast<net::BindingMessage*>(client->binding_)->PlayerRef().ptr == player;
        CRefObject**& finish = players_.Finish();
        if (finish == players_.EndOfStorage()) {
            stlp::AsRefPtrs(players_).InsertOverflow<stlp::CountedRefElement>(
                stlp::AsRefPtr(own ? players_.Start() : finish), stlp::AsRefPtr(node->data.ptr), 1);
        } else if (!own || players_.Start() == finish) {
            if (finish) {
                *finish = player;
                AddRef(player);
            }
            ++finish;
        } else {
            // insert(begin(), x) with room: the last element copied up, the rest moved back by
            // assignment, then x at the front.
            *finish = finish[-1];
            AddRef(*finish);
            ++finish;
            CRefObject* copy = player;
            AddRef(copy);
            for (CRefObject** p = finish - 2; p != players_.Start(); --p) Assign(p, p[-1]);
            Assign(players_.Start(), copy);
            if (copy) copy->Release();
        }
        ++listed_;
    }

    // The faces (the strings live across players, as the original's: the sex letter carries over).
    mfc::CString model, picture, sex, build;
    for (CRefObject** p = players_.Start(); p != players_.Finish(); ++p) {
        net::PlayerMessage* player = PlayerOf(*p);
        UnitPrototypeRecord* prototype;
        {
            prototype = unitdb::PrototypeNamed(stlp::string(player->text_));
            // EI_Plugin's Fixes (006093B7): an unknown prototype becomes the first one that is no creature.
            if (!prototype && pluginfixes::Enabled())
                prototype = reinterpret_cast<UnitPrototypeRecord*>(pluginfixes::FallbackPrototype());
        }
        FigurePrototypeRecord* figure;
        {
            stlp::string key(static_cast<const char*>(prototype->modelName));
            figure = reinterpret_cast<FigurePrototypeRecord*>(unitdb::ModelNamed(key));
        }
        const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes (00609427): the model's fifth character
        if (fixes ? !pluginfixes::ModelIsFemale(figure->model) : SameText(figure->model, "unhuma"))
            sex = "m";
        else if (fixes || SameText(figure->model, "unhufe"))
            sex = "f";
        build = "me";
        model.Format("infa%s%s%dface", static_cast<const char*>(figure->model), static_cast<const char*>(build), prototype->hair + 1);
        picture.Format("face%s%02d", static_cast<const char*>(sex), prototype->skin);
        AddPortrait(model, picture);
        regions_.data[portraits_ - 1]->SetTipString(player->name_.data(), 1, 0);
    }
}
EI_REPLACE_METHOD(0x006091C0, CChatWindow::ShowPlayers);

// --- 006095A0: slot 24 --------------------------------------------------------------------------

void CChatWindow::Update() {
    net::ClientSession* client = net::GameClient();
    if (Live(client->trade_)) {
        GameSendLeave04(9);
        return;
    }
    CUiSurface* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    surface->Clear();
    mfc::CString status, offer;
    int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? kFirstFaceLeft : kFirstStatusLeft;
    for (CRefObject** p = players_.Start(); p != players_.Finish(); ++p, left -= kFaceWidth) {
        net::PlayerMessage* player = PlayerOf(*p);
        status.Empty();
        switch (player->state_) {
        case 0: status = texts_.data[0]; break;
        case 1: status = texts_.data[1]; break;
        case 2: status = texts_.data[2]; break;
        case 3:
        case 4: status = texts_.data[3]; break;
        default: break;
        }
        offer.Empty();
        auto* own = static_cast<net::PartyMessage*>(client->party_.ptr);
        if (own && own->partner_.ptr == *p) offer = texts_.data[kWeOffer];
        const stlp::CountedRefList& offers = client->offers_;
        for (auto* node = offers.First(); node != offers.End(); node = node->next) {
            if (static_cast<net::PartyMessage*>(node->data.ptr)->player_.ptr == *p) {
                offer = texts_.data[kTheyOffer];
                break;
            }
        }
        if (status.GetLength() != 0) {
            RECT line{left, 0, left + kFaceWidth, 0xF};
            SurfaceDrawText(surface, nullptr, status, &line, 0, kWhite, 1, 1);
        }
        if (offer.GetLength() != 0) {
            RECT line{left, 0xF, left + kFaceWidth, 0x1E};
            SurfaceDrawText(surface, nullptr, offer, &line, 0, kWhite, 1, 1);
        }
    }
    UiSurfaceReleaseDC(surface);
}
EI_REPLACE_METHOD(0x006095A0, CChatWindow::Update);

// --- 006097E0 / 00609900: the input side's left button and mouse move ---------------------------

void CChatWindow::OnLButtonDown(int, int x, int y) {
    int index = RegionAt(x, y);
    if (index < 0 || static_cast<unsigned>(index) >= players_.size()) return;
    inputHandled_ = true;
    int pick = static_cast<CBriefingScreen*>(interface_->GetActiveWindow())->chatActive_;
    if (pick != 0) {
        char text[0x80];
        gamecrt::sprintf(text, "%d", PlayerOf(players_[index])->serial_);
        if (pick == 1)
            net::GameClient()->SendCommand(kPickCommandA, text);
        else if (pick == 2)
            net::GameClient()->SendCommand(kPickCommandB, text);
        static_cast<CBriefingScreen*>(interface_->GetActiveWindow())->chatActive_ = 0;
        return;
    }
    uiorig::PlayUiSound(interface_, OnOffSound(), 0);
    net::ClientSession* client = net::GameClient();
    CRefObject* player = players_[index];
    // The binding is not checked for null here.
    if (static_cast<net::BindingMessage*>(client->binding_)->PlayerRef().ptr == player) return;
    auto* own = static_cast<net::PartyMessage*>(client->party_.ptr);
    if (own && own->partner_.ptr == player) {
        net::ClientDropParty(client);
        return;
    }
    client->StartParty(player);
}

void CChatWindow::OnMouseMove(int, int x, int y) {
    int index = RegionAt(x, y);
    int pick = static_cast<CBriefingScreen*>(interface_->GetActiveWindow())->chatActive_;
    if (pick != 0 && index >= 0 && static_cast<unsigned>(index) < players_.size()) TheCursor()->SetShape(0);
}

namespace {
} // namespace
EI_REPLACE_METHOD(0x006097E0, InputForward<&CChatWindow::OnLButtonDown>::Call);
EI_REPLACE_METHOD(0x00609900, InputForward<&CChatWindow::OnMouseMove>::Call);
