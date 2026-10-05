// Differential checks for the shared object helpers (object.cpp, object_maths.cpp, object_tables.cpp).
// Getters and lookups are pure: both run, results compared bit for bit. Constructors, destructor
// bodies and setters write only their own object: the original runs on a scratch copy of the bytes
// it may touch, ours on the real object, and the bytes are compared.
#include "verify.h"

#include "../../src/game/core_maths.h"
#include "../../src/game/object.h"
#include "../../src/game/object_maths.h"
#include "../../src/game/object_tables.h"

#include <cstring>

#include <windows.h>

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Scratch copy of the first N bytes of an object.
template <std::size_t N>
struct Scratch {
    alignas(4) unsigned char bytes[N];
    explicit Scratch(const void* from) { std::memcpy(bytes, from, N); }
    template <typename T>
    T* as() { return reinterpret_cast<T*>(bytes); }
    bool Same(const void* other) const { return std::memcmp(bytes, other, N) == 0; }
};

// --- CEngineObject / CTransformObject / CHoover -------------------------------------------------

// 00455940: MOV EAX,[ECX+14] (3), MOV EAX,[EAX+AC] (6).
void*(__fastcall* s_managerContext)(const CEngineObject*);
void* __fastcall ManagerContextVerify(const CEngineObject* self) {
    void* expected = s_managerContext(self);
    ei::verify::Check("CEngineObject::ManagerContext", expected, self->ManagerContext(), "owner %p",
                      static_cast<void*>(self->owner_));
    return expected;
}

// 00455970: MOV EDX,[ESP+4] (4), MOV EAX,[ECX+14] (3).
CBaseManager*(__fastcall* s_setOwner)(CEngineObject*, void*, CBaseManager*);
CBaseManager* __fastcall SetOwnerVerify(CEngineObject* self, void*, CBaseManager* owner) {
    Scratch<sizeof(CEngineObject)> expected(self);
    CBaseManager* expectedRet = s_setOwner(expected.as<CEngineObject>(), nullptr, owner);
    CBaseManager* actualRet = self->SetOwner(owner);
    ei::verify::Check("CEngineObject::SetOwner", true, expectedRet == actualRet && expected.Same(self), "%p -> %p",
                      static_cast<void*>(expectedRet), static_cast<void*>(owner));
    return actualRet;
}

// 00455930: MOV [ECX],imm32 (6).
void(__fastcall* s_destructBase)(CEngineObject*);
void __fastcall DestructBaseVerify(CEngineObject* self) {
    Scratch<sizeof(CEngineObject)> expected(self);
    s_destructBase(expected.as<CEngineObject>());
    self->DestructBase();
    ei::verify::Check("CEngineObject::DestructBase", true, expected.Same(self), "");
}

// 00453400: MOV EAX,ECX (2), MOV EDX,0x3F800000 (5).
CTransformObject*(__fastcall* s_transformConstruct)(CTransformObject*);
CTransformObject* __fastcall TransformConstructVerify(CTransformObject* self) {
    Scratch<sizeof(CTransformObject)> expected(self);
    CTransformObject* expectedRet = s_transformConstruct(expected.as<CTransformObject>());
    CTransformObject* actualRet = self->ConstructBase();
    bool same = expectedRet == expected.as<CTransformObject>() && actualRet == self && expected.Same(self);
    ei::verify::Check("CTransformObject::ConstructBase", true, same, "");
    return actualRet;
}

// 00461C10: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX+4],ECX (3).
CHoover*(__fastcall* s_hooverConstruct)(CHoover*);
CHoover* __fastcall HooverConstructVerify(CHoover* self) {
    Scratch<sizeof(CEngineObject)> expected(self);
    CHoover* expectedRet = s_hooverConstruct(expected.as<CHoover>());
    CHoover* actualRet = self->Construct();
    bool same = expectedRet == expected.as<CHoover>() && actualRet == self && expected.Same(self);
    ei::verify::Check("CHoover::Construct", true, same, "");
    return actualRet;
}

