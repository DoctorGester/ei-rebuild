"""Writes quests.txt and areas.txt: mission script files for tests/scenarios/script_quest_save.txt (L7).

usage: python tools/make_quest_script.py <saves dir>
  <saves dir>  the run's harness_saves folder (what the game sees as <game root>saves\\); the scenario loads the
               files with the console command "execs .\\saves\\<file>" (Script::LoadFile into the mission script).

QFinish loads the quest script it builds into the script that runs it, and only the mission script is in a save, so
the quests are built by a file execs loads (the console's "@" lines run in the world's second script). quests.txt
builds two quests into the mission script, the second's plain names (Su0, Su1) qualified by the parser after the
first's: "zqa" (the leader seen at once, then area 71) and "zqb" (area 72). Their conditions use the zone's i and
Heroes. areas.txt, loaded after the save is loaded back, makes the two areas around the leader, so both quests
finish in the loaded game.
"""
import os
import sys

QUESTS = r'''/* Mission script of tests/scenarios/script_quest_save.txt: two quests */
WorldScript(
    AddObject(Heroes,GetLeader())
    QStart("zqa") QObjSeeObject("GetLeader()") QObjArea(71) QFinish()
    QStart("zqb") QObjArea(72) QFinish()
    GSSetVar(0,"zq.loaded",1)
)
'''

AREAS = r'''/* Mission script of tests/scenarios/script_quest_save.txt: the quests' areas */
WorldScript(
    AddRoundToArea(71,GetX(GetLeader()),GetY(GetLeader()),10)
    AddRoundToArea(72,GetX(GetLeader()),GetY(GetLeader()),10)
)
'''


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for name, text in (('quests.txt', QUESTS), ('areas.txt', AREAS)):
        with open(os.path.join(sys.argv[1], name), 'w', newline='\r\n') as f:   # CRLF, as the game's own files
            f.write(text)


if __name__ == '__main__':
    main()
