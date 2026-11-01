// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4rt_shader_capture.h"

#include <atomic>
#include <limits>

namespace Ps4RtShaderCapture {
namespace {

constexpr std::uint16_t HeaderSize = 64;
std::atomic<std::uint64_t> queued{}, captured_bytes{}, fetch_shader{}, geometry_copy_shader{},
    failures{};

void Put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(std::uint8_t(value >> shift));
}

void Put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) out.push_back(std::uint8_t(value >> shift));
}

bool Get16(std::span<const std::uint8_t> in, std::size_t& at, std::uint16_t& value) {
    if (at > in.size() || in.size() - at < 2) return false;
    value = std::uint16_t(in[at]) | (std::uint16_t(in[at + 1]) << 8);
    at += 2;
    return true;
}

bool Get32(std::span<const std::uint8_t> in, std::size_t& at, std::uint32_t& value) {
    if (at > in.size() || in.size() - at < 4) return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= std::uint32_t(in[at++]) << shift;
    return true;
}

bool Get64(std::span<const std::uint8_t> in, std::size_t& at, std::uint64_t& value) {
    if (at > in.size() || in.size() - at < 8) return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) value |= std::uint64_t(in[at++]) << shift;
    return true;
}

// Small dependency-free checksum. It detects partial writes and accidental cache corruption;
// this is not a security boundary.
std::uint32_t Crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void AppendWords(std::vector<std::uint8_t>& out, std::span<const std::uint32_t> words) {
    for (const std::uint32_t word : words) Put32(out, word);
}

bool ReadWords(std::span<const std::uint8_t> payload, std::size_t& at, std::uint32_t count,
               std::vector<std::uint32_t>& out) {
    if (count > payload.size() / sizeof(std::uint32_t) ||
        at > payload.size() || std::size_t(count) * 4 > payload.size() - at) {
        return false;
    }
    out.resize(count);
    for (auto& word : out) {
        if (!Get32(payload, at, word)) return false;
    }
    return true;
}

void SetError(std::string* error, const char* text) {
    if (error) *error = text;
}

} // namespace