// 00461C80: MOV [ECX],imm32 (6); the original then jumps to 00455930 (its own verify wrapper).
void(__fastcall* s_hooverDestruct)(CHoover*);
void __fastcall HooverDestructVerify(CHoover* self) {
    Scratch<sizeof(CEngineObject)> expected(self);
    s_hooverDestruct(expected.as<CHoover>());
    self->Destruct();
    ei::verify::Check("CHoover::Destruct", true, expected.Same(self), "");
}

// --- Getters (pure) ---------------------------------------------------------------------------

// 00446820 / 00446880 / 00446890 / 00453440 / 004547C0: MOV EAX,[ECX+disp32] (6).
// 004468A0: FLD [ECX+0x328] (6).
const char*(__fastcall* s_unitName)(const CUnit*);
const char* __fastcall UnitNameVerify(const CUnit* unit) {
    const char* expected = s_unitName(unit);
    ei::verify::Check("UnitGetName", expected, UnitGetName(unit), "unit %p", unit);
    return expected;
}
int(__fastcall* s_unitFlag274)(const CUnit*);
int __fastcall UnitFlag274Verify(const CUnit* unit) {
    int expected = s_unitFlag274(unit);
    ei::verify::Check("UnitGetFlag274", expected, UnitGetFlag274(unit), "unit %p", unit);
    return expected;
}
int(__fastcall* s_unitFlag278)(const CUnit*);
int __fastcall UnitFlag278Verify(const CUnit* unit) {
    int expected = s_unitFlag278(unit);
    ei::verify::Check("UnitGetFlag278", expected, UnitGetFlag278(unit), "unit %p", unit);
    return expected;
}
float(__fastcall* s_unitValue328)(const CUnit*);
float __fastcall UnitValue328Verify(const CUnit* unit) {
    float expected = s_unitValue328(unit);
    ei::verify::Check("UnitGetValue328", Bits(expected), Bits(UnitGetValue328(unit)), "unit %p", unit);
    return expected;
}
const char*(__fastcall* s_figureName)(const CEngineObject*);
const char* __fastcall FigureNameVerify(const CEngineObject* figure) {
    const char* expected = s_figureName(figure);
    ei::verify::Check("FigureGetName", expected, FigureGetName(figure), "figure %p", figure);
    return expected;
}
const char*(__fastcall* s_fakeFigureName)(const CEngineObject*);
const char* __fastcall FakeFigureNameVerify(const CEngineObject* figure) {
    const char* expected = s_fakeFigureName(figure);
    ei::verify::Check("FakeFigureGetName", expected, FakeFigureGetName(figure), "figure %p", figure);
    return expected;
}

// --- Vector helpers -----------------------------------------------------------------------------
// Ours runs first into a scratch output (an output aliasing an input is then read unchanged by
// both), the original on the real output.

// 004489D0: FLD [EDX] (2), PUSH ESI (1), MOV ESI,[ESP+8] (4).
Vector3*(__fastcall* s_cross)(Vector3*, const Vector3*, const Vector3*);
Vector3* __fastcall CrossVerify(Vector3* out, const Vector3* a, const Vector3* b) {
    Vector3 actual;
    Vector3* actualRet = CrossProduct(&actual, *a, *b);
    Vector3 ia = *a, ib = *b;
    Vector3* expectedRet = s_cross(out, a, b);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("CrossProduct", true, same, "(%g, %g, %g) x (%g, %g, %g)", ia.x, ia.y, ia.z, ib.x, ib.y, ib.z);
    return expectedRet;
}

// 0044A230: FLD [EDX+8] (3), MOV EAX,ECX (2).
Vector3*(__fastcall* s_subtract)(Vector3*, const Vector3*, const Vector3*);
Vector3* __fastcall SubtractVerify(Vector3* out, const Vector3* a, const Vector3* b) {
    Vector3 actual;
    Vector3* actualRet = SubtractVectors(&actual, *a, *b);
    Vector3 ia = *a, ib = *b;
    Vector3* expectedRet = s_subtract(out, a, b);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("SubtractVectors", true, same, "(%g, %g, %g) - (%g, %g, %g)", ia.x, ia.y, ia.z, ib.x, ib.y,
                      ib.z);
    return expectedRet;
}

