// CLightning's initialiser (004680D0; called by the lightning client object's setter 0067A479).
// Plain float maths (rule 7); random words converted with x87::FromUnsigned. See
// docs/notes/objects.md ("Rewritten").
#include "lightning.h"
#include "texture.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_render.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "originals_core.h"
#include "game_data.h"

namespace {

// 00500A00: releases the texture in `holder` and loads `name` (kind 6).

IsaacRandom* Rng() { return ei::GamePointer<IsaacRandom>(0x007A9D40); }
float GameFloat(std::uintptr_t address) {
    float f;
    std::memcpy(&f, ei::GamePointer<const void>(address), sizeof(f));
    return f;
}
double GameDouble(std::uintptr_t address) {
    double d;
    std::memcpy(&d, reinterpret_cast<const void*>(address), sizeof(d));
    return d;
}
float Absolute(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits &= 0x7FFFFFFFu;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The noise amplitude of a segment's keys: 1.5 for positive power, 2 for zero (or NaN), else 1.
float KeyAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return !(power < 0.0f) && !(power > 0.0f) ? 2.0f : 1.0f;
}

void FreshKeys(LightningSegment& s, float time, float amplitude) {
    s.noiseTime = time;
    for (int k = 0; k < 3; ++k) {
        s.sideKeys[k].Randomize(amplitude);
        s.upKeys[k].Randomize(amplitude);
    }
}

} // namespace

void CLightning::Start(const Vector3* from, const Vector3* to, float power, bool demon) {
    int ticks = ei::GameData<const volatile int>(0x007AF61C);
    float fraction = ei::GameData<const volatile float>(0x007AFCA8);
    float time = static_cast<float>((static_cast<double>(ticks) + fraction) / GameFloat(0x0073C4F8));   // 4
    power_ = power;
    const float strength = Absolute(power);
    amplitude_ = power_ > GameFloat(0x0073B858) ? GameFloat(0x0073C52C) : GameFloat(0x0073C54C);   // 0.05 : 0.3
    // A bolt about 30 units high sways less.
    if (!(Absolute(Absolute(to->z - from->z) - GameFloat(0x0073BAF0)) >= GameFloat(0x0073C548))) amplitude_ = 0.05f;

    main_.Init(from, to);
    FreshKeys(main_, time, KeyAmplitude(power_));
    main_.width = strength;
    if (!(power_ >= GameFloat(0x0073B858))) main_.width = strength * GameFloat(0x0073C51C);   // 1.5

    if (strength > 0.0f || strength < 0.0f) {
        const float positions[6] = {0.2f, 0.3f, 0.5f, 0.8f, 0.9f, 0.9f};
        std::memcpy(branchPos_, positions, sizeof(positions));
        for (int i = 0; i < 6; ++i) {
            float along = branchPos_[i];
            float amplitude = KeyAmplitude(power_);
            float rest = 1.0f - along;
            Vector3 origin;
            origin.x = along * from->x + rest * to->x;
            origin.y = along * from->y + rest * to->y;
            origin.z = along * from->z + rest * to->z;
            branches_[i].Init(&origin, to);
            FreshKeys(branches_[i], time, amplitude);
            double random = x87::FromUnsigned(Rng()->Next());
            branches_[i].width = static_cast<float>((random * GameDouble(0x0073C540) + GameFloat(0x0073C538)) * strength *
                                                    GameFloat(0x0073B86C));
        }
    } else {
        main_.width = 4.0f;
        for (int i = 0; i < 6; ++i) {
            float dx = from->x - to->x, dy = from->y - to->y, dz = from->z - to->z;
            float reach = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * GameFloat(0x0073C52C));
            float low = -reach;
            const double scale = GameDouble(0x0073C468);
            float offsetY = static_cast<float>((reach - low) * x87::FromUnsigned(Rng()->Next()) * scale + low);
            float offsetX = static_cast<float>((reach - low) * x87::FromUnsigned(Rng()->Next()) * scale + low);
            float offsetZ = static_cast<float>((reach - low) * x87::FromUnsigned(Rng()->Next()) * scale + low);
            float amplitude = KeyAmplitude(power_);
            Vector3 shiftedTo{offsetX + to->x, offsetY + to->y, offsetZ + to->z};
            Vector3 shiftedFrom{offsetX + from->x, offsetY + from->y, offsetZ + from->z};
            branches_[i].Init(&shiftedFrom, &shiftedTo);
            FreshKeys(branches_[i], time, amplitude);
            double random = x87::FromUnsigned(Rng()->Next());
            branches_[i].width = static_cast<float>(random * GameDouble(0x0073C540) * GameFloat(0x0073C534) +
                                                    GameFloat(0x0073C530));   // 2.45 .. 3.25
        }
    }

    Animate();
    UpdateBounds();
    boundsMin_.x -= 1.0f;
    boundsMin_.y -= 1.0f;
    boundsMin_.z -= 1.0f;
    boundsMax_.x += 1.0f;
    boundsMax_.y += 1.0f;
    boundsMax_.z += 1.0f;
    radius_ = boundsMax_.Length();
    auto** glow = reinterpret_cast<CTexture**>(&glowTexture_);
    auto** main = reinterpret_cast<CTexture**>(&texture_);
    if (demon) {
        TextureHolderLoad(glow, nullptr, 6, "DemonLight");
        TextureHolderLoad(main, nullptr, 6, "DemonLightning");
    } else {
        TextureHolderLoad(glow, nullptr, 6, "SmtgLight");
        TextureHolderLoad(main, nullptr, 6, "Lightning");
    }
}

