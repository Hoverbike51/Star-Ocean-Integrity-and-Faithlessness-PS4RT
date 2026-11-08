// SPDX-License-Identifier: GPL-2.0-or-later
// Reproduce an alpha-only AO export using the production GCN translator.
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/profile.h"
#include "video_core/amdgpu/regs_color.h"

using namespace Shader;

static void CheckExport(AmdGpu::DataFormat format, AmdGpu::ColorBuffer::SwapMode swap,
                        u32 expected_source) {
    AmdGpu::ColorBuffer cb{};
    cb.info.format = u32(format);
    cb.info.comp_swap = swap;
    const auto mapping = cb.Swizzle();
    Info info{};
    info.hw_stage = HwStage::Fragment;
    info.sw_stage = SwStage::Fragment;
    RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    auto& output = runtime.hw.fs.color_buffers[0];
    output.data_format = format;
    output.num_format = AmdGpu::NumberFormat::Float;
    output.export_format = AmdGpu::ShaderExportFormat::ABGR_32;
    output.swizzle = mapping;
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    Profile profile{};
    Gcn::Translator translator{info, runtime, profile};
    Gcn::GcnInst instruction{};
    instruction.opcode = Gcn::Opcode::EXP;
    instruction.category = Gcn::InstCategory::Export;
    instruction.control.exp.en = 15;
    instruction.control.exp.target = u32(IR::Attribute::RenderTarget0);
    for (u32 i = 0; i < 4; ++i) instruction.src[i].code = i;
    translator.Translate(&block, 0, IR::Condition::True, std::span{&instruction, 1});
    bool found = false;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::SetAttribute || inst.Arg(2).U32() != 0) continue;
        auto value = inst.Arg(1);
        if (format == AmdGpu::DataFormat::Format16) {
            assert(value.Inst()->GetOpcode() == IR::Opcode::FPClamp32);
            assert(value.Inst()->Arg(1).F32() == -65504.f);
            assert(value.Inst()->Arg(2).F32() == 65504.f);
            value = value.Inst()->Arg(0);
        }
        while (value.Inst()->GetOpcode() == IR::Opcode::BitCastF32U32)
            value = value.Inst()->Arg(0);
        assert(value.Inst()->GetOpcode() == IR::Opcode::GetVectorRegister);
        assert(u32(value.Inst()->Arg(0).VectorReg()) == expected_source);
        found = true;
    }
    assert(found);
    // The color mask must enable the stored channel when its shader source is
    // enabled, and disable it when only other shader sources are enabled.
    for (u32 mask = 0; mask < 16; ++mask)
        assert((mapping.ApplyMask(mask) & 1) == ((mask >> expected_source) & 1));
}

// Evaluate the scalar boundary expressions emitted by the production translator.
// Inputs include values which would overflow attachment/half conversion; the
// expected ranges below are the actual representable format limits, not [0,1].
static float Half(u16 bits) {
    const u32 exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    const float magnitude = exponent == 31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN() :
        std::numeric_limits<float>::infinity()) : exponent ?
        std::ldexp(1.f + mantissa / 1024.f, int(exponent) - 15) : std::ldexp(float(mantissa), -24);
    return bits & 0x8000 ? -magnitude : magnitude;
}

