"""The managed objects (timed objects, spells, items) and the world's lists in a decoded save (tools/savefile.py).

CBaseManager::SaveObjects (00404860) writes each object's Save (virtual slot 10) by its type (type_):
  timed (manager48, CEffectManager 0073F2C4; EffectNewObject 0067E170 makes them, world_effect.h):
    0x48 CEffectSound 006766D0, 0x49 CEffectAmbientSound 00677C60, 0x4A CEffectParticle 006794E0,
    0x4B CEffectPointLight 00678740, 0x4D CEffectLightning 0067A7F0, 0x4E CEffectArrow 0067B9A0,
    0x5A CEffectWall 00679ED0, 0x5B CEffectArrowFigure 0067C290, 0x5E CEffectShowVisibility 0067C910,
    99 CEffectSoundSeq 00677430, 100 CEffectMoshka (the base's slot 10: nothing), 0x65 CEffectTornado 0067E070
  spell (manager4C, CSpellManager): 0x3002 CSpell::SaveSpell 0068B920, 0x3003 CSpellContainer (00402D10: nothing)
  item (the item manager): CItem::Save 006A0360 and the subclasses' (item_save.cpp): 0x3004 weapon 006A2EB0,
    0x3005 armour 006A3AA0, 0x3006 quick 006A3CA0, 0x3007 loot 006A5680, 0x3008 spell item (CItemQuick's
    006A3CA0), 0x3009 quest 006A4590
Then SaveWorld's lists: the missiles (CSpellEffect::Save 0067E960), the AI events (AIEventTable::Save
005CAFA0), the zone script (CScript::SaveState 004C0F90), the diplomacy tables (CDiplomacy::WriteTo
004557F0) and the countdowns (TweenSave 006D47E0).
"""

def read_fixed_string(r, name, size, why):
    """A char[size] buffer written whole: the text up to its terminator, the rest left out."""
    raw = r.raw(None, size)
    end = raw.find(b'\0')
    text = raw[:end if end >= 0 else size].decode('cp1251', 'replace')
    r.put(name, '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"')
    r.excluded(name + 'Tail', 'junk', why)
    r.put(name + 'Tail', '~junk')


# ---- timed objects (world_effect*.cpp) ------------------------------------------------------------------

def read_sound(r):
    """CEffectSound::SaveState (006766D0)."""
    r.vec('position')
    read_fixed_string(r, 'name', 0x100, 'CEffectSound::name_ (char[0x100]) past its terminator: Reset only clears '
                      'name_[0], so the rest is operator new memory or the earlier name of a recycled spare; written '
                      'whole')
    r.f32('volume')
    r.f32('radius')
    r.vec('velocity')
    r.i32('looping')
    r.u8('noFade')


def read_names(r, name):
    count = r.i32(name + 'Count')
    for i in range(count):
        r.string(f'{name}{i}')


def read_sound_seq(r):
    """CEffectSoundSeq::SaveState (00677430)."""
    r.vec('position')
    r.vec('velocity')
    r.string('name')
    read_names(r, 'names')
    r.f32('volume')
    r.f32('radius')
    r.u8('noFade')


def read_ambient_sound(r):
    """CEffectAmbientSound::SaveState (00677C60)."""
    r.vec('position')
    read_names(r, 'names')
    r.f32('volume')
    r.f32('radius')
    r.i32('minInterval')
    r.i32('maxInterval')


def read_point_light(r):
    """CEffectPointLight::SaveState (00678740)."""
    r.vec('position')
    r.vec('color')
    r.f32('range')
    r.vec('velocity')
    r.x32('lightFlags')


def read_particle(r):
    """CEffectParticle::SaveState (006794E0)."""
    r.vec('position')
    r.x32('particleType')
    r.f32('size')
    r.id('attachedTo')
    r.vec('velocity')
    r.i32('startTick')
    r.vec('target')
    r.id('unitId')
    r.f32('alpha')
    r.f32('param164')


def read_wall(r):
    """CEffectWall::SaveState (00679ED0): the particle's, then the direction."""
    read_particle(r)
    r.vec('direction')


def read_lightning(r):
    """CEffectLightning::SaveState (0067A7F0)."""
    r.vec('position')
    r.vec('from')
    r.vec('to')
    r.f32('width')
    r.id('toObject')
    r.id('fromObject')
    r.u8('flag')


