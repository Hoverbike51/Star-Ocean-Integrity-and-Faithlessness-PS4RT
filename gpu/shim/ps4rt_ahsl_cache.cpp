// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4rt_ahsl_cache.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace Ps4Rt::Ahsl {
namespace {

constexpr std::array<std::byte, 7> BinarySignature = {
    std::byte{'O'}, std::byte{'r'}, std::byte{'b'}, std::byte{'S'},
    std::byte{'h'}, std::byte{'d'}, std::byte{'r'},
};
constexpr std::uint32_t GcnStartToken = 0xBEEB03FF;

std::uint16_t ReadLe16(std::span<const std::byte> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw FormatError("truncated 16-bit AHSL field");
    }
    return std::uint16_t(std::to_integer<std::uint8_t>(data[offset])) |
           (std::uint16_t(std::to_integer<std::uint8_t>(data[offset + 1])) << 8);
}

std::uint32_t ReadLe32(std::span<const std::byte> data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        throw FormatError("truncated 32-bit AHSL field");
    }
    std::uint32_t result{};
    for (unsigned i = 0; i < 4; ++i) {
        result |= std::uint32_t(std::to_integer<std::uint8_t>(data[offset + i])) << (i * 8);
    }
    return result;
}

std::uint64_t ReadLe64(std::span<const std::byte> data, std::size_t offset) {
    if (offset + 8 > data.size()) {
        throw FormatError("truncated 64-bit AHSL field");
    }
    std::uint64_t result{};
    for (unsigned i = 0; i < 8; ++i) {
        result |= std::uint64_t(std::to_integer<std::uint8_t>(data[offset + i])) << (i * 8);
    }
    return result;
}

std::uint16_t ReadBe16(std::span<const std::byte> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw FormatError("truncated AHSL compressed stream");
    }
    return (std::uint16_t(std::to_integer<std::uint8_t>(data[offset])) << 8) |
           std::uint16_t(std::to_integer<std::uint8_t>(data[offset + 1]));
}

std::string RecordPrefix(std::uint32_t index) {
    return "AHSL record " + std::to_string(index) + ": ";
}

} // namespace

std::uint16_t Crc16X25(std::span<const std::byte> data) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (const std::byte value : data) {
        crc ^= std::to_integer<std::uint8_t>(value);
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? std::uint16_t((crc >> 1) ^ 0x8408) : std::uint16_t(crc >> 1);
        }
    }
    return crc ^ 0xFFFF;
}

std::vector<std::byte> DecodeWideLz77(std::span<const std::byte> source,
                                     std::span<const std::byte> history,
                                     std::size_t expected_size, std::size_t* consumed) {
    std::vector<std::byte> output(history.begin(), history.end());
    const std::size_t history_size = output.size();
    std::size_t cursor{};
    std::uint16_t flags{};
    unsigned flags_left{};
    bool ended{};

    while (cursor + 1 < source.size()) {
        if (flags_left == 0) {
            flags = ReadBe16(source, cursor);
            cursor += 2;
            flags_left = 16;
            if (cursor + 1 >= source.size()) {
                break;
            }
        }

        const bool literal = (flags & 1) != 0;
        flags >>= 1;
        --flags_left;
        if (literal) {
            if (cursor + 2 > source.size()) {
                throw FormatError("truncated AHSL literal");
            }
            output.push_back(source[cursor]);
            output.push_back(source[cursor + 1]);
            cursor += 2;
        } else {
            const std::uint16_t token = ReadBe16(source, cursor);
            cursor += 2;
            const std::size_t distance = token & 0x0FFF;
            if (distance == 0) {
                ended = true;
                break;
            }
            const std::size_t distance_bytes = distance * 2;
            if (distance_bytes > output.size()) {
                throw FormatError("compressed AHSL record reaches before its dictionary");
            }
            const std::size_t start = output.size() - distance_bytes;
            const std::size_t length = ((token >> 12) + 2) * 2;
            for (std::size_t i = 0; i < length; ++i) {
                output.push_back(output[start + i]);
            }
        }

        if (output.size() - history_size > expected_size) {
            throw FormatError("compressed AHSL record exceeds its declared decoded size");
        }
    }

    if (!ended) {
        throw FormatError("compressed AHSL record has no terminating token");
    }
    if (output.size() - history_size != expected_size) {
        throw FormatError("decoded AHSL record has an unexpected size");
    }
    if (consumed != nullptr) {
        *consumed = cursor;
    }
    output.erase(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(history_size));
    return output;
}

