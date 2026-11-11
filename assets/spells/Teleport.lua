-- Res\spells\Teleport.lua: the game's teleport ("teleport"). CastSpell casts it once at each target point
-- (per_target). Sparks rise where the caster stands and gather at the point, with a sound and a light there; 30 ticks
-- later the caster goes to the point (at its own height) if it can stand there, else it says it cannot. Arrived, it
-- stands stunned for the cast's duration (the record's effect, "teleport") while the light rises away, and the cast ends
-- 2 ticks after that (at least 1 tick of stun); at once when the caster is gone or dead.
local ARRIVE = 30           -- the caster arrives this many ticks after the cast
local LINGER = 2            -- the cast ends this many ticks after its stun
-- The game's arrival sparks stand this far below the ground (0.2 as a float, exactly).
local DEPTH = 0.20000000298023224
local POWER = 16            -- the arrival's effect's strength (the game's effect record's)

local spell = {}

-- The ticks the caster stands stunned after it arrived: the duration, at least 1.
local function Stun(cast)
    return cast.duration > 1 and cast.duration or 1
end

-- The arrival and the end of a cast whose next tick has `left` on the game's count (the stun + 30 at the cast).
local function Schedule(cast, left)
    cast.data.arrive = left - Stun(cast)
    cast.data.done = left + LINGER
end

function spell.OnCast(cast)
    local data = cast.data
    local from = cast.caster and cast:Position(cast.caster) or cast.from
    data.visual = cast:Effect("Teleport", { at = cast:Ground(from, { above = from.z }) })
    local at = cast:Ground(cast.point, { above = -DEPTH })
    cast:Effect("Teleport", { at = at, size = -1, free = true })
    cast:Sound("magic\\Teleport\\start.wav", at)
    data.light = cast:Light(at)
    Schedule(cast, Stun(cast) + ARRIVE)
end

local function Arrive(cast, caster)
    local data = cast.data
    if not cast:Move(caster, cast.point) then
        cast:Shout(caster, "cant_teleport")
        return
    end
    cast:Release(data.visual)
    cast:Apply(caster, "own", { power = POWER })
    cast:SetVelocity(data.light, { x = 0, y = 0, z = cast.lightRise })
end

function spell.OnTick(cast)
    local caster = cast.caster
    if cast.tick >= cast.data.done or not caster or cast:IsDead(caster) then
        cast:Finish()
    elseif cast.tick == cast.data.arrive then
        Arrive(cast, caster)
    end
end

-- A cast from a save made before the teleport moved onto this file, with `old.ticks` left on the game's count (the
-- caster arrived when it came down to the duration, the cast ended at the second update after it ran out).
function spell.OnResume(cast, old)
    cast.data.visual, cast.data.light = old.visual, old.light
    Schedule(cast, old.ticks)
end

-- The arrival's effect (the record's `effect`, "teleport") stuns the caster while it lasts.
function spell.OnStats(effect, stats)
    stats:Add("Stunned", 1)
end

return spell
