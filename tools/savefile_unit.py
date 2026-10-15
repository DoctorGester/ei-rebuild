"""Units in a decoded save (tools/savefile.py): CUnit::SaveUnit (005321C0, unit_save.cpp) and what it calls.

Map object types 0x32-0x34 and the fake unit 0x39 all save through CUnit::SaveUnit (CFakeUnit's vtable 0073BFE8
keeps slot 10); a unit's controller on the server is CUnitServer (vtable 0073EF38, class 0x3E), whose Save is
CUnitServer::SaveServer (005561C0, combat_save.cpp).
"""

import savefile_objects as objects

UNSET_SHELTER = 0xC4A42000  # -1313.0f, FearMode::Construct's "no shelter"


def peek_u32(r, at=0):
    """The dword `at` bytes ahead, unread (None past the end)."""
    v = r.peek_i32(at)
    return None if v is None else v & 0xFFFFFFFF


def junk_vec(r, name, why, n=3):
    with r.scope(name):
        for i in range(n):
            r.junk('xyzw'[i], 4, why)


# ---- small records -------------------------------------------------------------------------------------

def read_step_list(r, name):
    """SaveStepList (00558420): the count, then each AnimationStep (12 bytes, combat.h) raw."""
    with r.scope(name):
        count = r.i32('count')
        for i in range(count):
            with r.scope(f'step{i}'):
                r.u8('animation')
                r.u8('variant')
                r.u8('flags')
                r.junk('padding', 1, 'AnimationStep +3: 00550A60 never sets the byte, the step carries a stack byte')
                r.i32('duration')
                r.i32('kind')
    return count


def read_path_cell(r, name):
    """A PathCell (ai_map.h, 8 bytes) written raw."""
    with r.scope(name):
        r.i16('x')
        r.i16('y')
        r.x32('flags')


def read_motion_order(r):
    """AIPath::SaveOrder (005C8370, ai_path_save.cpp): the MotionOrder, then its steps (8 bytes each)."""
    with r.scope('order'):
        kind = r.i32('kind')
        r.f32('x')
        r.f32('y')
        r.f32('angle')
        r.i32('startTime')
        r.x32('arg5')
        r.x32('arg6')
        r.x32('arg7')
        count = r.i32('steps')
        for i in range(count):
            read_path_cell(r, f'step{i}')
    return kind


def read_path(r, name):
    """AIPath::SavePath (005B2570, ai_path_save.cpp): the cells, the order, the target, the goal, the flags,
    the excluded blocks and the smoothing value. Returns the order's kind."""
    with r.scope(name):
        count = r.i32('cells')
        for i in range(count):
            read_path_cell(r, f'cell{i}')
        kind = read_motion_order(r)
        x = peek_u32(r)
        r.f32('targetX')
        y = peek_u32(r)
        r.f32('targetY')
        if x == 0 and y == 0:
            junk_vec(r, 'goal', 'AIPath +0x60 goal while targetX/Y are 0 (the clear 005B23A0, no search since): only '
                     'FindPath sets it (PathTo then +0x68), read only after a search (Replan, ReportPath); unset '
                     'before the first one')
        else:
            r.vec('goal')
        r.u8('reached')
        r.u8('failed')
        r.u8('flag6E')
        r.u8('flag6F')
        count = r.i32('excluded')
        for i in range(count):
            with r.scope(f'excluded{i}'):
                r.i32('x')
                r.i32('y')
        r.i32('smoothing')
    return kind


def read_plan(r, name):
    """The current plan: AnimationSteps::Save (00544F60: the count, the steps, extra), then the plan's fields
    (SavePlanFields, combat_save.cpp)."""
    with r.scope(name):
        steps = read_step_list(r, 'animation')
        r.i32('animation.extra')
        r.i32('startTime')
        end = r.i32('endTime')
        target = r.id('targetId')
        r.i32('hitLocation')
        r.f32('rangeSq')
        r.u8('isCast')
        r.u8('notSkill')
        r.i32('castKind')
        if steps == 0 and end == 0 and target == -1:
            junk_vec(r, 'direction', 'OrderPlan +0x38 direction of an empty plan (OrderPlanReset 00544FD0 / '
                     'ResetController leave it): only PlanAction (0054CA40) sets it, for the plan it makes; unset '
                     'before the first plan, a leftover after')
        else:
            r.vec('direction')


