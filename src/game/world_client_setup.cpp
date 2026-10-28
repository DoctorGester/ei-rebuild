// The client world's set-up at startup: SetUpGraphics and the light colours by hour
// from the lights table (LoadLights; config\LightsGipat.ini). See world.h.
#include "material.h"
#include "options.h"
#include "world.h"

#include "model.h"
#include "game_strings.h"
#include "sky_object.h"
#include "texture.h"
#include "ui_flying_hit_points.h"
#include "unit_db.h"

#include "console.h"
#include "core_maths.h"
#include "ini_file.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "game_singletons.h"

#include <bit>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr float kByteToUnit = 0.003921569f;   // 1 / 255
static_assert(std::bit_cast<std::uint32_t>(kByteToUnit) == 0x3B808081u);

// The game's data folder (a string global) and the strings passed by address.
const char* ConfigFolder() { return "config\\"; }
const char* SunlightSection() { return "sunlight"; }
const char* AmbientSection() { return "ambient"; }
const char* SkySection() { return "sky"; }

constexpr int kNumbersKind = 6;
constexpr std::uint32_t kTextureKeep = 0x40000;   // CEngineObject::flags_ of the digits' texture

const char* NumbersName() { return "numbers"; }
const char* FiguresResource() { return options::g_options->resourcesPath_.c_str(); }

// One section into `table`: each key's hour gets its colour, the hours since the previous key
// (in file order; hour 0 before the first) are stepped linearly from that key's colour. The
// accumulated colour stays unrounded between the steps (FPU stack), each stored as a float.
void ReadTable(CIniFile& ini, std::vector<std::string>& keys, std::string& hour, std::vector<std::uint32_t>& values, const char* section,
               LightColour* table) {
    keys.clear();
    ini.GetKeyNames(section, &keys);
    int previous = 0;
    for (const std::string& key : keys) {
        hour = str::Mid(key, 4, 2);
        int index = str::ToInt(hour);
        values.clear();      // the sky section frees the block inline (the same state)
        ini.GetIntArray(section, key.c_str(), &values);
        LightColour& to = table[index];
        to.r = static_cast<float>(static_cast<double>(static_cast<int>(values[0])) * kByteToUnit);
        to.g = static_cast<float>(static_cast<double>(static_cast<int>(values[1])) * kByteToUnit);
        to.b = static_cast<float>(static_cast<double>(static_cast<int>(values[2])) * kByteToUnit);
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

int CWorldClient::LoadLights(const std::string& file) {
    std::vector<std::string> keys;
    std::string hour;
    std::vector<std::uint32_t> values{};
    CIniFile ini;   // the empty path, freed at the end
    {
        std::string folder = *g_dataFolder + ConfigFolder();
        std::string path = folder + file;
        ini.SetPath(path);
    }
    ReadTable(ini, keys, hour, values, SunlightSection(), sunlight_);
    ReadTable(ini, keys, hour, values, AmbientSection(), ambient_);
    ReadTable(ini, keys, hour, values, SkySection(), sky_);
    values.clear();
    return 1;
}

void CWorldClient::SetUpGraphics(CTextureDevice* device) {
    TextureManager()->SetDevice(device);
    LoadSky();
    numbers_ = new CFlyingHitPoints();
    auto* texture = TextureManager()->FindOrCreate(kNumbersKind, NumbersName());
    if (texture) {
        ++texture->refCount_;
        TextureManager()->LoadTexture(texture);
        numbers_->texture_ = texture;
        texture->flags_ |= kTextureKeep;
    }
    CLinkFigureLoader* loader;
    {
        std::string resource(FiguresResource());
        loader = new CLinkFigureLoader(g_sceneRenderer->device_, &resource);
    }
    CLinkFigureLoader* old = figureLoader_;
    figureLoader_ = loader;
    if (loader) ++loader->refCount_;
    if (old && --old->refCount_ == 0) delete old;
    g_unitDatabase->CountSoundFiles(sounds_);
}

// ---- The sky --------------------------------------------------------------------------

void CWorldClient::LoadSky() {
    if (skyDome_) return;
    skyDome_ = new CSky();
    auto* figures = FigureManager();
    auto* object = figures->CreateObjectAs<CSkyObject>(kSkyFigureClass, kSkyObjectId);
    if (!object) return;
    object->name_ = SkyPartName();
    object->directory_ = game::kEmptyText;
    char* data = static_cast<char*>(figures->GetFile(SkyFigureFile(), nullptr));
    if (!data) {
        ++object->refCount_;
        object->Release();
        return;
    }
    object->LoadFromMemory(&data, 0.0f, 0.0f, 0.0f);
    object->flags_ |= kSkyObjectFlag;
    AssignCounted(skyDome_->object, object);
    CEngineObject* material = MaterialManager()->NewMaterial(kSkyMaterialType);
    AssignCounted(skyDome_->material, material);
}
