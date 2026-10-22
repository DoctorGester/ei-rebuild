#pragma once

// Compiled tables of the interface windows (C19; docs/notes/standalone.md, "Compiler-made vtables"). A window
// (CUiWindow, vtable_ at +0) dispatches through game.exe's window tables; a tag over UiWindowClass declares the same
// slots, so a window's constructor and destructor store `Tag::Vtable()` in place of game.exe's table and the
// dispatch through vtable_ reaches the tag's slots, which call the bodies game.exe's table reached. The defaults are
// the plain window's (game.exe's 00740F60): CTransformObject's slots 0-17 with the interface objects' 2 / 11 / 12
// (world_class_slots.h), 18 Build empty, 19 ShowSurfaces, 20 DrawBase, 21 UpdateRegionTips, 22 RedrawBase, 23
// RedrawTips, 24 Update empty. Slot 0 is `Delete`, a member of the window class (its scalar deleting destructor).
// A tag's own bodies override the slots; `this` is the window (AsWindow()). The input side at +0x94 has its own
// tags (ui_input_class.h).

#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"
#include "ui.h"
#include "ui_input_class.h"
#include "world_class_slots.h"

#include "lib/compiled_vtable.h"

#include <type_traits>

template <typename R, typename C, typename Flags>
C* UiWindowDeleteType(R (C::*)(Flags));

template <typename Self, auto Delete>
class UiWindowClass : public ei::CompiledTag<Self> {
public:
    using Window = std::remove_pointer_t<decltype(UiWindowDeleteType(Delete))>;

    virtual void* DeleteScalar(unsigned flags) { return (AsWindow()->*Delete)(flags); }      // 0
    virtual void CopyFrom(const CEngineObject& other) { Object()->CopyFromImpl(other); }     // 1: 00402D40
    virtual int Create() { return UiObjectCreate(Object()); }                                // 2: 00579560 (1)
    virtual void Destroy() {}                                                                // 3: 00579570 (empty)
    virtual int Release() {   // 4: 00455980 (EAX: this, or what the manager's slot 24 leaves there)
        CEngineObject* object = Object();
        if (--object->refCount_ == 0) return object->owner_->ReleaseObject(object);
        return reinterpret_cast<int>(object);
    }
    virtual void ClearState() {}                                                               // 5: 00402CC0 (empty)
    virtual int RunTick() { return 0; }                                                        // 6: 00402CD0
    virtual const char* GetName() { return Object()->CEngineObject::GetName(); }               // 7: 00402CE0
    virtual const char* GetDescription() { return Object()->CEngineObject::GetDescription(); } // 8: 00402CF0
    virtual void Load(CStream*) {}                                                             // 9: 00402D00 (empty)
    virtual void Save(CStream*) {}                                                             // 10: 00402D10 (empty)
    virtual const char* ClassName() { return UiObjectClassName(Object()); }                    // 11: 00579580
    virtual int ClassId() { return UiObjectClassId(Object()); }                                // 12: 00579590
    virtual CTransformObject* CopyTransform(const CTransformObject& other) {                   // 13: 00401490
        return Transform()->CopyTransformImpl(other);
    }
    virtual void CombineRotation(const Quaternion& parent) { Transform()->CombineRotationImpl(parent); }   // 14: 0040CB80
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition) {   // 15: 0040CC30
        Transform()->UpdateWorldImpl(parentRotation, parentPosition);
    }
    virtual void SaveTransform(void* file) { Transform()->SaveTransformImpl(file); }   // 16: 00401510
    virtual void LoadTransform(void* file) { Transform()->LoadTransformImpl(file); }   // 17: 00401540
    virtual void Build() {}                                                     // 18: 00579550 (empty)
    virtual void Show(int show) { AsWindow()->ShowSurfaces(show); }             // 19: 006002C0
    virtual BOOL Draw(int pass) { return AsWindow()->DrawBase(pass); }          // 20: 005FEB90
    virtual void DrawChild(int pass) { AsWindow()->UpdateRegionTips(pass); }    // 21: 005FEBF0
    virtual void Redraw() { AsWindow()->RedrawBase(); }                         // 22: 005FECC0
    virtual void RedrawTips() { AsWindow()->CUiWindow::RedrawTips(); }          // 23: 005FED00
    virtual void Update() {}                                                    // 24: 005EE010 (empty)

protected:
    Window* AsWindow() { return reinterpret_cast<Window*>(this); }
    CEngineObject* Object() { return reinterpret_cast<CEngineObject*>(this); }
    CTransformObject* Transform() { return reinterpret_cast<CTransformObject*>(this); }
};

// The plain window's table (game.exe's 00740F60): the window base's constructor and destructor store it.
struct UiWindowBaseClass final : UiWindowClass<UiWindowBaseClass, &CUiWindow::UiWindowDelete> {};

// The scroll bar's slots over the window's (game.exe's 00742C18): 24 Update (scrolling while an arrow is held), 25
// SetPosition and 26 SetRect, its own. The slider (00742CBC) and the scrolling text (00742E9C) override them.
template <typename Self, auto Delete>
class ScrollbarClass : public UiWindowClass<Self, Delete> {
public:
    void Update() override { this->AsWindow()->CUiScrollbar::Update(); }                               // 24: 0065B360
    virtual void SetPosition(int position) { this->AsWindow()->CUiScrollbar::SetPosition(position); }   // 25: 0065B0C0
    virtual void SetRect(const RECT* rect) { this->AsWindow()->CUiScrollbar::SetRect(rect); }           // 26: 0065B490
};

// The scroll bar's input side (game.exe's 00742BE4): the thumb and arrows by the left button, the drag, the wheel.
template <typename Self, auto Delete>
class ScrollbarInputClass : public InputSideClass<Self, Delete> {
public:
    int LeftDown(int b, int x, int y) override { return this->SideWindow()->CUiScrollbar::OnMouseDown(b, x, y), 0; }    // 2
    int LeftUp(int b, int x, int y) override { return this->SideWindow()->CUiScrollbar::OnMouseUp(b, x, y), 0; }        // 3
    int MouseMove(int b, int x, int y) override { return this->SideWindow()->CUiScrollbar::OnMouseMove(b, x, y), 0; }   // 4
    int Wheel(int f, int d, int x, int y) override { return this->SideWindow()->CUiScrollbar::OnWheel(f, d, x, y); }     // 12
};

// A window class whose slot 0 is a base's deleting destructor (the plain window's, say): the window as
// `Panel` (AsPanel), for the slots its table has of its own.
template <typename Self, auto Delete, typename Panel>
class PanelClass : public UiWindowClass<Self, Delete> {
protected:
    Panel* AsPanel() { return static_cast<Panel*>(this->AsWindow()); }
};

// The HUD panels' input sides (game.exe's 00742594, 007424FC, 0074262C, 007426C4, 00742768, 00742808, 007428B0,
// 0074294C, 007429E4): `Panel`'s left button down (2) and up (3) and mouse move (4); a panel's tag adds the double
// click (7) or the wheel (12) where its table has them.
template <typename Self, auto Delete, typename Panel>
class PanelInputClass : public InputSideClass<Self, Delete> {
public:
    Panel* AsPanel() { return static_cast<Panel*>(this->SideWindow()); }
    int LeftDown(int b, int x, int y) override { return AsPanel()->OnMouseDown(b, x, y), 0; }    // 2
    int LeftUp(int b, int x, int y) override { return AsPanel()->OnMouseUp(b, x, y), 0; }        // 3
    int MouseMove(int b, int x, int y) override { return AsPanel()->OnMouseMove(b, x, y), 0; }   // 4
};
