// SPDX-License-Identifier: GPL-2.0-or-later
// Developer utility: validates and summarizes live PS4RT shader-context captures.

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

#include "ps4rt_shader_capture.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ps4rt-shader-capture-inspector <capture file or directory>\n";
        return 2;
    }
    const fs::path input{argv[1]};
    std::vector<fs::path> files;
    std::error_code error;
    if (fs::is_regular_file(input, error)) {
        files.push_back(input);
    } else if (fs::is_directory(input, error)) {
        for (const auto& entry : fs::directory_iterator(input, error)) {
            if (entry.is_regular_file(error) && entry.path().extension() == ".capture") {
                files.push_back(entry.path());
            }
        }
    }
    if (files.empty()) {
        std::cerr << "No .capture files found\n";
        return 2;
    }

    std::set<std::uint64_t> programs;
    std::array<std::uint64_t, 8> stages{};
    std::uint64_t valid = 0, invalid = 0, bytes = 0, gcn_words = 0, metadata_bytes = 0;
    std::uint64_t fetch = 0, geometry = 0;
    for (const fs::path& path : files) {
        const auto size = fs::file_size(path, error);
        if (error || size > Ps4RtShaderCapture::MaxFileSize) {
            ++invalid;
            std::cerr << path.filename().string() << ": invalid file size\n";
            continue;
        }
        std::ifstream stream(path, std::ios::binary);
        std::vector<std::uint8_t> data(size);
        if (!stream.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) {
            ++invalid;
            std::cerr << path.filename().string() << ": read failed\n";
            continue;
        }
        Ps4RtShaderCapture::Record record;
        std::string message;
        if (!Ps4RtShaderCapture::Decode(data, record, &message)) {
            ++invalid;
            std::cerr << path.filename().string() << ": " << message << '\n';
            continue;
        }
        ++valid;
        bytes += data.size();
        gcn_words += record.gcn_code.size();
        metadata_bytes += record.metadata.size();
        programs.insert(record.program_hash);
        if (record.hardware_stage < stages.size()) ++stages[record.hardware_stage];
        fetch += (record.flags & Ps4RtShaderCapture::HasFetchShader) != 0;
        geometry += (record.flags & Ps4RtShaderCapture::HasGeometryCopyShader) != 0;
    }

    std::cout << "Shader context capture summary\n"
              << "files=" << files.size() << " valid=" << valid << " invalid=" << invalid
              << " bytes=" << bytes << '\n'
              << "unique_programs=" << programs.size() << " gcn_words=" << gcn_words
              << " metadata_bytes=" << metadata_bytes << '\n'
              << "fetch_shader=" << fetch << " geometry_copy_shader=" << geometry << '\n'
              << "stages(fs,vs,gs,es,hs,ls,cs,other)=";
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << stages[i];
    }
    std::cout << '\n';
    return invalid ? 1 : 0;
}
