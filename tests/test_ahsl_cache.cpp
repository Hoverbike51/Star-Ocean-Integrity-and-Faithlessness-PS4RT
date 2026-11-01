// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "ps4rt_ahsl_cache.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/regs_shader.h"

namespace {

void AppendLe16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(std::byte(value & 0xFF));
    output.push_back(std::byte(value >> 8));
}

void AppendLe32(std::vector<std::byte>& output, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        output.push_back(std::byte((value >> (i * 8)) & 0xFF));
    }
}

void AppendLe64(std::vector<std::byte>& output, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        output.push_back(std::byte((value >> (i * 8)) & 0xFF));
    }
}

void AppendBe16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(std::byte(value >> 8));
    output.push_back(std::byte(value & 0xFF));
}

std::vector<std::byte> LiteralStream(std::span<const std::byte> payload) {
    assert((payload.size() & 1) == 0);
    std::vector<std::byte> result;
    std::size_t pair{};
    const std::size_t pairs = payload.size() / 2;
    while (pair < pairs) {
        const std::size_t count = std::min<std::size_t>(16, pairs - pair);
        const std::uint16_t flags = count == 16 ? 0xFFFF : std::uint16_t((1u << count) - 1);
        AppendBe16(result, flags);
        result.insert(result.end(), payload.begin() + static_cast<std::ptrdiff_t>(pair * 2),
                      payload.begin() + static_cast<std::ptrdiff_t>((pair + count) * 2));
        pair += count;
        if (pair == pairs && count < 16) {
            AppendBe16(result, 0);
            return result;
        }
    }
    AppendBe16(result, 0);
    AppendBe16(result, 0);
    return result;
}

std::vector<std::byte> BuildFixture() {
    using namespace Ps4Rt::Ahsl;
    std::array<std::byte, DictionarySize> dictionary{};
    for (std::size_t i = 0; i < dictionary.size(); ++i) {
        dictionary[i] = std::byte((i * 17) & 0xFF);
    }

    std::vector<std::byte> payload;
    AppendLe32(payload, 0xBEEB03FF);
    for (unsigned i = 1; i < 8; ++i) {
        AppendLe32(payload, 0xBF810000);
    }
    const std::size_t binary_offset_in_payload = payload.size();
    for (char value : std::array{'O', 'r', 'b', 'S', 'h', 'd', 'r'}) {
        payload.push_back(std::byte(value));
    }
    payload.push_back(std::byte{1});
    const std::uint32_t packed = 1u | (1u << 1) | (1u << 2) | (3u << 6) | (32u << 8);
    AppendLe32(payload, packed);
    payload.push_back(std::byte{4});
    payload.push_back(std::byte{5});
    payload.push_back(std::byte{7});
    payload.push_back(std::byte{});
    AppendLe64(payload, 0x1122334455667788ULL);
    AppendLe32(payload, 0xAABBCCDD);
    assert(payload.size() - binary_offset_in_payload == BinaryInfoSize);

    const auto compressed = LiteralStream(payload);
    constexpr std::array<std::byte, 4> key = {
        std::byte{1}, std::byte{2}, std::byte{4}, std::byte{0}};
    constexpr std::uint16_t coded_offset = RecordHeaderSize + key.size();
    const auto stored_size = std::uint16_t(coded_offset + compressed.size());
    const auto decoded_size = std::uint16_t(coded_offset + payload.size());

    std::vector<std::byte> file;
    for (char value : std::array{'O', 'S', 'H', 'A'}) {
        file.push_back(std::byte(value));
    }
    AppendLe32(file, 0);
    AppendLe32(file, 0x00010086);
    AppendLe32(file, 1);
    AppendLe32(file, 0);
    file.resize(CacheHeaderSize);
    file.insert(file.end(), dictionary.begin(), dictionary.end());
    AppendLe16(file, 0x1234);
    AppendLe16(file, stored_size);
    AppendLe16(file, decoded_size);
    AppendLe16(file, coded_offset);
    AppendLe16(file, Crc16X25(dictionary));
    AppendLe16(file, 0x83);
    file.insert(file.end(), key.begin(), key.end());
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

void TestStandaloneShaderTranslation() {
    // Minimal autonomous pixel shader observed in the validated CUSA03219 AHSL cache. It does not
    // read user data or call a fetch shader, so it can be translated without fabricating draw state.
    const std::array<std::uint32_t, 7> code = {
        0xBEEB03FF, 0x00000004, 0x7E0002FF, 0x3C003C00,
        0xF8001C0F, 0x00000000, 0xBF810000,
    };
    std::array<std::uint32_t, Shader::ShaderParams::NumShaderUserData> user_data{};
    const Shader::ShaderParams params{
        .user_data = user_data,
        .code = code,
        .hash = 0x36CFEF9A,
    };
    Shader::Info info{Shader::HwStage::Fragment, Shader::SwStage::Fragment, params};
    Shader::RuntimeInfo runtime{};
    runtime.Initialize(Shader::HwStage::Fragment, Shader::SwStage::Fragment);
    Shader::Profile profile{};
    profile.max_viewport_width = 16384;
    profile.max_viewport_height = 16384;
    profile.max_shared_memory_size = 65536;
    profile.supported_spirv = 0x00010600;
    profile.subgroup_size = 64;
    profile.support_int8 = true;
    profile.support_int16 = true;
    profile.support_int64 = true;
    profile.support_float16 = true;
    profile.support_float64 = true;
    profile.support_fp32_denorm_flush = true;
    profile.support_fp32_signed_zero_inf_nan_preserve = true;

    Shader::Pools pools;
    const auto program = Shader::TranslateProgram(code, pools, info, runtime, profile);
    Shader::Backend::Bindings bindings{};
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    assert(spirv.size() >= 5);
    assert(spirv[0] == 0x07230203);
}

} // namespace

int main() {
    using namespace Ps4Rt::Ahsl;
    // The serialized prefix is 28 bytes. The native structure may have tail padding (32 bytes on
    // clang/MinGW), so never copy sizeof(BinaryInfo) bytes from an on-disk record.
    static_assert(sizeof(AmdGpu::BinaryInfo) >= BinaryInfoSize);

    const auto path = std::filesystem::current_path() / "ahsl-cache-test-fixture.bin";
    const auto bytes = BuildFixture();
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        assert(file);
    }

    try {
        Cache cache(path);
        assert(cache.Header().record_count == 1);
        assert(cache.Header().format_version == 0x00010086);
        assert(cache.Records().size() == 1);
        assert(cache.Records()[0].offset == RecordsOffset);

        const auto decoded = cache.DecodeRecord(0);
        assert(decoded.size() == cache.Records()[0].decoded_size);
        const auto binaries = ExtractBinaryInfo(decoded);
        assert(binaries.size() == 1);
        assert(binaries[0].code_offset == RecordHeaderSize + 4);
        assert(binaries[0].code_length == 32);
        assert(binaries[0].type == 1);
        assert(binaries[0].source_type == 3);
        assert(binaries[0].shader_hash == 0x1122334455667788ULL);

        AmdGpu::BinaryInfo runtime_info{};
        std::memcpy(&runtime_info, decoded.data() + binaries[0].info_offset, BinaryInfoSize);
        assert(runtime_info.Valid());
        assert(runtime_info.type == binaries[0].type);
        assert(runtime_info.length == binaries[0].code_length);
        assert(runtime_info.shader_hash == binaries[0].shader_hash);
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
    assert(std::filesystem::remove(path));
    TestStandaloneShaderTranslation();
    return 0;
}