std::vector<BinaryInfo> ExtractBinaryInfo(std::span<const std::byte> decoded_record) {
    std::vector<BinaryInfo> binaries;
    for (std::size_t info_offset = 0; info_offset + BinarySignature.size() <= decoded_record.size();
         ++info_offset) {
        if (!std::equal(BinarySignature.begin(), BinarySignature.end(),
                        decoded_record.begin() + static_cast<std::ptrdiff_t>(info_offset))) {
            continue;
        }
        if (info_offset + BinaryInfoSize > decoded_record.size()) {
            throw FormatError("truncated OrbShdr binary information");
        }

        const std::uint32_t packed = ReadLe32(decoded_record, info_offset + 8);
        BinaryInfo info{
            .info_offset = info_offset,
            .version = std::to_integer<std::uint8_t>(decoded_record[info_offset + 7]),
            .pssl_or_cg = std::uint8_t(packed & 1),
            .cached = std::uint8_t((packed >> 1) & 1),
            .type = std::uint8_t((packed >> 2) & 0xF),
            .source_type = std::uint8_t((packed >> 6) & 0x3),
            .code_length = packed >> 8,
            .chunk_usage_offset_dwords =
                std::to_integer<std::uint8_t>(decoded_record[info_offset + 12]),
            .input_usage_slots = std::to_integer<std::uint8_t>(decoded_record[info_offset + 13]),
            .usage_flags = std::to_integer<std::uint8_t>(decoded_record[info_offset + 14]),
            .shader_hash = ReadLe64(decoded_record, info_offset + 16),
            .crc32 = ReadLe32(decoded_record, info_offset + 24),
        };
        if (info.code_length == 0 || (info.code_length & 3) != 0) {
            throw FormatError("OrbShdr declares an invalid GCN code length");
        }

        bool found_code{};
        if (info_offset >= info.code_length) {
            for (std::size_t candidate = info_offset - info.code_length + 1; candidate-- > 0;) {
                if (candidate + info.code_length > info_offset || candidate + 4 > decoded_record.size()) {
                    continue;
                }
                if (ReadLe32(decoded_record, candidate) == GcnStartToken) {
                    info.code_offset = candidate;
                    found_code = true;
                    break;
                }
            }
        }
        if (!found_code) {
            throw FormatError("OrbShdr GCN start token was not found");
        }
        binaries.push_back(info);
        info_offset += BinarySignature.size() - 1;
    }
    return binaries;
}

