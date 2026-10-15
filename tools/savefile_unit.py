"""Units in a decoded save (tools/savefile.py): CUnit::SaveUnit (005321C0) and what it calls."""
import savefile_objects as objects


def read_unit(r):
    objects.read_map_object_base(r)
    r.fail('the unit controller is not decoded yet')
