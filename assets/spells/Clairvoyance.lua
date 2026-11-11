-- Res\spells\Clairvoyance.lua: the game's clairvoyance ("clairvoyence"). CastSpell casts it once at each target point
-- (per_target). A ring of earth rises at the point with a sound and a light of the cast's radius; every tick for the
-- cast's duration the caster's player sees the units in the radius, and the units around wake. Ten ticks before the
-- end the ring sinks with a closing sound. The cast ends with its duration, or when the caster is gone.
local ABOVE = 1             -- the light and the sounds stand this far above the ground
local WAKE = 5              -- the units this far past the radius wake
local CLOSE = 10            -- the ring sinks when the count has this many ticks left

local spell = {}

function spell.OnCast(cast)
    local data = cast.data
    local at = cast:Ground(cast.point, { above = ABOVE })
    data.at = at
    data.count = cast.duration
    cast:Effect("ClayRing", { at = cast.point, size = cast.radius, free = true })
    data.light = cast:Light(at, { range = cast.radius })
    cast:Sound("magic\\Clairvoyence\\start.wav", at)
end

function spell.OnTick(cast)
    local data, caster = cast.data, cast.caster
    if caster then
        for _, unit in ipairs(cast:FindUnits(data.at, cast.radius, { unsorted = true })) do cast:Reveal(unit) end
        cast:Wake(data.at, cast.radius + WAKE)
    end
    local left = data.count - cast.tick
    if left < 0 or not caster then cast:Finish() end
    if left == CLOSE then
        cast:Effect("ClayRing", { at = cast.point, size = -cast.radius, free = true })
        cast:Sound("magic\\Clairvoyence\\end.wav", data.at)
    end
end

-- A cast from a save made before clairvoyance moved onto this file, with `old.ticks` left on the game's count; it
-- stood where the game kept it (`cast.from`).
function spell.OnResume(cast, old)
    cast.data.at = cast.from
    cast.data.count = old.ticks
end

return spell
