-- Res\spells\Missile.lua: the game's spell missiles, the fire arrow ("arrow"), Rick magic ("rick_magic", the fire arrow
-- with a smoke trail) and the acid ray ("acid_ray"). CastSpell casts it once at each target (per_target). A missile
-- flies from the caster at a unit; where it lands the unit takes the damage, a splash flashes on it, an end sound plays
-- and a light rises away until the cast ends. A missile still flying at its limit ends the cast.
local LIMIT_TICKS = 152     -- a missile that has not landed this many ticks after the cast ends it
local HIT_TICKS = 36        -- the cast ends (its light with it) this many ticks after the hit
-- The game's scales as floats, exactly (a product of two floats in a Lua number rounds back to the float's product).
local ACID_SIZE = 0.4000000059604645    -- the acid ray's missile size per power (0.4 as a float)
local SPLASH_SIZE = 0.699999988079071   -- the splash's size per power (0.7 as a float)
local IMPACT_LIGHT = 1.5                -- the hit's light rises this much faster and reaches this much further

-- By the spell's code: the missile's effect and size per power (none: the power), the sounds' folder, the splash and
-- the damage type.
local KINDS = {
    arrow = { trail = "FireArrow", sounds = "magic\\Firearrow\\", splash = "FireStar", damage = "fire" },
    rick_magic = { trail = "RikArrowSmoke", sounds = "magic\\Firearrow\\", splash = "FireStar", damage = "fire" },
    acid_ray = { trail = "AcidRay", size = ACID_SIZE, sounds = "magic\\Acidray\\", splash = "AcidStar", damage = "acid" },
}

local function KindOf(cast)
    return KINDS[cast.code] or KINDS.arrow
end

local spell = {}

function spell.OnCast(cast)
    if not cast:IsUnit(cast.target) then return end     -- a missile flies at a unit: the cast ends
    local kind = KindOf(cast)
    cast:Missile { to = cast.target, effect = kind.trail, size = kind.size and cast.power * kind.size,
                   sound = kind.sounds .. "circle.wav" }
    cast:Sound(kind.sounds .. "start.wav", cast.from)
    cast:After(LIMIT_TICKS, "Expire")
end

function spell.OnHit(cast, target)
    cast.data.hit = true
    if not target then
        cast:Finish()
        return
    end
    local kind = KindOf(cast)
    cast:Damage(target, cast.damage, kind.damage)
    local at = cast:HitPoint(target)
    cast:Effect(kind.splash, { attach = target, size = cast.power * SPLASH_SIZE, free = true })
    cast:Sound(kind.sounds .. "end.wav", at)
    cast:Light(at, { range = cast.lightRange * IMPACT_LIGHT, rise = cast.lightRise * IMPACT_LIGHT })
    cast:After(HIT_TICKS, "Done")
end

function spell.Expire(cast)
    if not cast.data.hit then cast:Finish() end
end

function spell.Done(cast)
    cast:Finish()
end

-- A cast from a save made before these spells moved onto this file: its missile still flying or its light rising, with
-- `old.ticks` left on the game's count, which ended the cast at the second update after it ran out.
function spell.OnResume(cast, old)
    if old.state == "missile" then
        cast:After(old.ticks + 2, "Expire")
    else
        cast.data.hit = true
        cast:After(old.ticks + 2, "Done")
    end
end

return spell
