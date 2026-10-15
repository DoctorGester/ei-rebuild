"""The managed objects (timed objects, spells, items) and the world's lists in a decoded save (tools/savefile.py)."""


def read_managed(r, manager, kind):
    r.fail(f'{manager} object type {kind!r} is not decoded yet')


def read_projectile(r):
    r.fail('missiles are not decoded yet')


def read_time_wheel(r):
    r.fail('the time wheel is not decoded yet')


def read_command_queue(r):
    r.fail('the command queue is not decoded yet')


def read_diplomacy(r):
    r.fail('diplomacy is not decoded yet')


def read_countdowns(r):
    r.fail('countdowns are not decoded yet')