static float Eval(IR::Value value, const std::array<float,4>& registers);
static bool Bool(IR::Value v,const std::array<float,4>& r) {
    if(v.IsImmediate())return v.U1();
    const auto* i=v.Inst();using O=IR::Opcode;
    switch(i->GetOpcode()) {
    case O::LogicalOr:return Bool(i->Arg(0),r)||Bool(i->Arg(1),r);
    case O::LogicalAnd:return Bool(i->Arg(0),r)&&Bool(i->Arg(1),r);
    case O::LogicalNot:return !Bool(i->Arg(0),r);
    case O::FPOrdEqual32:return Eval(i->Arg(0),r)==Eval(i->Arg(1),r);
    case O::FPOrdLessThan32:return Eval(i->Arg(0),r)<Eval(i->Arg(1),r);
    case O::FPOrdGreaterThan32:return Eval(i->Arg(0),r)>Eval(i->Arg(1),r);
    case O::FPIsInf32:return std::isinf(Eval(i->Arg(0),r));
    default:assert(false);return false;
    }
}
static float Eval(const IR::Value value, const std::array<float, 4>& registers) {
    if (value.IsImmediate()) return value.F32();
    const auto* inst = value.Inst();
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetVectorRegister:
        return registers[u32(inst->Arg(0).VectorReg())];
    case IR::Opcode::BitCastF32U32:
    case IR::Opcode::BitCastU32F32:
        return Eval(inst->Arg(0), registers);
    case IR::Opcode::FPAdd32:return Eval(inst->Arg(0),registers)+Eval(inst->Arg(1),registers);
    case IR::Opcode::FPMul32:return Eval(inst->Arg(0),registers)*Eval(inst->Arg(1),registers);
    case IR::Opcode::FPAbs32:return std::abs(Eval(inst->Arg(0),registers));
    case IR::Opcode::FPRecip32:return 1.f/Eval(inst->Arg(0),registers);
    case IR::Opcode::FPRecipSqrt32:return 1.f/std::sqrt(Eval(inst->Arg(0),registers));
    case IR::Opcode::FPLog2:return std::log2(Eval(inst->Arg(0),registers));
    case IR::Opcode::SelectF32:return Eval(inst->Arg(Bool(inst->Arg(0),registers)?1:2),registers);
    case IR::Opcode::FPClamp32:
        return std::clamp(Eval(inst->Arg(0), registers), inst->Arg(1).F32(), inst->Arg(2).F32());
    case IR::Opcode::CompositeExtractF32x2: {
        const auto* unpack = inst->Arg(0).Inst();
        assert(unpack->GetOpcode() == IR::Opcode::UnpackHalf2x16);
        const auto bits = std::bit_cast<u32>(Eval(unpack->Arg(0), registers));
        return Half(u16(bits >> (16 * inst->Arg(1).U32())));
    }
    default:
        assert(false && "Unexpected boundary expression");
        return 0;
    }
}

static void CheckFiniteExport(AmdGpu::DataFormat format, bool compressed,
                              AmdGpu::NumberFormat number = AmdGpu::NumberFormat::Float) {
    using namespace AmdGpu;
    Info info{}; info.hw_stage = HwStage::Fragment; info.sw_stage = SwStage::Fragment;
    RuntimeInfo runtime{}; runtime.Initialize(info.hw_stage, info.sw_stage);
    ColorBuffer cb{}; cb.info.format = u32(format);
    auto& output = runtime.hw.fs.color_buffers[0];
    output.data_format = cb.GetDataFmt(); output.num_format = number; output.swizzle = cb.Swizzle();
    output.export_format = compressed ? ShaderExportFormat::ABGR_FP16 : ShaderExportFormat::ABGR_32;
    Common::ObjectPool<IR::Inst> pool; IR::Block block{pool}; Profile profile{};
    Gcn::Translator translator{info, runtime, profile};
    Gcn::GcnInst instruction{}; instruction.opcode = Gcn::Opcode::EXP;
    instruction.category = Gcn::InstCategory::Export;
    instruction.control.exp.en = compressed ? 5 : 15;
    instruction.control.exp.compr = compressed;
    instruction.control.exp.target = u32(IR::Attribute::RenderTarget0);
    for (u32 i = 0; i < 4; ++i) instruction.src[i].code = i;
    translator.Translate(&block, 0, IR::Condition::True, std::span{&instruction, 1});
    const bool packed = format == DataFormat::Format10_11_11 || format == DataFormat::Format11_11_10;
    const bool half = format == DataFormat::Format16_16_16_16;
    const float infinity = std::numeric_limits<float>::infinity();
    unsigned checked = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::SetAttribute) continue;
        const u32 component = inst.Arg(2).U32();
        if (component >= NumComponents(format)) continue;
        const auto value = inst.Arg(1);
        const bool bounded = number == NumberFormat::Float && (packed || half || compressed);
        if (!bounded) {
            assert(value.Inst()->GetOpcode() != IR::Opcode::FPClamp32);
            if (number == NumberFormat::Float) assert(Eval(value, {1.e20f,1.e20f,1.e20f,1.e20f}) == 1.e20f);
        } else {
            const float low = packed ? 0.f : -65504.f;
            const float high = packed ? (component == 2 ? 64512.f : 65024.f) : 65504.f;
            assert(value.Inst()->GetOpcode() == IR::Opcode::FPClamp32);
            assert(value.Inst()->Arg(1).F32() == low && value.Inst()->Arg(2).F32() == high);
            if (compressed) {
                const auto positive = std::bit_cast<float>(u32(0x7c007c00));
                const auto negative = std::bit_cast<float>(u32(0xfc00fc00));
                assert(Eval(value, {positive,positive,positive,positive}) == high);
                assert(Eval(value, {negative,negative,negative,negative}) == low);
                const auto hdr = std::bit_cast<float>(u32(0x40004000));
                assert(Eval(value, {hdr,hdr,hdr,hdr}) == 2.f);
            } else {
                for (const float input : {1.e20f, infinity}) assert(Eval(value, {input,input,input,input}) == high);
                for (const float input : {-1.e20f, -infinity}) assert(Eval(value, {input,input,input,input}) == low);
                assert(Eval(value, {2.f,2.f,2.f,2.f}) == 2.f); // preserve ordinary HDR
            }
        }
        ++checked;
    }
    assert(checked == NumComponents(format));
}

