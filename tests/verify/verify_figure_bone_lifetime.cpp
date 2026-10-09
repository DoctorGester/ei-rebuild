// Checks for the skeleton bone's lifetime (figure_bone_lifetime.cpp).
#include "verify.h"

#include "../../src/game/map_object_figures.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// The state CreateBone leaves (names are not compared: they point at the shared empty string).
bool IsBlank(const FigureBone* bone) {
    const Quaternion& rest = *reinterpret_cast<const Quaternion*>(std::uintptr_t(0x007AF100));
    const Vector3& restPosition = *reinterpret_cast<const Vector3*>(std::uintptr_t(0x007AF110));
    return std::strcmp(bone->name, "none") == 0 && std::strcmp(bone->name2, "none") == 0 &&
           std::strcmp(bone->name3, "none") == 0 && !bone->parent && bone->children.count == 0 && !bone->children.head && !bone->children.blocks &&
           !bone->mesh && !bone->pose && !bone->animator && !bone->counted54 && !bone->counted58 &&
           std::memcmp(&bone->position, &restPosition, sizeof(Vector3)) == 0 &&
           std::memcmp(&bone->rotation, &rest, sizeof(Quaternion)) == 0 && bone->matrices.size == 0 &&
           bone->matrices.data && bone->morphS == 0.0f && bone->morphT == 0.0f && bone->morphU == 0.0f;
}

} // namespace

// 0050AF40 allocates only the matrices' empty block: the original builds into a raw copy of the
// object's memory, ours into the object; the bytes are compared except the block pointer (+0x90)
// and the offset (+0x5C..+0x67, which neither writes); the copy's block is freed. Reached by every
// figure with a skeleton (unit spawns, world load).
static void* s_construct;
static FigureBone* __fastcall ConstructVerify(FigureBone* bone, void*) {
    alignas(8) unsigned char scratch[sizeof(FigureBone)];
    std::memcpy(scratch, static_cast<void*>(bone), sizeof(scratch));
    auto* copy = reinterpret_cast<FigureBone*>(scratch);
    FigureBone* expected = reinterpret_cast<FigureBone*(__fastcall*)(FigureBone*)>(s_construct)(copy);
    FigureBone* result = bone->Construct();
    const auto* a = reinterpret_cast<const unsigned char*>(copy);
    const auto* b = reinterpret_cast<const unsigned char*>(bone);
    // Compared as bytes except: the three CStrings (+0x1C..+0x27; separate runs hold separate string
    // blocks, so their text is compared), the offset the original leaves unwritten, and the matrix
    // block pointer (each run allocates its own).
    constexpr std::size_t kNames = offsetof(FigureBone, name), kAfterNames = offsetof(FigureBone, parent);
    constexpr std::size_t kOffset = offsetof(FigureBone, offset), kMorph = offsetof(FigureBone, morphS);
    constexpr std::size_t kBlock = offsetof(FigureBone, matrices), kAfterBlock = kBlock + sizeof(void*);
    bool sameNames = std::strcmp(copy->name, bone->name) == 0 && std::strcmp(copy->name2, bone->name2) == 0 &&
                     std::strcmp(copy->name3, bone->name3) == 0;
    bool ok = expected == copy && result == bone && sameNames && std::memcmp(a, b, kNames) == 0 &&
              std::memcmp(a + kAfterNames, b + kAfterNames, kOffset - kAfterNames) == 0 &&
              std::memcmp(a + kMorph, b + kMorph, kBlock - kMorph) == 0 &&
              std::memcmp(a + kAfterBlock, b + kAfterBlock, sizeof(FigureBone) - kAfterBlock) == 0 &&
              (copy->matrices.data != nullptr) == (bone->matrices.data != nullptr);
    int differs = -1;
    for (std::size_t i = 0; i < sizeof(FigureBone) && differs < 0; ++i)
        if (a[i] != b[i] && !(i >= kNames && i < kAfterNames) && !(i >= kOffset && i < kMorph) &&
            !(i >= kBlock && i < kAfterBlock))
            differs = static_cast<int>(i);
    if (copy->matrices.data) lib::AlignedArray<Matrix4>::Free(copy->matrices.data);
    copy->name.~CString();
    copy->name2.~CString();
    copy->name3.~CString();
    ei::verify::Check("FigureBone::Construct", true, ok, "bone %p: names %s, first byte difference at +0x%X", static_cast<void*>(bone),
                      sameNames ? "same" : "differ", differs);
    return result;
}
EI_VERIFY(0x0050AF40, ConstructVerify, 7, s_construct);

// 0050B370 / 0050B510 free list blocks and release objects: ours only, the bone is blank after.
static void* s_create;
static int __fastcall CreateBoneVerify(FigureBone* bone, void*) {
    int result = bone->CreateBone();
    ei::verify::Check("FigureBone::CreateBone", true, result == 1 && IsBlank(bone), "result %d", result);
    return result;
}
EI_VERIFY(0x0050B370, CreateBoneVerify, 0, s_create);

static void* s_destroy;
static void __fastcall DestroyBoneVerify(FigureBone* bone, void*) {
    bone->DestroyBone();
    ei::verify::Check("FigureBone::DestroyBone", true, IsBlank(bone), "bone %p", static_cast<void*>(bone));
}
EI_VERIFY(0x0050B510, DestroyBoneVerify, 0, s_destroy);

// 0050B1D0 / 0050B1B0: ours only, the engine object's vtable back and no owner.
static void* s_destruct;
static void __fastcall DestructBodyVerify(FigureBone* bone, void*) {
    bone->DestructBody();
    std::uintptr_t vtable;
    std::memcpy(&vtable, static_cast<void*>(bone), sizeof(vtable));
    ei::verify::Check("FigureBone::DestructBody", true, vtable == CEngineObject::kVtableAddress && !bone->owner_,
                      "vtable %08X", static_cast<unsigned>(vtable));
}
EI_VERIFY(0x0050B1D0, DestructBodyVerify, 0, s_destruct);

static void* s_delete;
static FigureBone* __fastcall DeleteBoneVerify(FigureBone* bone, void*, unsigned flags) {
    FigureBone* result = bone->DeleteBone(flags);
    ei::verify::Check("FigureBone::DeleteBone", true, result == bone, "flags %u", flags);
    return result;
}
EI_VERIFY(0x0050B1B0, DeleteBoneVerify, 0, s_delete);
