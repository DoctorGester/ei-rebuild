// The client world's set-up at startup: SetUpGraphics and the light colours by hour
// from the lights table (LoadLights; config\LightsGipat.ini). See world.h.
#include "figure_manager.h"   // FigureObjectType
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
#include "lib/pc24.h"
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

constexpr int kNumbersKind = static_cast<int>(TextureKind::Effects);

const char* NumbersName() { return "numbers"; }
const char* FiguresResource() { return options::g_options.resourcesPath.c_str(); }

// One section into `table`: each key's hour gets its colour, the hours since the previous key
// (in file order; hour 0 before the first) are stepped linearly from that key's colour. The file's
// ints enter the product exactly (pc24::Mul rounds it once).
void ReadTable(IniFile& ini, const char* section, LightColour* table) {
    int previous = 0;
    for (const std::string& key : ini.GetKeyNames(section)) {
        int index = str::ToInt(str::Mid(key, 4, 2));
        const std::vector<std::uint32_t> values = ini.GetIntArray(section, key.c_str());
        LightColour& to = table[index];
        to.r = pc24::Mul(static_cast<double>(static_cast<int>(values[0])), kByteToUnit);
        to.g = pc24::Mul(static_cast<double>(static_cast<int>(values[1])), kByteToUnit);
        to.b = pc24::Mul(static_cast<double>(static_cast<int>(values[2])), kByteToUnit);
        to.a = 1.0f;
        const LightColour& from = table[previous];
        float r = from.r, g = from.g, b = from.b;
        if (previous + 1 < index) {
            float steps = static_cast<float>(index - previous);   // two digits: exact
            for (int k = previous + 1; k < index; ++k) {
                r += (to.r - from.r) / steps;
                g += (to.g - from.g) / steps;
                b += (to.b - from.b) / steps;
                table[k].r = r;
                table[k].g = g;
                table[k].b = b;
                table[k].a = 1.0f;
            }
        }
        previous = index;
    }
}

constexpr int kSkyObjectId = -666;
constexpr int kSkyMaterialType = 0x12;
const char* SkyPartName() { return "nask0"; }
const char* SkyFigureFile() { return "nask0sky.fig"; }

} // namespace

int WorldClient::LoadLights(const std::string& file) {
    IniFile ini;   // the empty path, freed at the end
    {
        std::string folder = g_dataFolder + ConfigFolder();
        std::string path = folder + file;
        ini.SetPath(path);
    }
    ReadTable(ini, SunlightSection(), sunlight);
    ReadTable(ini, AmbientSection(), ambient);
    ReadTable(ini, SkySection(), sky);
    return 1;
}

void WorldClient::SetUpGraphics(TextureDevice* device) {
    GetTextureManager()->SetDevice(device);
    LoadSky();
    numbers = new FlyingHitPoints();
    auto* texture = GetTextureManager()->FindOrCreate(kNumbersKind, NumbersName());
    if (texture) {
        numbers->texture.Assign(texture);
        GetTextureManager()->LoadTexture(texture);
        texture->flags |= TextureFlag::Keep;
    }
    LinkFigureLoader* loader;
    {
        std::string resource(FiguresResource());
        loader = new LinkFigureLoader(g_sceneRenderer.device, &resource);
    }
    figureLoader.Assign(loader);
    g_unitDatabase.CountSoundFiles(sounds);
}

// ---- The sky --------------------------------------------------------------------------

void WorldClient::LoadSky() {
    if (skyDome) return;
    skyDome = new Sky();
    auto* figures = GetFigureManager();
    auto* object = figures->CreateObjectAs<SkyObject>(FigureObjectType::SkyObject, kSkyObjectId);
    if (!object) return;
    object->name = SkyPartName();
    object->directory = game::kEmptyText;
    char* data = static_cast<char*>(figures->GetFile(SkyFigureFile(), nullptr));
    if (!data) {
        object->AddRef();
        object->Release();
        return;
    }
    object->LoadFromMemory(&data);
    object->flags |= FigureMeshFlag::Lit;
    skyDome->object.Assign(object);
    EngineObject* material = GetMaterialManager()->NewMaterial(kSkyMaterialType);
    skyDome->material.Assign(material);
}
