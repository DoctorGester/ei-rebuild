"""The map objects' part of a decoded save (tools/savefile.py): SaveMapObjects (00596730) calls each object's
Save (virtual slot 10) by its class. Types are the map object manager's factory's (00512730):
  0x32-0x34 CUnit, 0x39 the fake unit (CUnit's Save too)       savefile_unit.read_unit
  0x35 tree, 0x36 building, 0x37 stone, 0x38 stuff              CMapObject::SaveMapObject 0050F900
  0x3A torch (0052C2A0), 0x3B magic trap (0052B070), 0x3C lever (0052BE40), 0x3010 trap object (00529DA0)
"""

END_OF_OBJECTS = -0x521   # CBaseManager::SaveObjects' end mark (base_manager.cpp kEndOfObjects)

PLAIN = {0x35: 'tree', 0x36: 'building', 0x37: 'stone', 0x38: 'stuff'}
UNITS = (0x32, 0x33, 0x34, 0x39)
BUILT_FLAGS = "+0x00: the only write is `|= 1` on ApplyPrototype's (00513B60) stack copies; GiveDefaults (00513630) and the Resets (00523370, 005233E0) leave it, so it holds whatever that stack held; nothing reads it"


def read_vitals(r, name):
    """UnitVitals (0x58 bytes, unit.h), written raw."""
    with r.scope(name):
        r.junk('builtFlags', 4, 'UnitVitals ' + BUILT_FLAGS)
        r.i32('regenTicks')
        for field in ('hp', 'maxHp', 'hpRegen', 'mana', 'maxMana', 'manaRegen'):
            r.f32(field)
        for i in range(7):
            r.f32(f'protection{i}.base')
            r.f32(f'protection{i}.current')


def read_server(r):
    """CMapObjectServer::SaveServer (00515AC0): the vitals, two names, the strings."""
    with r.scope('server'):
        read_vitals(r, 'vitals')
        r.string('name78')
        r.string('name7C')
        count = r.i32('strings')
        for i in range(count):
            r.string(f'string{i}')


def read_effect(r, name):
    """MapEffect::Save (006885A0): type, power, ticksLeft, value, strength one by one."""
    with r.scope(name):
        r.u8('type')
        r.u8('power')
        r.i32('ticksLeft')
        r.x32('value')
        r.f32('strength')


def read_map_object_base(r):
    """CMapObject::SaveMapObject (0050F900): the companion (CMapObjectServer), animation speed, position,
    orientation (a quaternion), name, scale, effects, flags, the second name, the base scale."""
    read_server(r)
    r.f32('animationSpeed')
    r.vec('position')
    r.vec('orientation', 4)
    r.string('name')
    r.vec('scale')
    count = r.i32('effects')
    for i in range(count):
        read_effect(r, f'effect{i}')
    r.x32('flags')
    r.string('name2')
    r.vec('baseScale')


def read_torch(r):
    read_map_object_base(r)
    with r.scope('spec'):   # CTorchObjectSpec::SaveSpec (0052C500)
        r.u32('lightKind')
        r.string('name')
        r.f32('value0')
        r.f32('value1')
        for i in range(2, 6):
            r.f32(f'value{i}')
        r.vec('colour0')
        r.vec('colour1')


def read_magic_trap(r):
    """MagicTrap::SaveTrap (0052B070)."""
    read_map_object_base(r)
    with r.scope('trap'):
        r.id('spell')
        count = r.i32('circles')
        for i in range(count):
            r.vec(f'circle{i}')
        count = r.i32('inside')
        for i in range(count):
            r.id(f'inside{i}')
        count = r.i32('points')
        for i in range(count):
            r.vec(f'point{i}', 2)
        r.i32('period')
        r.i32('countdown')
        r.i32('singleTarget')
        r.x32('sides')


def read_lever(r):
    """Lever::SaveLever (0052BE40)."""
    read_map_object_base(r)
    with r.scope('lever'):
        r.i32('state')
        r.i32('stateCount')
        r.i32('cycling')
        r.x32('lock.mask')
        r.i32('lock.id')
        r.i32('lock.level')
        r.u8('flag260')
        r.u8('keepsMapFlag')


def read_trap_object(r):
    """CTrapObject::SaveTrapObject (00529DA0)."""
    read_map_object_base(r)
    r.x32('savedValue')


def read_map_object(r, kind):
    if kind in UNITS:
        import savefile_unit
        savefile_unit.read_unit(r)
    elif kind in PLAIN:
        read_map_object_base(r)
    elif kind == 0x3A:
        read_torch(r)
    elif kind == 0x3B:
        read_magic_trap(r)
    elif kind == 0x3C:
        read_lever(r)
    elif kind == 0x3010:
        read_trap_object(r)
    else:
        r.fail(f'map object type {kind!r} has no reader')
