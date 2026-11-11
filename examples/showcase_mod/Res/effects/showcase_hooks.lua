-- Res\effects\showcase_hooks.lua: the hooks of the showcase mod's showcase_hooks effect (docs/modding.md, "Effect
-- hooks"). The effect's file names this one (`script = "showcase_hooks.lua"`). It runs on each player's machine
-- alone, in the effects' own Lua, and nothing of it is saved: keep what a source needs in its `e`.
local ARMS = 5
local TURN = 0.05       -- the spiral's turn per tick
local EMBER_TURN = 0.08 -- the embers' turn round the centre per tick

local hooks = {}        ---@type ei.EffectScript

-- The effect starts in its storm mode.
function hooks.on_start(e)
    e:set("mode", "storm")
    e.made = 0
end

-- The charge swells and ebbs; the data's rate reads it.
function hooks.on_tick(e, age)
    e.spin = age * TURN
    e:set("charge", 0.5 + 0.5 * math.sin(age * 0.1))
end

-- A pulse makes it flare (the data's stage "flare" waits for `burst`); the game's `calm` ends that.
function hooks.on_event(e, name)
    if name == "pulse" then e:set("burst", 1) end
end

-- Each flame goes onto the next arm of the spiral, further out the later it is drawn, tinted by the charge.
function hooks.on_spawn(e, p)
    if p:get("kind") ~= "flame" then return end
    e.made = e.made + 1
    local x, y = e:get("origin")
    local size = e:get("size")
    local out = e:random(0.15, 1)
    local angle = (e.made % ARMS) * (2 * math.pi / ARMS) + out * 3 + e.spin
    local px, py = x + math.cos(angle) * out * size * 1.5, y + math.sin(angle) * out * size * 1.5
    p:set("position", px, py, e:ground(px, py) + 0.1)
    p:set("color", 255, 110 + 120 * e:get("charge"), 50)
end

-- The opt-in per particle: each ember turns round the centre (the flames are the data's alone).
function hooks.on_update(e, p)
    if p:get("kind") ~= "ember" then return end
    local cx, cy = e:get("origin")
    local x, y, z = p:get("position")
    local dx, dy = x - cx, y - cy
    local c, s = math.cos(EMBER_TURN), math.sin(EMBER_TURN)
    p:set("position", cx + dx * c - dy * s, cy + dx * s + dy * c, z + 0.01)
end

return hooks
