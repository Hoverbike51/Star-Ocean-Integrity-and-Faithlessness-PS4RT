// SPDX-License-Identifier: GPL-2.0-or-later

// Developer-only native validator for the PS4 AHSL cache. This executable is excluded from the
// normal build and from release packaging; it never writes to the source cache.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "ps4rt_ahsl_cache.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/decode.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/runtime_info.h"

namespace {

const char* ShaderTypeName(std::uint8_t type) {
    switch (type) {
    case 0: return "pixel";
    case 1: return "vertex";
    case 2: return "export";
    case 3: return "local";
    case 4: return "compute";
    case 5: return "geometry";
    case 7: return "hull";
    case 8: return "domain-vertex";
    default: return "unknown";
    }
}

std::optional<std::pair<Shader::HwStage, Shader::SwStage>> StandaloneStage(std::uint8_t type) {
    switch (type) {
    case 0: return std::pair{Shader::HwStage::Fragment, Shader::SwStage::Fragment};
    case 1: return std::pair{Shader::HwStage::Vertex, Shader::SwStage::Vertex};
    case 4: return std::pair{Shader::HwStage::Compute, Shader::SwStage::Compute};
    default: return std::nullopt;
    }
}

bool NeedsLiveDrawContext(std::span<const std::uint32_t> code) {
    Shader::Gcn::GcnCodeSlice slice{code.data(), code.data() + code.size()};
    Shader::Gcn::GcnDecodeContext decoder;
    while (!slice.atEnd()) {
        if (decoder.decodeInstruction(slice).opcode == Shader::Gcn::Opcode::S_SWAPPC_B64) {
            return true;
        }
    }
    return false;
}

std::vector<std::uint32_t> TranslateOne(const Ps4Rt::Ahsl::Cache& cache, std::size_t record_index,
                                        Ps4Rt::Ahsl::BinaryInfo* translated) {
    std::cerr << "Translation probe: decoding AHSL record " << record_index << std::endl;
    const auto decoded = cache.DecodeRecord(record_index);
    const auto binaries = Ps4Rt::Ahsl::ExtractBinaryInfo(decoded);
    for (const auto& binary : binaries) {
        const auto stage = StandaloneStage(binary.type);
        if (!stage) {
            continue;
        }
        if (binary.code_offset + binary.code_length > decoded.size()) {
            throw Ps4Rt::Ahsl::FormatError("extracted GCN code extends beyond its AHSL record");
        }
        std::vector<std::uint32_t> code(binary.code_length / sizeof(std::uint32_t));
        std::memcpy(code.data(), decoded.data() + binary.code_offset, binary.code_length);
        if (NeedsLiveDrawContext(code)) {
            throw std::runtime_error(
                "shader calls a fetch shader and requires captured user data from a live draw");
        }

        std::array<std::uint32_t, Shader::ShaderParams::NumShaderUserData> user_data{};
        const Shader::ShaderParams params{
            .user_data = user_data,
            .code = code,
            .hash = binary.shader_hash,
        };
        auto [hw_stage, sw_stage] = *stage;
        Shader::Info info{hw_stage, sw_stage, params};
        Shader::RuntimeInfo runtime{};
        runtime.Initialize(hw_stage, sw_stage);
        Shader::Profile profile{
            .max_viewport_width = 16384,
            .max_viewport_height = 16384,
            .max_shared_memory_size = 65536,
            .supported_spirv = 0x00010600,
            .subgroup_size = 64,
            .support_int8 = true,
            .support_int16 = true,
            .support_int64 = true,
            .support_float16 = true,
            .support_float64 = true,
        };
        Shader::Pools pools;
        std::cerr << "Translation probe: translating " << binary.code_length << " GCN bytes"
                  << std::endl;
        auto program = Shader::TranslateProgram(code, pools, info, runtime, profile);
        std::cerr << "Translation probe: emitting SPIR-V" << std::endl;
        Shader::Backend::Bindings bindings{};
        auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
        if (spirv.size() < 5 || spirv[0] != 0x07230203) {
            throw std::runtime_error("shader recompiler returned an invalid SPIR-V module");
        }
        if (translated != nullptr) {
            *translated = binary;
        }
        return spirv;
    }
    throw std::runtime_error("record has no shader stage suitable for standalone translation");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ps4rt-ahsl-inspector <AHSLDiskCachePs4> "
                     "[--deep] [--translate-record INDEX]\n";
        return 2;
    }
    bool deep{};
    std::optional<std::size_t> translate_record;
    for (int arg = 2; arg < argc; ++arg) {
        const std::string_view option{argv[arg]};
        if (option == "--deep") {
            deep = true;
        } else if (option == "--translate-record" && arg + 1 < argc) {
            try {
                translate_record = std::stoull(argv[++arg]);
            } catch (const std::exception&) {
                std::cerr << "Invalid record index\n";
                return 2;
            }
        } else {
            std::cerr << "Unknown or incomplete option: " << option << '\n';
            return 2;
        }
    }