def read_arrow(r):
    """CEffectArrow::SaveState (0067B9A0)."""
    r.vec('position')
    r.id('targetId')
    r.vec('velocity')
    r.i32('arrived')
    r.string('soundName')
    r.vec('lightColor')
    r.f32('lightRange')
    r.x32('particleType')
    r.f32('particleSize')
    r.id('casterId')
    r.u8('fromCaster')


def read_damage(r, name):
    """DamageInfo (0x3C bytes, spell.h), written raw."""
    with r.scope(name):
        for i in range(7):
            r.f32(f'typeWeight{i}')
        r.u32('weaponValue')
        r.f32('attackRoll')
        r.f32('amount')
        r.f32('multiplier')
        r.i32('location')
        r.i32('hitKind')
        r.f32('backstabFactor')
        r.u8('calledShot')
        r.junk('pad', 3, 'DamageInfo +0x39..+0x3B: padding after calledShot; DeliverAttack (0054F450) copies the hit '
               'whole from a stack DamageInfo, and it is written whole')


def read_arrow_figure(r):
    """CEffectArrowFigure::SaveState (0067C290); the figure through AIRef::Save (0050E290)."""
    r.vec('position')
    r.id('targetId')
    r.id('casterId')
    r.vec('velocity')
    r.i32('arrived')
    r.id('figure')
    read_damage(r, 'hit')
    r.u8('fromCaster')


def read_show_visibility(r):
    """CEffectShowVisibility::SaveState (0067C910)."""
    r.id('observerId')
    r.id('seerId')


def read_tornado(r):
    """CEffectTornado::SaveState (0067E070)."""
    r.vec('position')
    r.vec('velocity')
    r.i32('framesLeft')
    r.i32('launchTick')


TIMED = {0x48: read_sound, 0x49: read_ambient_sound, 0x4A: read_particle, 0x4B: read_point_light,
         0x4D: read_lightning, 0x4E: read_arrow, 0x5A: read_wall, 0x5B: read_arrow_figure,
         0x5E: read_show_visibility, 99: read_sound_seq, 100: lambda r: None, 0x65: read_tornado}


# ---- spells (spell_save.cpp) ------------------------------------------------------------------------------

def read_spell(r):
    """CSpell::SaveSpell (0068B920): the effect record (MapEffect::Save 006885A0 on +0x1C), the parameters,
    the modifiers, the object's flags and the skill needed."""
    r.u8('prototypeIndex')
    r.u8('effectPower')
    r.i32('duration')
    r.f32('damage')
    r.f32('radius')
    r.f32('speed')
    r.f32('range')
    r.i32('targetCount')
    r.x32('targetFlags')
    r.f32('manaCost')
    r.i32('complexity')
    r.f32('price')
    r.x32('prototypeField14')
    r.raw('modifiers', 8)
    r.x32('flags')
    r.i32('skillNeeded')


# ---- items (item_save.cpp) --------------------------------------------------------------------------------

def read_item(r):
    """CItem::Save (006A0360)."""
    r.x32('prototypeId')
    r.f32('condition')
    r.f32('conditionMax')
    r.f32('charge')
    r.f32('chargeMax')
    r.f32('chargeRate')
    r.f32('weight')
    r.f32('price')
    r.f32('value3C')
    r.id('spell')
    r.i32('count')
    r.u32('magicLevel')


def read_item_of(r, kind):
    read_item(r)
    if kind == 0x3004:       # CItemWeapon::Save (006A2EB0)
        r.u32('actionValue')
        r.f32('damage')
        r.f32('damageSpread')
        r.f32('reach')
    elif kind == 0x3005:     # CItemArmor::Save (006A3AA0)
        for layer in range(2):
            for i in range(7):
                r.f32(f'absorb{layer}.{i}')
    elif kind in (0x3006, 0x3008):   # CItemQuick::Save (006A3CA0; CItemSpell's slot 10 too)
        r.u32('actionValue')
    elif kind == 0x3007:     # CItemLoot::Save (006A5680)
        r.u32('kind')
        r.u32('recordType')
        r.u32('index')
    elif kind == 0x3009:     # CItemQuest::Save (006A4590)
        r.u32('questItemId')
    else:
        r.fail(f'item type {kind!r} is not known')