ORDER_NO_SOURCE = (0, 2, 7, 8, 9, 11)   # UnitAction kinds whose setters leave +0x04
ORDER_NO_POINT = (0, 7, 8, 9, 11)       # and +0x0C


def read_order(r, name):
    """SaveOrder (combat_save.cpp): a UnitAction's kind, source, target id (AIRef::Save 0050E290), point,
    param, modifier and flag18."""
    with r.scope(name):
        kind = r.i32('kind')
        if kind in ORDER_NO_SOURCE:
            r.junk('source', 4, 'UnitAction +0x04 of an order of kind 0, 2, 7, 8, 9 or 11: none of their setters '
                   '(OrderTurn, PlayAnimation, StopQueued, the resets 0054B230 / 00545F70) sets it; a leftover '
                   'or never set')
        else:
            r.x32('source')
        r.id('target')
        if kind in ORDER_NO_POINT:
            junk_vec(r, 'point', 'UnitAction +0x0C of an order of kind 0, 7, 8, 9 or 11: their setters '
                     '(PlayAnimation, StopQueued, the resets 0054B230 / 00545F70, ResetController) leave it; a '
                     'leftover or never set')
        else:
            r.vec('point')
        r.i32('param')
        r.u8('modifier')
        r.u8('flag18')


STATS_STACK = ('UnitStats is built as a stack copy (UnitStatsBuilder::ApplyPrototype 00513B60, the party records) '
               'and UnitStats::Reset (005233E0) does not clear this')


def read_stat_slot(r, name):
    """A UnitStatSlot (unit.h, 0xF4 bytes) of the raw stats block."""
    spare = ('UnitStatLayer +0x20..+0x3B spareAbsorb: nothing reads it, only ClearOuterLayers zeroes it; '
             + STATS_STACK)
    with r.scope(name):
        r.i32('type')
        r.x32('vitalMask')
        for l in range(3):
            with r.scope(f'layer{l}'):
                r.x32('layerMask')
                for t in range(7):
                    r.f32(f'absorb{t}')
                for t in range(7):
                    r.junk(f'spareAbsorb{t}', 4, spare)
                r.i32('state')
                r.x32('spareA')
                r.x32('spareB')
        kind = r.i32('kind')
        r.f32('current')
        r.f32('maximum')
        if kind == 0:
            why = ('UnitStatSlot +0xEC/+0xF0 (share, factor) of an unused slot (kind 0): UnitStatSlot::Reset (00523600) '
                   'leaves them and the builders set them only for the slots of the model; ' + STATS_STACK)
            r.junk('share', 4, why)
            r.junk('factor', 4, why)
        else:
            r.f32('share')
            r.f32('factor')