// 0044CC10: FLD [EDX+4] (3), MOV EAX,ECX (2).
Vector2*(__fastcall* s_subtract2)(Vector2*, const Vector2*, const Vector2*);
Vector2* __fastcall Subtract2Verify(Vector2* out, const Vector2* a, const Vector2* b) {
    Vector2 actual;
    Vector2* actualRet = SubtractVectors2(&actual, *a, *b);
    Vector2 ia = *a, ib = *b;
    Vector2* expectedRet = s_subtract2(out, a, b);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("SubtractVectors2", true, same, "(%g, %g) - (%g, %g)", ia.x, ia.y, ib.x, ib.y);
    return expectedRet;
}

// 00448A10 (copy of ScaleVector): FLD [ESP+4] (4), FMUL [EDX+8] (3).
Vector3*(__fastcall* s_scale2)(Vector3*, const Vector3*, float);
Vector3* __fastcall Scale2Verify(Vector3* out, const Vector3* v, float s) {
    Vector3 actual;
    Vector3* actualRet = ScaleVector(&actual, *v, s);
    Vector3 iv = *v;
    Vector3* expectedRet = s_scale2(out, v, s);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("ScaleVector@00448A10", true, same, "(%g, %g, %g) * %g", iv.x, iv.y, iv.z, s);
    return expectedRet;
}

// 004679B0: FLD [ECX] (2), FDIV [ESP+4] (4).
void(__fastcall* s_divide)(Vector3*, void*, float);
void __fastcall DivideVerify(Vector3* self, void*, float d) {
    Vector3 actual = *self;
    actual.DivideInPlace(d);
    Vector3 before = *self;
    s_divide(self, nullptr, d);
    ei::verify::Check("Vector3::DivideInPlace", true, std::memcmp(self, &actual, sizeof(actual)) == 0,
                      "(%g, %g, %g) / %g", before.x, before.y, before.z, d);
}

// 004679D0: MOV EAX,ECX (2), MOV ECX,[ESP+4] (4).
Vector4*(__fastcall* s_fromPoint)(Vector4*, void*, const Vector3*);
Vector4* __fastcall FromPointVerify(Vector4* self, void*, const Vector3* p) {
    Vector4 actual;
    std::memcpy(&actual, self, sizeof(actual));
    Vector4* actualRet = actual.FromPoint(*p);
    Vector4* expectedRet = s_fromPoint(self, nullptr, p);
    bool same = expectedRet == self && actualRet == &actual && std::memcmp(self, &actual, sizeof(actual)) == 0;
    ei::verify::Check("Vector4::FromPoint", true, same, "");
    return expectedRet;
}

// --- Tables (pure) ------------------------------------------------------------------------------

// 00461360: PUSH ESI (1), PUSH EDI (1), MOV EDI,ECX (2), XOR ESI,ESI (2).
int(__fastcall* s_textKey)(const char*);
int __fastcall TextKeyVerify(const char* name) {
    int expected = s_textKey(name);
    ei::verify::Check("TextKeyFromName", expected, TextKeyFromName(name), "'%s'", name);
    return expected;
}

// 004613A0: CMP ECX,imm32 (6).
BOOL(__fastcall* s_isTextKey)(int);
BOOL __fastcall IsTextKeyVerify(int id) {
    BOOL expected = s_isTextKey(id);
    ei::verify::Check("IsTextKey", expected, IsTextKey(id), "%d", id);
    return expected;
}

// 0045E850 / 0045E910: PUSH EBX, PUSH EBP, PUSH ESI (3), MOV ESI,ECX (2).
MapRecord*(__fastcall* s_findMap)(CMapDatabase*, void*, const char*);
MapRecord* __fastcall FindMapVerify(CMapDatabase* self, void*, const char* name) {
    MapRecord* expected = s_findMap(self, nullptr, name);
    ei::verify::Check("CMapDatabase::FindMap", expected, self->FindMap(name), "'%s'", name);
    return expected;
}
AllodRecord*(__fastcall* s_findAllod)(CMapDatabase*, void*, const char*);
AllodRecord* __fastcall FindAllodVerify(CMapDatabase* self, void*, const char* name) {
    AllodRecord* expected = s_findAllod(self, nullptr, name);
    ei::verify::Check("CMapDatabase::FindAllod", expected, self->FindAllod(name), "'%s'", name);
    return expected;
}

