// CAIMap::ApplyBridgeDecks: applies or clears the bridge decks of a footprint's bridge
// cells. Applying: each cell inside the map with low < high becomes kind 4; a grid cell not yet
// under a bridge has its ground level, its object height (flags & 0x3F) and its occluder kind
// (occluder word & 0x1F) saved in the bridge map (+0x10C, keyed (short)x << 16 | (short)y, the
// dword at the node's value) unless already there; then the occluder is added (a failure ends the
// call with false), the cell is marked under a bridge (0x40, object height and bit 0x80 cleared),
// its ground raised to the deck (`high`, u16 maximum) and the occluder word's low byte becomes
// (b & 0xE7) | 7. Clearing: each such cell becomes kind 4 and its occluder is removed; when that
// worked and no bridge deck is left on the grid cell, a cell that was under a bridge is restored
// from the bridge map, the 0x40 flag cleared and 0x80 set when the cell still has an
// object height; with a deck left, the ground becomes max(saved ground, highest deck). See
// ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/stlp.h"

#include <cstdint>

namespace {

// The bridge map's node: {next, key, value}; the original reads the low 16 bits of the value and
// stores a whole dword (ground, object height, occluder kind).
using BridgeNode = BridgeMap::Node;

std::uint32_t BridgeKey(int x, int y) {
    return static_cast<std::uint32_t>((static_cast<int>(static_cast<short>(x)) << 16) + static_cast<short>(y));
}



} // namespace

// 005B30B0 (thiscall on the bridge map, RET 0x10; x and y are read as shorts). A cell that has an
// entry gets back its ground level, its object height (flags & 0x3F) and the occluder word's low
// five bits; the entry is erased.
void BridgeHeights::RestoreCell(int x, int y, AIMapCell* cell, std::uint32_t* occluder) {
    BridgeNode* node = Find(BridgeKey(x, y));
    if (!node) return;
    cell->groundLevel = static_cast<std::uint16_t>(node->value);
    cell->flags = static_cast<std::uint8_t>((cell->flags & 0xC0) | ((node->value >> 16) & 0x3F));
    *occluder = (*occluder & 0xFFFFFFE0u) | ((node->value >> 24) & 0x1F);
    Erase(node);
}
EI_REWRITTEN(0x005B30B0, BridgeHeights::RestoreCell);

bool CAIMap::ApplyBridgeDecks(FootprintCellSet* set, bool remove) {
    if (remove) {
        for (int i = 0; i < set->cells.size; ++i) {
            FootprintCell& c = set->cells[i];
            int x = c.x, y = c.y;
            if (x < 0 || y < 0 || x >= cells.width || y >= cells.height || c.low >= c.high) continue;
            c.kind = 4;
            if (!ClearOccluderRecord(&c)) continue;
            AIMapCell* cell = &cells[y][x];
            int deck;
            if (!BridgeDeckAt(x, y, &deck)) {
                if (cell->flags & kCellBridge)
                    bridgeHeights.RestoreCell(x, y, &cells[y][x], &occluders[y][x]);
                auto w = static_cast<std::uint8_t>(cell->flags & 0xBF);
                cell->flags = static_cast<std::uint8_t>(((w & 0x3F) ? 0x80 : 0) | (w & 0x7F));
            } else {
                BridgeNode* node = bridgeHeights.Find(BridgeKey(x, y));
                int saved = node ? static_cast<std::uint16_t>(node->value) : 0;
                cell->groundLevel = static_cast<std::uint16_t>(saved > deck ? saved : deck);
            }
        }
        return true;
    }
    for (int i = 0; i < set->cells.size; ++i) {
        FootprintCell& c = set->cells[i];
        int x = c.x, y = c.y;
        if (x < 0 || y < 0 || x >= cells.width || y >= cells.height || c.low >= c.high) continue;
        c.kind = 4;
        AIMapCell* cell = &cells[y][x];
        if (!(cell->flags & kCellBridge)) {
            std::uint32_t* occluderRow = occluders[y];
            std::uint32_t key = BridgeKey(x, y);
            if (!bridgeHeights.Find(key)) {
                std::uint32_t value = cell->groundLevel | static_cast<std::uint32_t>(cell->flags & 0x3F) << 16 |
                                      static_cast<std::uint32_t>(occluderRow[x] & 0x1F) << 24;
                bridgeHeights[key] = value;
            }
        }
        if (!AddOccluderRecord(&c)) return false;
        cell->flags = static_cast<std::uint8_t>((cell->flags & 0x40) | 0x40);
        std::uint16_t deck = static_cast<std::uint16_t>(c.high);
        if (cell->groundLevel <= deck) cell->groundLevel = deck;
        std::uint32_t& occluder = occluders[y][x];
        occluder = (occluder & 0xFFFFFF00u) | ((occluder & 0xE7) | 7);
    }
    return true;
}

EI_REWRITTEN(0x005B6740, CAIMap::ApplyBridgeDecks);