def read_stats(r, name, text_key):
    """UnitStats::SaveTo (00528800, unit_stats_io.cpp): the whole 0x704-byte block (unit.h) raw."""
    with r.scope(name):
        r.junk('builtFlags', 4, 'UnitStats ' + objects.BUILT_FLAGS)
        r.f32('experience')
        r.f32('spentExperience')
        for n in ('stat10Base', 'stat10', 'stat18Base', 'stat18', 'stat20Base', 'stat20', 'speedBase', 'speed',
                  'rateBase', 'rate'):
            r.f32(n)
        for i in range(4):
            r.f32(f'modeSpeeds{i}')
        for n in ('sightCone', 'peripheralRange', 'sideCone', 'load', 'capacity', 'loadFactor', 'armsFactor'):
            r.f32(n)
        r.i32('movementKind')
        r.x32('modelField8')
        r.i16('sizeClass')
        r.junk('unused6A', 2, 'UnitStats +0x6A: an int16 nothing writes; ' + STATS_STACK)
        r.i16('sizeClass20')
        r.i16('sizeClass60')
        for n in ('reach', 'attack', 'attackSpread', 'defence', 'speedCombat', 'damage', 'damageSpread'):
            r.f32(n)
        r.x32('prototypeField12')
        for i in range(7):
            r.f32(f'damageWeights{i}')
        r.i32('naturalWeapon')
        for group in ('valuesB0', 'valuesE0'):
            for i in range(6):
                r.f32(f'{group}_{i}.base')
                r.f32(f'{group}_{i}.current')
        for s in range(6):
            read_stat_slot(r, f'slot{s}')
        r.raw('attributes', 3)
        r.raw('skills', 8)
        r.raw('skillLevels', 0x24)
        r.junk('skillLevels36', 1, 'UnitStats +0x6F7: the 37th skill level byte; Reset clears 36 (0x6D3..0x6F6), '
               'the trade panel copies 36 and no skill reaches index 36; ' + STATS_STACK)
        r.x32('flags')
        if text_key:
            r.junk('fixedMaxHitPoints', 4, 'UnitStats +0x6FC of a text-key unit: RecomputeHitPoints (005243D0) reads '
                   'it only for units without a text key, and the stats of a text-key unit come from its party record, '
                   'which does not set it; ' + STATS_STACK)
        else:
            r.f32('fixedMaxHitPoints')
        r.f32('pendingExperience')


def read_damage_info(r, name):
    """The pending hit (DamageInfo, spell.h, 0x3C bytes) written raw."""
    with r.scope(name):
        for i in range(7):
            r.f32(f'typeWeights{i}')
        r.x32('weaponValue')
        r.x32('attackRoll')
        r.f32('amount')
        r.f32('multiplier')
        r.i32('location')
        r.i32('hitKind')
        r.f32('backstabFactor')
        r.u8('calledShot')
        r.raw('pad39', 3)   # zeroed with the record by ResetController (memset); the builders write fields


# ---- the AI brain (AIBrain::Save 005D8860, unit_ai_brain.cpp) --------------------------------------------

def read_look(r, name):
    with r.scope(name):
        r.f32('angle')
        r.f32('time')
        r.f32('wait')
        r.u8('stopFirst')


def read_mode(r, name):
    """A mode: its type (slot 8), then its Save (slot 12; unit_ai_modes.cpp)."""
    with r.scope(name):
        kind = r.i32('type')
        if kind == 0:       # GuardMode::Save 005D0F00
            r.f32('radius')
            r.f32('wander')
            r.vec('point')
            r.i32('failures')
        elif kind == 1:     # PatrolMode::Save 005D1A40
            count = r.i32('points')
            for i in range(count):
                with r.scope(f'point{i}'):
                    r.vec('position')
                    n = r.i32('looks')
                    for j in range(n):
                        read_look(r, f'look{j}')
            r.i32('current')
        elif kind == 2:     # SentryMode::Save 005D1DC0
            r.vec('point')
        elif kind == 3:     # SuspectionMode::Save 005D2360
            why = ('SuspectionMode +0x08..+0x17 (point, level): the constructors (AddSuspectionMode, NewModeOfType, '
                   'the script\'s) leave them; Evaluate (005D1E90, each Think) either copies the brain\'s noticed point '
                   'and level (saved with the brain: noticedX/Y/Level) with z 0, or, while the point compares the same '
                   '(unordered too), keeps the leftover point and takes min(leftover level, noticedLevel). Whether it '
                   'ever copied cannot be told from the save when the leftover z is 0 (the original\'s plain heap holds '
                   'zeros, EI_HEAP_JITTER fill and ours other bytes), so both are left out')
            junk_vec(r, 'point', why)
            r.junk('level', 4, why)
        elif kind == 4:     # AggressionMode::Save 005D27A0
            r.id('enemy')
            r.u8('fixed')
        elif kind == 5:     # CorpseWatcher: the base Save 004BA2C0 (nothing)
            pass
        elif kind in (6, 8):    # PlayerMode::Save 005D4DB0 (Player and Tamed vtables)
            r.id('target')
            r.id('leader')
            r.vec('point')
            r.i32('state')
        elif kind == 7:     # FearMode::Save 005D36F0
            r.f32('distance')
            r.f32('spread')
            with r.scope('shelter'):
                x = peek_u32(r)
                r.f32('x')
                if x == UNSET_SHELTER:
                    why = ('FearMode +0x14/+0x18 (shelter y, z) while x is -1313 (no shelter): FearMode::Construct '
                           '(005D2850) sets only x')
                    r.junk('y', 4, why)
                    r.junk('z', 4, why)
                else:
                    r.f32('y')
                    r.f32('z')
            r.u8('flag')
            with r.scope('fleePoint'):
                if peek_u32(r, 8) == 0:
                    why = ('FearMode +0x1C/+0x20 (fleePoint x, y) while z is 0 (none pending): FearMode::Construct '
                           '(005D2850) sets only z, and they are read only while z is 1')
                    r.junk('x', 4, why)
                    r.junk('y', 4, why)
                else:
                    r.f32('x')
                    r.f32('y')
                r.f32('z')
        elif kind == 10:    # FollowMode::Save 005D1E80
            r.id('leader')
        else:
            r.fail(f'AI mode type {kind} has no reader')