namespace {
// CRT eh vector constructor / destructor iterators (pointer, element size, count, [constructor,]
// destructor); the segment's element constructor 00468900 and destructor 00468920; the engine
// object's destructor body 00455930.
void __stdcall VectorCtorIterator(void* a0, unsigned a1, int a2, void* a3, void* a4) { libcrt::ConstructElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3), reinterpret_cast<libcrt::ElementFunction>(a4)); }
void __stdcall VectorDtorIterator(void* a0, unsigned a1, int a2, void* a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
// The segment's element constructor and destructor as the iterators call them (ECX the element).
void __fastcall SegmentConstruct(LightningSegment* segment) { segment->Construct(); }
void __fastcall SegmentDestruct(LightningSegment* segment) { segment->Destruct(); }

// A texture reference dropped: cleared, then released.
void DropTexture(CEngineObject*& slot) {
    CEngineObject* old = slot;
    slot = nullptr;
    if (old) old->Release();
}
void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }
} // namespace

// 00468680 (CLightningManager slot 14, the object factory; `type` is not used): a new bolt (the
// engine object's constructor inlined, the bounds cleared, the main segment's points empty, the
// branches constructed), reset like slot 3.
CLightning* CLightningManager::NewObjectImpl(int /*type*/) {
    auto* bolt = static_cast<CLightning*>(gamecrt::operator_new(sizeof(CLightning)));
    if (!bolt) return nullptr;
    auto* bytes = reinterpret_cast<std::uint8_t*>(bolt);
    SetVtable(bolt, CEngineObject::kVtableAddress);
    bolt->refCount_ = 0;
    bolt->id_ = -1;
    bolt->prototypeId_ = 0;
    bolt->type_ = 0;
    bolt->flags_ = 0;
    bolt->owner_ = nullptr;
    bolt->texture_ = nullptr;
    bolt->glowTexture_ = nullptr;
    std::memset(bytes + 0x24, 0, 14 * 4);
    bolt->main_.points.data = nullptr;
    bolt->main_.points.growBy = 0;
    bolt->main_.points.capacity = 0;
    bolt->main_.points.size = 0;
    VectorCtorIterator(bolt->branches_, sizeof(LightningSegment), 6, reinterpret_cast<void*>(SegmentConstruct),
                       reinterpret_cast<void*>(SegmentDestruct));
    SetVtable(bolt, CLightning::kVtableAddress);
    std::uint32_t one = 0x3F800000u, tenth = 0x3DCCCCCDu;
    std::memcpy(&bolt->power_, &one, 4);
    std::memcpy(&bolt->amplitude_, &tenth, 4);
    DropTexture(bolt->texture_);
    DropTexture(bolt->glowTexture_);
    return bolt;
}
EI_REPLACE_METHOD(0x00468680, CLightningManager::NewObjectImpl);

// 00468810: the destructor body: reset like slot 3, the branches destroyed, the main segment's
// points freed, the texture references released again (both null by then), the engine object's
// body.
void __fastcall LightningDestruct(CLightning* self) {
    SetVtable(self, CLightning::kVtableAddress);
    std::uint32_t one = 0x3F800000u, tenth = 0x3DCCCCCDu;
    std::memcpy(&self->power_, &one, 4);
    std::memcpy(&self->amplitude_, &tenth, 4);
    DropTexture(self->texture_);
    DropTexture(self->glowTexture_);
    VectorDtorIterator(self->branches_, sizeof(LightningSegment), 6, reinterpret_cast<void*>(SegmentDestruct));
    if (self->main_.points.data) gamecrt::operator_delete(reinterpret_cast<void**>(self->main_.points.data)[-1]);
    if (self->glowTexture_) self->glowTexture_->Release();
    if (self->texture_) self->texture_->Release();
    static_cast<CEngineObject*>(self)->DestructBase();
}
EI_REPLACE(0x00468810, LightningDestruct);

EI_REPLACE_METHOD(0x004680D0, CLightning::Start);
