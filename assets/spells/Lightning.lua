-- Res\spells\Lightning.lua: the game's lightning arc ("lightning") and curse magic ("curse_magic", the same bolt in a
-- demon's look). CastSpell casts it once at each target (per_target). A bolt from the caster (or where the cast
-- starts) to the target strikes it at once, with a light at the target's eye; when the bolt goes the light rises away
-- and lingers until the cast ends.
local BOLT_TICKS = 8        -- the bolt lasts this long
local END_TICK = 89         -- the cast ends (its light with it) this many ticks after the strike

local spell = {}

function spell.OnCast(cast)
    local target = cast.target
    if target then
        cast:Bolt(cast.caster or cast.from, target, { lifetime = BOLT_TICKS, demon = cast.code == "curse_magic" })
    end
    cast:Sound("magic\\lightning\\start.wav", cast.from)
    if target then
        cast.data.light = cast:Light(cast:Eye(target))
        cast:Damage(target, cast.damage, "lightning")
    end
    cast:After(BOLT_TICKS, "Fade")
    cast:After(END_TICK, "Done")
end

function spell.Fade(cast)
    if cast.data.light then cast:SetVelocity(cast.data.light, { x = 0, y = 0, z = cast.lightRise }) end
end

function spell.Done(cast)
    cast:Finish()
end

return spell
