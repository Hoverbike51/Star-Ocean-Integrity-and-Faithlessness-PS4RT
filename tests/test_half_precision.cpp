// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "common/object_pool.h"
using namespace Shader;
int main() {
 for(bool keep : {false,true}) {
  Common::ObjectPool<IR::Inst> pool; IR::Block block{pool}; IR::IREmitter ir{block};
  auto pair=ir.CompositeConstruct(IR::F32{ir.GetVectorReg<IR::F32>(IR::VectorReg::V0)},ir.Imm32(1.f));
  auto packed=ir.Pack2x16(AmdGpu::NumberFormat::Float,pair);
  auto unpacked=ir.Unpack2x16(AmdGpu::NumberFormat::Float,packed);
  ir.SetVectorReg(IR::VectorReg::V1,IR::F32{ir.CompositeExtract(unpacked,0)});
  IR::BlockList blocks{&block}; Optimization::ConstantPropagationPass(blocks,keep);
  bool found=false; for(auto& inst:block.Instructions()) found|=inst.GetOpcode()==IR::Opcode::UnpackHalf2x16;
  assert(found==keep);
 }
 puts("half precision: production optimizer preserves observable conversion");
}
