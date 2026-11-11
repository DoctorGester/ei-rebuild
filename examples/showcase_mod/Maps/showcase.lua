-- The showcase island's script (docs/modding.md, "Scripts"): the script of the mod's own zone (Res\texts\map.toml).
-- One quest: break the golem (Maps\showcase.mob.toml's object 4001) that guards the path with its two shards; its
-- reward is the mod's hammer and boots. Taking it gives the party the mod's spell for the fight: a spell container of
-- Chain Spark (Res\database\spells.sdb.toml's "spark"), which the camp's magic page puts on a hero. The quest's title
-- and objective are Res\texts\texts.toml's "quest showcase_golem", its experience Res\database\quests.qdb.toml's
-- record.

GlobalVars {
    Heroes = "group",      -- the party: the game adds its members when they arrive
    Golem = "object",
}

local QUEST = "q.showcase.showcase_golem"   -- 1 taken, 2 done; QUEST .. ".1" its one objective

-- The quest ends when the golem falls.
GolemFalls = Script {
    {
        condition = function(golem) return IsDead(golem) ~= 0 end,
        action = function(golem)
            KillScript()
            SetGameVarMax(0, QUEST .. ".1", 2)
            SetGameVarMax(0, QUEST, 2)
            QuestComplete(0, "showcase_golem")
            GiveItem(0, "showcase hammer.granite")
            GiveItem(0, "showcase boots.thin")
            print("Showcase mod: the golem is broken; its hammer and boots are the party's")
        end,
    },
}

-- Runs when the zone loads.
function WorldScript()
    Sleep(2)
    Golem = GetObject(4001)
    print("Showcase mod: the island loads")
    SetGameVarMax(0, QUEST, 1)
    SetGameVarMax(0, QUEST .. ".1", 1)
    GiveItem(0, "spell container[spark]")
    print("Showcase mod: the party has the Chain Spark")
    GolemFalls(Golem)
end
