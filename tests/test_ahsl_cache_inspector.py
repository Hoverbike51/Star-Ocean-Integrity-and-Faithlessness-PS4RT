"""Tests for the read-only PS4 AHSL cache inspector."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest

from paths import ROOT

sys.path.insert(0, str(ROOT / "tools"))
from ahsl_cache_inspector import (  # noqa: E402
    AhslCache,
    AhslFormatError,
    BINARY_INFO_SIGNATURE,
    CACHE_HEADER_SIZE,
    DICTIONARY_SIZE,
    crc16_x25,
)


def literal_stream(payload):
    if len(payload) % 2 or len(payload) > 30:
        raise ValueError("the test encoder accepts 0 to 30 even payload bytes")
    pairs = len(payload) // 2
    flags = (1 << pairs) - 1
    return flags.to_bytes(2, "big") + payload + b"\0\0"


def fixture_cache(payload=b"ABCD"):
    dictionary = bytes((index * 17) & 0xFF for index in range(DICTIONARY_SIZE))
    crc = crc16_x25(dictionary)
    key = b"\x01\x02\x04\x00"
    coded_offset = 12 + len(key)
    stream = literal_stream(payload)
    stored_size = coded_offset + len(stream)
    decoded_size = coded_offset + len(payload)
    record = struct.pack(
        "<6H", 0x1234, stored_size, decoded_size, coded_offset, crc, 0x83)
    record += key + stream
    header = struct.pack("<4sIIII12s", b"OSHA", 0, 0x00010086, 1, 0, bytes(12))
    return header + dictionary + record


class AhslCacheInspectorTests(unittest.TestCase):
    def open_fixture(self, data):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "AHSLDiskCachePs4"
        path.write_bytes(data)
        cache = AhslCache(path)
        self.addCleanup(cache.close)
        return cache

    def test_valid_cache_walks_exactly_and_decodes(self):
        cache = self.open_fixture(fixture_cache())
        self.assertEqual(cache.header.record_count, 1)
        self.assertEqual(cache.records[0].offset, CACHE_HEADER_SIZE + DICTIONARY_SIZE)
        self.assertTrue(cache.decode_record(cache.records[0]).endswith(b"ABCD"))
        summary = cache.summary()
        self.assertEqual(summary["records"]["parsed"], 1)
        self.assertTrue(summary["records"]["exactly_tiles_file"])

    def test_deep_scan_indexes_orbshdr_binary_information(self):
        packed = 1 | (1 << 1) | (2 << 2) | (3 << 6) | (0x120 << 8)
        binary_info = BINARY_INFO_SIGNATURE + bytes([1]) + struct.pack(
            "<I4BQ I", packed, 4, 5, 7, 0, 0x1122334455667788, 0xAABBCCDD)
        self.assertEqual(len(binary_info), 28)
        cache = self.open_fixture(fixture_cache(binary_info))
        deep = cache.summary(deep=True)["deep_scan"]
        self.assertEqual(deep["orbshdr_headers"], 1)
        self.assertEqual(deep["unique_shader_hashes"], 1)
        self.assertEqual(deep["shader_types"], {"2: export": 1})
        self.assertEqual(deep["source_types"], {"3": 1})

    def test_wrong_magic_is_rejected(self):
        data = bytearray(fixture_cache())
        data[:4] = b"NOPE"
        with self.assertRaises(AhslFormatError):
            self.open_fixture(bytes(data))

    def test_trailing_data_is_rejected(self):
        with self.assertRaises(AhslFormatError):
            self.open_fixture(fixture_cache() + b"extra")

    def test_dictionary_crc_mismatch_is_rejected(self):
        data = bytearray(fixture_cache())
        data[0x40] ^= 1
        with self.assertRaises(AhslFormatError):
            self.open_fixture(bytes(data))


if __name__ == "__main__":
    unittest.main()
