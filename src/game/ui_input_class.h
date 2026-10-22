#pragma once

// Compiled tables of the windows' input sides (C19; docs/notes/standalone.md, "Compiler-made vtables"). A window's
// input side (CUiWindow::inputVtable_ at +0x94, UiInputSide) dispatches through game.exe's 13-slot input tables; a tag
// over InputSideClass declares the same slots, so a window stores `Tag::Vtable()` in place of game.exe's table. The
// defaults are the base input side's (game.exe's 007401D0 / 00740F2C): 1-6 and 9-11 do nothing (005E2800 ...
// 005E28A0: RET / RET 0xC, EAX left as it was: 0 here, which no caller reads), 7 / 8 a double click as a press
// (005E2860 / 005E2880), 12 the wheel not used (005E28B0, 0). Slot 0 is `Delete`: a member of the window class, the
// window's deleting destructor through the side (SUB ECX,0x94; JMP), or for an extra side of its own (a container's
// +0x158 list) UiInputSide's. A tag's own handlers override the slots; `this` is the side, SideWindow() the window.

#include "ui.h"

#include "lib/compiled_vtable.h"

#include <cstddef>
#include <type_traits>

template <typename R, typename C, typename Flags>
C* InputSideWindowType(R (C::*)(Flags));

template <typename Self, auto Delete>
class InputSideClass : public ei::CompiledTag<Self> {
public:
    using Window = std::remove_pointer_t<decltype(InputSideWindowType(Delete))>;

    UiInputSide* Side() { return reinterpret_cast<UiInputSide*>(this); }
    Window* SideWindow() {
        return reinterpret_cast<Window*>(reinterpret_cast<char*>(this) - offsetof(CUiWindow, inputVtable_));
    }

    virtual void* DeleteWindow(unsigned flags) {   // 0
        if constexpr (std::is_same_v<Window, UiInputSide>) return (Side()->*Delete)(flags);
        else return (SideWindow()->*Delete)(flags);
    }
    virtual void Deactivate() {}                                                            // 1: 005E2800
    virtual int LeftDown(int, int, int) { return 0; }                                       // 2: 005E2810
    virtual int LeftUp(int, int, int) { return 0; }                                         // 3: 005E2820
    virtual int MouseMove(int, int, int) { return 0; }                                      // 4: 005E2830
    virtual int RightDown(int, int, int) { return 0; }                                      // 5: 005E2840
    virtual int RightUp(int, int, int) { return 0; }                                        // 6: 005E2850
    virtual int LeftDouble(int buttons, int x, int y) { return Side()->LeftDoubleAsDown(buttons, x, y); }     // 7
    virtual int RightDouble(int buttons, int x, int y) { return Side()->RightDoubleAsDown(buttons, x, y); }   // 8
    virtual int KeyDown(int, int, int) { return 0; }                                        // 9: 005EA840
    virtual int Char(int, int, int) { return 0; }                                           // 10: 005EA800
    virtual int KeyUp(int, int, int) { return 0; }                                          // 11: 005E28A0
    virtual int Wheel(int flags, int delta, int x, int y) { return Side()->WheelIgnored(flags, delta, x, y); }   // 12
};

// An extra input side whose slots do nothing (0x0C bytes; game.exe's 00741154: slots 9 / 11 are its own empty RET 0xC,
// 005EB8A0 / 005EB8B0): the battle screen's, the briefing's, the main menu's and the view screens'.
struct InertInputClass final : InputSideClass<InertInputClass, &UiInputSide::InputSideDelete> {};
inline void** InertInputVtable() { return static_cast<void**>(InertInputClass::Vtable()); }
