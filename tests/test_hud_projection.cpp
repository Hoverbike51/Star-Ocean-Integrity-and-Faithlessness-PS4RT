// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual SPIR-V epilogue, not a second implementation of it.
#include <cassert>
#include <fstream>
#include <filesystem>
#include <cmath>
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/runtime_info.h"
#include "ps4rt_hud_runtime.h"
#include "ps4rt_ultrawide.h"
#include "hud_runtime_model.h"

int main(int argc,char** argv) {
    assert(argc==2);using namespace Shader;using namespace Backend::SPIRV;
    unsigned ordinal=0;
    for(const auto hash:{0xc87f248eull,0x1932ead9ull,0x59cb445aull,0xd3ecde9eull,0x12345678ull})for(bool clip_disabled:{false,true}) {
        Info info{};info.hw_stage=HwStage::Vertex;info.sw_stage=SwStage::Vertex;info.pgm_hash=hash;
        RuntimeInfo runtime{};runtime.Initialize(info.hw_stage,info.sw_stage);runtime.hw.vs.clip_disable=clip_disabled;
        Profile profile{};profile.supported_spirv=0x00010600;profile.max_viewport_width=profile.max_viewport_height=16384;
        Backend::Bindings binding{};EmitContext ctx(profile,runtime,info,binding);
        // EmitSPIRV normally adds these core capabilities after creating its
        // context. This focused harness calls the production epilogue directly.
        ctx.AddCapability(spv::Capability::Int8);ctx.AddCapability(spv::Capability::Int16);
        ctx.AddCapability(spv::Capability::Int64);ctx.AddCapability(spv::Capability::DrawParameters);
        auto main=ctx.OpFunction(ctx.void_id,spv::FunctionControlMask::MaskNone,ctx.TypeFunction(ctx.void_id));
        ctx.AddLabel(ctx.OpLabel());ctx.OpStore(ctx.output_position,ctx.ConstF32(-1.9f,0.f,.5f,1.f));
        EmitEpilogue(ctx);ctx.OpReturn();ctx.OpFunctionEnd();
        ctx.AddEntryPoint(spv::ExecutionModel::Vertex,main,"main",ctx.interfaces);
        const auto code=ctx.Assemble();unsigned bitcasts=0;
        for(size_t at=5;at<code.size();) {
            const auto words=code[at]>>16;assert(words&&words<=code.size()-at);
            bitcasts+=(code[at]&65535)==unsigned(spv::Op::OpBitcast);at+=words;
        }
        assert(bitcasts==unsigned(Ps4RtHud::VertexProgram(hash)&&!clip_disabled));
        const auto path=std::filesystem::path(argv[1])/("hud-projection-"+std::to_string(ordinal++)+".spv");
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(code.data()),code.size()*4);assert(out.good());
    }
    // At all supported ratios, the new viewport/projection pair preserves original
    // centered UI positions and admits edge-translated vertices before clipping.
    const auto& party=So5Hud::RuntimeResources[0];
    for(auto size:{std::pair{1920.,1080.},{2560.,1080.},{3440.,1440.},{5120.,1440.},{1280.,960.},{1600.,1200.},{1920.,1200.},{2560.,1600.}}) {
        const auto [w,h]=size;const auto fit=Ps4RtUltrawide::Hud(unsigned(w),unsigned(h));
        const auto packed=Ps4RtUltrawide::HudProjectionParam(float(w/1920),float(h/1080),0,0,true);
        const float encoded=packed?std::bit_cast<float>(packed):1.f;
        const double factor=encoded<0?1:encoded,yfactor=encoded<0?-encoded:1;
        for(double logical:{0.,100.,540.,1080.}) {
            const double pixel=((logical/540-1)*yfactor+1)*h/2;
            assert(std::abs(pixel-(logical*fit.scale_y+fit.offset_y))<.001);
        }
        const auto full=Ps4RtUltrawide::HudScissor(unsigned(w),unsigned(h),{0,0,1920,1080},{float(w/1920),float(h/1080),0,0},true);
        assert(full.left==0&&full.top==0&&full.right==w&&full.bottom==h);
        const auto partial=Ps4RtUltrawide::HudScissor(unsigned(w),unsigned(h),{100,100,1800,1000},{float(w/1920),float(h/1080),0,0},true);
        assert(std::abs(partial.left-(100*fit.scale_x+fit.offset_x))<=1);
        assert(std::abs(partial.top-(100*fit.scale_y+fit.offset_y))<=1);
        // A complete black/background quad must survive clipping and cover
        // the exact output, without stretching any foreground glyphs.
        const auto fill=So5Hud::BackgroundTransform(w,h);
        assert(std::abs(((fill.x/960-1)*factor+1)*w/2)<.001);
        assert(std::abs(((fill.y/540-1)*yfactor+1)*h/2)<.001);
        assert(std::abs((((fill.x+1920*fill.sx)/960-1)*factor+1)*w/2-w)<.001);
        assert(std::abs((((fill.y+1080*fill.sy)/540-1)*yfactor+1)*h/2-h)<.001);
        for(double logical:{0.,100.,960.,1920.}) {
            const double clip=logical/960-1;
            const double pixel=(clip*factor+1)*w/2;
            assert(std::abs(pixel-(logical*fit.scale_x+fit.offset_x))<.001);
        }
        auto setting=So5Hud::Base(So5Hud::OutputAspect(w,h)).groups[0];
        const auto visual=So5Hud::Transform({100,900,1,1},party,setting,w,h);assert(visual);
        const double clip=(visual->x/960-1)*factor;
        assert(clip>=-1&&clip<=1);
        // Safety bases retain the native centered 16:9 composition. Only an
        // explicit custom left anchor may remove the output-side offset.
        assert(std::abs((clip+1)*w/2-(100*fit.scale_x+fit.offset_x))<.001);
        setting.anchor=So5Hud::Anchor::BottomLeft;
        const auto edge=So5Hud::Transform({100,900,1,1},party,setting,w,h);assert(edge);
        const double edge_clip=(edge->x/960-1)*factor;
        assert(edge_clip>=-1&&edge_clip<=1);
        // Edge anchoring preserves native overscan rather than clamping it.
        assert(std::abs((edge_clip+1)*w/2-100*fit.scale_x)<.001);
    }
}
