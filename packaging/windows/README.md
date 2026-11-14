# Native Windows x64 build

The resulting runtime is a native Windows 10/11 x64 executable. MSYS2 is used only as the build
toolchain and is not required on the player's computer after packaging.

## Why not MSVC or the Visual Studio generator?

The PS4 guest code uses the x86-64 System V ABI. The loader relies on
`__attribute__((sysv_abi))`, GNU assembly, and a specialized Win64 `setjmp` implementation that
crosses guest frames without SEH unwind information. MSVC does not provide these mechanisms.
The port therefore uses CMake/Ninja with clang/MinGW-w64 in **MSYS2 CLANG64**. Visual Studio can
be used as an editor, but its compiler and linker must not be selected for this target.
The separate DLSS bridge uses the official NVIDIA SDK and the Visual Studio C++
toolchain. This does not change the System V ABI requirements of the main runtime.

## DLSS in full player builds

Install Visual Studio 2022 or newer with **Desktop development with C++**, x64 tools
and a Windows SDK. `build-windows.cmd` downloads the required SDK files from the
official NVIDIA repository at a pinned commit, verifies SHA-256 hashes, and builds
the bridge. Both `ps4rt_dlss.dll` and `nvngx_dlss.dll` and the NVIDIA license must
be present in a full player package. A partial pair is rejected.

For offline builds use `build-windows.cmd -DlssSdk "D:\Dependencies\DLSS"` or
`DLSS_SDK_ROOT`. `build-windows.cmd -NoDlss` explicitly creates a reduced build;
it does not advertise functional DLSS. `bash build.sh` builds the main runtime
only; use the Windows wrapper for the complete player folder including DLSS.

The SDK is needed only for compilation. DLSS remains restricted to NVIDIA GPUs
that pass runtime feature checks; shipping the DLLs does not bypass those checks.

## FSR 4 v07 shader assets

The external `fsr4_shaders` folder contains the six existing v07 INT8/DOT4 model
variants and all three compiled graph tiers: `1080`, `2160` and `4320`.
The runtime selects the tier by **output dimensions**: up to 1920 x 1080,
up to 3840 x 2160, then larger supported outputs (within the runtime/GPU limits).
The `4320` tier supplies missing graph variants; it does not add a quality preset,
change model weights, retrain a model, or guarantee native ultrawide image quality.

These files are loaded at runtime and copied into the player folder. Adding the
matching precompiled shaders does not require recompiling C++ executables.
Arbitrary model names or incompatible shader/weight layouts are not auto-discovered.
Restart the game after updating assets; already created pipelines retain their code.
The runtime's embedded edge-safe RCAS remains authoritative.

