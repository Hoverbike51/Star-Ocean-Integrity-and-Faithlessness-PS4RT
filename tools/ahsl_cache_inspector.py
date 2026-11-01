#!/usr/bin/env python3
"""Inspect Star Ocean 5's PS4 AHSL shader cache without modifying it.

This is a developer diagnostic tool. It is not used by the player build and
does not add a Python dependency to Star Ocean 5 Launcher.exe or Star Ocean 5.exe.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import hashlib
import json
import mmap
import struct
import sys
from pathlib import Path


CACHE_MAGIC = b"OSHA"
CACHE_HEADER_SIZE = 0x20
DICTIONARY_SIZE = 0x2000
RECORDS_OFFSET = CACHE_HEADER_SIZE + DICTIONARY_SIZE
HEADER = struct.Struct("<4sIIII12s")
RECORD = struct.Struct("<6H")
BINARY_INFO_SIGNATURE = b"OrbShdr"
BINARY_INFO_SIZE = 28
SHADER_TYPE_NAMES = {
    0: "pixel",
    1: "vertex",
    2: "export",
    3: "local",
    4: "compute",
    5: "geometry",
    7: "hull",
    8: "domain-vertex",
}


class AhslFormatError(ValueError):
    """Raised when an AHSL cache fails structural validation."""


@dataclasses.dataclass(frozen=True)
class CacheHeader:
    magic: bytes
    header_version: int
    format_version: int
    record_count: int
    flags: int


@dataclasses.dataclass(frozen=True)
class RecordMetadata:
    index: int
    offset: int
    key_hash: int
    stored_size: int
    decoded_size: int
    coded_offset: int
    dictionary_crc: int
    flags: int


def crc16_x25(data: bytes | memoryview) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def decode_wide_lz77(source: bytes | memoryview, history: bytes | memoryview,
                     expected_size: int) -> tuple[bytes, int]:
    """Decode tri-Ace's halfword LZ77 stream used by PS4 AHSL records.

    Cache metadata is little-endian, while compression flags and tokens remain
    big-endian. The 8 KiB dictionary is preloaded into the history window and
    is not returned.
    """
    output = bytearray(history)
    history_size = len(output)
    cursor = 0
    flags = 0
    flags_left = 0
    ended = False

    while cursor + 1 < len(source):
        if flags_left == 0:
            flags = int.from_bytes(source[cursor:cursor + 2], "big")
            cursor += 2
            flags_left = 16
            if cursor + 1 >= len(source):
                break

        literal = flags & 1
        flags >>= 1
        flags_left -= 1
        if literal:
            output += source[cursor:cursor + 2]
            cursor += 2
        else:
            token = int.from_bytes(source[cursor:cursor + 2], "big")
            cursor += 2
            distance = token & 0x0FFF
            if distance == 0:
                ended = True
                break
            start = len(output) - distance * 2
            length = ((token >> 12) + 2) * 2
            if start < 0:
                raise AhslFormatError("compressed record reaches before its dictionary")
            if start + length <= len(output):
                output += output[start:start + length]
            else:
                for index in range(length):
                    output.append(output[start + index])

        if len(output) - history_size > expected_size:
            raise AhslFormatError("compressed record exceeds its declared decoded size")

    decoded = bytes(output[history_size:])
    if not ended:
        raise AhslFormatError("compressed record has no terminating token")
    if len(decoded) != expected_size:
        raise AhslFormatError(
            f"decoded record size is {len(decoded)}, expected {expected_size}")
    return decoded, cursor


def parse_binary_info(data: bytes, offset: int) -> dict[str, int]:
    if offset < 0 or offset + BINARY_INFO_SIZE > len(data):
        raise AhslFormatError("truncated OrbShdr binary information")
    if data[offset:offset + len(BINARY_INFO_SIGNATURE)] != BINARY_INFO_SIGNATURE:
        raise AhslFormatError("invalid OrbShdr signature")

    version = data[offset + 7]
    packed = struct.unpack_from("<I", data, offset + 8)[0]
    chunk_usage_offset = data[offset + 12]
    input_usage_slots = data[offset + 13]
    usage_flags = data[offset + 14]
    shader_hash = struct.unpack_from("<Q", data, offset + 16)[0]
    crc32 = struct.unpack_from("<I", data, offset + 24)[0]
    return {
        "version": version,
        "pssl_or_cg": packed & 1,
        "cached": (packed >> 1) & 1,
        "type": (packed >> 2) & 0xF,
        "source_type": (packed >> 6) & 0x3,
        "code_length": packed >> 8,
        "chunk_usage_offset_dwords": chunk_usage_offset,
        "input_usage_slots": input_usage_slots,
        "is_srt": usage_flags & 1,
        "is_srt_used_info_valid": (usage_flags >> 1) & 1,
        "is_extended_usage_info": (usage_flags >> 2) & 1,
        "shader_hash": shader_hash,
        "crc32": crc32,
    }


class AhslCache:
    def __init__(self, path: Path | str):
        self.path = Path(path)
        self._file = self.path.open("rb")
        self.data = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            self.header = self._read_header()
            self.dictionary = memoryview(self.data)[CACHE_HEADER_SIZE:RECORDS_OFFSET]
            self.dictionary_crc = crc16_x25(self.dictionary)
            self.records = self._walk_records()
        except Exception:
            self.close()
            raise

    def close(self) -> None:
        dictionary = getattr(self, "dictionary", None)
        if dictionary is not None:
            dictionary.release()
            self.dictionary = None
        data = getattr(self, "data", None)
        if data is not None:
            data.close()
            self.data = None
        file_handle = getattr(self, "_file", None)
        if file_handle is not None:
            file_handle.close()
            self._file = None

    def __enter__(self) -> "AhslCache":
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    def _read_header(self) -> CacheHeader:
        if len(self.data) < RECORDS_OFFSET:
            raise AhslFormatError("file is too small to contain an AHSL cache")
        magic, header_version, format_version, count, flags, reserved = HEADER.unpack_from(
            self.data, 0)
        if magic != CACHE_MAGIC:
            raise AhslFormatError(
                f"invalid AHSL cache magic {magic!r}; expected {CACHE_MAGIC!r}")
        if any(reserved):
            raise AhslFormatError("AHSL header reserved bytes are not zero")
        if count == 0:
            raise AhslFormatError("AHSL cache declares no records")
        return CacheHeader(magic, header_version, format_version, count, flags)

    def _walk_records(self) -> list[RecordMetadata]:
        records = []
        offset = RECORDS_OFFSET
        file_size = len(self.data)
        for index in range(self.header.record_count):
            if offset + RECORD.size > file_size:
                raise AhslFormatError(f"record {index} header is outside the cache")
            values = RECORD.unpack_from(self.data, offset)
            metadata = RecordMetadata(index, offset, *values)
            if metadata.stored_size < RECORD.size:
                raise AhslFormatError(f"record {index} is smaller than its header")
            if not RECORD.size <= metadata.coded_offset <= metadata.stored_size:
                raise AhslFormatError(f"record {index} has an invalid coded offset")
            if metadata.decoded_size < metadata.coded_offset:
                raise AhslFormatError(f"record {index} has an invalid decoded size")
            if metadata.dictionary_crc != self.dictionary_crc:
                raise AhslFormatError(
                    f"record {index} dictionary CRC is 0x{metadata.dictionary_crc:04X}, "
                    f"expected 0x{self.dictionary_crc:04X}")
            end = offset + metadata.stored_size
            if end > file_size:
                raise AhslFormatError(f"record {index} extends beyond the cache")

            key_storage_size = metadata.coded_offset - RECORD.size
            key_length_modulo = self.data[offset + 0x0E]
            if ((key_length_modulo + 3) & ~3) & 0xFF != key_storage_size & 0xFF:
                raise AhslFormatError(f"record {index} key length is inconsistent")

            records.append(metadata)
            offset = end

        if offset != file_size:
            raise AhslFormatError(
                f"record walk ends at 0x{offset:X}, file ends at 0x{file_size:X}")
        return records

    def decode_record(self, metadata: RecordMetadata) -> bytes:
        start = metadata.offset
        coded = start + metadata.coded_offset
        end = start + metadata.stored_size
        decoded_tail, consumed = decode_wide_lz77(
            memoryview(self.data)[coded:end], self.dictionary,
            metadata.decoded_size - metadata.coded_offset)
        if consumed != metadata.stored_size - metadata.coded_offset:
            raise AhslFormatError(
                f"record {metadata.index} consumed {consumed} compressed bytes, "
                f"expected {metadata.stored_size - metadata.coded_offset}")
        return self.data[start:coded] + decoded_tail

    def summary(self, deep: bool = False) -> dict:
        stored_sizes = [record.stored_size for record in self.records]
        decoded_sizes = [record.decoded_size for record in self.records]
        coded_offsets = [record.coded_offset for record in self.records]
        flags = collections.Counter(record.flags for record in self.records)
        result = {
            "format": "PS4 AHSL cache index 1",
            "source": str(self.path.resolve()),
            "file_size": len(self.data),
            "file_sha256": hashlib.sha256(self.data).hexdigest(),
            "header": {
                "magic_on_disk": self.header.magic.decode("ascii"),
                "semantic_magic": "AHSO (little-endian)",
                "header_version": self.header.header_version,
                "format_version": f"0x{self.header.format_version:08X}",
                "record_count": self.header.record_count,
                "flags": f"0x{self.header.flags:08X}",
            },
            "dictionary": {
                "offset": f"0x{CACHE_HEADER_SIZE:X}",
                "size": DICTIONARY_SIZE,
                "crc16_x25": f"0x{self.dictionary_crc:04X}",
            },
            "records": {
                "offset": f"0x{RECORDS_OFFSET:X}",
                "parsed": len(self.records),
                "compressed": sum(record.stored_size != record.decoded_size
                                  for record in self.records),
                "stored": sum(record.stored_size == record.decoded_size
                              for record in self.records),
                "stored_size": {"min": min(stored_sizes), "max": max(stored_sizes)},
                "decoded_size": {"min": min(decoded_sizes), "max": max(decoded_sizes)},
                "coded_offset": {"min": min(coded_offsets), "max": max(coded_offsets)},
                "flags": {f"0x{key:04X}": value for key, value in sorted(flags.items())},
                "exactly_tiles_file": True,
            },
        }
        if deep:
            result["deep_scan"] = self._deep_scan()
        return result

    def _deep_scan(self) -> dict:
        signatures_per_record = collections.Counter()
        shader_types = collections.Counter()
        binary_versions = collections.Counter()
        source_types = collections.Counter()
        hashes = set()
        headers = 0

        for metadata in self.records:
            body = self.decode_record(metadata)
            offsets = []
            cursor = 0
            while True:
                cursor = body.find(BINARY_INFO_SIGNATURE, cursor)
                if cursor < 0:
                    break
                offsets.append(cursor)
                cursor += len(BINARY_INFO_SIGNATURE)
            signatures_per_record[len(offsets)] += 1
            for offset in offsets:
                info = parse_binary_info(body, offset)
                headers += 1
                shader_types[info["type"]] += 1
                binary_versions[info["version"]] += 1
                source_types[info["source_type"]] += 1
                hashes.add(info["shader_hash"])

        return {
            "records_decoded": len(self.records),
            "orbshdr_headers": headers,
            "unique_shader_hashes": len(hashes),
            "headers_per_record": {str(key): value
                                   for key, value in sorted(signatures_per_record.items())},
            "binary_versions": {str(key): value
                                for key, value in sorted(binary_versions.items())},
            "shader_types": {
                f"{key}: {SHADER_TYPE_NAMES.get(key, 'unknown')}": value
                for key, value in sorted(shader_types.items())
            },
            "source_types": {str(key): value for key, value in sorted(source_types.items())},
        }


def print_human(summary: dict) -> None:
    header = summary["header"]
    records = summary["records"]
    dictionary = summary["dictionary"]
    print("PS4 AHSL cache")
    print(f"  Source: {summary['source']}")
    print(f"  Size: {summary['file_size']} bytes")
    print(f"  Magic: {header['semantic_magic']} ({header['magic_on_disk']} on disk)")
    print(f"  Format version: {header['format_version']}")
    print(f"  Records: {records['parsed']} (exact file walk)")
    print(f"  Compressed records: {records['compressed']}")
    print(f"  Dictionary CRC-16/X.25: {dictionary['crc16_x25']}")
    if "deep_scan" in summary:
        deep = summary["deep_scan"]
        print("Deep scan")
        print(f"  Decoded records: {deep['records_decoded']}")
        print(f"  OrbShdr headers: {deep['orbshdr_headers']}")
        print(f"  Unique shader hashes: {deep['unique_shader_hashes']}")
        print(f"  Headers per record: {deep['headers_per_record']}")
        print(f"  Shader types: {deep['shader_types']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate and index Star Ocean 5's PS4 AHSL shader cache.")
    parser.add_argument("cache", type=Path, help="path to AHSLDiskCachePs4")
    parser.add_argument(
        "--deep", action="store_true",
        help="decompress every record and index embedded OrbShdr headers")
    parser.add_argument(
        "--json", type=Path,
        help="write the metadata index as JSON (no shader payload is exported)")
    args = parser.parse_args(argv)

    try:
        with AhslCache(args.cache) as cache:
            summary = cache.summary(deep=args.deep)
    except (AhslFormatError, OSError) as error:
        print(f"AHSL cache inspection failed: {error}", file=sys.stderr)
        return 1

    print_human(summary)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(f"Metadata index written to {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
