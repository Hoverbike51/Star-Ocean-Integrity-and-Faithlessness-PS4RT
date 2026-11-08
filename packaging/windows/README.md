# Native Windows x64 build

The resulting runtime is a native Windows 10/11 x64 executable. MSYS2 is used only as the build
toolchain and is not required on the player's computer after packaging.

## Why not MSVC or the Visual Studio generator?

The PS4 guest code uses the x86-64 System V ABI. The loader relies on
`__attribute__((sysv_abi))`, GNU assembly, and a specialized Win64 `setjmp` implementation that
crosses guest frames without SEH unwind information. MSVC does not provide these mechanisms.
The port therefore uses CMake/Ninja with clang/MinGW-w64 in **MSYS2 CLANG64**. Visual Studio can
be used as an editor, but its compiler and linker must not be selected for this target.

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
`dist\ps4rt-star-ocean-5-windows-x64-v1.0.0-latest\`. It contains only the executables, required DLLs,
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
.\packaging\windows\smoke-test.ps1 .\dist\ps4rt-star-ocean-5-windows-x64-v1.0.0-latest
.\packaging\windows\smoke-test.ps1 .\dist\ps4rt-star-ocean-5-windows-x64-v1.0.0-latest.zip
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
