-- The showcase mod's spell "ember", Ember Brand (Res\database\spells.sdb.toml): a curse that brands its target for the
-- spell's duration (docs/modding.md, "Active effects"). While the brand lasts, the victim's strength is lower by the
-- spell's damage (OnStats: a stat none of the game's buffs touches) and every BURN_TICKS ticks it burns for BURN fire
-- damage (OnEffectTick). OnEffectEnded says on the console how the brand went.
-- The burns go into the game variables ember_brand.burns (how many) and ember_brand.lost (the hit points they took),
-- the end's reason into ember_brand.ended (1 expired, 2 removed, 3 renewed, 4 died), for a mission script to read.
-- Nothing of the brand is kept in the script: the ticks it has left (effect.ticks) pace the burns, so a save and load
-- goes on as before.

local BURN = 2          -- each burn's fire damage
local BURN_TICKS = 20   -- a burn every this many ticks left
local REASONS = { expired = 1, removed = 2, renewed = 3, died = 4 }

local spell = {}

function spell.OnCast(cast)
    if cast.target then cast:Apply(cast.target, "own") end
    cast:Finish()
end

function spell.OnStats(effect, stats)
    stats:Subtract("Strength", effect.value)
end

function spell.OnEffectApplied(effect, unit)
    SetGameVar(0, "ember_brand.burns", 0)
    SetGameVar(0, "ember_brand.lost", 0)
    print(string.format("Showcase mod: the ember brand on %d for %d ticks", GetObjectID(unit), effect.ticks))
end

function spell.OnEffectTick(effect, unit)
    if effect.ticks % BURN_TICKS ~= 0 then return end
    local lost = effect:Damage(BURN, "fire")
    SetGameVar(0, "ember_brand.burns", GetGameVar(0, "ember_brand.burns") + 1)
    SetGameVar(0, "ember_brand.lost", GetGameVar(0, "ember_brand.lost") + lost)
end

function spell.OnEffectEnded(effect, unit, reason)
    SetGameVar(0, "ember_brand.ended", REASONS[reason])
    print(string.format("Showcase mod: the ember brand on %d %s after %d burns", GetObjectID(unit), reason,
                        GetGameVar(0, "ember_brand.burns")))
end

return spell
