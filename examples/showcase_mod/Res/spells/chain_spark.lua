-- The showcase mod's spell "spark", Chain Spark (Res\database\spells.sdb.toml): this file runs each cast of it, on the
-- server (docs/modding.md, "Spells").
-- A spark flies at the target and strikes it. An arc then reaches from the struck unit to the nearest enemy within the
-- spell's radius that it has not struck yet, and the spark leaps along it a moment later. Each strike deals FALLOFF of
-- the one before (12, 7.2 and 4.32 at the record's damage) and leaves its victim shocked for a while: the spell's own
-- effect, which slows it (OnStats below) and crackles with sparks (the record's `look`). The chain stops after
-- cast.targetCount strikes (3; each 't' rune adds one) or when no enemy is left within reach; the cast then ends once
-- the last strike's sparks have gone.
-- The last chain's strikes go into the game variables chain_spark.strikes (how many) and chain_spark.strike1, 2, ...
-- (each one's damage), for a mission script to read.

local FALLOFF = 0.6     -- each strike deals this share of the one before
local LEAP_TICKS = 10   -- the arc stands this long before the spark leaps along it
local SHOCK_TICKS = 40  -- a strike shocks its victim this long
local SPARKS_TICKS = 30 -- a strike's sparks (ChainSparkHit) last this long

local spell = {}

-- The spark from `from` (a unit; nil: the caster) at `victim`, carrying its strike's damage.
local function Launch(cast, from, victim, damage)
    cast:Missile { from = from, to = victim, effect = "ChainSpark", payload = { damage = damage } }
end

function spell.OnCast(cast)
    if not cast.target then return cast:Finish() end
    cast.data.struck = { [GetObjectID(cast.target)] = true }
    cast.data.strikes = 0
    Launch(cast, nil, cast.target, cast.damage)
end

function spell.OnHit(cast, victim, payload, point)
    if not victim then return end   -- the unit is gone: the chain ends here
    cast:Damage(victim, payload.damage, "lightning")
    cast:Apply(victim, "own", { ticks = SHOCK_TICKS, value = 1 })
    cast:Effect("ChainSparkHit", { attach = victim, size = cast.power, lifetime = SPARKS_TICKS })
    cast:Sound("magic\\lightning\\start.wav", victim)
    local strikes = cast.data.strikes + 1
    cast.data.strikes = strikes
    SetGameVar(0, "chain_spark.strikes", strikes)
    SetGameVar(0, "chain_spark.strike" .. strikes, payload.damage)
    if strikes >= cast.targetCount then return end

    local next = cast:FindUnits(victim, cast.radius, {
        enemies = true, alive = true, sight = true, affect = true, exclude = cast.data.struck,
    })[1]
    if not next then return end
    cast.data.struck[GetObjectID(next)] = true
    cast:Bolt(victim, next, { width = 2 * cast.power, lifetime = LEAP_TICKS + 4 })
    cast:After(LEAP_TICKS, "Leap", { from = victim, to = next, damage = payload.damage * FALLOFF })
end

-- The arc's timer: the spark leaps on to the next victim.
function spell.Leap(cast, leap)
    if not leap.to then return end   -- the next victim is gone
    Launch(cast, leap.from, leap.to, leap.damage)
end

-- The shock on a struck unit (cast:Apply "own"; docs/modding.md, "Active effects"): its action rate down by the
-- effect's value, as the game's Slow lowers it.
function spell.OnStats(effect, stats)
    stats:Subtract("Haste", effect.value)
end

return spell