    try {
        const Ps4Rt::Ahsl::Cache cache{std::filesystem::path{argv[1]}};
        std::cout << "PS4 AHSL cache (native reader)\n"
                  << "  Source: " << cache.Path().string() << '\n'
                  << "  Size: " << cache.FileSize() << " bytes\n"
                  << "  Format version: 0x" << std::hex << std::uppercase
                  << std::setw(8) << std::setfill('0') << cache.Header().format_version << '\n'
                  << "  Records: " << std::dec << cache.Records().size() << " (exact file walk)\n"
                  << "  Dictionary CRC-16/X.25: 0x" << std::hex << std::setw(4)
                  << cache.DictionaryCrc() << std::dec << '\n';

        if (deep) {
            std::array<std::uint64_t, 16> type_counts{};
            std::array<std::uint64_t, 8> headers_per_record{};
            std::set<std::uint64_t> hashes;
            std::uint64_t header_count{};
            for (const auto& record : cache.Records()) {
                const auto decoded = cache.DecodeRecord(record);
                const auto binaries = Ps4Rt::Ahsl::ExtractBinaryInfo(decoded);
                if (binaries.size() < headers_per_record.size()) {
                    ++headers_per_record[binaries.size()];
                }
                for (const auto& binary : binaries) {
                    ++header_count;
                    ++type_counts[binary.type];
                    hashes.insert(binary.shader_hash);
                }
            }
            std::cout << "Deep scan\n"
                      << "  Decoded records: " << cache.Records().size() << '\n'
                      << "  OrbShdr headers: " << header_count << '\n'
                      << "  Unique shader hashes: " << hashes.size() << '\n'
                      << "  Headers per record:";
            for (std::size_t count = 0; count < headers_per_record.size(); ++count) {
                if (headers_per_record[count] != 0) {
                    std::cout << ' ' << count << '=' << headers_per_record[count];
                }
            }
            std::cout << "\n  Shader types:";
            for (std::size_t type = 0; type < type_counts.size(); ++type) {
                if (type_counts[type] != 0) {
                    std::cout << ' ' << type << ':' << ShaderTypeName(static_cast<std::uint8_t>(type))
                              << '=' << type_counts[type];
                }
            }
            std::cout << '\n';
        }
        if (translate_record) {
            Ps4Rt::Ahsl::BinaryInfo translated{};
            const auto spirv = TranslateOne(cache, *translate_record, &translated);
            std::cout << "Controlled translation\n"
                      << "  Record: " << *translate_record << '\n'
                      << "  Shader hash: 0x" << std::hex << std::uppercase
                      << translated.shader_hash << std::dec << '\n'
                      << "  Stage: " << ShaderTypeName(translated.type) << '\n'
                      << "  GCN bytes: " << translated.code_length << '\n'
                      << "  SPIR-V words: " << spirv.size() << '\n'
                      << "  SPIR-V magic: 0x" << std::hex << std::uppercase << spirv[0]
                      << std::dec << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "AHSL cache inspection failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