static void CheckHalfPackOverflow() {
    Info info{}; info.hw_stage = HwStage::Fragment; info.sw_stage = SwStage::Fragment;
    RuntimeInfo runtime{}; runtime.Initialize(info.hw_stage, info.sw_stage);
    Common::ObjectPool<IR::Inst> pool; IR::Block block{pool}; Profile profile{};
    Gcn::Translator translator{info, runtime, profile};
    Gcn::GcnInst instruction{}; instruction.opcode = Gcn::Opcode::V_CVT_PKRTZ_F16_F32;
    instruction.category = Gcn::InstCategory::VectorALU;
    for (u32 i = 0; i < 2; ++i) {
        instruction.src[i].field = Gcn::OperandField::VectorGPR;
        instruction.src[i].code = i;
    }
    instruction.dst[0].field = Gcn::OperandField::VectorGPR;
    instruction.dst[0].code = 2;
    translator.Translate(&block, 0, IR::Condition::True, std::span{&instruction, 1});
    bool found = false;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::PackHalf2x16) continue;
        const auto* pair = inst.Arg(0).Inst();
        assert(pair->GetOpcode() == IR::Opcode::CompositeConstructF32x2);
        for (unsigned lane = 0; lane < 2; ++lane) {
            const auto value = pair->Arg(lane);
            assert(value.Inst()->GetOpcode() == IR::Opcode::FPClamp32);
            for (const float input : {65504.f,65520.f,1.e20f,std::numeric_limits<float>::infinity()})
                assert(Eval(value, {input,input,0,0}) == 65504.f);
            for (const float input : {-65504.f,-65520.f,-1.e20f,-std::numeric_limits<float>::infinity()})
                assert(Eval(value, {input,input,0,0}) == -65504.f);
            assert(Eval(value, {2.f,2.f,0,0}) == 2.f);
        }
        found = true;
    }
    assert(found);
}

