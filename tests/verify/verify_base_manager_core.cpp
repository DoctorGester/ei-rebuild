// Checks for base_manager_core.cpp.
//  - ConstructBase: ours on the real manager; the original on a scratch block holding a copy of its
//    bytes before construction; BuiltCompare (bucket allocations pair up); the scratch is destroyed
//    with the destructor body.
//  - CopyFrom, the default name / description: pure, side by side.
//  - The destructor bodies: ours only.
#include "net_verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/lib/crt.h"

#include <cstring>

CBaseManager* __fastcall BaseManagerDeletingDestructor(CBaseManager* self, void*, unsigned flags);
CEngineObject* __fastcall EngineObjectDeletingDestructor(CEngineObject* self, void*, unsigned flags);

namespace {

void* s_unused;

// 00402B20: PUSH -1 (2), PUSH imm32 (5).
CBaseManager*(__fastcall* s_construct)(CBaseManager*);
CBaseManager* __fastcall ConstructVerify(CBaseManager* self) {
    constexpr std::size_t kSize = sizeof(CBaseManager);
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, self, kSize);
    CBaseManager* actual = self->ConstructBase();
    CBaseManager* expected = s_construct(reinterpret_cast<CBaseManager*>(scratch));
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), kSize};
    cmp.Range(0, kSize);
    ei::verify::Check("CBaseManager::ConstructBase", true,
                      cmp.ok && expected == reinterpret_cast<CBaseManager*>(scratch) && actual == self, "%s", cmp.detail);
    reinterpret_cast<CBaseManager*>(scratch)->DestructBody();
    gamecrt::operator_delete(scratch);
    return actual;
}

void __fastcall DestructVerify(CBaseManager* self) {
    self->DestructBody();
    ei::verify::Check("CBaseManager::DestructBody", true, true, "");
}
CBaseManager* __fastcall ManagerDeleteVerify(CBaseManager* self, void*, unsigned flags) {
    ei::verify::Check("BaseManagerDeletingDestructor", true, true, "");
    return BaseManagerDeletingDestructor(self, nullptr, flags);
}
CEngineObject* __fastcall ObjectDeleteVerify(CEngineObject* self, void*, unsigned flags) {
    ei::verify::Check("EngineObjectDeletingDestructor", true, true, "");
    return EngineObjectDeletingDestructor(self, nullptr, flags);
}

// 00402D40: MOV EAX,[ESP+4] (4), MOV EDX,[EAX+8] (3).
void(__fastcall* s_copy)(CEngineObject*, void*, const CEngineObject*);
void __fastcall CopyVerify(CEngineObject* self, void*, const CEngineObject* other) {
    alignas(4) unsigned char scratch[sizeof(CEngineObject)];
    std::memcpy(scratch, self, sizeof(scratch));
    s_copy(reinterpret_cast<CEngineObject*>(scratch), nullptr, other);
    self->CEngineObject::CopyFrom(*other);
    ei::verify::Check("CEngineObject::CopyFrom", true, std::memcmp(scratch, self, sizeof(scratch)) == 0, "");
}

// 00402CE0 / 00402CF0: MOV EAX,imm32 (5).
const char*(__fastcall* s_name)(void*);
const char*(__fastcall* s_description)(void*);
const char* __fastcall NameVerify(void* self) {
    const char* expected = s_name(self);
    const char* actual = reinterpret_cast<const char*>(0x00783BF0);
    ei::verify::Check("DefaultName", expected, actual, "");
    return actual;
}
const char* __fastcall DescriptionVerify(void* self) {
    const char* expected = s_description(self);
    const char* actual = reinterpret_cast<const char*>(0x00783BF8);
    ei::verify::Check("DefaultDescription", expected, actual, "");
    return actual;
}

// 004031E0: ours only; afterwards no object is registered.
void __fastcall DestroyVerify2(CBaseManager* self) {
    self->CBaseManager::Destroy();
    ei::verify::Check("CBaseManager::Destroy", 0u, static_cast<unsigned>(self->objects_.Size()), "");
}

} // namespace

EI_VERIFY(0x004031E0, DestroyVerify2, 0, s_unused);
EI_VERIFY(0x00402B20, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00402DF0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00402DD0, ManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00402D20, ObjectDeleteVerify, 0, s_unused);
EI_VERIFY(0x00402D40, CopyVerify, 7, s_copy);
EI_VERIFY(0x00402CE0, NameVerify, 5, s_name);
EI_VERIFY(0x00402CF0, DescriptionVerify, 5, s_description);
