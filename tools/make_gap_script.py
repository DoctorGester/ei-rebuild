"""Writes gaps.txt: a mission script file for tests/scenarios/script_file.txt (H43d).

usage: python tools/make_gap_script.py <saves dir>
  <saves dir>  the run's harness_saves folder (what the game sees as <game root>saves\\); the scenario loads the
               file with the console command "execs .\\saves\\gaps.txt" (Script::LoadFile into the mission script).

The console's "@" lines run in the world's second script, which a save does not hold; a file loaded with execs
runs in the mission script, which it does. The file has "#define" lines (their names of different lengths, so
AddDefine inserts first, before a shorter name and before the last one), a script that sleeps (an instance a
save keeps), and a WorldScript block that makes one effect of each kind the mission script saves: an FX
source (CreateFXSource, a sound that plays once; CreateRandomizedFXSource, one that plays again and again),
a particle source, a point light and a lightning bolt.
"""
import os
import sys

SCRIPT = r'''/* Mission script of tests/scenarios/script_file.txt */
#define GAPA 3
#define GAP_LONGER_B 10
#define GAPC 5
#define GAP_THE_LONGEST_NAME_OF_ALL_D 7
#define GAPSOUND "nature\thunder\1.wav"

GlobalVars(gGapUnit:object)

Script GapSleeper(n:float)(
    if(Mul(IsEqual(n,1),IsEqual(GSGetVar(0,"gap.s1"),0))) then(
        GSSetVar(0,"gap.s1",1)
        Sleep(300)
        GSSetVar(0,"gap.s2",1)
    )
)

WorldScript(
    gGapUnit = GetLeader()
    GSSetVar(0,"gap.defs",Add(Add(GAPA,GAP_LONGER_B),Add(GAPC,GAP_THE_LONGEST_NAME_OF_ALL_D)))
    GSSetVar(0,"gap.x",GetX(gGapUnit))
    GSSetVar(0,"gap.y",GetY(gGapUnit))
    GSSetVar(0,"gap.z",GetZ(gGapUnit))
    GapSleeper(1)
    CreateFXSource(61,Add(GSGetVar(0,"gap.x"),2),GSGetVar(0,"gap.y"),GSGetVar(0,"gap.z"),50,100,GAPSOUND)
    CreateRandomizedFXSource(62,Sub(GSGetVar(0,"gap.x"),2),GSGetVar(0,"gap.y"),GSGetVar(0,"gap.z"),60,150,20,40,3,GAPSOUND)
    CreateParticleSource(63,GSGetVar(0,"gap.x"),Add(GSGetVar(0,"gap.y"),3),GSGetVar(0,"gap.z"),1,"CampFire")
    CreatePointLight(64,GSGetVar(0,"gap.x"),Sub(GSGetVar(0,"gap.y"),3),3,6,255,200,120)
    CreateLightning(65,Add(GSGetVar(0,"gap.x"),4),GSGetVar(0,"gap.y"),Add(GSGetVar(0,"gap.z"),3),Add(GSGetVar(0,"gap.x"),4),Add(GSGetVar(0,"gap.y"),4),GSGetVar(0,"gap.z"),1)
)
'''


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    with open(os.path.join(sys.argv[1], 'gaps.txt'), 'w', newline='\r\n') as f:   # CRLF, as the game's own files
        f.write(SCRIPT)


if __name__ == '__main__':
    main()