static void CheckLegacyArithmetic(Gcn::Opcode opcode,std::array<float,4> input,float expected) {
    Info info{};info.hw_stage=HwStage::Fragment;info.sw_stage=SwStage::Fragment;
    RuntimeInfo runtime{};runtime.Initialize(info.hw_stage,info.sw_stage);
    Common::ObjectPool<IR::Inst> pool;IR::Block block{pool};Profile profile{};profile.fp_legacy=31;
    Gcn::Translator translator{info,runtime,profile};Gcn::GcnInst instruction{};
    instruction.opcode=opcode;instruction.category=Gcn::InstCategory::VectorALU;
    for(u32 i=0;i<3;++i){instruction.src[i].field=Gcn::OperandField::VectorGPR;instruction.src[i].code=i;}
    instruction.dst[0].field=Gcn::OperandField::VectorGPR;instruction.dst[0].code=3;
    translator.Translate(&block,0,IR::Condition::True,std::span{&instruction,1});bool found=false;
    for(const auto& inst:block.Instructions())if(inst.GetOpcode()==IR::Opcode::SetVectorRegister && u32(inst.Arg(0).VectorReg())==3) {
        const float actual=Eval(inst.Arg(1),input);assert(std::isnan(expected)?std::isnan(actual):actual==expected);found=true;
    }
    assert(found);
}

int main() {
    using namespace AmdGpu;
    const float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN(),max=std::numeric_limits<float>::max();
    CheckLegacyArithmetic(Gcn::Opcode::V_MAD_LEGACY_F32,{0,inf,7,11},7);
    CheckLegacyArithmetic(Gcn::Opcode::V_MAC_LEGACY_F32,{0,inf,7,11},11);
    CheckLegacyArithmetic(Gcn::Opcode::V_MAD_LEGACY_F32,{0,nan,7,11},7);
    CheckLegacyArithmetic(Gcn::Opcode::V_MAD_LEGACY_F32,{2,3,7,11},13);
    CheckLegacyArithmetic(Gcn::Opcode::V_RCP_CLAMP_F32,{0,0,0,0},max);
    CheckLegacyArithmetic(Gcn::Opcode::V_RCP_CLAMP_F32,{-0.f,0,0,0},-max);
    CheckLegacyArithmetic(Gcn::Opcode::V_RSQ_LEGACY_F32,{0,0,0,0},0);
    CheckLegacyArithmetic(Gcn::Opcode::V_LOG_CLAMP_F32,{0,0,0,0},-max);
    for (auto format : {DataFormat::Format8, DataFormat::Format16, DataFormat::Format32}) {
        CheckExport(format, ColorBuffer::SwapMode::Standard, 0);
        CheckExport(format, ColorBuffer::SwapMode::Alternate, 1);
        CheckExport(format, ColorBuffer::SwapMode::StandardReverse, 2);
        CheckExport(format, ColorBuffer::SwapMode::AlternateReverse, 3);
        // Verify the texture-read alpha alias remains intact.
        const CompMapping alpha{.r=CompSwizzle::Zero, .g=CompSwizzle::Zero,
                                .b=CompSwizzle::Zero, .a=CompSwizzle::Alpha};
        assert(RemapSwizzle(format, alpha).a == CompSwizzle::Red);
    }
    // Non-involutive packed-format mappings must use the same direction for
    // write masks as for the shader's destination-to-source mapping.
    const CompMapping cycle{.r=CompSwizzle::Alpha, .g=CompSwizzle::Red,
                            .b=CompSwizzle::Blue, .a=CompSwizzle::Green};
    assert(cycle.ApplyMask(1) == 2);
    assert(cycle.ApplyMask(2) == 8);
    assert(cycle.ApplyMask(8) == 1);
    for (const auto format : {DataFormat::Format10_11_11, DataFormat::Format11_11_10,
                              DataFormat::Format16_16_16_16, DataFormat::Format32_32_32_32}) {
        CheckFiniteExport(format, false);
        CheckFiniteExport(format, true);
    }
    CheckFiniteExport(DataFormat::Format16_16_16_16, false, NumberFormat::Uint);
    CheckHalfPackOverflow();
    std::puts("color-export-test: passed (GCN export channels and physical write masks)");
    std::puts("PASS: finite packed HDR/f16 export limits, compressed infinity, PKRTZ overflow, HDR preservation and untouched 32-bit/integer exports");
}
