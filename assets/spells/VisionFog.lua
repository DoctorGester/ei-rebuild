-- Res\spells\VisionFog.lua: the game's vision fog ("vision_fog"). CastSpell casts it once at each target (per_target).
-- The caster's player sees what the unit it is cast at sees (in the fog of war, with the caster's senses), with a sound
-- at the unit, for the cast's duration or until the unit dies. A cast without a caster or a target does nothing.
local LINGER = 2            -- the cast ends this many ticks after its duration

local spell = {}

function spell.OnCast(cast)
    if not cast.caster or not cast.target then return cast:Finish() end
    cast:Vision(cast.target, cast.caster)
    cast:Sound("magic\\VisionFog\\start.wav", cast:Eye(cast.target))
    cast.data.done = cast.duration + LINGER
end

function spell.OnTick(cast)
    local target = cast.target
    if cast.tick >= cast.data.done or not target or cast:IsDead(target) then cast:Finish() end
end

-- A cast from a save made before vision fog moved onto this file, with `old.ticks` left on the game's count, which
-- ended the cast at the second update after it ran out.
function spell.OnResume(cast, old)
    cast.data.done = old.ticks + LINGER
end

return spell
