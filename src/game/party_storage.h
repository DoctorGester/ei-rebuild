#pragma once

// An embedded Party (party.h) whose owner constructs and destroys it in its own Construct and
// Destruct bodies (Player, the party records, the client's trade parties, the
// game flow's request): a union member, so the compiler runs neither. Get() is the party.

#include "party.h"

