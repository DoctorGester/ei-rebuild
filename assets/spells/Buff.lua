-- Res\spells\Buff.lua: the game's twenty spells that put a lasting effect on a unit, from the protections to shrink
-- (prot_fire, prot_electro, prot_acid, eagle_sight, infravision, detect_life, invisibility, silence, lichdom, stench,
-- stun, antimagic, strength, weak, regeneration, feeblemind, speed, slow, enlarge, shrink). CastSpell casts it once
-- at each target (per_target). The target gets the spell's own effect for the cast's duration, and the cast ends.
-- What the effect does to the unit's stats is OnStats below, by the effect's kind (the record's `effect`,
-- spells.sdb.toml); its look and sounds are the record's fields there.
local spell = {}

-- The game's 0.01 as a float, exactly: invisibility lowers the sight conspicuity by its value in percent.
local PERCENT = 0.009999999776482582

function spell.OnCast(cast)
    if cast.target then cast:Apply(cast.target, "own") end
    cast:Finish()
end

-- What each kind gives, with `value` the strongest of its effects on the unit. A protection is the larger of its
-- own and antimagic's; strength and weak, enlarge and shrink are steps of the figure whatever their value.
local kinds = {
    prot_fire = function(value, stats) stats:Larger("FireProtection", value) end,
    prot_electro = function(value, stats) stats:Larger("LightningProtection", value) end,
    prot_acid = function(value, stats) stats:Larger("AcidProtection", value) end,
    eagle_sight = function(value, stats) stats:Add("Sight", value) end,
    infravision = function(value, stats) stats:Add("DaySight", value) end,
    detect_life = function(value, stats) stats:Add("NearSense", value) end,
    invisibility = function(value, stats) stats:Subtract("SightConspicuity", value * PERCENT) end,
    silence = function(value, stats) stats:Subtract("HearingConspicuity", value) end,
    lichdom = function(value, stats) stats:Subtract("NearConspicuity", value) end,
    stench = function(value, stats) stats:Add("SmellConspicuity", value) end,
    stun = function(value, stats) stats:Add("Stunned", 1) end,
    antimagic = function(value, stats)
        stats:Larger("FireProtection", value)
        stats:Larger("LightningProtection", value)
        stats:Larger("AcidProtection", value)
    end,
    strength = function(value, stats)
        stats:Add("Might", value)
        stats:Add("Girth", 1)
    end,
    weak = function(value, stats)
        stats:Add("Frailty", value)
        stats:Subtract("Girth", 1)
    end,
    regeneration = function(value, stats) stats:Add("Regeneration", value) end,
    feeblemind = function(value, stats) stats:Add("NoCasting", 1) end,
    speed = function(value, stats) stats:Add("Haste", value) end,
    slow = function(value, stats) stats:Subtract("Haste", value) end,
    enlarge = function(value, stats) stats:Add("Size", 1) end,
    shrink = function(value, stats) stats:Subtract("Size", 1) end,
}

function spell.OnStats(effect, stats)
    local give = kinds[effect.kind]
    if give then give(effect.value, stats) end
end

return spell
