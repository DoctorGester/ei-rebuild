-- Res\spells\Healing.lua: the game's healing ("healing"). CastSpell casts it once at each target (per_target). The
-- unit it is cast at is healed by the cast's damage, with a glow on it; a sound and a light rising from the point (the
-- record's light range negated, as the game's) follow, unless an item's own periodic cast (cast.silent) finds the unit
-- at full health. The cast ends 32 ticks later, its light with it.
local END_TICK = 32         -- the cast ends (its light with it) at this tick

local spell = {}

function spell.OnCast(cast)
    local target = cast.target
    if not cast:IsUnit(target) then return end      -- only a unit is healed: the cast ends
    cast:Heal(target, cast.damage)
    cast:Effect("Healing", { attach = target, size = cast.power, free = true })
    if not cast.silent or cast:IsHurt(target) then
        cast:Sound("magic\\Healing\\start.wav", target)
        cast:Light(cast.point, { range = -cast.lightRange, rise = cast.lightRise })
    end
    cast:After(END_TICK, "Done")
end

function spell.Done(cast)
    cast:Finish()
end

-- A cast from a save made before healing moved onto this file, its light rising with `old.ticks` left on the game's
-- count, which ended the cast at the second update after it ran out.
function spell.OnResume(cast, old)
    cast:After(old.ticks + 2, "Done")
end

return spell
