-- Res\spells\Buff.lua: the game's twenty spells that put a lasting effect on a unit, from the protections to shrink
-- (prot_fire, prot_electro, prot_acid, eagle_sight, infravision, detect_life, invisibility, silence, lichdom, stench,
-- stun, antimagic, strength, weak, regeneration, feeblemind, speed, slow, enlarge, shrink). CastSpell casts it once
-- at each target (per_target). The target gets the spell's own effect for the cast's duration, and the cast ends.
-- What the effect does (its stat effect, its look and sounds on the unit) is the record's `effect`
-- (spells.sdb.toml).
local spell = {}

function spell.OnCast(cast)
    if cast.target then cast:Apply(cast.target, "own") end
    cast:Finish()
end

return spell
