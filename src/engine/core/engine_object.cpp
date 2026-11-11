// EngineObject: the default slots of its vtable and its non-virtual members.
#include "engine/core/engine_object.h"

#include "engine/core/base_manager.h"
#include "engine/core/manager_context.h"

constexpr const char* kNoName = "noname";

void EngineObject::CopyFrom(const EngineObject& other) {
    flags = other.flags;
    type = other.type;
    prototypeId = other.prototypeId;
}

// The default slots: the destructor lets go of the owner; 1 is CopyFrom, 5 / 9 / 10 empty, 6 returns 0.
EngineObject::~EngineObject() { owner = nullptr; }
void EngineObject::ClearState() {}
bool EngineObject::RunTick() { return false; }
void EngineObject::Load(ByteStream*) {}
void EngineObject::Save(ByteStream*) {}

// When the last reference goes, the owning manager releases the object (its
// BaseManager::ReleaseObject). The result is `this`, or what the manager's call
// leaves there; no caller reads it.
void EngineObject::Release() {
    if (--refCount == 0) owner->ReleaseObject(this);
}

std::string_view EngineObject::GetName() { return kNoName; }

ManagerContext* EngineObject::GetManagerContext() const { return owner->context; }

BaseManager* EngineObject::SetOwner(BaseManager* newOwner) {
    BaseManager* previous = owner;
    owner = newOwner;
    return previous;
}

World* EngineObject::OwningWorld() const {
    auto* context = owner->context;
    return context ? context->ThisWorld() : nullptr;
}

WorldServer* EngineObject::ServerWorld() const {
    auto* context = owner->context;
    return context ? context->ThisServerWorld() : nullptr;
}

WorldClient* EngineObject::ClientWorld() const {
    auto* context = owner->context;
    return context ? context->ThisClientWorld() : nullptr;
}
