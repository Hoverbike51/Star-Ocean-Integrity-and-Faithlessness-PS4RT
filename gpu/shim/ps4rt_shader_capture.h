// SPDX-License-Identifier: GPL-2.0-or-later
// Portable envelope for the live shader inputs required by the offline cache builder.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Ps4RtShaderCapture {

inline constexpr std::uint32_t Magic = 0x31504353; // "SCP1" in little endian.
inline constexpr std::uint16_t Version = 1;
inline constexpr std::size_t MaxFileSize = 64u * 1024u * 1024u;

enum Flag : std::uint32_t {
    HasFetchShader = 1u << 0,
    HasGeometryCopyShader = 1u << 1,
};

struct Record {
    std::uint64_t program_hash{};
    std::uint64_t permutation_hash{};
    std::uint32_t permutation{};
    std::uint8_t hardware_stage{};
    std::uint8_t software_stage{};
    std::uint32_t flags{};
    std::vector<std::uint32_t> gcn_code;
    std::vector<std::uint32_t> user_data;
    std::vector<std::uint32_t> geometry_copy_code;
    // Existing shadPS4 Shader::Info + StageSpecialization serialization. It contains the
    // flattened descriptors, fetch-shader data, runtime registers and binding specialization.
    std::vector<std::uint8_t> metadata;
};

struct Statistics {
    std::uint64_t queued{};
    std::uint64_t bytes{};
    std::uint64_t fetch_shader{};
    std::uint64_t geometry_copy_shader{};
    std::uint64_t failures{};
};

std::vector<std::uint8_t> Encode(const Record& record);
bool Decode(std::span<const std::uint8_t> bytes, Record& record, std::string* error = nullptr);
void NoteQueued(std::size_t bytes, std::uint32_t flags);
void NoteFailure();
Statistics GetStatistics();

} // namespace Ps4RtShaderCapture
