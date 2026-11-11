-- Res\spells\Campfire.lua: the game's campfire ("campfire"), cast with no caster by each flame a zone places on the
-- ground (the torch shows the fire; the cast has no look of its own). It stands at its point for ever: its count
-- starts at the duration and goes up a tick at a time, and at each count divisible by 4 it burns every unit within its
-- radius that area spells hit (the damage shared by the duration's targets).
local PULSE = 4         -- it burns every this many ticks
local ABOVE = 1         -- it stands this far above the ground
local BURNS = { area = true, unsorted = true }  -- whom it burns: those area spells hit, in the map's order

local spell = {}

-- The pulses of a cast whose next tick has `count` on the game's count: the first at the next count divisible by 4.
local function Schedule(cast, count)
    cast:After(-count % PULSE + 1, "Pulse")
end

function spell.OnCast(cast)
    cast.data.at = cast:Ground(cast.point, { above = ABOVE })
    Schedule(cast, cast.duration)
end

function spell.Pulse(cast)
    local units = cast:FindUnits(cast.data.at, cast.radius, BURNS)
    if units[1] then
        local hit = { area = true, split = cast.duration }
        for _, unit in ipairs(units) do
            cast:Damage(unit, cast.damage, "fire", hit)
        end
    end
    cast:After(PULSE, "Pulse")
end

-- A cast from a save made before the campfire moved onto this file, `old.ticks` on the game's count; its point where
-- the game kept it (`cast.from`).
function spell.OnResume(cast, old)
    cast.data.at = cast.from
    Schedule(cast, old.ticks)
end

return spell
