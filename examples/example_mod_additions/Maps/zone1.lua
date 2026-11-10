-- What ei-convert --example-mod adds to zone 1's script as ei-convert writes it: the mod's Maps\zone1.lua is the whole
-- of the zone's script (docs/modding.md, "Scripts"). This file goes in before the WorldScript, whose first line then
-- calls ExampleModStart.

-- The pig the mod adds (ExamplePig, id 3001, in its Maps\zone1.mob.toml) greets the party once the leader is near.
ExamplePigGreets = Script {
    {
        condition = function(pig) return IsLess(DistanceUnitUnit(GetLeader(), pig), 8) ~= 0 end,
        action = function(pig)
            KillScript()
            print("Example mod: the pig greets the party")
        end,
    },
}

-- Called by the WorldScript when the zone loads.
function ExampleModStart()
    print("Example mod: zone1.lua runs")
    ExamplePigGreets(GetObject(3001))
end

