# Windows port audit

## Code and build

- Runtime and loader: C11, plus x86-64 GNU assembly embedded in C.
- Renderer: C++23, Vulkan, adapted shadPS4 code and C/C++ third-party components.
- Preparation/launcher: native C++23 Win32/GDI+ application.
- PKG import: branded native Win32 wrapper around the bundled GPL-2.0 ShadPS4Plus extractor;
  output is captured by the launcher and the imported game is stored per-user.
- Existing Linux build: shell + CMake/Ninja for `ps4rtgpu`, then a direct C compiler/linker command
  for the loader.
- Windows build: one CMake/Ninja graph creates the static renderer, `ps4rt.exe` and the native
  `PS4RT-Launcher.exe`; clang/MinGW-w64
  is mandatory because the guest boundary needs the System V ABI on a Win64 host.

## External dependencies

Build-time/system packages: clang, MinGW-w64/winpthreads, CMake, Ninja, pkg-config, Python (tests only), Vulkan
headers/loader, SDL3, FFmpeg (`avformat`, `avcodec`, `avutil`, `swscale`, `swresample`), fmt, Boost,
xxHash, tsl-robin-map, Vulkan Memory Allocator, Zydis, glslang, SPIR-V Tools/Headers/Cross.

Vendored components include LibAtrac9, ImGui, miniz, magic_enum, xbyak, sirit, the GCN helper,
half, FSR-Vulkan and FSR 4 v07 assets. Each retains its own license. Windows players only need
Windows 10/11 x64 and a Vulkan 1.3-capable display driver; the public package includes all user-space DLLs.
The Windows package also isolates the ShadPS4Plus PKG extractor and its libc++/zlib runtime under
`tools/pkg-extractor`, with the applicable notices included.

## Linux/POSIX surface and Windows counterpart

| Linux/POSIX dependency | Windows implementation |
|---|---|
| `mmap`, `munmap`, `mprotect`, `memfd` and fixed shared views | Reserved placeholders, pagefile-backed sections, `VirtualAlloc2`, `MapViewOfFile3`, `UnmapViewOfFile2`, `VirtualProtect` |
| `SIGSEGV`, `SIGILL`, `SIGTRAP`, `ucontext`, `sigsetjmp` | Vectored exception handler, Win64 `CONTEXT`, custom `ps4rt_setjmp`/`ps4rt_longjmp` |
| Movable GS base via `arch_prctl` | Low TEB TLS slot; guest `fs:[0]` loads are rewritten to the slot displacement in `gs:` |
| pthreads and POSIX condition variables | MinGW-w64 winpthreads; guest stacks are committed fully |
| System V fiber context assembly | COFF-compatible System V save/restore/start bridge on guest-provided stacks |
| `clock_gettime`, `nanosleep` | winpthreads clocks plus high-resolution waitable timers for short sleeps |
| `pread`, `pwrite`, `fsync`, `mkdir`, `realpath`, POSIX errno | CRT/Win32 wrappers in `compat_win.c`; UTF-8 process manifest |
| `dirent`, `stat`, rename/unlink | MinGW CRT where compatible; Win32 rename and recursive tree deletion where semantics differ |
| `/dev/urandom` / `getrandom` | `BCryptGenRandom` |
| BSD sockets and `arpa/inet.h` | WinSock2 / `ws2tcpip.h`, linked with `ws2_32` |
| `dlopen`/`dlsym`, ELF host symbol lookup | Not needed for the statically linked Windows renderer; Win32 module APIs are used for crash locations |
| X11 Vulkan surface | SDL Win32 handle and `VK_KHR_win32_surface` |
| `fork`/`exec`-style restart | `CreateProcessA`; the new launcher waits for the old GPU process |
| rusage/process CPU accounting | `GetProcessTimes` / `GetThreadTimes` |
| Descriptor-based save backup | Reparse-point-safe recursive Win32 copy followed by atomic directory rename |

## Intentionally Linux-only

- Signal-driven watchdog/thread sampling and `close_range` diagnostics.
- Linux PGO flags and Linux binary/container packaging.
- X11 integration. Windows uses the SDL Win32 surface path.

These exclusions do not remove game features. They affect diagnostics, profiling and packaging.

## Remaining validation and constraints

- Preparation and a live rendering/audio/input startup were validated with a real CUSA03219
  extraction. The game files are not included and must not be distributed.
- The launcher also recognizes the PS4 editions CUSA03246, CUSA01722 and CUSA03567, but marks
  them as untested. Any other PS4 `TITLE_ID` is rejected before preparation or launch.
- A Windows Vulkan 1.3 driver and suitable GPU are required for renderer validation.
- The full native link and GPU execution require the documented MSYS2 CLANG64 toolchain.
- The project remains experimental; successful startup and live rendering do not imply a verified
  full Windows playthrough.
- Enabling Git long paths is required before checkout because generated FSR filenames exceed the
  classic 260-character path limit.