def read_id_list(r, name):
    """A counted list of objects: the count, then each AIRef::Save (0050E290: the id)."""
    with r.scope(name):
        count = r.i32('count')
        for i in range(count):
            r.id(str(i))


def read_memory(r):
    """AIBrain::SaveMemory (005CC350, unit_ai_save.cpp): the perception memory."""
    with r.scope('memory'):
        read_id_list(r, 'nearby')
        read_id_list(r, 'visible')
        read_id_list(r, 'seenCorpses')
        read_id_list(r, 'corpses')
        r.f32('noticedX')
        r.f32('noticedY')
        r.i32('noticedLevel')
        r.i32('nearbyCountdown')
        count = r.i32('reports')
        for i in range(count):
            with r.scope(f'report{i}'):
                r.id('object')
                r.i32('what')
        count = r.i32('calls')
        for i in range(count):
            with r.scope(f'call{i}'):
                r.vec('point')
                r.i32('kind')
                r.string('text')
                r.x32('hostileMask')
        r.i32('noticedKind')


def read_behaviour(r, name):
    """mapspec::Behaviour::Write (0045D760, map_spec_unit.cpp): an alarm record."""
    with r.scope(name):
        r.i32('type')
        count = r.i32('patrols')
        for i in range(count):
            with r.scope(f'patrol{i}'):
                r.vec('position')
                n = r.i32('waypoints')       # WaypointsWrite 0045DFB0: Waypoint (0x14 bytes) raw
                for j in range(n):
                    with r.scope(f'waypoint{j}'):
                        r.vec('position')
                        r.f32('time')
                        r.i16('lookWait')
                        r.u16('stopFirst')
        r.vec('guard')
        r.u32('loop')
        r.x32('flags')
        r.f32('guardRadius')
        r.u32('alarmKind')
        r.u32('enabled')
        r.f32('callRadius')
        r.i32('alertMode')
        r.i32('alarmDelay')
        r.i32('alarm')


def read_attack_option(r, name):
    """AIAttackOption::Save (005CE920, unit_ai_options.cpp)."""
    with r.scope(name):
        r.i32('kind')
        r.i32('part')
        r.f32('range')
        r.i32('maxCount')
        r.f32('radius')
        r.i32('effect')
        with r.scope('damage'):
            for i in range(8):
                r.u16(str(i))


