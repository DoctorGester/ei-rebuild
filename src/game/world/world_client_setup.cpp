// The client world's set-up at startup: SetUpGraphics and the light colours by hour
// from the lights table (LoadLights; config\LightsGipat.ini). See world.h.
#include "engine/models/figure_manager.h"   // FigureObjectType

#include "game/console/console.h"
#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "engine/data/ini_file.h"
#include "engine/models/material.h"
#include "engine/scene/device.h"
#include "engine/models/link_loader.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/net/net_session.h"
#include "game/net/net_link.h"
#include "engine/data/options.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/players/player_source.h"
#include "engine/scene/sky_object.h"
#include "game/ui/ui_flying_hit_points.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "lib/scales.h"
#include "lib/crt.h"
#include "lib/str.h"

#include <bit>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr float kByteToUnit = lib::kByteScale;

// The game's data folder (a string global) and the strings passed by address.
constexpr const char* kConfigFolder = "config\\";
constexpr const char* kSunlightSection = "sunlight";
constexpr const char* kAmbientSection = "ambient";
constexpr const char* kSkySection = "sky";

constexpr int kNumbersKind = static_cast<int>(TextureKind::Effects);

constexpr const char* kNumbersName = "numbers";

// One section into `table`: each key's hour gets its colour, the hours since the previous key
// (in file order; hour 0 before the first) are stepped linearly from that key's colour.
void ReadTable(IniFile& ini, const char* section, LightColour* table) {
    int previous = 0;
    for (const std::string& key : ini.GetKeyNames(section)) {
        int index = str::ToInt(str::Mid(key, 4, 2));
        const std::vector<std::uint32_t> values = ini.GetIntArray(section, key);
        LightColour& to = table[index];
        to.r = static_cast<float>(static_cast<int>(values[0])) * kByteToUnit;
        to.g = static_cast<float>(static_cast<int>(values[1])) * kByteToUnit;
        to.b = static_cast<float>(static_cast<int>(values[2])) * kByteToUnit;
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
constexpr const char* kSkyPartName = "nask0";
constexpr const char* kSkyFigureFile = "nask0sky.fig";

} // namespace

void WorldClient::LoadLights(const std::string& file) {
    IniFile ini;
    ini.Load(g_dataFolder + kConfigFolder + file);
    ReadTable(ini, kSunlightSection, sunlight);
    ReadTable(ini, kAmbientSection, ambient);
    ReadTable(ini, kSkySection, sky);
}

void WorldClient::SetUpGraphics(TextureDevice* device) {
    GetTextureManager()->SetDevice(device);
    LoadSky();
    numbers = std::make_unique<FlyingHitPoints>();
    auto* texture = GetTextureManager()->FindOrCreate(kNumbersKind, kNumbersName);
    if (texture) {
        numbers->texture.Assign(texture);
        GetTextureManager()->LoadTexture(texture);
        texture->flags |= TextureFlag::Keep;
    }
    LinkFigureLoader* loader = new LinkFigureLoader(g_sceneRenderer.device, options::g_options.resourcesPath);
    figureLoader.Assign(loader);
    g_unitDatabase.CountSoundFiles(sounds.get());
}

// ---- The sky --------------------------------------------------------------------------

void WorldClient::LoadSky() {
    if (skyDome) return;
    skyDome = std::make_unique<Sky>();
    auto* figures = GetFigureManager();
    auto* object = figures->CreateObjectAs<SkyObject>(FigureObjectType::SkyObject, kSkyObjectId);
    if (!object) return;
    object->name = kSkyPartName;
    object->directory.clear();
    char* data = static_cast<char*>(figures->GetFile(kSkyFigureFile, nullptr));
    if (!data) {
        object->AddRef();
        object->Release();
        return;
    }
    object->LoadFromMemory(&data);
    skyDome->object.Assign(object);
    EngineObject* material = GetMaterialManager()->NewMaterial(kSkyMaterialType);
    skyDome->material.Assign(material);
}

// --- World client session ----------------------------------------------------------------------------

// The client world's session links: the local session takes a new link (single-player loopback, a network game
// client's driver) after the maps are emptied, and the per-frame pump the application
// asks whether messages are waiting. The same with a given link is dead code.

namespace {

// The local player's link when it is there and not detached (the dword +4's bit 31), else null.
netlink::NetLink* ActiveLink(Player* session) {
    netlink::NetLink* link = session->link;
    if (IsLive(link)) return link;
    return nullptr;
}

void PumpSources(WorldClient* world) {
    if (netlink::DriverSession* session = world->netSession.Get()) session->PumpDrivers(net::g_gameClient);
}

} // namespace

void WorldClient::SetPlayer(WorldHandle* player) {
    CloseSession();
    netlink::NetLink* link = netlink::LoopbackLink::CreatePair(player);
    ClearMaps();
    ResetZoneObjects();
    session->Connect(nullptr);
    session->Connect(link);
}

bool WorldClient::JoinGameClient() {
    net::ClientSession* client = net::g_gameClient;
    if (!IsLive(client)) return false;
    if (client->clients.empty()) return false;
    OpenSession();
    netlink::NetLink* link = netSession.Get()->Start(client->clients.front().Get());
    ClearMaps();
    ResetZoneObjects();
    session->Connect(nullptr);
    session->Connect(link);
    return true;
}

bool WorldClient::PumpHasIncoming() {
    PumpSources(this);
    netlink::NetLink* link = ActiveLink(session);
    return link ? link->HasIncoming() : false;
}

int WorldClient::PumpIncomingCount() {
    PumpSources(this);
    netlink::NetLink* link = ActiveLink(session);
    return link ? link->IncomingCount() : 0;
}

void WorldClient::DisconnectLocalPlayer() {
    PlayerConnection* view = session ? session->connection : nullptr;
    view->player->Disconnect();
}
