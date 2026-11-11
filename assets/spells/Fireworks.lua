-- Res\spells\Fireworks.lua: the game's fireworks ("fireworks"). CastSpell casts it once at each target point
-- (per_target). A fountain of sparks plays at the point with its light and its bangs (five sounds in random order);
-- every tick for the cast's duration the units around wake and those in the radius take fright at the point (their
-- AI notices it at a level falling from 300 to 50). Then it goes out with a last sound, its light rises away and the
-- cast ends 80 ticks later.
local ABOVE = 1             -- it stands this far above the ground
local WAKE = 5              -- the units this far past the radius wake
local LINGER = 80           -- the cast ends this many ticks after it went out
local FRIGHT_RANGE = 250    -- the fright falls by this over the duration
local FRIGHT_LEAST = 50     -- to this

local BANGS = {}
for i = 1, 5 do BANGS[i] = "magic\\Firework\\" .. i .. ".wav" end

-- A number rounded to a float, as the game computes with them.
local function Float(value)
    return (string.unpack("f", string.pack("f", value)))
end

-- The fright with `left` ticks of the duration to go: the game's float steps, each rounded, then truncated.
local function Fright(left, duration)
    local share = Float(left / duration)
    return math.floor(Float(Float(share * FRIGHT_RANGE) + FRIGHT_LEAST))
end

local spell = {}

function spell.OnCast(cast)
    local data = cast.data
    local at = cast:Ground(cast.point, { above = ABOVE })
    data.at = at
    data.count = cast.duration
    data.visual = cast:Effect("Geyser", { at = at, size = cast.power })
    data.light = cast:Light(at)
    data.sound = cast:SoundLoop(at, BANGS)
end

function spell.OnTick(cast)
    local data = cast.data
    local left = data.count - cast.tick
    if left >= 0 then
        cast:Wake(data.at, cast.radius + WAKE)
        local level = Fright(left, cast.duration)
        for _, unit in ipairs(cast:FindUnits(data.at, cast.radius, { unsorted = true })) do
            cast:Notice(unit, data.at, level)
        end
        return
    end
    if left == -1 then
        cast:Sound("magic\\Firework\\end.wav", data.at)
        cast:Release(data.visual)
        cast:Release(data.sound)
        cast:SetVelocity(data.light, { x = 0, y = 0, z = cast.lightRise })
    end
    if left < -LINGER then cast:Finish() end
end

-- A cast from a save made before fireworks moved onto this file, playing or gone out, with `old.ticks` left on the
-- game's count; it stood where the game kept it (`cast.from`).
function spell.OnResume(cast, old)
    local data = cast.data
    data.at = cast.from
    data.count = old.ticks
    data.visual, data.sound, data.light = old.visual, old.sound, old.light
end

return spell
