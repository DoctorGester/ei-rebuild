#pragma once

// An embedded CParty (party.h) whose owner constructs and destroys it in its own Construct and
// Destruct bodies (CPlayer, the party records, the class templates, the client's trade parties, the
// game flow's request): a union member, so the compiler runs neither. Get() is the party.

#include "party.h"

#include "lib/static_object.h"

