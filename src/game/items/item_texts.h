#pragma once

// An item's texts from the text resources (the text manager, engine/data/text_manager.h): its name, description and
// kind. Item keys are the record's name, spaces replaced by '_' and lower-cased, after a prefix that depends on the
// item kind ("weapon sword iron", "qitem ...", "spell ...").

#include <string>

class Item;
class TextManager;

// An item's texts; a part that is not found stays empty.
struct ItemTexts {
    std::string name;
    std::string description;
    std::string kind;          // "item_weapon", ... looked up as a string (TextManager::GetString)
};

// The name, description and kind of `item`, each looked up in `texts`.
ItemTexts GetItemTexts(TextManager* texts, Item* item);

// The item's display name: "format_item1" (or "format_item2" with the stack count when count is above 1) applied to
// the kind and the name.
std::string FormatItemName(TextManager* texts, Item* item);
