// The client world's set-up at startup: SetUpGraphics (00563710) and the light colours by hour
// from the lights table (LoadLights, 00563930; config\LightsGipat.ini). See world.h.
#include "material.h"
#include "world.h"

#include "model.h"
#include "game_strings.h"
#include "originals_render.h"
#include "sky_object.h"
#include "originals_ui.h"
#include "texture.h"
#include "ui_flying_hit_points.h"
#include "unit_db.h"

#include "console.h"
#include "core_maths.h"
#include "ini_file.h"
#include "originals_library.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "game_singletons.h"

#include <bit>

namespace {
const float kByteToUnit = std::bit_cast<float>(0x3B808081u);   // 0073C36C: 1 / 255

// The game's data folder (a CString global) and the strings the original passes by address.
const char* ConfigFolder() { return "config\\"; }
const char* SunlightSection() { return "sunlight"; }
const char* AmbientSection() { return "ambient"; }
const char* SkySection() { return "sky"; }

constexpr int kNumbersKind = 6;
constexpr std::uint32_t kTextureKeep = 0x40000;   // CEngineObject::flags_ of the digits' texture

const char* NumbersName() { return "numbers"; }
const char* FiguresResource() { return ei::GameData<const char* const>(0x0079BC94); } // a CString
UnitDatabase* Units() { return &unitdb::Database(); }

// Through their addresses: the texture manager's device (006913A0, RET 4; not rewritten) and the
// figure loader's constructor (004F68C0, RET 8, returns this; Rebirth-patched, its vanilla body is
// in link_loader_create.cpp).
__declspec(naked) CLinkFigureLoader* __fastcall ConstructFigureLoader(CLinkFigureLoader*, void*, void* /*renderer*/,
                                                                      const stlp::string* /*resource*/)
    EI_ORIGINAL(0x004F68C0)

// One section into `table`: each key's hour gets its colour, the hours since the previous key
// (in file order; hour 0 before the first) are stepped linearly from that key's colour. The
// accumulated colour stays unrounded between the steps (FPU stack), each stored as a float.
void ReadTable(CIniFile& ini, console::CStringArray& keys, mfc::CString& hour, DwordArray& values, const char* section,
               LightColour* table) {
    keys.SetSize(0, -1);
    ini.GetKeyNames(section, &keys);
    int previous = 0;
    for (int i = 0; i < keys.size; ++i) {
        hour = console::Ops(keys[i]).Mid(4, 2);
        int index = libcrt::Atoi(hour);
        values.SetSize(0, -1);      // the sky section frees the block inline (the same state)
        ini.GetIntArray(section, keys[i], &values);
        LightColour& to = table[index];
        to.r = static_cast<float>(static_cast<double>(static_cast<int>(values.data[0])) * kByteToUnit);
        to.g = static_cast<float>(static_cast<double>(static_cast<int>(values.data[1])) * kByteToUnit);
        to.b = static_cast<float>(static_cast<double>(static_cast<int>(values.data[2])) * kByteToUnit);
        to.a = 1.0f;
        const LightColour& from = table[previous];
        double r = from.r, g = from.g, b = from.b;
        if (previous + 1 < index) {
            double steps = index - previous;
            for (int k = previous + 1; k < index; ++k) {
                r += (static_cast<double>(to.r) - from.r) / steps;
                g += (static_cast<double>(to.g) - from.g) / steps;
                b += (static_cast<double>(to.b) - from.b) / steps;
                table[k].r = static_cast<float>(r);
                table[k].g = static_cast<float>(g);
                table[k].b = static_cast<float>(b);
                table[k].a = 1.0f;
            }
        }
        previous = index;
    }
}
} // namespace

int CWorldClient::LoadLights(const char* file) {
    console::CStringArray keys;
    mfc::CString hour;
    DwordArray values{};
    CIniFile ini;   // the empty path (00695B80), freed at the end (00695BE0)
    {
        mfc::CString folder = TheDataFolder() + ConfigFolder();
        mfc::CString path = folder + file;
        ini.SetPath(path);
    }
    ReadTable(ini, keys, hour, values, SunlightSection(), sunlight_);
    ReadTable(ini, keys, hour, values, AmbientSection(), ambient_);
    ReadTable(ini, keys, hour, values, SkySection(), sky_);
    values.SetSize(0, -1);
    return 1;
}
EI_REPLACE_METHOD(0x00563930, CWorldClient::LoadLights);

void CWorldClient::SetUpGraphics(void* device) {
    TextureManager()->SetDevice(static_cast<CTextureDevice*>(device));
    LoadSky();
    static_assert(sizeof(CFlyingHitPoints) == 8);
    auto* numbers = static_cast<CFlyingHitPoints*>(gamecrt::operator_new(sizeof(CFlyingHitPoints)));
    numbers_ = numbers ? numbers->Construct() : nullptr;
    auto* texture = TextureManager()->FindOrCreate(kNumbersKind, NumbersName());
    if (texture) {
        ++texture->refCount_;
        TextureManager()->LoadTexture(texture);
        numbers_->object_ = texture;
        texture->flags_ |= kTextureKeep;
    }
    auto* loader = static_cast<CLinkFigureLoader*>(gamecrt::operator_new(sizeof(CLinkFigureLoader)));
    if (loader) {
        stlp::string resource(FiguresResource());
        loader = ConstructFigureLoader(loader, nullptr, TheRenderDevice(), &resource);
    }
    CLinkFigureLoader* old = figureLoader_;
    figureLoader_ = loader;
    if (loader) ++loader->refCount_;
    if (old && --old->refCount_ == 0) old->DeleteThis(1);
    Units()->CountSoundFiles(sounds_);
}
EI_REPLACE_METHOD(0x00563710, CWorldClient::SetUpGraphics);

// ---- The sky (00562F70) --------------------------------------------------------------------------

namespace {
constexpr int kSkyFigureClass = 0x44;
constexpr int kSkyObjectId = -666;
constexpr int kSkyMaterialType = 0x12;
constexpr std::uint32_t kSkyObjectFlag = 0x10;
const char* SkyPartName() { return "nask0"; }
const char* SkyFigureFile() { return "nask0sky.fig"; }

// A counted slot gets `object` (referenced), the old one is released.
template <typename T>
void AssignCounted(T*& slot, T* object) {
    T* old = slot;
    slot = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}
} // namespace

void CWorldClient::LoadSky() {
    if (skyDome_) return;
    auto* sky = static_cast<CSky*>(gamecrt::operator_new(sizeof(CSky)));
    skyDome_ = sky ? sky->Construct() : nullptr;
    auto* figures = FigureManager();
    auto* object = static_cast<CSkyObject*>(figures->CreateObject(kSkyFigureClass, kSkyObjectId));
    if (!object) return;
    object->name_ = SkyPartName();
    object->directory_ = game::EmptyText();
    char* data = static_cast<char*>(figures->GetFile(SkyFigureFile(), nullptr));
    if (!data) {
        ++object->refCount_;
        object->Release();
        return;
    }
    object->LoadFromMemory(&data, 0.0f, 0.0f, 0.0f);
    object->flags_ |= kSkyObjectFlag;
    AssignCounted(skyDome_->object, object);
    auto* material = reinterpret_cast<CEngineObject*>(static_cast<CMaterialManager*>(MaterialManager())->NewMaterial(kSkyMaterialType));
    AssignCounted(skyDome_->material, material);
}
EI_REPLACE_METHOD(0x00562F70, CWorldClient::LoadSky);
