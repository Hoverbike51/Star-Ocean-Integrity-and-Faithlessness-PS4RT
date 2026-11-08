# Windows port audit

Current source: **1.0.0 (experimental, unreleased)**. Rendering/input/Tweaks and optional
DLSS additions are described in the [changelog](../CHANGELOG.md) and current player guides.
The portability and benchmark inventory below records the earlier Windows integration;
its dated test observations are not a new validation of this source export. The unresolved
FSR 4 v07 outdoor/bottom-edge defect remains listed in the current changelog.

## Code and build

- Runtime and loader: C11, plus x86-64 GNU assembly embedded in C.
- Renderer: C++23, Vulkan, adapted shadPS4 code and C/C++ third-party components.
- Preparation/launcher: native C++23 Win32/GDI+ application.
- Updates: native C++23 WinHTTP/CNG/miniz client and a statically linked Windows installer
  under `bin`. See [Native Windows updates](LAUNCHER_GUIDE.md#updates). No managed or shell
  runtime is required by the player update flow.
- PKG import: independent native C++ PS4 PKG/PFS/PFSC reader using Windows CNG and miniz;
  output is captured by the launcher and the imported game is stored per-user. See LAUNCHER_GUIDE.md, PKG import.
- Build entry points are Windows-specific. The MSYS2 build helper drives the same CMake/Ninja
  graph as the PowerShell/CMD entry points; the obsolete Linux build branch has been removed.
- Windows build: one CMake/Ninja graph creates the static renderer, `Star Ocean 5.exe` and the native
  `Star Ocean 5 Launcher.exe`; clang/MinGW-w64
  is mandatory because the guest boundary needs the System V ABI on a Win64 host.

## External dependencies

Build-time/system packages: clang, MinGW-w64/winpthreads, CMake, Ninja, pkg-config, Python (tests only), Vulkan
headers/loader, SDL3, FFmpeg (`avformat`, `avcodec`, `avutil`, `swscale`, `swresample`), fmt, Boost,
xxHash, tsl-robin-map, Vulkan Memory Allocator, Zydis, glslang, SPIR-V Tools/Headers/Cross.

Vendored components include LibAtrac9, ImGui, miniz, magic_enum, xbyak, sirit, the GCN helper,
half, FSR-Vulkan and FSR 4 v07 assets. Each retains its own license. Windows players only need
Windows 10/11 x64 and a Vulkan 1.3-capable display driver; the public package includes all user-space DLLs.
The native PKG extractor is statically linked and no longer needs the ShadPS4Plus helper or
its private DLLs. miniz and libc++ notices are included under docs.

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

## Windows cleanup inventory (0.4.0 experimental)

Removed the obsolete Linux launch script, container/build packaging, Python Linux release
packager, Linux player/distribution guides and xdotool-based developer control scripts.
The root README and build entry points now describe Windows only. Removed the obsolete
upstream Linux packaging checklist after collecting the actual Windows dependency notices.
The reference developer launcher is Windows-only; the first-party SDL window no longer has
X11/Wayland branches. CMake rejects non-Windows targets and no longer configures an X11
dependency or a Linux shared renderer. Licenses and attribution remain intact.

| Retained component | Windows role | Replacement boundary |
|---|---|---|
| MSYS2 CLANG64, build.sh and msys2-env.sh | Developer toolchain only; CMake builds native PE/COFF executables | A future shell-free build driver can replace orchestration after equivalent relocation/dependency tests; it would not change runtime behavior |
| winpthreads | Native Windows DLL implementing threading primitives used by host/guest bridges | A Win32 thread/condition-variable rewrite needs ABI, TLS, scheduling and shutdown tests; not a cosmetic removal |
| MinGW CRT and compat_win.c/h | Native Windows implementations of file, timer, random and compatibility functions | Replace individual wrappers only when their guest-visible behavior is preserved |
| System V ABI, ELF/SELF parser, GNU x86-64 assembly and FreeBSD-style guest contracts | Required to load and execute the original PS4 code on a Win64 host | MSVC or a wholesale ABI rename cannot replace these mechanisms without a separate guest bridge |
| Multiplatform branches inside vendored dependencies | Windows paths are selected by the toolchain | Preserve dependency sources, licenses and attribution; these branches are not Linux runtime requirements |
| Former Linux signal, /proc, X11 and host-syscall paths | Removed from the Windows-only runtime and adapted platform code; Win32 handlers and surfaces remain | No Linux kernel, X server, glibc, WSL or MSYS runtime is required by the player |
| Python preparation/reference scripts and tests | Developer regression/reference material; excluded from the player package | Native C++ preparation is already used for players |

The remaining platform references do not make the Windows binary a Linux workload. The game
still uses a compatibility runtime: native Windows packaging is not a claim of a full source
reimplementation of the original game engine. Keep rendering and memory correctness fixes
independent of source presentation cleanup.

## Save-backup integration

The Game switch maps to PS4RT_SAVEDATA_BACKUP=1/0 for the launched runtime. The previous
BackupBeforeLaunch setting is migrated to AutomaticSaveBackups without changing its value.
There is no second pre-launch or manual launcher copy engine. Existing legacy copies are
left untouched. Normal save/load remains available when backup snapshots are disabled.

The runtime retains its existing unmount-with-backup API, incomplete/complete snapshot
transaction and data/sce_sys layout. Snapshot paths use the mounted user, Title ID and slot,
so custom save paths are supported and different editions remain separate. Windows snapshot
names include a random suffix to prevent collisions between saves made in the same second.
Copy failure retains the mounted original and earlier complete backups. No automatic pruning
is introduced; users manage storage explicitly. Changes apply at the next runtime launch.

## Launcher responsiveness audit

Network checks/downloads already use owned worker threads; extraction and preparation run
outside the UI thread. Startup Vulkan detection and save-preview discovery are still synchronous;
no unsupported claim of asynchronous startup or measured startup-speed improvement is made.

Diagnostic pipe callbacks previously posted one window message per chunk. They now coalesce
outstanding notifications without discarding lines. Log notifications do not repaint a minimized
launcher, and an inactive modal does not take keyboard focus from another application. Restore
still invalidates the window, so accumulated logs appear when it is reopened. This reduces
redundant UI work; it does not claim a measured gameplay FPS improvement.

The Acer follow-up identified high-quality background/image resampling, per-frame bitmap
allocation and repeated font construction on hover. The launcher now caches scaled assets
and fonts, retains its canvas and invalidates only the previous/new hovered controls. Full
redraw benchmarks on the development PC fell from roughly 65–89 ms to 4–12 ms at 1360 x 760
and 1720 x 1040. These are local redraw measurements, not Acer measurements or startup times.
The --ui-benchmark developer switch writes an English JSON report without opening a window,
launching the game or modifying settings. Initial driver detection remains synchronous.

The display-format follow-up uses the actual disjoint Windows update region instead of its
bounding rectangle. Drawing helpers skip fully clipped text/artwork; filesystem availability
checks are cached outside painting. Internal hover redraw measurements across six pages at
1360 x 760 and 1720 x 1040 were roughly 0.13–0.74 ms, with matching pixels against a full
redraw. These timings exclude display scanout and do not guarantee end-to-end latency on
every PC. Full redraws, startup and resizing remain separate operations. The developer JSON
report includes hover median, p95 and maximum durations alongside full redraw measurements.
The extended check covered 144 page/size/language scenarios across all 12 interface languages
with a 1200-line log: hover medians were about 0.20–0.75 ms, p95 stayed below 1.0 ms and the
largest measured redraw was about 1.07 ms on the development PC. Pixel comparisons passed.

Initial window bounds use 84% of the selected monitor's work area, capped at 1580 x 940;
minimum resizing bounds fit the available work area. Translated navigation tabs can wrap
onto two lines, and the Home logo/play-button layout adapts to shorter windows.

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


## Windows-only source cleanup

The standalone Windows checkout no longer carries the inactive non-Windows paths in its
own runtime and shim sources. The adapted renderer platform layer, page manager, thread,
file and exception helpers likewise retain their Windows paths. Obsolete platform names
in the local window-system enum and stale launch-script descriptions have been removed.
Upstream copyright and license notices are unchanged. Vendored libraries remain intact.

The Linux-only CPU image-writeback fault fixture was removed from the Windows test target:
it previously printed a skip message and ran no assertions on Windows. The portable
writeback regression checks remain. A native Windows protected-page/fault equivalent is
still a testing gap; this cleanup does not claim to fill it.

The earlier deleted Linux packaging, launch scripts and player guides remain marked for
Git deletion until the owner stages and commits them. No history rewriting is required;
old commits will continue to contain their historical versions.

The unused adaptive pthread-mutex helper was also removed from the Windows tree;
page and region tracking explicitly retain the existing Windows SpinLock implementation.
