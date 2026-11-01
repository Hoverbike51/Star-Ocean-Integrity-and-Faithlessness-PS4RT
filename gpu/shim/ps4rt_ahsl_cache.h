// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace Ps4Rt::Ahsl {

inline constexpr std::size_t CacheHeaderSize = 0x20;
inline constexpr std::size_t DictionarySize = 0x2000;
inline constexpr std::size_t RecordsOffset = CacheHeaderSize + DictionarySize;
inline constexpr std::size_t RecordHeaderSize = 12;
inline constexpr std::size_t BinaryInfoSize = 28;

class FormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CacheHeader {
    std::array<char, 4> magic{};
    std::uint32_t header_version{};
    std::uint32_t format_version{};
    std::uint32_t record_count{};
    std::uint32_t flags{};
};

struct RecordMetadata {
    std::uint32_t index{};
    std::uint64_t offset{};
    std::uint16_t key_hash{};
    std::uint16_t stored_size{};
    std::uint16_t decoded_size{};
    std::uint16_t coded_offset{};
    std::uint16_t dictionary_crc{};
    std::uint16_t flags{};
};

struct BinaryInfo {
    std::size_t info_offset{};
    std::size_t code_offset{};
    std::uint8_t version{};
    std::uint8_t pssl_or_cg{};
    std::uint8_t cached{};
    std::uint8_t type{};
    std::uint8_t source_type{};
    std::uint32_t code_length{};
    std::uint8_t chunk_usage_offset_dwords{};
    std::uint8_t input_usage_slots{};
    std::uint8_t usage_flags{};
    std::uint64_t shader_hash{};
    std::uint32_t crc32{};
};

std::uint16_t Crc16X25(std::span<const std::byte> data) noexcept;

std::vector<std::byte> DecodeWideLz77(std::span<const std::byte> source,
                                     std::span<const std::byte> history,
                                     std::size_t expected_size,
                                     std::size_t* consumed = nullptr);

std::vector<BinaryInfo> ExtractBinaryInfo(std::span<const std::byte> decoded_record);

class Cache final {
public:
    explicit Cache(const std::filesystem::path& path);

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }
    [[nodiscard]] std::uint64_t FileSize() const noexcept { return file_size_; }
    [[nodiscard]] const CacheHeader& Header() const noexcept { return header_; }
    [[nodiscard]] std::uint16_t DictionaryCrc() const noexcept { return dictionary_crc_; }
    [[nodiscard]] const std::vector<RecordMetadata>& Records() const noexcept { return records_; }

    [[nodiscard]] std::vector<std::byte> DecodeRecord(const RecordMetadata& record) const;
    [[nodiscard]] std::vector<std::byte> DecodeRecord(std::size_t index) const;

private:
    [[nodiscard]] std::vector<std::byte> ReadAt(std::uint64_t offset, std::size_t size) const;

    std::filesystem::path path_;
    mutable std::ifstream file_;
    std::uint64_t file_size_{};
    CacheHeader header_{};
    std::array<std::byte, DictionarySize> dictionary_{};
    std::uint16_t dictionary_crc_{};
    std::vector<RecordMetadata> records_;
};

} // namespace Ps4Rt::Ahsl
