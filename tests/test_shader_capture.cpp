// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "ps4rt_shader_capture.h"

int main() {
    Ps4RtShaderCapture::Record input;
    input.program_hash = 0x0123456789abcdefull;
    input.permutation_hash = 0xfedcba9876543210ull;
    input.permutation = 3;
    input.hardware_stage = 1;
    input.software_stage = 2;
    input.flags = Ps4RtShaderCapture::HasFetchShader |
                  Ps4RtShaderCapture::HasGeometryCopyShader;
    input.gcn_code = {0xbeeb03ffu, 0xbf810000u};
    input.user_data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    input.geometry_copy_code = {0x10203040u, 0x50607080u};
    input.metadata = {0, 1, 2, 3, 4, 5, 0xfe, 0xff};

    const auto encoded = Ps4RtShaderCapture::Encode(input);
    assert(!encoded.empty());
    Ps4RtShaderCapture::Record output;
    std::string error;
    assert(Ps4RtShaderCapture::Decode(encoded, output, &error));
    assert(error.empty());
    assert(output.program_hash == input.program_hash);
    assert(output.permutation_hash == input.permutation_hash);
    assert(output.permutation == input.permutation);
    assert(output.hardware_stage == input.hardware_stage);
    assert(output.software_stage == input.software_stage);
    assert(output.flags == input.flags);
    assert(output.gcn_code == input.gcn_code);
    assert(output.user_data == input.user_data);
    assert(output.geometry_copy_code == input.geometry_copy_code);
    assert(output.metadata == input.metadata);

    auto corrupt = encoded;
    corrupt.back() ^= 0x80;
    assert(!Ps4RtShaderCapture::Decode(corrupt, output, &error));
    assert(error == "capture checksum mismatch");
    assert(!Ps4RtShaderCapture::Decode(std::span{encoded}.first(encoded.size() - 1), output,
                                       &error));
    return 0;
}
