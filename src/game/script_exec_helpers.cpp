// Command helpers of the script runtime: areas (AddRectToArea, AddRoundToArea, DeleteArea) and point
// lights (CreatePointLight, MovePointLight, DeletePointLight). See script.h and
// docs/notes/script.md ("Command execution and runtime").
#include "script.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // inline assembly: no optimisation across it

namespace {

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

__declspec(naked) void* __cdecl GameMemmove(void*, const void*, std::size_t) EI_ORIGINAL(0x006E79A0)
// 004BCF10: an area's destructor (vtable 0073D31C, frees the parts).
__declspec(naked) void __fastcall AreaDestruct(void*) EI_ORIGINAL(0x004BCF10)

struct World {
    void RemoveTimedObject(int id);       // 0058E5D0
    void AddTimedObject(void* object);    // 0058E520
};
__declspec(naked) void World::RemoveTimedObject(int) EI_ORIGINAL(0x0058E5D0)
__declspec(naked) void World::AddTimedObject(void*) EI_ORIGINAL(0x0058E520)
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

// CArray<ScriptAreaPart>::SetSize(newSize, -1) as the area functions inline it.
void SetPartCount(MfcArray<ScriptAreaPart>* parts, int newSize) {
    constexpr int kSize = sizeof(ScriptAreaPart);
    if (newSize == 0) {
        if (parts->data) {
            gamecrt::operator_delete(parts->data);
            parts->data = nullptr;
        }
        parts->maxSize = 0;
        parts->size = 0;
        return;
    }
    if (!parts->data) {
        parts->data = static_cast<ScriptAreaPart*>(gamecrt::operator_new(newSize * kSize));
        std::memset(parts->data, 0, newSize * kSize);
        parts->maxSize = newSize;
        parts->size = newSize;
        return;
    }
    if (newSize <= parts->maxSize) {
        if (newSize > parts->size) std::memset(&parts->data[parts->size], 0, (newSize - parts->size) * kSize);
        parts->size = newSize;
        return;
    }
    int grow = parts->growBy;
    if (grow == 0) {
        grow = parts->size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 1024) grow = 1024;
    }
    int newMax = parts->maxSize + grow;
    if (newSize >= newMax) newMax = newSize;
    auto* data = static_cast<ScriptAreaPart*>(gamecrt::operator_new(newMax * kSize));
    std::memcpy(data, parts->data, parts->size * kSize);
    std::memset(&data[parts->size], 0, (newSize - parts->size) * kSize);
    gamecrt::operator_delete(parts->data);
    parts->data = data;
    parts->size = newSize;
    parts->maxSize = newMax;
}

void AddPart(MfcArray<ScriptAreaPart>* parts, const ScriptAreaPart& part) {
    int at = parts->size;
    SetPartCount(parts, at + 1);
    parts->data[at] = part;
}

// A point light slot (CScript +0x04, 256 of them).
struct PointLight {
    CEngineObject* object;  // +0x00 effect object (class 0x4B)
    int id;                 // +0x04
    int removeWhenDone;     // +0x08
    int active;             // +0x0C moving
    int stepsLeft;          // +0x10
    float dx, dy, dz;       // +0x14 step
};
static_assert(sizeof(PointLight) == 0x20);

PointLight* Lights(CScript* script) { return Field<PointLight*>(script, 0x04); }
int LightCount(CScript* script) { return Field<int>(script, 0x08); }

// MovePointLight's step: (target - position) / steps per axis. The x difference stays in the FPU,
// the y and z differences are rounded to float first, as the original computes them.
void LightStep(PointLight* light, const float* target, const float* position) {
    float y;
    float z;
    float dy;
    float dz;
    float inverse;
    float* destination = &light->dx;
    int* steps = &light->stepsLeft;
    __asm {
        mov eax, target
        mov edx, position
        fld dword ptr [eax]
        fld dword ptr [eax + 4]
        fstp y
        fld dword ptr [eax + 8]
        fstp z
        fsub dword ptr [edx]
        fld y
        fsub dword ptr [edx + 4]
        fstp dy
        fld z
        fsub dword ptr [edx + 8]
        fstp dz
        mov ecx, steps
        fild dword ptr [ecx]
        fdivr dword ptr ds:[0x0073B84C]
        fstp inverse
        fmul inverse
        mov ecx, destination
        fstp dword ptr [ecx]
        fld dy
        fmul inverse
        fstp dword ptr [ecx + 4]
        fld dz
        fmul inverse
        fstp dword ptr [ecx + 8]
    }
}

} // namespace

// ---- Areas --------------------------------------------------------------------------------------

