// Create and Destroy of the client controller (CUnitClientSpecific slots 2 / 3). See
// unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "ai_map.h"
#include "effects.h"

#include "ei/hook.h"
#include "game_data.h"
#include "ei/vcall.h"

namespace {
constexpr int kNone = -1;
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;
constexpr int kList58BlockSize = 0x80;

// Takes the face texture and releases it.
void ReleaseFace(CUnitClientSpecific* self) {
    CEngineObject* face = self->faceTexture;
    self->faceTexture = nullptr;
    if (face) face->Release();
}
} // namespace

int CUnitClientSpecific::CreateClient() {
    sources[0] = nullptr;
    sources[1] = nullptr;
    sources[2] = nullptr;
    lastTime = 0.0f;
    MotionPath()->Reset();
    lastAnimationTime = 0.0f;
    lastAnimation = nullptr;
    weaponHidden = 0;
    arrowHidden = 0;
    snapHeading = 1;
    fadeIn = 1.0f;
    idleSound = kNone;
    otherSound = kNone;
    speechSound = kNone;
    lastSpeech = nullptr;
    lastSpeechTime = 0;
    flashCountdown = 0;
    footprintRepeats = kNone;
    footprint = kNone;
    blinks = 0;
    blinkTimer = 0;
    eyesClosed = 0;
    for (std::uint32_t& id : armourIds) id = kNoModel;
    for (std::uint32_t& id : weaponIds) id = kNoModel;
    ReleaseFace(this);
    blinks = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00533FE0, CUnitClientSpecific::CreateClient);

void CUnitClientSpecific::DestroyClient() {
    for (CParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        reinterpret_cast<CEngineObject*>(source)->Release();
        source = nullptr;
    }
    ReleaseFace(this);
    blinks = 0;
}
EI_REPLACE_METHOD(0x005340C0, CUnitClientSpecific::DestroyClient);

void CUnitClientSpecific::DestructClientBody() {
    ei::SetVtable(this, kVtableAddress);
    for (CParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        reinterpret_cast<CEngineObject*>(source)->Release();
        source = nullptr;
    }
    ReleaseFace(this);   // 004EBE60 with null; the original's release test after it finds null
    blinks = 0;
    // The speech queue: every node, then the header (00539400), freed (005393B0; no destructors).
    for (ClientSpeechNode* node = speech->next; node != speech;) {
        ClientSpeechNode* next = node->next;
        stlp::deallocate(node, sizeof(ClientSpeechNode));
        node = next;
    }
    speech->next = speech;
    speech->prev = speech;
    stlp::deallocate(speech, sizeof(ClientSpeechNode));
    list58.vtable = ei::GamePointer<void>(kList58VtableAddress);
    list58.RemoveAll();
    ei::SetVtable(this, kBaseVtableAddress);
    unit = nullptr;
    DestructBase();
}
EI_REPLACE_METHOD(0x00533EA0, CUnitClientSpecific::DestructClientBody);

CUnitClientSpecific* CUnitClientSpecific::ConstructClient() {
    ClearFields();
    ei::SetVtable(this, kBaseVtableAddress);
    unit = nullptr;
    list58.vtable = ei::GamePointer<void>(kList58VtableAddress);
    list58.count = 0;
    list58.free = nullptr;
    list58.tail = nullptr;
    list58.head = nullptr;
    list58.blocks = nullptr;
    list58.blockSize = kList58BlockSize;
    auto* header = static_cast<ClientSpeechNode*>(stlp::allocate(sizeof(ClientSpeechNode)));
    speech = header;
    header->next = header;
    header->prev = header;
    faceTexture = nullptr;
    ei::SetVtable(this, kVtableAddress);
    fadeIn = 1.0f;
    return this;
}
EI_REPLACE_METHOD(0x00533AD0, CUnitClientSpecific::ConstructClient);