// 00460AF0: MOV EAX,FS:[0] (6, absolute).
float(__fastcall* s_namedFloat)(NamedFloatTable*, void*, const char*);
float __fastcall NamedFloatVerify(NamedFloatTable* self, void*, const char* name) {
    float expected = s_namedFloat(self, nullptr, name);
    ei::verify::Check("NamedFloatTable::Get", Bits(expected), Bits(self->Get(name)), "'%s'", name);
    return expected;
}

// 00462F70 loops back to 00462F73, inside the bytes the hook's jump overwrites, so it cannot run
// through a trampoline. It has no calls and only short internal jumps: a copy of its whole body
// (61 bytes), taken when this DLL loads (before the hooks are installed), runs instead.
using FindCommandFn = void*(__fastcall*)(int);
FindCommandFn CopyFindCommand() {
    constexpr std::size_t kSize = 61;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x00462F70), kSize);
    return reinterpret_cast<FindCommandFn>(copy);
}
FindCommandFn s_findCommandCopy = CopyFindCommand();
void* s_findCommandUnused;
void* __fastcall FindCommandVerify(int key) {
    if (!s_findCommandCopy) return FindCommandByKey(key);
    void* expected = s_findCommandCopy(key);
    ei::verify::Check("FindCommandByKey", expected, FindCommandByKey(key), "key %d", key);
    return expected;
}

} // namespace

EI_VERIFY(0x00455940, ManagerContextVerify, 9, s_managerContext);
EI_VERIFY(0x00455970, SetOwnerVerify, 7, s_setOwner);
EI_VERIFY(0x00455930, DestructBaseVerify, 6, s_destructBase);
EI_VERIFY(0x00453400, TransformConstructVerify, 7, s_transformConstruct);
EI_VERIFY(0x00461C10, HooverConstructVerify, 7, s_hooverConstruct);
EI_VERIFY(0x00461C80, HooverDestructVerify, 6, s_hooverDestruct);
EI_VERIFY(0x00446820, UnitNameVerify, 6, s_unitName);
EI_VERIFY(0x00446880, UnitFlag274Verify, 6, s_unitFlag274);
EI_VERIFY(0x00446890, UnitFlag278Verify, 6, s_unitFlag278);
EI_VERIFY(0x004468A0, UnitValue328Verify, 6, s_unitValue328);
EI_VERIFY(0x00453440, FigureNameVerify, 6, s_figureName);
EI_VERIFY(0x004547C0, FakeFigureNameVerify, 6, s_fakeFigureName);
EI_VERIFY(0x004489D0, CrossVerify, 7, s_cross);
EI_VERIFY(0x0044A230, SubtractVerify, 5, s_subtract);
EI_VERIFY(0x0044CC10, Subtract2Verify, 5, s_subtract2);
EI_VERIFY(0x00448A10, Scale2Verify, 7, s_scale2);
EI_VERIFY(0x004679B0, DivideVerify, 6, s_divide);
EI_VERIFY(0x004679D0, FromPointVerify, 6, s_fromPoint);
EI_VERIFY(0x00461360, TextKeyVerify, 6, s_textKey);
EI_VERIFY(0x004613A0, IsTextKeyVerify, 6, s_isTextKey);
EI_VERIFY(0x0045E850, FindMapVerify, 5, s_findMap);
EI_VERIFY(0x0045E910, FindAllodVerify, 5, s_findAllod);
EI_VERIFY(0x00460AF0, NamedFloatVerify, 6, s_namedFloat);
EI_VERIFY(0x00462F70, FindCommandVerify, 0, s_findCommandUnused);