def read_managed(r, manager, kind):
    """One object's Save (virtual slot 10) as CBaseManager::SaveObjects (00404860) calls it."""
    if manager == 'timed':
        reader = TIMED.get(kind)
        if reader is None:
            r.fail(f'timed object type {kind!r} is not known')
        reader(r)
    elif manager == 'spell':
        if kind == 0x3002:
            read_spell(r)
        elif kind != 0x3003:   # CSpellContainer: the base's slot 10 (00402D10) writes nothing
            r.fail(f'spell manager type {kind!r} is not known')
    else:
        read_item_of(r, kind)


# ---- the missiles (spell_effect_save.cpp) -----------------------------------------------------------------

def read_projectile(r):
    """CSpellEffect::Save (0067E960, the world's missile list): position, caster and target (AIRef::Save
    0050E290), the target point, the state, the embedded spell (its slot 10, CSpell::SaveSpell), the packed id,
    the visual / sound / light ids, the timer, the flags and the position again."""
    r.vec('position')
    r.id('caster')
    r.id('target')
    r.vec('targetPoint')
    r.i32('state')
    with r.scope('spell'):
        read_spell(r)
    r.x32('spellId')
    r.id('visual')
    r.id('sound')
    r.id('light')
    r.i32('timer')
    r.x32('flags')
    r.vec('position2')


# ---- the AI events (ai_events.cpp) ------------------------------------------------------------------------

def read_time_wheel(r):
    """AIEventTable::Save (005CAFA0): per cell (rows, then columns) and side, a 1 byte and the event; 0 at
    the end."""
    n = 0
    while r.u8(None):
        with r.scope(f'event{n}'):
            r.f32('x')
            r.f32('y')
            r.f32('originX')
            r.f32('originY')
            r.i32('expiry')
            r.x32('payload0')
            r.x32('payload1')
            r.id('sourceId')
            r.x32('side')
        n += 1
    r.put('events', str(n))


# ---- the zone script (script_save.cpp, script_effects.cpp, script_vars.cpp) -----------------------------

def read_named(r, name, value):
    """ScriptNamedValues<T>::Save (004C2EF0 floats, 004C33C0 strings, 004C38C0 objects, 004C4330 groups):
    the count, then each value and its name (a lone 0 byte when the name array is shorter)."""
    with r.scope(name):
        count = r.i32('count')
        for i in range(count):
            with r.scope(i):
                value(r)
                r.string('name')


def read_group(r):
    n = r.i32('size')
    for k in range(n):
        r.id(f'object{k}')


def read_vars(r, name):
    """ScriptVars as the definition and run savers write them (WriteVars, script_save.cpp): floats, strings,
    objects (ScriptObjectRef::Save 004B25F0, each with its name), groups (004C4330)."""
    with r.scope(name):
        read_named(r, 'floats', lambda r: r.f32('value'))
        read_named(r, 'strings', lambda r: r.string('value'))
        read_named(r, 'objects', lambda r: r.id('value'))
        read_named(r, 'groups', read_group)


def read_command(r, depth=0):
    """SSCCmd::Save (004B6360): id, the float and string arguments, the object and group argument counts,
    the local and global bindings (ScriptBinding: type, argIndex, varIndex), the nested calls, the called
    script's id."""
    if depth > 64:
        r.fail('commands nested too deep')
    r.i32('id')
    n = r.i32('floats')
    for i in range(n):
        r.f32(f'float{i}')
    n = r.i32('strings')
    for i in range(n):
        r.string(f'string{i}')
    r.i32('objects')
    r.i32('groups')
    for kind in ('local', 'global'):
        n = r.i32(kind + 'Bindings')
        for i in range(n):
            with r.scope(f'{kind}{i}'):
                r.i32('type')
                r.i32('argIndex')
                r.i32('varIndex')
    n = r.i32('subCalls')
    for i in range(n):
        with r.scope(f'sub{i}'):
            r.i32('type')
            r.i32('argIndex')
            read_command(r, depth + 1)
    r.i32('calledScript')


def read_block(r):
    """ScriptDecl::Save (004C1230): the id, the command count, each command."""
    r.i32('id')
    n = r.i32('commands')
    for i in range(n):
        with r.scope(f'cmd{i}'):
            read_command(r)