Packaging and package checks verify all 273 required files against the pinned
size/SHA-256 inventory, including the `4320` shaders. Run this check separately:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-fsr4-assets.ps1
```

## Build dependencies

First enable long paths in Git (`git config --global core.longpaths true`); generated FSR 3 files
otherwise exceed the traditional Windows path-length limit. Install MSYS2, open an
**MSYS2 CLANG64** terminal, and run:

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

From the repository root:

```bash
bash build.sh
bash build.sh --test
```

Alternatively, use the wrapper that is compatible with the default Windows PowerShell execution
policy:

```powershell
.\build-windows.cmd
.\build-windows.cmd -Test
```

The wrapper also detects and regenerates a stale CMake build cache when a previously compiled
checkout has been copied or moved.

## Clean player folder

The CMD entry point builds a clean, self-contained player folder by default, without a ZIP:

```powershell
.\build-windows.cmd
```

The following explicit form remains supported:

```powershell
.\build-windows.cmd -PlayerFolder
```

The result is created under
`dist\ps4rt-star-ocean-5-windows-x64-v1.0.2-latest\`. It contains only the executables, required DLLs,
assets, integrated PKG extractor, license, and player documentation. Only a static native
`Star Ocean 5 Launcher.exe` entry remains at the root; the real launcher, runtime, extractor,
updater and DLLs are in `bin/`. Assets remain under their existing root subdirectories. Source files, CMake/Ninja
artifacts, Python scripts, tests, game files, saves, logs, and shader caches are excluded. The
folder is smoke-tested with a `PATH` that does not contain MSYS2.

The source-build runtime is `out\Star Ocean 5.exe`. To run it directly with your own extracted game files:

```powershell
.\run.ps1 -Game "D:\Games\CUSA03219"
```

The launcher automatically adds `C:\msys64\clang64\bin` (or
`$env:MSYS2_ROOT\clang64\bin`) to the `PATH` for a source build. A packaged build gives priority
to the DLLs placed next to `Star Ocean 5.exe`.

## Versioned ZIP

After release approval, run this command from MSYS2 CLANG64:

```bash
bash packaging/windows/package.sh
```

The versioned ZIP and its matching unpacked player folder are created under `dist/`. They contain
the native runtime, all required CLANG64 DLLs, the native C++ preparer, the integrated PKG
extractor, the launcher's visual resources, and the FSR 4 assets. The extractor is independent
of shadPS4 and statically linked; see docs/LAUNCHER_GUIDE.md for formats and passcode limits.
Players do not need Python, MSYS2, or PowerShell.

Test either the player folder or the ZIP from PowerShell with a `PATH` that does not contain MSYS2:

```powershell
.\packaging\windows\smoke-test.ps1 .\dist\ps4rt-star-ocean-5-windows-x64-v1.0.2-latest
.\packaging\windows\smoke-test.ps1 .\dist\ps4rt-star-ocean-5-windows-x64-v1.0.2-latest.zip
```

## Graphics menu

Graphics settings are available while the game is running. Press **Insert**, or hold **L2** and
click the right stick (**R3**), to open the menu. The Video tab in the launcher can select any
detected Vulkan 1.3 GPU; **Automatic** remains the recommended setting.

## Performance capture

The launcher can record one gameplay investigation without Windows Performance Recorder. Enable **Performance capture tools** in Advanced, then open
**Logs & diagnostics** and select **Capture next session**, start the game, and exit through the
in-game menu. **Open captures** opens the directory containing the English CSV timeline and JSON
summary. The report includes frame-time percentiles, CPU/GPU utilization, shader activity,
draw packets, DrawRec busy time, draw-queue occupancy, drain waits, the highest-cost drain reasons,
and a recording-phase breakdown. Disable the capture toggle for normal gameplay after collecting
the report.

## Platform differences

- `mmap`/`mprotect` are replaced by `VirtualAlloc2`, `MapViewOfFile3`, and `VirtualProtect`.
- Memory faults and unsupported instructions are handled by a vectored exception handler instead
  of `SIGSEGV`/`SIGILL`.
- The guest TCB occupies a TLS slot in the Windows TEB; the loader rewrites `fs:[0]` accesses.
- Threads are still provided by winpthreads with fully validated stacks; short waits use
  high-resolution waitable timers.
- Sockets use WinSock2, random data uses BCrypt, and file operations have Win32/CRT equivalents.
  The manifest enables UTF-8, long paths, and PerMonitorV2 DPI awareness.
- The former POSIX watchdog and signal-based dumps are not part of the Windows player build.

This port does not provide game files, firmware, or keys. The GPL-2.0-licensed PKG extractor is
integrated only for locally importing PS4 packages owned by the user. Startup, Vulkan rendering,
WASAPI audio, DualSense input, and save data have been validated with CUSA03219; a complete
playthrough still needs to be validated.

## Illustrated player guides

See the [launcher guide](../../docs/LAUNCHER_GUIDE.md),
[in-game menu guide](../../docs/IN_GAME_MENU_GUIDE.md) and
[Windows changelog](../../CHANGELOG.md). These public guides and their referenced screenshots
are included in the clean player folder. Development-only documents are not required for packaging.

## Build a validated release ZIP

```powershell
.\build-windows.cmd -Test -Package
```

`-Package` builds the normal full player binaries, creates the player folder and ZIP, checks
both with the native updater, and writes a fresh `.zip.sha256` sidecar under `dist/`.
The default command still creates only the player folder. Publish the generated ZIP and
matching sidecar together; the PDF manuals are already included. Do not add shortcuts or
other folders manually after the final check.

## Release-update compatibility checks

The updater installed in **v1.0.1** validates every downloaded path before it can run a newer
installer. Future player ZIPs must therefore retain its path contract. The official AMD
provider is packaged in `gpu/assets/amd-fsr-sdk24/`, its integration note in
`docs/third-party/AMD_FSR_SDK.md`, and the preserved PDF guides in
`launcher/assets/guides/`. Do not add a root `SO5_User_Guides` folder or Windows `.lnk`
shortcuts to a release ZIP. This does not remove the updater's file/resource/hash checks.

Packaging now validates the complete player folder and final ZIP with the native updater,
including the frozen v1.0.1 path contract and clean-PATH application self-tests. This gate
runs for normal builds, not only `-Test` builds. After **any** manual ZIP modification, run:

```powershell
.\packaging\windows\verify-update-package.ps1 -Package '.\dist\ps4rt-star-ocean-5-windows-x64-v1.1.1-latest.zip'
```

Use the version in your checkout's `VERSION` file. The validator comes from `out/SO5-Updater.exe`;
rebuild it after source changes. A successful path/self-test gate does not replace migration
tests covering installer handoff, locked files, rollback and user-data preservation. Rebuild
and repackage the affected release before replacing a published ZIP; source changes alone
do not repair an archive already downloaded by players.
