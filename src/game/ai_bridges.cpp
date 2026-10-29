// CAIMap::ApplyBridgeDecks: applies or clears the bridge decks of a footprint's bridge cells. Applying: each
// cell inside the map with low < high becomes a BridgeDeck; a grid cell not yet under a bridge has its ground
// level, its water depth and its terrain type saved in the bridge map (keyed (short)x << 16 | (short)y) unless
// already there; then the occluder is added (a failure ends the call with false), the cell's flags become
// kCellBridge alone (no water), its ground is raised to the deck (`high`, u16 maximum) and its terrain type
// becomes occluder::kBridgeType. Clearing: each such cell becomes a BridgeDeck and its occluder is removed;
// when that worked and no bridge deck is left on the grid cell, a cell that was under a bridge is restored from
// the bridge map, kCellBridge cleared and kCellWater set when the cell has a water depth; with a deck left, the
// ground becomes max(saved ground, highest deck). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include <cstdint>

namespace {

std::uint32_t BridgeKey(int x, int y) {
    return static_cast<std::uint32_t>((static_cast<int>(static_cast<short>(x)) << 16) + static_cast<short>(y));
}

// A bridge map entry's value: the ground level in the low word, the water depth in bits 16-21, the terrain type
// in bits 24-28.
std::uint32_t SavedCell(const AIMapCell& cell, std::uint32_t occluderWord) {
    return cell.groundLevel | static_cast<std::uint32_t>(cell.WaterDepth()) << 16 | occluder::Type(occluderWord) << 24;
}
int SavedDepth(std::uint32_t saved) { return static_cast<int>((saved >> 16) & kCellDepthMask); }
std::uint32_t SavedType(std::uint32_t saved) { return occluder::Type(saved >> 24); }

} // namespace

// A cell that has an entry gets back its ground level, its water depth and its terrain type; the entry is erased.
// x and y are read as shorts.
void BridgeHeights::RestoreCell(int x, int y, AIMapCell* cell, std::uint32_t* occluder) {
    auto node = find(BridgeKey(x, y));
    if (node == end()) return;
    std::uint32_t saved = node->second;
    cell->groundLevel = static_cast<std::uint16_t>(saved);
    cell->flags = static_cast<std::uint8_t>((cell->flags & ~kCellDepthMask) | SavedDepth(saved));
    *occluder = occluder::WithType(*occluder, SavedType(saved));
    erase(node);
}

bool CAIMap::ApplyBridgeDecks(FootprintCellSet* set, bool remove) {
    if (remove) {
        for (int i = 0; i < std::ssize(set->cells); ++i) {
            FootprintCell& c = set->cells[i];
            int x = c.x, y = c.y;
            if (x < 0 || y < 0 || x >= cells.width || y >= cells.height || c.low >= c.high) continue;
            c.kind = OccluderKind::BridgeDeck;
            if (!ClearOccluderRecord(&c)) continue;
            AIMapCell* cell = &cells[y][x];
            int deck;
            if (!BridgeDeckAt(x, y, &deck)) {
                if (cell->flags & kCellBridge)
                    bridgeHeights.RestoreCell(x, y, &cells[y][x], &occluders[y][x]);
                const int depth = cell->WaterDepth();
                cell->flags = static_cast<std::uint8_t>(depth | (depth != 0 ? kCellWater : 0));
            } else {
                int saved = bridgeHeights.HeightAt(static_cast<short>(x), static_cast<short>(y));
                cell->groundLevel = static_cast<std::uint16_t>(saved > deck ? saved : deck);
            }
        }
        return true;
    }
    for (int i = 0; i < std::ssize(set->cells); ++i) {
        FootprintCell& c = set->cells[i];
        int x = c.x, y = c.y;
        if (x < 0 || y < 0 || x >= cells.width || y >= cells.height || c.low >= c.high) continue;
        c.kind = OccluderKind::BridgeDeck;
        AIMapCell* cell = &cells[y][x];
        if (!(cell->flags & kCellBridge)) {
            std::uint32_t* occluderRow = occluders[y];
            std::uint32_t key = BridgeKey(x, y);
            if (!bridgeHeights.contains(key)) {
                bridgeHeights[key] = SavedCell(*cell, occluderRow[x]);
            }
        }
        if (!AddOccluderRecord(&c)) return false;
        cell->flags = kCellBridge;
        std::uint16_t deck = static_cast<std::uint16_t>(c.high);
        if (cell->groundLevel <= deck) cell->groundLevel = deck;
        std::uint32_t& occluder = occluders[y][x];
        occluder = occluder::WithType(occluder, occluder::kBridgeType);
    }
    return true;
}
