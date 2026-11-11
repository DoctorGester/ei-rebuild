-- Res\spells\LastingArea.lua: the game's lasting areas, the fire wall ("firewall"), the light wall ("litnwall") and the
-- poison cloud ("acid_fog"). CastSpell casts it once at each target point (per_target). Each stands at its point for
-- its duration: a wall across the line from the caster to the point (its radius each way), the cloud a disc of its
-- radius, with its light and its sounds, the AI map's cells under it blocked. Every 4th tick it hits every unit in it
-- that area spells hit (the damage shared by the duration's targets; only the first pulse is the caster's). Then it
-- goes out and its light rises away for 80 ticks.
local PULSE = 4         -- it hits every this many ticks
local LINGER = 80       -- its light rises away this long after it goes out
local ABOVE = 1         -- its light and sounds stand this far above the ground (the light wall's ends too)

local DAMAGE = { firewall = "fire", litnwall = "lightning", acid_fog = "acid" }

local spell = {}

-- The pulses and the end of a cast whose next tick has `left` on the game's count: a pulse at each count divisible by
-- 4 down to 0, out at -1, the end LINGER ticks after that.
local function Schedule(cast, left)
    cast.data.out = left + 2
    if left >= 0 then cast:After(left % PULSE + 1, "Pulse") end
    if left >= -1 then cast:After(left + 2, "Out") end
    cast:After(left + 2 + LINGER, "Done")
end

local function FireWall(cast, from)
    local data = cast.data
    local span = cast:Across(from, cast.point, cast.radius)
    local above = cast:Ground(cast.point, { above = ABOVE })
    data.at, data.along = cast.point, span
    data.visual = cast:Effect("Tendril", { at = cast.point, size = cast.power, alpha = cast.power, direction = span })
    data.light = cast:Light(above, { range = cast.radius + cast.lightRange })
    data.sound = cast:SoundLoop(above, "magic\\Firewall\\circle.wav", { first = "magic\\Firewall\\start.wav" })
    cast:Block(cast.point, { along = span })
end

-- The light wall: a bolt between the span's two ends, each on the ground or the water's surface.
local function LightWall(cast, from)
    local data, point = cast.data, cast.point
    local span = cast:Across(from, point, cast.radius)
    local a = cast:Ground({ x = point.x - span.x, y = point.y - span.y, z = 0 }, { water = true, above = ABOVE })
    local b = cast:Ground({ x = span.x + point.x, y = span.y + point.y, z = 0 }, { water = true, above = ABOVE })
    local above = cast:Ground(point, { above = ABOVE })
    data.at = point
    data.along = { x = b.x - a.x, y = b.y - a.y, z = b.z - a.z }
    data.visual = cast:Bolt(a, b, { width = 0 })
    data.light = cast:Light(above, { range = cast.radius + cast.lightRange })
    data.sound = cast:SoundLoop(above, "magic\\Lightwall\\circle.wav")
    cast:Block(point, { along = { x = data.along.x * 0.5, y = data.along.y * 0.5, z = data.along.z * 0.5 } })
end

local function PoisonCloud(cast)
    local data = cast.data
    local at = cast:Ground(cast.point, { above = ABOVE })
    data.at = at
    data.visual = cast:Effect("PoisonFog", { at = at, size = cast.radius, alpha = cast.power })
    data.sound = cast:SoundLoop(at, "magic\\poison\\circle.wav", { first = "magic\\poison\\start.wav" })
    data.light = cast:Light(at, { range = cast.radius + cast.lightRange })
    cast:Block(at, { radius = cast.radius })
end

function spell.OnCast(cast)
    local from = cast.caster and cast:Eye(cast.caster) or cast.from
    if cast.code == "firewall" then
        FireWall(cast, from)
    elseif cast.code == "litnwall" then
        LightWall(cast, from)
    else
        PoisonCloud(cast)
    end
    Schedule(cast, cast.duration)
end

function spell.Pulse(cast)
    local data = cast.data
    local hit = { area = true, split = cast.duration, noCredit = data.pulsed }
    for _, unit in ipairs(cast:FindUnits(data.at, cast.radius, { area = true, unsorted = true, along = data.along })) do
        cast:Damage(unit, cast.damage, DAMAGE[cast.code], hit)
    end
    data.pulsed = true
    if cast.tick + PULSE < data.out then cast:After(PULSE, "Pulse") end
end

local function Rise(cast)
    cast:SetVelocity(cast.data.light, { x = 0, y = 0, z = cast.lightRise })
end

-- Out: each in the game's own order (the timed objects' order is state).
function spell.Out(cast)
    local data = cast.data
    if cast.code == "firewall" then
        local above = cast:Ground(cast.point, { above = ABOVE })
        cast:Unblock()
        cast:Release(data.sound)
        cast:Release(data.visual)
        cast:Sound("magic\\Firewall\\end.wav", above)
        Rise(cast)
    elseif cast.code == "litnwall" then
        Rise(cast)
        cast:Unblock()
        cast:Release(data.sound)
        cast:Release(data.visual)
    else
        cast:Sound("magic\\poison\\end.wav", cast.point)
        Rise(cast)
        cast:Unblock()
        cast:Release(data.sound)
        cast:Release(data.visual)
    end
end

function spell.Done(cast)
    cast:Finish()
end

-- A cast from a save made before these spells moved onto this file, standing or gone out, with `old.ticks` left on
-- the game's count; its objects and the direction of its wall or bolt from `old`, its point where the game kept it
-- (`cast.from`). A standing one blocks the AI map again, as the game's load did.
function spell.OnResume(cast, old)
    local data = cast.data
    data.visual, data.sound, data.light = old.visual, old.sound, old.light
    -- The game's credit flag: set by the first pulse, at the duration's highest count divisible by 4.
    data.pulsed = old.ticks < cast.duration - cast.duration % PULSE
    if cast.code == "acid_fog" then
        data.at = cast.from
        if old.visual then cast:Block(data.at, { radius = cast.radius }) end
    elseif old.direction then
        data.at = cast.point
        data.along = old.direction
        local d = old.direction
        local half = cast.code == "litnwall" and { x = d.x * 0.5, y = d.y * 0.5, z = d.z * 0.5 } or d
        cast:Block(data.at, { along = half })
    end
    Schedule(cast, old.ticks)
end

return spell
