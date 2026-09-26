"""Offline inspection regressions: no console or real volume is accessed."""
import importlib.util
import io
import pathlib
import struct
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


rpl = module("inspect-rpl")
ios = module("inspect-ios")
fat = module("inspect-fat32-hai")


class InspectionTests(unittest.TestCase):
    def test_rpl(self):
        source = bytearray(52)
        source[:6] = b"\x7fELF\x01\x02"
        struct.pack_into(">H", source, 16, 0xfe01)
        struct.pack_into(">I", source, 32, 52)
        struct.pack_into(">HH", source, 46, 40, 2)
        source.extend(bytes(40))
        data = struct.pack(">I", 4) + zlib.compress(b"TEST")
        source.extend(struct.pack(">10I", 0,1,0x08000006,0x2000000,132,len(data),0,0,4,0))
        source.extend(data)
        original = bytes(source)
        output = rpl.expand(source)
        self.assertEqual(bytes(source), original)
        self.assertEqual(struct.unpack_from(">H", output,16)[0],1)
        shoff = struct.unpack_from(">I",output,32)[0]
        header = struct.unpack_from(">10I",output,shoff+40)
        self.assertEqual(header[2],6)
        self.assertEqual(output[header[4]:header[4]+header[5]],b"TEST")
        with self.assertRaises(ValueError):
            rpl.expand(source[:-1])

    def test_ios_range(self):
        source = bytearray(52)
        source[:6] = b"\x7fELF\x01\x02"
        struct.pack_into(">I",source,28,52)
        struct.pack_into(">HH",source,42,32,1)
        source.extend(struct.pack(">8I",1,84,0x5000000,0x5000000,8,16,5,4))
        source.extend(b"12345678")
        self.assertEqual(ios.extract(source,0x5000002,3),b"345")
        for start, size in [(0x4ffffff,2),(0x5000007,2),(0x5000008,1)]:
            with self.assertRaises(ValueError):
                ios.extract(source,start,size)
        with self.assertRaises(ValueError):
            ios.extract(source[:-4],0x5000000,8)

    def fixture(self, root=b"", links=None):
        reader = object.__new__(fat.Fat32)
        reader.file = io.BytesIO(bytes(512)+root.ljust(512,b"\0"))
        reader.data = 1
        reader.cluster_sectors = 1
        reader.cluster_count = 65526
        reader.root = 2
        table = bytearray(512)
        for index, value in (links or {2:0x0fffffff}).items():
            struct.pack_into("<I",table,index*4,value)
        reader.fat_sector = lambda _: table
        return reader

    def test_chain(self):
        reader = self.fixture(links={2:4,4:3,3:0x0fffffff})
        self.assertEqual(list(reader.chain(2)),[2,4,3])
        for links in [{2:2},{2:4,4:2},{2:0},{2:0x0ffffff7},{2:70000}]:
            with self.assertRaises(ValueError):
                list(self.fixture(links=links).chain(2))

    def test_directory_and_lfn(self):
        short = bytearray(32)
        short[:11] = b"MYGAME~1   "
        short[11] = 16
        struct.pack_into("<H",short,26,5)
        checksum = 0
        for byte in short[:11]:
            checksum = (((checksum & 1)<<7)+(checksum>>1)+byte)&255
        name = "My Game [USA] é"
        encoded = name.encode("utf-16-le") + b"\0\0"
        encoded += b"\xff\xff" * ((-len(encoded)//2)%13)
        slots = []
        for i in range(len(encoded)//26):
            slot = bytearray(32)
            slot[0] = i+1
            slot[11], slot[13] = 15,checksum
            chunk = encoded[i*26:(i+1)*26]
            slot[1:11],slot[14:26],slot[28:32] = chunk[:10],chunk[10:22],chunk[22:]
            slots.append(slot)
        slots[-1][0] |= 64
        raw = b"".join(reversed(slots)) + short
        reader = self.fixture(raw)
        self.assertEqual(list(reader.entries(2)),[(name,16,5,0)])
        self.assertEqual(reader.directory(name.lower()),5)
        corrupt = bytearray(raw)
        corrupt[13] ^= 1
        with self.assertRaises(ValueError):
            list(self.fixture(corrupt).entries(2))


if __name__ == "__main__":
    unittest.main()
