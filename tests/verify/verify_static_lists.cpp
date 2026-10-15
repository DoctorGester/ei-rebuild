// Checks for the static initialisers' list constructors (src/game/static_lists.cpp). They run before
// the hooks, so each is checked at start-up: the list's head kept, the original builds the list, its
// sentinel goes back on top of its free list, ours builds it (same sentinel, linked to itself), the
// sentinel is freed again and the kept head restored.
#include "verify.h"

#include "../../src/game/static_lists.h"
#include "../../src/game/texture.h"
#include "../../src/game/world.h"
#include "../../src/lib/stlp_alloc.h"

namespace {

void(__stdcall* s_driversOriginal)();
void(__stdcall* s_deferredOriginal)();

template <typename List>
bool Empty(const List& list) {
    return list.head && list.head->next == list.head && list.head->prev == list.head;
}

template <typename List>
void Compare(const char* name, List& list, void(__stdcall* original)(), void(__stdcall* ours)()) {
    auto* kept = list.head;
    original();
    auto* expected = list.head;
    bool expectedEmpty = Empty(list);
    stlp::deallocate(expected, sizeof(*expected));
    ours();
    auto* actual = list.head;
    bool ok = expectedEmpty && Empty(list) && actual == expected;
    stlp::deallocate(actual, sizeof(*actual));
    list.head = kept;
    ei::verify::Check(name, true, ok, "sentinel %p, original %p", static_cast<const void*>(actual),
                      static_cast<const void*>(expected));
}

void __stdcall DriversVerify() {
    static_lists::ConstructDirectDrawDrivers();
    ei::verify::Check("static_lists::ConstructDirectDrawDrivers", true, Empty(DirectDrawDrivers()), "");
}
void __stdcall DeferredVerify() {
    static_lists::ConstructDeferredUnits();
    ei::verify::Check("static_lists::ConstructDeferredUnits", true, Empty(*FrameDeferredUnits()), "");
}

void StaticListsStartupCheck() {
    Compare("static_lists::ConstructDirectDrawDrivers", DirectDrawDrivers(), s_driversOriginal,
            &static_lists::ConstructDirectDrawDrivers);
    Compare("static_lists::ConstructDeferredUnits", *FrameDeferredUnits(), s_deferredOriginal,
            &static_lists::ConstructDeferredUnits);
}

} // namespace

EI_STARTUP_CHECK(StaticListsStartupCheck);
EI_VERIFY(0x004ED540, DriversVerify, 7, s_driversOriginal);
EI_VERIFY(0x0056A410, DeferredVerify, 7, s_deferredOriginal);
