-- Res\spells\Strike.lua: the game's area strikes at a point, the light strike ("inv_lit") and the acid column
-- ("acid_column"). CastSpell casts it once at each target (per_target). Each hits every unit in the radius that area
-- spells hit once (the damage shared by the duration's targets), with a flash, a start sound and a light rising away
-- until the cast ends: the light strike at once, from two bolts out of the sky into the ground; the acid column halfway
-- through its rise.
local DEPTH = -3            -- the light strike's point: this far below the water's surface or the ground
local BOLT_HEIGHT = 30      -- its bolts fall from this far above the point
local BOLT_WIDTH = -7       -- their width (per power for the first)
local LIGHT_ABOVE = 5       -- its light this far above the point
local ECHO_TICK = 3         -- the second bolt strikes at this tick, for one tick
local STRIKE_END = 9        -- the light strike ends (its light with it) at this tick
local COLUMN_HIT = 15       -- the acid column hits at this tick
local COLUMN_END = 32       -- and ends at this one

local spell = {}

local function HitArea(cast, at, damageType, area)
    for _, unit in ipairs(cast:FindUnits(at, cast.radius, { area = true, unsorted = true })) do
        cast:Damage(unit, cast.damage, damageType, { area = area, split = cast.duration })
    end
end

local function Bolt(cast, at, width)
    cast:Bolt({ x = at.x, y = at.y, z = at.z + BOLT_HEIGHT }, at, { width = width, lifetime = 1 })
end

local function LightStrike(cast)
    local at = cast:Ground(cast.point, { water = true, above = DEPTH })
    cast.data.at = at
    Bolt(cast, at, cast.power * BOLT_WIDTH)
    cast:Effect("LightningBlast", { at = at, size = cast.radius, alpha = cast.power, free = true })
    cast:Sound("magic\\InvokeLight\\start.wav", at, 1000, 1000)
    cast:Light({ x = at.x, y = at.y, z = at.z + LIGHT_ABOVE }, { rise = cast.lightRise })
    HitArea(cast, at, "lightning", false)
    cast:After(ECHO_TICK, "Echo")
    cast:After(STRIKE_END, "Done")
end

local function AcidColumn(cast)
    cast:Effect("Nuke", { at = cast.point, free = true })
    cast:Sound("magic\\Acidcolumn\\start.wav", cast.point)
    cast:Light(cast.point, { rise = cast.lightRise })
    cast:After(COLUMN_HIT, "Column")
    cast:After(COLUMN_END, "Done")
end

function spell.OnCast(cast)
    if cast.code == "acid_column" then AcidColumn(cast) else LightStrike(cast) end
end

function spell.Echo(cast)
    Bolt(cast, cast.data.at, BOLT_WIDTH)
end

function spell.Column(cast)
    HitArea(cast, cast.point, "acid", true)
end

function spell.Drop(cast, handle)
    cast:Release(handle)
end

function spell.Done(cast)
    cast:Finish()
end

-- A cast from a save made before these spells moved onto this file, with `old.ticks` left on the game's count (the
-- light strike's bolt dropped at the next update, its second bolt at the count's 5; the column's hit at its 15; each
-- ended at the second update after the count ran out).
function spell.OnResume(cast, old)
    if cast.code == "acid_column" then
        if old.ticks > COLUMN_HIT then cast:After(old.ticks - COLUMN_HIT, "Column") end
    else
        cast.data.at = cast.from
        if old.visual then cast:After(1, "Drop", old.visual) end
        if old.ticks >= 5 then cast:After(old.ticks - 4, "Echo") end
    end
    cast:After(old.ticks + 2, "Done")
end

return spell
