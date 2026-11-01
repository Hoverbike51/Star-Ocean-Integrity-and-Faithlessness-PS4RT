# Upstream source credits and notices

PS4RT is an independent project and release effort, but its
compatibility runtime is **adapted open-source software**, not a clean-room implementation.
The current source tree incorporates earlier runtime and renderer work. The original
contributors remain credited here even where their game-specific features were removed.

The Windows port was developed with assistance from OpenAI Codex to speed up developmentand 
help with implementation and debugging. This is my first project of this kind. I’m learning
as I go and will continue improving the Windows version, focusing on stability, performance, and useful features.

## Runtime lineage

- [Felipe Prado / gow3_pc](https://github.com/FelipePrado0/gow3_pc): original
  God of War III PC compatibility-runtime implementation from which the runtime was adapted.
- [deadinside28 / bloodborne_pc](https://github.com/deadinside28/bloodborne_pc):
  earlier runtime lineage.
- [Supermedo / bloodborne_pc](https://github.com/Supermedo/bloodborne_pc):
  earlier Windows adaptation and launcher lineage.
- [shadPS4 contributors](https://github.com/shadps4-emu/shadPS4):
  emulator video core, GPU drivers and shader-recompiler foundations.
- [cuesta4 / shadPS4](https://github.com/cuesta4/shadPS4/tree/eltutz):
  related renderer and compatibility contributions.
- [AzaharPlus / shadPS4Plus PKG Extractor](https://github.com/AzaharPlus/shadPS4Plus):
  historical PKG helper used by earlier Windows builds. It has been replaced by the
  independent HoverMods Vault implementation and is no longer included in the player package.
- [HekamiAeon / SO5-PS4RT](https://github.com/HekamiAeon/Star-Ocean-Integrity-and-Faithlessness-PS4RT):
  Main Linux build version. The Windows build was forked from this Linux version.

## Research and reverse-engineering tools

- [REA: Reverse Engineer Anything (morluto/rea)](https://github.com/morluto/rea):
  agent-facing investigation tooling used during development with native binary
  analysis. REA is **not** a runtime-code ancestor or a bundled game dependency.
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra):
  reverse-engineering suite used for binary analysis alongside REA. Its
  software is not incorporated into the distributed runtime.

## Licensing

This project retains source-level copyright, SPDX and license notices.
The project license is [GPL-2.0-or-later](../LICENSE). Adapted files and vendored
dependencies keep their original license notices under their respective source
directories. Source builds may link local system dependencies; the Windows package includes
their declared terms and installed notices in its own dependency inventory.

FSR-Vulkan/AMD FidelityFX, ImGui, LibAtrac9, Sirit, half, xbyak, miniz, magic_enum,
DejaVu fonts, and additional vendored libraries retain their own authors and licenses.
Refer to the included third-party source and license files for the applicable terms.
Windows player packages additionally collect the exact bundled DLL package versions,
declared licenses, upstream URLs and installed notices under docs/third-party/. GNU
license texts for these dependencies and notices from linked source dependencies are retained.

The native Windows updater uses the existing Boost headers (Boost Software License 1.0),
miniz (MIT), and the Windows WinHTTP, CNG and version-resource APIs. Its standalone installer
links the CLANG64 libc++ runtime statically; the libc++ notice is retained under
`docs/LIBCXX_LICENSE.txt`. It does not use a Python, PowerShell,
or .NET update engine.

HoverMods Vault's native PKG Extractor uses original C++ source, Windows CNG and miniz.
Format research references and supported-package limits are listed in NATIVE_PKG.md.
The miniz notice is distributed in docs/MINIZ_LICENSE.txt. The independent extractor's
publisher metadata does not change any upstream runtime or third-party attribution.

A distinct PS4RT project name does not change the copyright or origin of
code incorporated from upstream projects.
