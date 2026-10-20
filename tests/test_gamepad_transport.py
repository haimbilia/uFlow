import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('transport', Path(__file__).resolve().parents[1]/'tools/inspect-gamepad-transport.py')
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)

def elf():
    data = bytearray(128)
    data[:6] = b'\x7fELF\x01\x02'
    struct.pack_into('>I', data, 28, 52)
    struct.pack_into('>HH', data, 42, 32, 1)
    struct.pack_into('>8I', data, 52, 1, 96, 0x138b0000, 0, 32, 64, 5, 4)
    struct.pack_into('>I', data, 96, 0x0d000570)
    return data

class TransportTest(unittest.TestCase):
    def test_embedded_address_and_no_input_changes(self):
        data = b'prefix' + elf()
        before = bytes(data)
        result = transport.inspect(data)
        self.assertEqual(result['elf_offset'], 6)
        self.assertEqual(result['segments'][0]['register_literals'], {'0d000570':['138b0000']})
        self.assertEqual(data, before)

    def test_reject_malformed(self):
        for offset, value, fmt in [(4, 2, 'B'), (42, 28, '>H'), (44, 300, '>H'),
                                   (56, 127, '>I'), (68, 65, '>I'), (60, 0xfffffff0, '>I')]:
            data = elf(); struct.pack_into(fmt, data, offset, value)
            with self.assertRaises(ValueError): transport.inspect(data)
        with self.assertRaises(ValueError): transport.inspect(b'not firmware')

if __name__ == '__main__': unittest.main()
