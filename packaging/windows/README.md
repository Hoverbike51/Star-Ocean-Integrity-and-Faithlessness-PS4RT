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

The Release binary is `out\ps4rt.exe`. To run it with your own extracted game files:

```powershell
.\run.ps1 -Game "D:\Games\CUSA03219"
```

The launcher automatically adds `C:\msys64\clang64\bin` (or
`$env:MSYS2_ROOT\clang64\bin`) to the `PATH` for a source build. A packaged build gives priority
to the DLLs placed next to `ps4rt.exe`.

## Self-contained library package

From MSYS2 CLANG64:

```bash
bash packaging/windows/package.sh
```

The versioned ZIP is created under `dist/` (for example,
`ps4rt-star-ocean-5-windows-x64-v0.3.5.zip`). It contains the runtime, all required CLANG64 DLLs,
the native C++ preparer, the integrated PKG extractor with its private libraries, the launcher's
visual resources, and the FSR 4 assets. Players do not need Python, MSYS2, or PowerShell: the
runtime, launcher, and their libraries are all included in the ZIP.

Test the ZIP from PowerShell with a `PATH` that does not contain MSYS2:

```powershell
.\packaging\windows\smoke-test.ps1 .\dist\ps4rt-star-ocean-5-windows-x64-v0.3.5.zip
```

## Platform differences

- `mmap`/`mprotect` are replaced by `VirtualAlloc2`, `MapViewOfFile3`, and `VirtualProtect`.
- Memory faults and unsupported instructions are handled by a vectored exception handler instead
  of `SIGSEGV`/`SIGILL`.
- The guest TCB occupies a TLS slot in the Windows TEB; the loader rewrites `fs:[0]` accesses.
- Threads are still provided by winpthreads with fully validated stacks; short waits use
  high-resolution waitable timers.
- Sockets use WinSock2, random data uses BCrypt, and file operations have Win32/CRT equivalents.
  The manifest enables UTF-8, long paths, and PerMonitorV2 DPI awareness.
- The watchdog and signal-based dumps remain Linux-only features.

This port does not provide game files, firmware, or keys. The GPL-2.0-licensed PKG extractor is
integrated only for locally importing PS4 packages owned by the user. Startup, Vulkan rendering,
WASAPI audio, DualSense input, and save data have been validated with CUSA03219; a complete
playthrough still needs to be validated.