Cache::Cache(const std::filesystem::path& path) : path_{path}, file_{path, std::ios::binary} {
    if (!file_) {
        throw std::runtime_error("unable to open AHSL cache: " + path.string());
    }
    std::error_code error;
    file_size_ = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("unable to determine AHSL cache size: " + error.message());
    }
    if (file_size_ < RecordsOffset) {
        throw FormatError("file is too small to contain an AHSL cache");
    }

    const auto header = ReadAt(0, CacheHeaderSize);
    std::copy_n(reinterpret_cast<const char*>(header.data()), 4, header_.magic.begin());
    if (header_.magic != std::array<char, 4>{'O', 'S', 'H', 'A'}) {
        throw FormatError("invalid AHSL cache magic; expected OSHA on disk");
    }
    header_.header_version = ReadLe32(header, 4);
    header_.format_version = ReadLe32(header, 8);
    header_.record_count = ReadLe32(header, 12);
    header_.flags = ReadLe32(header, 16);
    if (std::any_of(header.begin() + 20, header.end(), [](std::byte value) { return value != std::byte{}; })) {
        throw FormatError("AHSL header reserved bytes are not zero");
    }
    if (header_.record_count == 0) {
        throw FormatError("AHSL cache declares no records");
    }
    if (header_.record_count > (file_size_ - RecordsOffset) / RecordHeaderSize) {
        throw FormatError("AHSL record count cannot fit in the file");
    }

    const auto dictionary = ReadAt(CacheHeaderSize, DictionarySize);
    std::copy(dictionary.begin(), dictionary.end(), dictionary_.begin());
    dictionary_crc_ = Crc16X25(dictionary_);

    records_.reserve(header_.record_count);
    std::uint64_t offset = RecordsOffset;
    for (std::uint32_t index = 0; index < header_.record_count; ++index) {
        if (offset + RecordHeaderSize > file_size_) {
            throw FormatError(RecordPrefix(index) + "header is outside the cache");
        }
        const auto bytes = ReadAt(offset, RecordHeaderSize);
        RecordMetadata record{
            .index = index,
            .offset = offset,
            .key_hash = ReadLe16(bytes, 0),
            .stored_size = ReadLe16(bytes, 2),
            .decoded_size = ReadLe16(bytes, 4),
            .coded_offset = ReadLe16(bytes, 6),
            .dictionary_crc = ReadLe16(bytes, 8),
            .flags = ReadLe16(bytes, 10),
        };
        const std::string prefix = RecordPrefix(index);
        if (record.stored_size < RecordHeaderSize) {
            throw FormatError(prefix + "stored size is smaller than its header");
        }
        if (record.coded_offset < RecordHeaderSize || record.coded_offset > record.stored_size) {
            throw FormatError(prefix + "coded offset is invalid");
        }
        if (record.decoded_size < record.coded_offset) {
            throw FormatError(prefix + "decoded size is invalid");
        }
        if (record.dictionary_crc != dictionary_crc_) {
            throw FormatError(prefix + "dictionary CRC does not match");
        }
        if (offset + record.stored_size > file_size_) {
            throw FormatError(prefix + "extends beyond the cache");
        }
        if (record.coded_offset <= 14) {
            throw FormatError(prefix + "does not contain a complete key header");
        }
        const auto key_header = ReadAt(offset + 14, 1);
        const std::size_t key_storage_size = record.coded_offset - RecordHeaderSize;
        const std::size_t key_length = std::to_integer<std::uint8_t>(key_header[0]);
        if (((key_length + 3) & ~std::size_t{3} & 0xFF) != (key_storage_size & 0xFF)) {
            throw FormatError(prefix + "key length is inconsistent");
        }
        records_.push_back(record);
        offset += record.stored_size;
    }
    if (offset != file_size_) {
        throw FormatError("AHSL record walk does not exactly tile the file");
    }
}

std::vector<std::byte> Cache::ReadAt(std::uint64_t offset, std::size_t size) const {
    if (offset > file_size_ || size > file_size_ - offset ||
        offset > std::uint64_t(std::numeric_limits<std::streamoff>::max())) {
        throw FormatError("AHSL read is outside the cache");
    }
    std::vector<std::byte> bytes(size);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("unable to read AHSL cache");
    }
    return bytes;
}

std::vector<std::byte> Cache::DecodeRecord(const RecordMetadata& record) const {
    if (record.index >= records_.size() || records_[record.index].offset != record.offset) {
        throw std::out_of_range("AHSL record does not belong to this cache");
    }
    auto stored = ReadAt(record.offset, record.stored_size);
    const auto compressed = std::span<const std::byte>{stored}.subspan(record.coded_offset);
    std::size_t consumed{};
    auto tail = DecodeWideLz77(compressed, dictionary_, record.decoded_size - record.coded_offset,
                               &consumed);
    if (consumed != compressed.size()) {
        throw FormatError(RecordPrefix(record.index) + "compressed byte count is inconsistent");
    }
    stored.resize(record.coded_offset);
    stored.insert(stored.end(), tail.begin(), tail.end());
    return stored;
}

std::vector<std::byte> Cache::DecodeRecord(std::size_t index) const {
    if (index >= records_.size()) {
        throw std::out_of_range("AHSL record index is outside the cache");
    }
    return DecodeRecord(records_[index]);
}

} // namespace Ps4Rt::Ahsl