def read_brain(r):
    """AIBrain::Save (005D8860, unit_ai_brain.cpp)."""
    with r.scope('brain'):
        r.i32('action')
        r.vec('actionPoint')
        count = r.i32('looks')          # SaveLooks 005D91B0
        for i in range(count):
            read_look(r, f'look{i}')
        r.i32('actionTime')
        r.id('target')
        r.id('seen')
        r.f32('callRadius')
        r.f32('nearX')
        r.f32('nearY')
        r.f32('decay')
        r.vec('previousPosition')
        r.vec('position')
        r.i32('alertMode')
        count = r.i32('modes')
        r.i32('currentMode')
        for i in range(count):
            read_mode(r, f'mode{i}')
        read_memory(r)
        r.u8('alerted')
        r.i8('alarm')
        count = r.i32('alarmRecords')
        for i in range(count):
            read_behaviour(r, f'alarmRecord{i}')
        r.i32('alarmCountdown')
        r.u8('alwaysUpdated')
        count = r.i32('weapons')
        for i in range(count):
            read_attack_option(r, f'weapon{i}')
        count = r.i32('spells')
        for i in range(count):
            read_attack_option(r, f'spell{i}')
        r.f32('healWeight')
        r.i8('alarmSlot')


# ---- the controller (CUnitServer::SaveServer 005561C0, combat_save.cpp) ---------------------------------

def read_controller(r, text_key):
    """CUnitServer::SaveServer (005561C0). Returns the motion order's kind."""
    with r.scope('controller'):
        r.u8('frozen')
        r.u8('animationHeld')
        read_step_list(r, 'queue')
        r.i32('queue.extra')
        motion_kind = read_path(r, 'motion')
        read_plan(r, 'plan')
        r.u8('keepHitDirection')
        r.u8('stoodBlocked')
        r.u8('motionChanged')
        r.u8('movingAnimation')
        read_step_list(r, 'savedQueue')
        r.i32('savedQueue.extra')
        r.x32('hitDirection')
        r.i32('state')
        sneaking = 0
        for name in ('moveMode', 'moveMode2'):      # MoveModeState (8 bytes) raw
            with r.scope(name):
                r.i32('mode')
                r.u8('flag4')
                w = r.u8('wasMode3')
                if name == 'moveMode':
                    sneaking = w
                r.junk('pad', 2, 'MoveModeState +6..+7: padding nothing writes (ResetController sets the fields), '
                       'written with the 8-byte record')
        r.i32('nextAttackTime')
        r.i32('interceptTime')
        if sneaking:
            r.i32('sneakWait')
        else:
            r.junk('sneakWait', 4, 'CUnitServer +0x178 while moveMode.wasMode3 is 0: UpdateSneak (00548520) counts '
                   'and reads it only while mode 3 is on and zeroes it when mode 3 starts, PathTo (00548700) zeroes '
                   'it; neither the constructor nor ResetController sets it')
        read_order(r, 'action')
        read_order(r, 'action2')
        read_stats(r, 'stats', text_key)
        for i in range(4):
            r.f32(f'moveFactors{i}')
        r.i32('tameStage')
        r.i32('ownSide')
        r.i32('tamerSide')
        r.i32('speedClass')
        r.x32('claimedHostile')
        read_brain(r)
        with r.scope('voice'):          # AIAlarm::Save 005AEC80
            r.i32('level')
            r.i32('pending')
            r.i32('active')
            r.i32('timer')
        r.u8('orderStopped')
        r.u8('deathReported')
        r.f32('sinkSpeed')
        r.id('effectObject')
        count = r.i32('armourSpells')
        for i in range(count):
            with r.scope(f'armourSpell{i}'):    # ArmourSpell::Save 00545430
                r.id('item')
                r.i32('countdown')
        r.u8('followsLeader')
        r.id('followedLeader')
        r.i32('actingCheckTime')
        r.i32('savedConstant')
        r.u8('searchingTarget')
        with r.scope('protection'):
            for i in range(8):
                r.i16(str(i))
        r.i32('boredTicks')
        read_damage_info(r, 'pendingHit')
        r.junk('waitFrame', 4, 'CUnitServer +0x93C: set to the world tick only when a queued order is taken over '
               '(TakeQueuedOrder 0054A6E0, LeaveMoveMode), never initialised (the constructor and ResetController '
               'leave it), so the save cannot tell a stamp from a leftover; the original\'s value follows memory '
               '(EI_HEAP_JITTER)')
    return motion_kind


