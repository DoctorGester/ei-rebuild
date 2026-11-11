-- Res\spells\Fireball.lua: the game's fireball ("fireball"). CastSpell casts it once at each target (per_target). A
-- ball of fire flies along the ground from the caster's hand to the target point, with its light and its sound; where
-- it lands it bursts: every unit in the radius that area spells hit takes the damage (shared by the duration's
-- targets), a blast flashes, an end sound plays, the caster's AI noise goes out and a light rises away until the cast
-- ends.
local BLAST_TICKS = 37      -- the cast ends (its light with it) this many ticks after the blast
-- The game's scales as floats, exactly (a product of two floats in a Lua number rounds back to the float's product).
local BALL_SIZE = 0.30000001192092896   -- the ball's size per power (0.3 as a float)
local NEAR = 1.25                       -- a target within the speed x this: one start sound, not the flight's loop
local IMPACT_LIGHT = 1.5                -- the blast's light rises this much faster and reaches this much further
local NOISE = 5                         -- the blast's loudness, heard by the AI
local NOISE_DELAY = 4                   -- its call delay

-- A number rounded to a float, as the game compares its products.
local function Float(value)
    return (string.unpack("f", string.pack("f", value)))
end

local spell = {}

function spell.OnCast(cast)
    local from = cast.caster and cast:Eye(cast.caster) or cast.from
    local flight = cast:Flight(cast.from, cast.point)
    local at = cast:Ground(cast.point, { water = true, above = 1 })
    cast.data.at = at
    cast.data.ball = cast:Effect("FireBall", { at = from, size = cast.power * BALL_SIZE, velocity = flight.velocity,
                                               unit = cast.caster, target = at })
    if Float(flight.speed * NEAR) > flight.distance then
        cast:Sound("magic\\FireBall\\start.wav", cast.from, { velocity = flight.velocity })
    else
        cast.data.sound = cast:SoundLoop(cast.from, "magic\\Fireball\\circle.wav",
                                         { first = "magic\\Fireball\\start.wav", velocity = flight.velocity })
    end
    cast.data.light = cast:Light(from, { velocity = flight.velocity })
    -- The game counted the flight's ticks less 2 down: the ball went at the count's end, the blast came a tick later.
    local count = flight.ticks - 2
    if count >= 0 then cast:After(count + 1, "Out") end
    cast:After(count + 2, "Blast")
end

function spell.Out(cast)
    cast:Release(cast.data.ball)
end

function spell.Blast(cast)
    local at = cast.data.at
    for _, unit in ipairs(cast:FindUnits(at, cast.radius, { area = true, unsorted = true })) do
        cast:Damage(unit, cast.damage, "fire", { split = cast.duration })
    end
    cast:Release(cast.data.ball)
    cast:Release(cast.data.sound)
    cast:Release(cast.data.light)
    local size = cast.areaPower >= cast.power and cast.areaPower or cast.power
    cast:Effect("FireBlast", { at = at, size = size, alpha = cast.power, param = cast.radius, free = true })
    cast:Sound("magic\\fireball\\end.wav", at)
    cast:Light(at, { range = cast.lightRange * IMPACT_LIGHT, rise = cast.lightRise * IMPACT_LIGHT })
    cast:Noise(at, NOISE, NOISE_DELAY)
    cast:After(BLAST_TICKS, "Done")
end

function spell.Done(cast)
    cast:Finish()
end

-- A cast from a save made before the fireball moved onto this file: its ball still flying, or its blast's light
-- rising, with `old.ticks` left on the game's count (the ball went when it ran out, the blast and the end came at the
-- update after).
function spell.OnResume(cast, old)
    cast.data.at = cast.point
    if old.state == "missile" then
        cast.data.ball, cast.data.sound, cast.data.light = old.visual, old.sound, old.light
        if old.ticks >= 0 then cast:After(old.ticks + 1, "Out") end
        cast:After(old.ticks + 2, "Blast")
    else
        cast:After(old.ticks + 2, "Done")
    end
end

return spell