void __fastcall ScriptAreaAddRect(MfcArray<ScriptAreaPart>* area, void*, float x1, float y1, float x2, float y2) {
    ScriptAreaPart part = {};
    part.isRect = 1;
    part.x = x1;
    part.y = y1;
    part.x2OrRadiusSquared = x2;
    part.y2 = y2;
    AddPart(area, part);
}
EI_REPLACE(0x004B1ED0, ScriptAreaAddRect);

void __fastcall ScriptAreaAddRound(MfcArray<ScriptAreaPart>* area, void*, float x, float y, float radius) {
    ScriptAreaPart part = {};
    part.isRect = 0;
    part.x = x;
    part.y = y;
    part.x2OrRadiusSquared = radius * radius;
    AddPart(area, part);
}
EI_REPLACE(0x004B2070, ScriptAreaAddRound);

void __fastcall ScriptDeleteArea(CScript* script, void*, int id) {
    // CArray of areas at +0x194 (data +0x198, size +0x19C): {parts (CArray), id}, 0x18 each.
    constexpr int kAreaSize = 0x18;
    for (int i = 0; i < Field<int>(script, 0x19C); ++i) {
        char* areas = Field<char*>(script, 0x198);
        if (Field<int>(areas + i * kAreaSize, 0x14) != id) continue;
        int after = Field<int>(script, 0x19C) - i - 1;
        AreaDestruct(areas + i * kAreaSize);
        if (after) {
            areas = Field<char*>(script, 0x198);
            GameMemmove(areas + i * kAreaSize, areas + (i + 1) * kAreaSize, after * kAreaSize);
        }
        --Field<int>(script, 0x19C);
        --i;   // the next area moved into this slot
    }
}
EI_REPLACE(0x004BCF30, ScriptDeleteArea);

// ---- Point lights -------------------------------------------------------------------------------

void __fastcall ScriptCreatePointLight(CScript* script, void*, const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = x87::fistp(f[0]);
    // The light with this id is replaced, else the first free slot is used (one with the id after
    // a free slot is not found).
    int i = 0;
    for (; i < LightCount(script); ++i) {
        PointLight* light = &Lights(script)[i];
        if (!light->object) break;
        if (light->id == id) {
            TheWorld()->RemoveTimedObject(Lights(script)[i].object->id_);
            Lights(script)[i].object->Release();
            break;
        }
    }
    Lights(script)[i].active = 0;
    Lights(script)[i].id = id;
    Lights(script)[i].removeWhenDone = 0;
    PointLight* slot = &Lights(script)[i];
    void* factory = *reinterpret_cast<void**>(0x007AFFA8);
    slot->object = ei::VCall<CEngineObject*>(factory, 0x5C / 4, 0x4B, -1);
    CEngineObject* object = Lights(script)[i].object;
    ++object->refCount_;
    f = cmd->floatArgs.data;
    const float scale = *reinterpret_cast<const float*>(0x0073C36C);   // 1 / 255
    float red = f[5] * scale;
    float green = f[6] * scale;
    float blue = f[7] * scale;
    Field<float>(object, 0x14C) = red;
    Field<float>(object, 0x150) = green;
    Field<float>(object, 0x154) = blue;
    Field<float>(object, 0x1C) = f[1];
    Field<float>(object, 0x20) = f[2];
    Field<float>(object, 0x24) = f[3];
    std::memcpy(&Field<float>(object, 0x158), &f[4], 4);   // radius
    TheWorld()->AddTimedObject(object);
    slot = &Lights(script)[i];
    slot->dx = 0.0f;
    slot->dy = 0.0f;
    slot->dz = 0.0f;
    slot->stepsLeft = 0;
}
EI_REPLACE(0x004BD6A0, ScriptCreatePointLight);

void __fastcall ScriptMovePointLight(CScript* script, void*, const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = x87::fistp(f[0]);
    int i = 0;
    for (;; ++i) {
        if (i >= LightCount(script)) return;
        PointLight* light = &Lights(script)[i];
        if (light->object && light->id == id) break;
    }
    PointLight* light = &Lights(script)[i];
    light->active = 1;
    light->removeWhenDone = x87::fistp(f[5]);
    light->stepsLeft = x87::fistp(f[4]);
    LightStep(light, &f[1], &Field<float>(light->object, 0x1C));
}
EI_REPLACE(0x004BD820, ScriptMovePointLight);

void __fastcall ScriptDeletePointLight(CScript* script, void*, const SSCCmd* cmd) {
    int id = x87::fistp(cmd->floatArgs.data[0]);
    for (int i = 0; i < LightCount(script); ++i) {
        PointLight* light = &Lights(script)[i];
        if (!light->object || light->id != id) continue;
        TheWorld()->RemoveTimedObject(light->object->id_);
        Lights(script)[i].object->Release();
        Lights(script)[i].object = nullptr;
        return;
    }
}
EI_REPLACE(0x004BD920, ScriptDeletePointLight);
