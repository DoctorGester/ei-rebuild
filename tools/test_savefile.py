"""Tests of tools/savefile.py's frame, coder and readers (no game files needed): python tools/test_savefile.py"""
import os, struct, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import savefile


def literal_stream(data):
    """The LZ coder's output for `data` as literals only: the size, then per byte a 0 bit and the byte (the bit
    cursor reserves a byte for each eight flag bits, literal bytes follow the flag byte they come after)."""
    out = bytearray(struct.pack('<i', len(data)))
    for start in range(0, len(data), 8):
        chunk = data[start:start + 8]
        out.append(0)            # eight 0 flag bits (one per literal)
        out += chunk
    return bytes(out)


class SaveFileTest(unittest.TestCase):
    def test_literals_decompress(self):
        data = bytes(range(20)) + b'hello world'
        self.assertEqual(savefile.decompress(literal_stream(data)), data)

    def test_match_copies_from_the_window(self):
        # 'ab' as literals, then a match of length 4 at offset 1 (distance 2): bits LSB first.
        # flag 0, 'a'; flag 0, 'b'; flag 1, length 4 = bits 0,1; offset 1 (distance 2) = prefix 0,0,1,1,1, then 1.
        bits = [0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1]
        flags = 0
        for i, b in enumerate(bits[:8]):
            flags |= b << i
        # the byte order: the first flag byte, 'a' after the first bit, 'b' after the second, then the next byte
        stream = struct.pack('<i', 6) + bytes([flags]) + b'a' + b'b' + bytes([bits[8] | bits[9] << 1 | bits[10] << 2])
        self.assertEqual(savefile.decompress(stream), b'ababab')

    def test_frame_uncompressed_and_checksum(self):
        plain = b'\x01\x02\x03\x04\x05\x06\x07\x08'
        data = struct.pack('<IIIB', savefile.MAGIC_SCENARIO, 0x74, savefile.checksum(plain), 0) + plain
        self.assertEqual(savefile.read_frame(data), (savefile.MAGIC_SCENARIO, 0x74, plain))
        bad = struct.pack('<IIIB', savefile.MAGIC_SCENARIO, 0x74, 0, 0) + plain
        with self.assertRaises(savefile.DecodeError):
            savefile.read_frame(bad)

    def test_diff_runs(self):
        # 3 equal, 2 changed (aa bb), 255 equal then an empty changed run, 1 equal, then the end mark.
        diff = bytes([3, 2, 0xAA, 0xBB, 255, 0, 1, 0])
        r = savefile.Reader(diff + b'\x7f', 0x74)
        r.entity('aigraph')
        savefile.read_diff(r, 'diff')
        self.assertEqual(r.entities[0][2], [('diff.size', '261'), ('diff.+3', 'aabb')])
        self.assertEqual(r.p, len(diff))

    def test_rules_and_identities(self):
        r = savefile.Reader(struct.pack('<IIIII', 0x10AE8238, 0, 0x10AE8238, 0x10B00000, 7), 0x74)
        r.entity('party')
        r.identity('a', 'test')
        r.identity('b', 'test')
        r.identity('c', 'test')
        r.addr('d', 'test')
        r.junk('e', 4, 'test')
        self.assertEqual([v for _, v in r.entities[0][2]], ['p1', '0', 'p1', 'p2', '~junk'][:3] + ['~addr', '~junk'])
        self.assertEqual(savefile.EXCLUDED[('e', 'junk')], 'test')

    def test_differences_in_order(self):
        a = ['+ obj1 unit', 'obj1 hp 1.0', 'obj1 mana 2.0', '+ obj2 tree', 'obj2 x 1']
        b = ['+ obj1 unit', 'obj1 hp 1.0', 'obj1 mana 3.0', '+ obj3 tree', 'obj3 x 1']
        self.assertEqual(savefile.differences(a, b),
                         [('obj1', 'mana', '2.0', '3.0'), ('obj2', 'x', '1', '(none)'), ('obj3', 'x', '(none)', '1')])

    def test_mp_hero_decodes(self):
        # make_mp_hero's files decode to the end (the party reader takes every byte), the strong one too.
        import make_mp_hero
        for data in (make_mp_hero.hero_file(), make_mp_hero.hero_file('peer', 1000, 60, 60, 60, 300, 100)):
            entities = savefile.decode_data(data, 'hero')
            self.assertEqual(entities[0][1], f'magic {savefile.MAGIC_PARTY:#x}')
            self.assertGreater(len(savefile.lines(entities)), 100)


if __name__ == '__main__':
    unittest.main()
