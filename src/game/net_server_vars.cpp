// The game server's script variables: named floats the script reads and writes in a
// network game, replicated to the clients as VariableMessages. See net_messages.h and docs/notes/network.md.

#include "net_messages.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

namespace net {

// The search the original inlines in each function: the first variable whose name has the same bytes as
// `name`.
std::vector<SharedRef<VariableMessage>>::iterator ServerSession::FindVariable(const std::string* variableName) {
    auto node = variables.begin();
    for (; node != variables.end(); ++node)
        if (node->Get()->name == *variableName) break;
    return node;
}

std::vector<SharedRef<VariableMessage>>::iterator ServerSession::FindOrAddVariable(const std::string* variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) return node;

    auto first = variables.emplace(variables.begin(), NewCounted<VariableMessage>());

    // The name, assigned in place (string::operator= inlined: overwrite, then cut or append).
    std::string* to = &first->Get()->name;
    if (variableName != to) *to = *variableName;
    return first;
}

// Sets the variable; a zero value removes it instead. A set bumps the version and takes the next serial.
void ServerSession::SetVariable(const std::string* newName, float value) {
    EI_NAN_AUDIT(value);
    if (value != 0.0f) {
        VariableMessage* variable = FindOrAddVariable(newName)->Get();
        variable->value = value;
        variable->serial = g_variableSerial;
        ++g_variableSerial;
        ++variable->version;
        return;
    }
    auto found = FindVariable(newName);
    if (found != variables.end()) variables.erase(found);
}

// Raises the variable to `value` (the larger of the two; `value` when either is NaN),
// made when missing; a result of zero (or NaN) removes it.
void ServerSession::RaiseVariable(const std::string* variableName, float value) {
    auto found = FindOrAddVariable(variableName);
    VariableMessage* variable = found->Get();
    float current = variable->value;
    std::uint32_t currentBits, valueBits;
    currentBits = std::bit_cast<std::uint32_t>(current);
    valueBits = std::bit_cast<std::uint32_t>(value);
    std::uint32_t bits = current >= value ? currentBits : valueBits;
    float selected;
    selected = std::bit_cast<float>(bits);
    volatile double loaded = selected;   // reloaded through memory, as the original does
    found->Get()->value = static_cast<float>(loaded);
    found->Get()->serial = g_variableSerial;
    ++g_variableSerial;
    ++found->Get()->version;
    EI_NAN_AUDIT(found->Get()->value);
    if (found->Get()->value == 0.0f) {
        auto node = FindVariable(variableName);
        if (node != variables.end()) variables.erase(node);
    }
}

// Removes the variable when it exists.
void ServerSession::RemoveVariable(const std::string* variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) variables.erase(node);
}

// The variable's value, 0 when missing (returned unrounded).
double ServerSession::GetVariable(const std::string* variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) return node->Get()->value;
    return 0.0f;
}

} // namespace net
