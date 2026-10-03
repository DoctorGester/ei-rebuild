// CWorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// The ten mercenary slots and the zone where each is briefed (pointers at 007907AC).
const char* MercZone(int slot) { return reinterpret_cast<const char* const*>(0x007907AC)[slot]; }
constexpr int kMercSlots = 10;

constexpr std::uintptr_t kMercName = 0x007909E4;    // "merc%i"
constexpr std::uintptr_t kVarN8 = 0x007909D8;       // "b.%s.n8_%i"
constexpr std::uintptr_t kVarN1 = 0x007909CC;       // "b.%s.n1_%i"
constexpr std::uintptr_t kVarN2 = 0x007909C0;       // "b.%s.n2_%i"
constexpr std::uintptr_t kVarN3 = 0x007909B4;       // "b.%s.n3_%i"
constexpr std::uintptr_t kVarN9 = 0x007909A8;       // "b.%s.n9_%i"
constexpr std::uintptr_t kVarN10 = 0x0079099C;      // "b.%s.n10_%i"
constexpr std::uintptr_t kVarN11 = 0x00790990;      // "b.%s.n11_%i"
constexpr std::uintptr_t kVarHired = 0x00790984;    // "amerc_%i"

float Zero() { return *reinterpret_cast<const volatile float*>(0x0073B858); }
float One() { return *reinterpret_cast<const volatile float*>(0x0073B84C); }

// 00461360. The text key (object id) of a name (object_tables.cpp), ECX = name.
__declspec(naked) unsigned __fastcall TextKey(const char*) EI_ORIGINAL(0x00461360)
// 00460AF0 (RET 4). Named float table Get; the value is left in ST0.
__declspec(naked) double __fastcall VariableGet(void* /*table*/, void*, const char*) EI_ORIGINAL(0x00460AF0)
// 0066EE10 (RET 8). CPlayerServer::SetVariable (player_messages.cpp).
__declspec(naked) void __fastcall StateSetVariable(void*, void*, const char*, float) EI_ORIGINAL(0x0066EE10)

// C3 of FCOMP: equal or unordered (the original's TEST AH,0x40).
bool EqualOrUnordered(double a, float b) { return !(a < b) && !(a > b); }

// The briefing variables of one mercenary slot (declared so that they are destroyed in the
// original's order: n11, amerc, n10, n2, n8, n9, n3, n1).
struct Briefing {
    mfc::CString n1, n3, n9, n8, n2, n10, hired, n11;
};

} // namespace

void CWorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next()) {
        void* state = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(player) + 0x20);
        int otherMercs = CountLivingMercs(player->id);
        auto set = [&](const mfc::CString& name, float value) { StateSetVariable(state, nullptr, name, value); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            mfc::CString merc;
            merc.Format(Text(kMercName), slot);
            unsigned key = TextKey(merc);
            bool briefedHere = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(zoneName_),
                                                reinterpret_cast<const unsigned char*>(MercZone(slot - 1))) == 0;
            Briefing b;
            b.n8.Format(Text(kVarN8), static_cast<const char*>(merc), slot);
            b.n1.Format(Text(kVarN1), static_cast<const char*>(merc), slot);
            b.n2.Format(Text(kVarN2), static_cast<const char*>(merc), slot);
            b.n3.Format(Text(kVarN3), static_cast<const char*>(merc), slot);
            b.n9.Format(Text(kVarN9), static_cast<const char*>(merc), slot);
            b.n10.Format(Text(kVarN10), static_cast<const char*>(merc), slot);
            b.n11.Format(Text(kVarN11), static_cast<const char*>(merc), slot);
            b.hired.Format(Text(kVarHired), slot);
            set(b.n11, 0.0f);

            bool inParty = false;
            for (int i = 0; i < player->mercCount; ++i) {
                if (*reinterpret_cast<unsigned*>(player->mercs + i * 0x784 + 0x1C) == key) {
                    inParty = true;
                    break;
                }
            }
            void* variables = player->variables;
            if (inParty) {
                if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n10), Zero())) {
                    set(b.n8, 0.0f);
                    set(b.n1, 0.0f);
                    set(b.n2, 0.0f);
                    set(b.n3, 0.0f);
                    set(b.n9, 0.0f);
                } else {
                    set(b.n8, 0.0f);
                    set(b.n1, 0.0f);
                    set(b.n3, 0.0f);
                    set(b.n9, 0.0f);
                    set(b.n2, briefedHere ? 1.0f : 0.0f);
                }
            } else if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n10), Zero())) {
                set(b.n8, 0.0f);
                set(b.n1, 0.0f);
                set(b.n2, 0.0f);
                set(b.n3, 0.0f);
                set(b.n9, 0.0f);
                set(b.n10, 0.0f);
                set(b.hired, 0.0f);
            } else {
                double hiredValue = VariableGet(variables, nullptr, b.hired);
                float hired = static_cast<float>(hiredValue);   // FST: stored as float, compared unrounded
                volatile float hiredStored = hired;
                if (!EqualOrUnordered(hiredValue, Zero())) {
                    if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n8), One())) {
                        if (otherMercs < 2) {
                            bool firstTime = EqualOrUnordered(hiredStored, One());
                            set(b.n1, firstTime ? 1.0f : 0.0f);
                            set(b.n3, firstTime ? 0.0f : 1.0f);
                            set(b.n8, 0.0f);
                            set(b.n2, 0.0f);
                            set(b.n9, 0.0f);
                        } else {
                            set(b.n1, 0.0f);
                            set(b.n3, 0.0f);
                            set(b.n8, 0.0f);
                            set(b.n2, 0.0f);
                            set(b.n9, 1.0f);
                        }
                    } else {
                        set(b.n1, 0.0f);
                        set(b.n2, 0.0f);
                        set(b.n3, 0.0f);
                        set(b.n9, 0.0f);
                        set(b.n10, 0.0f);
                    }
                } else if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n8), One())) {
                    set(b.n11, 1.0f);
                }
            }
        }
    }
}

EI_REPLACE_METHOD(0x00591800, CWorldServer::UpdateMercBriefings);