std::vector<std::uint8_t> Encode(const Record& record) {
    if (record.gcn_code.empty() || record.user_data.size() > 16 ||
        record.gcn_code.size() > std::numeric_limits<std::uint32_t>::max() ||
        record.geometry_copy_code.size() > std::numeric_limits<std::uint32_t>::max() ||
        record.metadata.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> payload;
    const std::size_t payload_size =
        (record.gcn_code.size() + record.user_data.size() + record.geometry_copy_code.size()) * 4 +
        record.metadata.size();
    if (payload_size > MaxFileSize - HeaderSize) return {};
    payload.reserve(payload_size);
    AppendWords(payload, record.gcn_code);
    AppendWords(payload, record.user_data);
    AppendWords(payload, record.geometry_copy_code);
    payload.insert(payload.end(), record.metadata.begin(), record.metadata.end());

    std::vector<std::uint8_t> out;
    out.reserve(HeaderSize + payload.size());
    Put32(out, Magic);
    Put16(out, Version);
    Put16(out, HeaderSize);
    Put64(out, HeaderSize + payload.size());
    Put32(out, Crc32(payload));
    Put32(out, record.flags);
    Put64(out, record.program_hash);
    Put64(out, record.permutation_hash);
    Put32(out, record.permutation);
    out.push_back(record.hardware_stage);
    out.push_back(record.software_stage);
    Put16(out, 0);
    Put32(out, std::uint32_t(record.gcn_code.size()));
    Put32(out, std::uint32_t(record.user_data.size()));
    Put32(out, std::uint32_t(record.geometry_copy_code.size()));
    Put32(out, std::uint32_t(record.metadata.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool Decode(std::span<const std::uint8_t> bytes, Record& record, std::string* error) {
    if (bytes.size() < HeaderSize || bytes.size() > MaxFileSize) {
        SetError(error, "capture size is outside the supported range");
        return false;
    }
    std::size_t at = 0;
    std::uint32_t magic{}, checksum{}, flags{}, permutation{};
    std::uint16_t version{}, header_size{}, reserved{};
    std::uint64_t total_size{}, program_hash{}, permutation_hash{};
    std::uint32_t code_words{}, user_words{}, geometry_words{}, metadata_bytes{};
    std::uint8_t hardware_stage{}, software_stage{};
    if (!Get32(bytes, at, magic) || !Get16(bytes, at, version) ||
        !Get16(bytes, at, header_size) || !Get64(bytes, at, total_size) ||
        !Get32(bytes, at, checksum) || !Get32(bytes, at, flags) ||
        !Get64(bytes, at, program_hash) || !Get64(bytes, at, permutation_hash) ||
        !Get32(bytes, at, permutation) || at + 2 > bytes.size()) {
        SetError(error, "capture header is truncated");
        return false;
    }
    hardware_stage = bytes[at++];
    software_stage = bytes[at++];
    if (!Get16(bytes, at, reserved) || !Get32(bytes, at, code_words) ||
        !Get32(bytes, at, user_words) || !Get32(bytes, at, geometry_words) ||
        !Get32(bytes, at, metadata_bytes)) {
        SetError(error, "capture header is truncated");
        return false;
    }
    if (magic != Magic || version != Version || header_size != HeaderSize ||
        total_size != bytes.size() || at != HeaderSize || !code_words || user_words > 16) {
        SetError(error, "capture header is invalid or unsupported");
        return false;
    }
    const std::uint64_t expected_payload =
        (std::uint64_t(code_words) + user_words + geometry_words) * 4 + metadata_bytes;
    if (expected_payload != bytes.size() - HeaderSize) {
        SetError(error, "capture section sizes do not match the file");
        return false;
    }
    const auto payload = bytes.subspan(HeaderSize);
    if (Crc32(payload) != checksum) {
        SetError(error, "capture checksum mismatch");
        return false;
    }

    Record decoded;
    decoded.program_hash = program_hash;
    decoded.permutation_hash = permutation_hash;
    decoded.permutation = permutation;
    decoded.hardware_stage = hardware_stage;
    decoded.software_stage = software_stage;
    decoded.flags = flags;
    std::size_t payload_at = 0;
    if (!ReadWords(payload, payload_at, code_words, decoded.gcn_code) ||
        !ReadWords(payload, payload_at, user_words, decoded.user_data) ||
        !ReadWords(payload, payload_at, geometry_words, decoded.geometry_copy_code) ||
        metadata_bytes > payload.size() - payload_at) {
        SetError(error, "capture payload is truncated");
        return false;
    }
    decoded.metadata.assign(payload.begin() + payload_at,
                            payload.begin() + payload_at + metadata_bytes);
    payload_at += metadata_bytes;
    if (payload_at != payload.size()) {
        SetError(error, "capture payload contains trailing data");
        return false;
    }
    record = std::move(decoded);
    if (error) error->clear();
    return true;
}

void NoteQueued(std::size_t bytes, std::uint32_t flags) {
    queued.fetch_add(1, std::memory_order_relaxed);
    captured_bytes.fetch_add(bytes, std::memory_order_relaxed);
    if (flags & HasFetchShader) fetch_shader.fetch_add(1, std::memory_order_relaxed);
    if (flags & HasGeometryCopyShader) {
        geometry_copy_shader.fetch_add(1, std::memory_order_relaxed);
    }
}

void NoteFailure() {
    failures.fetch_add(1, std::memory_order_relaxed);
}

Statistics GetStatistics() {
    return {
        .queued = queued.load(std::memory_order_relaxed),
        .bytes = captured_bytes.load(std::memory_order_relaxed),
        .fetch_shader = fetch_shader.load(std::memory_order_relaxed),
        .geometry_copy_shader = geometry_copy_shader.load(std::memory_order_relaxed),
        .failures = failures.load(std::memory_order_relaxed),
    };
}

} // namespace Ps4RtShaderCapture
