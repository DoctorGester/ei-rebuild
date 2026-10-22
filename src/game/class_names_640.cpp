// The CEngineObject class name / id slots (11 / 12, each a MOV EAX,imm32) of the classes in
// 0x640000-0x6C0000 whose files do not have them (A-H24; class_names_640.h). The names are the
// exe's own strings, returned by address as the originals do.
#include "class_names_640.h"
#include "game_strings.h"

#include "ei/hook.h"

#include <cstdint>

// 0065F560 / 0065F570: "CParty", 9.

// The world's effects (world_effect_*.cpp).
// 00676060 / 00676070: "CEffectSound", 0x48.
const char* __fastcall EffectSoundClassName(CEngineObject*) { return "CEffectSound"; }
int __fastcall EffectSoundClassId(CEngineObject*) { return 0x48; }
EI_REWRITTEN(0x00676060, EffectSoundClassName);
EI_REWRITTEN(0x00676070, EffectSoundClassId);
// 00676940 / 00676950: "CEffectSoundSeq", 0x63.
const char* __fastcall EffectSoundSeqClassName(CEngineObject*) { return "CEffectSoundSeq"; }
int __fastcall EffectSoundSeqClassId(CEngineObject*) { return 0x63; }
EI_REWRITTEN(0x00676940, EffectSoundSeqClassName);
EI_REWRITTEN(0x00676950, EffectSoundSeqClassId);
// 006776A0 / 006776B0: "CEffectAmbientSound", 0x49.
const char* __fastcall EffectAmbientSoundClassName(CEngineObject*) { return "CEffectAmbientSound"; }
int __fastcall EffectAmbientSoundClassId(CEngineObject*) { return 0x49; }
EI_REWRITTEN(0x006776A0, EffectAmbientSoundClassName);
EI_REWRITTEN(0x006776B0, EffectAmbientSoundClassId);
// 00677F80 / 00677F90: "CEffectPointLight", 0x4B.
const char* __fastcall EffectPointLightClassName(CEngineObject*) { return "CEffectPointLight"; }
int __fastcall EffectPointLightClassId(CEngineObject*) { return 0x4b; }
EI_REWRITTEN(0x00677F80, EffectPointLightClassName);
EI_REWRITTEN(0x00677F90, EffectPointLightClassId);
// 00678950 / 00678960: "CEffectParticle", 0x4A.
const char* __fastcall EffectParticleClassName(CEngineObject*) { return "CEffectParticle"; }
int __fastcall EffectParticleClassId(CEngineObject*) { return 0x4a; }
EI_REWRITTEN(0x00678950, EffectParticleClassName);
EI_REWRITTEN(0x00678960, EffectParticleClassId);
// 006799A0 / 006799B0: "CEffectWall", 0x5A.
const char* __fastcall EffectWallClassName(CEngineObject*) { return "CEffectWall"; }
int __fastcall EffectWallClassId(CEngineObject*) { return 0x5a; }
EI_REWRITTEN(0x006799A0, EffectWallClassName);
EI_REWRITTEN(0x006799B0, EffectWallClassId);
// 0067A070 / 0067A080: "CEffectLightning", 0x4D.
const char* __fastcall EffectLightningClassName(CEngineObject*) { return "CEffectLightning"; }
int __fastcall EffectLightningClassId(CEngineObject*) { return 0x4d; }
EI_REWRITTEN(0x0067A070, EffectLightningClassName);
EI_REWRITTEN(0x0067A080, EffectLightningClassId);
// 0067B180 / 0067B190: "CEffectArrow", 0x4E.
const char* __fastcall EffectArrowClassName(CEngineObject*) { return "CEffectArrow"; }
int __fastcall EffectArrowClassId(CEngineObject*) { return 0x4e; }
EI_REWRITTEN(0x0067B180, EffectArrowClassName);
EI_REWRITTEN(0x0067B190, EffectArrowClassId);
// 0067BC90 / 0067BCA0: "CEffectArrowFigure", 0x5B.
const char* __fastcall EffectArrowFigureClassName(CEngineObject*) { return "CEffectArrowFigure"; }
int __fastcall EffectArrowFigureClassId(CEngineObject*) { return 0x5b; }
EI_REWRITTEN(0x0067BC90, EffectArrowFigureClassName);
EI_REWRITTEN(0x0067BCA0, EffectArrowFigureClassId);
// 0067C450 / 0067C460: "CEffectShowVisibility", 0x5E.
const char* __fastcall EffectShowVisibilityClassName(CEngineObject*) { return "CEffectShowVisibility"; }
int __fastcall EffectShowVisibilityClassId(CEngineObject*) { return 0x5e; }
EI_REWRITTEN(0x0067C450, EffectShowVisibilityClassName);
EI_REWRITTEN(0x0067C460, EffectShowVisibilityClassId);
// 0067CAB0 / 0067CAC0: "CEffectMoshka", 0x64.
const char* __fastcall EffectMoshkaClassName(CEngineObject*) { return "CEffectMoshka"; }
int __fastcall EffectMoshkaClassId(CEngineObject*) { return 0x64; }
EI_REWRITTEN(0x0067CAB0, EffectMoshkaClassName);
EI_REWRITTEN(0x0067CAC0, EffectMoshkaClassId);
// 0067D7C0 / 0067D7D0: "CEffectTornado", 0x65.
const char* __fastcall EffectTornadoClassName(CEngineObject*) { return "CEffectTornado"; }
int __fastcall EffectTornadoClassId(CEngineObject*) { return 0x65; }
EI_REWRITTEN(0x0067D7C0, EffectTornadoClassName);
EI_REWRITTEN(0x0067D7D0, EffectTornadoClassId);

// 0068B280 / 0068B290: "CSpell", 0x4F.


// The items (item.h).
