// Destructors of the battle HUD's panels,
// with the deleting destructors and the input sides' copies. See ui.h
// and docs/notes/ui.md.
#include "ui.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "game_data.h"

CHudWeaponBar::~CHudWeaponBar() = default;

CHudQuickItems::~CHudQuickItems() = default;

CHudSpellColumn::~CHudSpellColumn() = default;

CHudActionColumn::~CHudActionColumn() = default;

CHudMap::~CHudMap() = default;

CHudUnitPanel::~CHudUnitPanel() = default;

// Only the two page texts (page 1 first) and the window's.
CHudTopPanel::~CHudTopPanel() {}
