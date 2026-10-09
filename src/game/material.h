#pragma once

#include "engine_object.h"

#include "lib/mfc.h"
#include "lib/vecmath.h"

#include <cstddef>

// A material, made by the material manager (*007AF740, 0068E970 MaterialNewOriginal): an engine
// object with a name and a Direct3D material (D3DMATERIAL7: diffuse, ambient, specular and
// emissive colours (r, g, b, a), power). Name provisional; only the members used so far.
class CMaterial : public CEngineObject {
public:
    mfc::CString name;      // +0x1C
    Vector4 diffuse;        // +0x20
    Vector4 ambient;        // +0x30
    Vector4 specular;       // +0x40
    Vector4 emissive;       // +0x50
    float power;            // +0x60
};
static_assert(offsetof(CMaterial, name) == 0x1C && offsetof(CMaterial, diffuse) == 0x20 &&
              offsetof(CMaterial, emissive) == 0x50 && offsetof(CMaterial, power) == 0x60);
