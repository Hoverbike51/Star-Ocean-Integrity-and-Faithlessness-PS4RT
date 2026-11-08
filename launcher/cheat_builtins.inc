// SPDX-License-Identifier: GPL-2.0-or-later
// CUSA03219 / APP_VER 01.00 only. Every original byte and code cave is checked.
// HP/MP/OHK/movement adapted from the audited source handoff; progression/resource
// patches from the owner-tested trainer. The unsafe mass-inventory hook is retired.
std::vector<Cheat> Builtins() {
    std::vector<Cheat> result;
    { Cheat c; c.id=1; c.builtin=true; c.name=Catalog(1)->name;
      c.lines.push_back({0x95BF00,Hex("6683bba0000000ff7408c5fa108bc4000000c5fa118bb4000000e9d1fa8dff"),Hex("00000000000000000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23B9E8,Hex("e913057200909090"),Hex("c5fa118bb4000000"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=2; c.builtin=true; c.name=Catalog(2)->name;
      c.lines.push_back({0x95BF40,Hex("6683bba0000000ff740dc5fa108300010000e9f6fb8dffc5fa5d8300010000e9e9fb8dff"),Hex("000000000000000000000000000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BB45,Hex("e9f6037200909090"),Hex("c5fa5d8300010000"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=4; c.builtin=true; c.name=Catalog(4)->name;
      c.lines.push_back({0x95BD2B,Hex("2dffe0f5058983b0000000e9c2ff8dff"),Hex("00000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BCF7,Hex("e92f00720090"),Hex("8983b0000000"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=5; c.builtin=true; c.name=Catalog(5)->name;
      c.lines.push_back({0x95BD3B,Hex("51b9003c1c46660f6ec159c5fa1183b8000000e9e1008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BE2C,Hex("e90aff7100909090"),Hex("c5fa1183b8000000"),false,-1});
      c.lines.push_back({0x95BD53,Hex("51b9003c1c46660f6ec159c5fa1183fc000000e9d9008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BE3C,Hex("e912ff7100909090"),Hex("c5fa1183fc000000"),false,-1});
      c.lines.push_back({0x95BD8B,Hex("51b9003c1c46660f6ec159c5fa118370010000e9b9008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BE54,Hex("e932ff7100909090"),Hex("c5fa118370010000"),false,-1});
      c.lines.push_back({0x95BDA3,Hex("51b9003c1c46660f6ec159c5fa118374010000e9b1008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BE64,Hex("e93aff7100909090"),Hex("c5fa118374010000"),false,-1});
      c.lines.push_back({0x95BDBB,Hex("51b9003c1c46660f6ec159c5fa118378010000e9b1008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BE7C,Hex("e93aff7100909090"),Hex("c5fa118378010000"),false,-1});
      c.lines.push_back({0x95BDD3,Hex("51b9003c1c46660f6ec159c5fa11837c010000e9a9008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BE8C,Hex("e942ff7100909090"),Hex("c5fa11837c010000"),false,-1});
      c.lines.push_back({0x95BDEB,Hex("51b9003c1c46660f6ec159c5fa118380010000e9a9008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BEA4,Hex("e942ff7100909090"),Hex("c5fa118380010000"),false,-1});
      c.lines.push_back({0x95BE03,Hex("51b9003c1c46660f6ec159c5fa118384010000e9a1008eff"),Hex("000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23BEB4,Hex("e94aff7100909090"),Hex("c5fa118384010000"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=6; c.builtin=true; c.name=Catalog(6)->name;
      c.lines.push_back({0x95BD6B,Hex("b8ffc99a3b8987e0990700e9d4898dff"),Hex("00000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x234749,Hex("e91d76720090"),Hex("8987e0990700"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=7; c.builtin=true; c.name=Catalog(7)->name;
      c.lines.push_back({0x95BD7B,Hex("b9ffc99a3b898fe4990700e9b3678dff"),Hex("00000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x232538,Hex("e93e98720090"),Hex("898fe4990700"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=8; c.builtin=true; c.name=Catalog(8)->name;
      c.lines.push_back({0x95BF80,Hex("c5fa104dc86683bba0000000ff751780bb880c000000750ec5f82e8bb40000007304c5f057c9e939fa8dff"),Hex("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x23B9DF,Hex("e99c057200"),Hex("c5fa104dc8"),false,-1});
      result.push_back(std::move(c)); }
    { Cheat c; c.id=9; c.builtin=true; c.name=Catalog(9)->name;
      c.lines.push_back({0x95BFB0,Hex("488b8b005a00004885c9742a504883ec08c5fa1104244889dfe852979aff88c1c5fa1004244883c4085884c97408c5fa59050e000000c5fa598d10ffffffe94a5698ff900000c03f"),Hex("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"),false,-1});
      c.lines.push_back({0x2E1635,Hex("e976a96700909090"),Hex("c5fa598d10ffffff"),false,-1});
      c.slot=0x95BFF4; c.choices={1.5f,2.0f};
      result.push_back(std::move(c)); }
    return result;
}