def read_definition(r):
    """ScriptDefinition::Save (004BBFE0): index, name, parameter types, the if-then pairs' block ids, the
    variables."""
    r.i32('index')
    r.string('name')
    r.string('params')
    n = r.i32('ifThen')
    for i in range(n):
        r.i32(f'ifThen{i}.conditions')
        r.i32(f'ifThen{i}.commands')
    read_vars(r, 'vars')


def read_run(r):
    """ScriptRun::Save (004BB320): block id, index, ticks, sleep start, the instance's index (-1: none), the
    variables."""
    r.i32('block')
    r.i32('index')
    r.i32('ticks')
    r.i32('sleepStart')
    r.i32('instance')
    read_vars(r, 'vars')


def read_list(r, name, reader):
    with r.scope(name):
        count = r.i32('count')
        for i in range(count):
            with r.scope(i):
                reader(r)


def read_script(r):
    """CScript::SaveState (004C0F90): the script's point lights (004B1940), particle sources (004B1A80), sound
    sources (004B1C50), lightning (004B1DB0), the next ids, the command blocks and WorldScript blocks, the
    definitions, the globals, the instances, the runs, two counters and the areas (004CAEC0)."""
    def point_light(r):
        r.id('object')
        r.i32('id')
        r.i32('removeWhenDone')
        r.i32('moving')
        r.i32('steps')
        r.vec('step')

    def particle(r):
        r.id('object')
        r.i32('id')
        r.i32('deleteAfterMove')
        r.i32('moving')
        r.i32('steps')
        r.vec('step')
        r.i32('lifetime')

    def fx(r):
        r.id('object')
        r.i32('id')
        r.string('name')
        r.vec('position')
        r.f32('volume')
        r.f32('radius')
        r.i32('idle')
        r.f32('spread')
        r.i32('minDelay')
        r.i32('maxDelay')
        r.i32('countdown')

    def lightning(r):
        r.id('object')
        r.i32('id')

    # The 256 point light slots: the free ones (object -1, every field 0) are only counted.
    with r.scope('pointLights'):
        count = r.i32('count')
        free = 0
        for i in range(count):
            if r.d[r.p:r.p + 0x20] == b'\xff\xff\xff\xff' + bytes(0x1C):
                r.take(0x20)
                free += 1
                continue
            with r.scope(i):
                point_light(r)
        r.put('free', str(free))
    read_list(r, 'particles', particle)
    read_list(r, 'fxSources', fx)
    read_list(r, 'lightning', lightning)
    r.i32('nextParticleId')
    r.i32('nextFxId')
    read_list(r, 'blocks', read_block)
    read_list(r, 'worldBlocks', read_block)
    read_list(r, 'definitions', read_definition)
    with r.scope('globals'):
        read_named(r, 'floats', lambda r: r.f32('value'))
        read_named(r, 'strings', lambda r: r.string('value'))
        read_named(r, 'objects', lambda r: r.id('value'))
        read_named(r, 'groups', read_group)
    read_list(r, 'instances', read_definition)
    read_list(r, 'runs', read_run)
    r.i32('loaded')
    r.i32('fileCount')

    def area(r):
        r.i32('id')
        n = r.i32('parts')
        for k in range(n):
            with r.scope(f'part{k}'):
                r.u8('isRect')
                r.junk('pad', 3, 'ScriptAreaPart +0x01..+0x03: AddRect / AddRound (004B1ED0 / 004B2070) set '
                       'only the isRect byte of their stack part; the padding is stack leftovers copied with it')
                r.f32('x')
                r.f32('y')
                r.f32('x2OrRadiusSquared')
                r.f32('y2')

    read_list(r, 'areas', area)


def read_command_queue(r):
    """The zone script (CWorldServer +0x12874), CScript::SaveState (004C0F90)."""
    read_script(r)


# ---- diplomacy and countdowns ---------------------------------------------------------------------------------

def read_diplomacy(r):
    """CDiplomacy::WriteTo (004557F0): the allied and hostile masks of the 32 players."""
    for table in ('allied', 'hostile'):
        with r.scope(table):
            for i in range(32):
                r.x32(i)


def read_countdowns(r):
    """TweenSave (006D47E0, model_db.cpp): the count, then the 16-byte TweenEntry records."""
    count = r.i32('count')
    for i in range(count):
        with r.scope(i):
            r.i32('key')
            r.i32('steps')
            r.f32('value')
            r.f32('step')
