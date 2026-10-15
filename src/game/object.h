#pragma once

// Shared pieces of the world objects (00440000-00470000): small engine objects defined in this
// range and the trivial virtual getters of units and figures. Names are provisional. See
// docs/notes/objects.md for the class map. (CEngineObject: engine_object.h; the object list
// iterator: base_manager.h; CMemoryStream: core_streams.h.)

#include "ai_map.h"
#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Reads a field of an object whose class layout is owned by another area (CUnit in ai_map.h) or not
// recovered yet. Copies the bytes, so floats are not re-rounded.
template <typename T>
T FieldAt(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(T));
    return value;
}

// 007AFFA0: the manager that owns the world's objects (units, buildings, ...).
inline CBaseManager* WorldObjectManager() { return *reinterpret_cast<CBaseManager**>(0x007AFFA0); }

// CUnit fields used here (ai_map.h owns the class layout).
namespace unitfield {
constexpr std::size_t kName = 0x238;          // char* (CString): slot 7 returns it
constexpr std::size_t kController = 0x240;    // object most unit virtuals forward to
constexpr std::size_t kHostileMask = 0x244;   // bit p: hostile to player p
constexpr std::size_t kParty = 0x24C;         // group the unit belongs to, or null
constexpr std::size_t kPlayer = 0x250;        // owning player 0..31
constexpr std::size_t kFlag274 = 0x274;       // slot 55 (AI map: non-zero skips the unit layers)
constexpr std::size_t kFlag278 = 0x278;       // slot 54 (AI map: large unit)
constexpr std::size_t kValue328 = 0x328;      // slot 60, float
} // namespace unitfield

// 0052E970. Sets the unit's hostile mask, tells its controller and marks it dirty. Not rewritten.
void __fastcall UnitSetHostileMask(CUnit* unit, void* edx, std::uint32_t mask);
// 0052E940. Ors bits into the hostile mask (marks the unit dirty when it changes). Not rewritten.
void __fastcall UnitAddHostileMask(CUnit* unit, void* edx, std::uint32_t bits);

// Virtual getters shared by CUnit (vtable 0073EA8C) and CFakeUnit (0073BFE8). Written as __fastcall
// shims (this in ECX, no stack arguments) because the classes' virtual interfaces are not declared.
const char* __fastcall UnitGetName(const CUnit* unit);       // slot 7:  00446820
int __fastcall UnitGetFlag278(const CUnit* unit);            // slot 54: 00446890
int __fastcall UnitGetFlag274(const CUnit* unit);            // slot 55: 00446880
float __fastcall UnitGetValue328(const CUnit* unit);         // slot 60: 004468A0

// Slot 7 (GetName) of CFigure (0073C1C4, 0073C224, 0073FBF0; also CSkyObject, CI3DFigure) and of
// CFakeFigure (0073C274): the name pointer at +0x120 / +0x94.
const char* __fastcall FigureGetName(const CEngineObject* figure);       // 00453440
const char* __fastcall FakeFigureGetName(const CEngineObject* figure);   // 004547C0

// "CHoover" (class id 0x1F, vtable 0073C434): no fields of its own seen.
class CHoover : public CEngineObject {
public:
    CHoover* DeleteScalar(unsigned flags);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073C434);

    // 00461C10. Constructor body: the engine object's fields cleared, then its vtable. Returns
    // this.
    CHoover* Construct();
    // 00461C80. Destructor body: restores its vtable, then CEngineObject::DestructBase.
    void Destruct();
};
