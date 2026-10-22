// R20: the new renderer's shadow casters outside the view (world_client_shadow_casters.cpp).
#pragma once

class CWorldClient;

// After the world's object draws (CWorldClient::Render): the objects the game did not draw (or the
// parts of drawn ones it left out) whose shadow can reach the visible ground go into this frame's
// shadow map (render::ShadowCasterMesh). Reads the game's objects only.
void HandOverOutsideCasters(CWorldClient* world);