# ---- the unit ----------------------------------------------------------------------------------------------

def read_unit_motion(r, order_kind):
    """UnitMotion::SaveState (005C79B0, ai_motion.cpp): the trajectory points, the corners, the step speeds,
    the turn values. `order_kind`: the controller's motion order's kind."""
    with r.scope('motion'):
        count = r.i32('points')
        for i in range(count):
            with r.scope(f'point{i}'):      # MotionPoint (0x1C bytes) raw
                r.f32('x')
                r.f32('y')
                r.f32('dirX')
                r.f32('dirY')
                r.f32('speed')
                r.i32('cellX')
                r.i32('cellY')
        count = r.i32('waypoints')
        for i in range(count):
            with r.scope(f'waypoint{i}'):   # MotionWaypoint (0xC bytes) raw
                r.f32('x')
                r.f32('y')
                r.i32('cells')
        count = r.i32('stepSpeeds')
        for i in range(count):
            r.f32(f'stepSpeed{i}')
        r.f32('speedScale')
        r.f32('baseTurnRate')
        why = ('UnitMotion +0x44/+0x4C (turnRate, turn) under a motion order of kind 0 or 3: only Build (005C67D0) '
               'and SamplePath set them, for kinds 1, 2 and 4, and only those kinds read them (Sample 005C7430); '
               'the unit (operator new) starts with them unset')
        if order_kind in (0, 3):
            r.junk('turnRate', 4, why)
        else:
            r.f32('turnRate')
        r.f32('startAngle')
        if order_kind in (0, 3):
            r.junk('turn', 4, why)
        else:
            r.f32('turn')


def read_items(r, name, n):
    with r.scope(name):
        for i in range(n):
            r.id(str(i))


def read_unit(r):
    """CUnit::SaveUnit (005321C0, unit_save.cpp)."""
    objects.read_map_object_base(r)
    key = r.current[0].split('#')[0] if r.current else ''
    oid = int(key[3:]) if key.startswith('obj') and key[3:].lstrip('-').isdigit() else -1
    text_key = 1000000000 <= oid < 2000000000      # IsTextKey (004613A0)
    order_kind = read_controller(r, text_key)
    r.string('scriptName')
    r.x32('relationsA')
    player = r.i32('player')
    r.i32('side')
    r.i8('animationEntry')
    r.i8('secondEntry')
    r.i32('animationStart')
    r.i32('motionMode')
    r.i32('pathTimeShift')
    r.i32('moveMode')
    r.i32('moveMode2')
    r.i32('activeWeapon')
    r.i32('pendingWeapon')
    r.i32('dead')
    r.i32('obstacle')
    read_unit_motion(r, order_kind)
    r.f32('heightOffset')
    r.i32('sizeClass')
    r.f32('unitRadius')
    r.i32('activeWeapon2')
    read_items(r, 'weapons', 4)
    read_items(r, 'quickItems', 4)
    read_items(r, 'spells', 8)
    read_items(r, 'armor', 7)
    with r.scope('levels4C8'):
        for i in range(5):
            if player == -1:
                r.junk(str(i), 4, 'CUnit +0x4C8 of a unit without a player: only the party unit\'s memory update '
                       '(005CB850 from UpdateServer) writes it; the unit (operator new) starts with it unset')
            else:
                r.f32(str(i))
    r.x32('relationsB')
    count = r.i32('carried')
    for i in range(count):
        r.id(f'carried{i}')
    r.f32('standingHeight')
    r.i32('fightAtWill')
    r.string('prototypeName')
    r.string('prototype2Name')
