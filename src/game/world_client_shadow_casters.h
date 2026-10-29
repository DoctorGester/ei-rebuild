// The new renderer's shadow casters outside the view (world_client_shadow_casters.cpp).
#pragma once

class WorldClient;

// After the world's object draws (WorldClient::Render): the objects the game did not draw (or the
// parts of drawn ones it left out) whose shadow can reach the visible ground go into this frame's
// shadow map (render::ShadowCasterMesh). Reads the game's objects only.
void HandOverOutsideCasters(WorldClient* world);

// The frame's light for the GPU lighting (render::SceneLighting), after UpdateLighting: the sun
// (direction, colour), the frame's ambient colour and the landscape's point lights. Reads only.
void HandOverSceneLights(WorldClient* world);

// After the ground's draws, with the landscape's sun shadows on (flag 0x10): the loaded sectors
// whose ground can shadow the visible ground cast into the shadow map (render::ShadowTerrainSector).
// Reads the sector grid directly (no SectorAt: the cache's order and loads stay the game's).
void HandOverTerrainCasters(WorldClient* world);
