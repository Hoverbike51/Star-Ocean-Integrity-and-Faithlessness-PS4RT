> [!IMPORTANT]
> The Windows version is experimental and still requires extensive testing, particularly by playing through the entire game and verifying its compatibility with a wider range of drivers and hardware. <br>
> Tested with a `Ryzen 9 9950X3D2` CPU and a `Radeon RX 9070 XT` GPU running Windows 11 x64

# Star Ocean 5: Integrity and Faithlessness — PS4RT for PC

An experimental, game-specific compatibility runtime for **Star Ocean: Integrity and Faithlessness**
(PlayStation 4), available for **Windows 10/11 x64** and Linux x86-64. The native Windows branch is
validated with **CUSA03219**; other recognized PS4 regions are clearly marked as untested.

PS4RT prepares and runs the game's original x86-64 code on the host, supplies
compatibility implementations of the PS4 services it needs, and uses a Vulkan
graphics renderer derived from shadPS4. This is **not a general-purpose PS4
emulator**, a native recreation of the game, or a replacement for owning the
game.

<p align="center">
  <img src="docs/images/readme/sample.png" alt="Star Ocean: Integrity and Faithlessness running on Linux with PS4RT" width="900">
</p>

**No game files, firmware, keys, or PSN downloads are supplied.** You must provide your own
legally obtained PS4 game. The Windows package includes the licensed ShadPS4Plus PKG extractor
for local imports; the Linux release still expects an extracted, plaintext game directory.
This project does not download games, connect to PSN, or bypass operating-system security and
is not affiliated with Square Enix, Sony, or AMD.

## Download and play (no compiling required)

**Want to play the game? Use the prebuilt Linux release. You do not need to compile PS4RT or install FFmpeg, SDL3, or other bundled application libraries.**

1. **Download:** Go to the [latest GitHub release](https://github.com/HekamiAeon/Star-Ocean-Integrity-and-Faithlessness-PS4RT/releases/latest) and download the **`star-ocean-5-ps4rt-linux-x86_64-412d2b1.tar.gz`** file. This is the game runtime; the separately labeled `source` archive is for developers.
2. **Have your own extracted PS4 game:** The launcher needs an already extracted, plaintext **`CUSA03219` folder** containing `sce_sys/param.sfo`, `eboot.bin`, `sce_module/`, and the game's data. **Point to the CUSA03219 folder itself**, not the PKG, not its parent directory, and not the `eboot.bin` file.
3. **Extract the Linux download** (using your file manager's Extract option, or the commands below).
4. **Launch the game** using the command below, replacing the example game path with the real location of your `CUSA03219` folder:

```bash
tar -xzf star-ocean-5-ps4rt-linux-x86_64-412d2b1.tar.gz
cd star-ocean-5-ps4rt-linux-x86_64-412d2b1
bash Play-Star-Ocean.sh --game "/path/to/CUSA03219"
```

**That's all you need to launch it.** Keep the extracted release folder together and writable. The first launch prepares the game's working files locally; it does not modify your original game extraction. Paths containing spaces must be inside quotes, as shown.

**Graphics are controlled in-game.** Hold **L2** and **click the right stick (R3)** to open the graphics settings. On a keyboard, press **Insert**. Choose the resolution (1080p, 1440p, 4K), upscaling (Off, FSR 3.1, or FSR 4 v07 where supported), sharpening, AF, and Sync/Async shader compilation. Use the D-pad, Cross to confirm, and Circle to go back.

**You do NOT need to specify FSR or resolution at launch.** Those command-line options are entirely optional. If you prefer, you can start directly with FSR 4 and 4K output:

```bash
bash Play-Star-Ocean.sh --game "/path/to/CUSA03219" --upscaler fsr4 --output-resolution 4k
```

**IMPORTANT: Changing anisotropic filtering (AF) requires restarting the game.** The other graphics settings above apply without a restart.

**Player requirements:** x86-64 Linux with glibc **2.35 or newer**, Python 3, a working Vulkan loader/driver, and compatible host display/input libraries. Required app libraries such as FFmpeg 8 and SDL3 are already in the download. No `LD_LIBRARY_PATH` command is needed. Fedora gameplay is verified; SteamOS and other Linux distributions have not yet been independently verified.

**Architecture support:** The prebuilt runtime targets **native x86-64 (AMD64) CPUs only**. ARM64 devices, including Snapdragon-based handhelds running Linux through FEX or Box64, are **not supported or validated**. Such translation layers may start the game, but correct execution and stability are not guaranteed. An ARM-compatible Linux distribution does not remove the CPU translation requirement.

For a more detailed walkthrough, see [Running on Linux](docs/RUNNING_LINUX.md). The download does **not** include PS4 game files or firmware.

## Current status

**Experimental and playable through tested early-story sections; not a verified
full playthrough.** Tested on Fedora with an **AMD Ryzen 7 9700X** CPU and
**NVIDIA GeForce RTX 3080 Ti** and **AMD Radeon RX 9070 XT-class**
graphics (the AMD hardware used was a **Radeon AI PRO R9700**). Verified
gameplay includes the introduction, story cinematics, menus, audio,
controller input, town exploration, battles, and save/load progression
through **Chapter 4**.
4K output with FSR 4 v07 has also been tested. Other hardware
configurations, Linux distributions, and the full game have not
been independently verified.

- SDL-compatible gamepads, including DualSense and XInput, plus keyboard controls.
- Output at **1080p, 1440p, or 4K**, with **FSR 3.1**, optional **FSR 4 v07**,
  and sharpening controls.
- **Anti-aliasing (AA)** is provided through the selected FSR reconstruction
  mode. Sharpening is a separate image adjustment.
- **Anisotropic filtering (AF)** is separate from AA: choose Game default,
  2×, 4×, 8×, or 16×. AF changes require restarting the game.
- Custom illustrated PS4RT in-game graphics overlay with menu sound effects.
  Open it with **Insert**, or press **L2 + R3 click**. Navigate with the D-pad,
  confirm with Cross, and go back with Circle.
- **Synchronous / asynchronous shader compilation** is selectable live from
  the in-game overlay, with the choice saved when the overlay closes:
  - **Sync (default):** occasional stutters while new shaders load, with
    more reliable rendering.
  - **Async:** smoother gameplay during shader loading, with occasional brief
    visual flickers in some areas.
- **Exit Game** in the graphics menu requests confirmation before returning
  to the Linux desktop. Save your in-game progress first; this action does
  not trigger an automatic game save.
- Higher output resolutions use reconstruction/upscaling rather than a claim
  of native 4K game rendering.
- Original game frame-rate behavior is retained; no bundled 60 FPS patch or
  frame-generation gameplay mode is claimed.

FSR 3.1 is available in the runtime. The FSR 4 **v07** shader/model assets
and their license notice are included in `fsr4_shaders/`; hashes and provenance
are recorded in `packaging/fsr4-v07-assets.json`. FSR 4 requires compatible
Vulkan GPU features and otherwise falls back to FSR 3.1. These are **upscaler
dependencies**, not cached shaders compiled from the game.

## Build from source (developers only)

**Downloaded the prebuilt Linux release? Skip this section.** The commands below are for contributors compiling PS4RT; players should use the `Play-Star-Ocean.sh` instructions above.

From a source checkout, install a C/C++ toolchain, CMake, Ninja, pkg-config, Python 3, and development packages for Vulkan, SDL3, FFmpeg, fmt, Boost, xxhash, robin-map, Vulkan Memory Allocator, Zydis, glslang, and X11. Included third-party source components are built as needed.

```bash
PS4RT_LTO=OFF bash build.sh --test
bash run.sh --game "/path/to/CUSA03219"
```

The old source-build commands remain valid on a host that has the required development packages. The `--test` build runs automated tests, not a full gameplay test. Source checkouts prepare private, game-derived files under `out/` without modifying the original game folder. Never commit or distribute `out/`, which can contain game-derived binaries, saves, and GPU caches.

For creating the glibc 2.35 release binaries with the standardized Ubuntu 22.04 container instead of a host-native source build, see [Linux release build instructions](packaging/linux/README.md) and the [distribution guide](docs/DISTRIBUTING_LINUX.md).

### Windows 10/11 x64 source build

The native Windows target uses CMake/Ninja and clang/MinGW-w64 from **MSYS2 CLANG64**. MSVC is
not ABI-compatible with the guest bridge (`sysv_abi`, GNU x86-64 assembly and recovery across
guest frames), so the Visual Studio compiler/generator is not supported. From PowerShell, after
installing the dependencies listed in [the Windows build guide](packaging/windows/README.md):

```powershell
.\build-windows.ps1
.\out\PS4RT-Launcher.exe
```

The Cockpit 0.3.5 graphical launcher remembers the PS4 game directory, prepares and starts the
game without opening a PowerShell window, manages save locations and backups, applies the video
settings supported by the runtime, and provides exportable diagnostics. `CUSA03219` is the tested
edition and receives a green status indicator. The `CUSA03246`, `CUSA01722`, and `CUSA03567` PS4
editions are recognized as untested and receive an orange warning. Every other `TITLE_ID` is
rejected with a red indicator and cannot be prepared or launched.

The `.pkg` button starts the integrated ShadPS4Plus extractor. Imports are stored under
`%LOCALAPPDATA%\PS4RT\Games\Imports` by default or in a directory selected by the user. The
launcher detects the PS4 `TITLE_ID` and remembers the extracted game directory automatically.
The native C++ SELF/link preparer is built into the launcher and does not require Python on the
player's computer.

The Release executables are `out\PS4RT-Launcher.exe`, `out\ps4rt.exe` and
`out\PS4RT-PkgExtractor.exe`. To create the versioned
0.3.5 ZIP containing their native SDL3, FFmpeg, Vulkan-loader and MinGW runtime DLLs, run
`bash packaging/windows/package.sh` from MSYS2 CLANG64.
The public ZIP keeps `PS4RT-Launcher.exe`, `ps4rt.exe` and their DLLs in the same directory and
requires no Python, PowerShell or MSYS2 installation. The Windows port is experimental and
must still be validated against a complete playthrough and a wider driver/hardware matrix.

### Keyboard and controller remapping

Default keyboard controls include **WASD** (left stick), **arrow keys**
(right stick), **Space** (Cross), **Left Shift** (Circle), **E** (Square),
**Q** (Triangle), and **I/J/K/L** (D-pad).

You can remap PS4 buttons at launch through `PS4RT_KEY_MAP`:

```bash
PS4RT_KEY_MAP="cross=Q,triangle=Space" \
  bash run.sh --game /path/to/your/CUSA03219
```

`PS4RT_PAD_MAP` similarly remaps gamepad buttons. These are launch-time
environment variables, **not keys in `ps4rt.ini`**; there is currently
no in-game key-binding editor. Default mappings and keyboard stick axes
are defined in [`src/runtime_pad.c`](src/runtime_pad.c). These button maps
do not remap the analog stick axes or the PS4RT menu shortcut.

### Saves and private files

The source checkout stores generated game code, settings, saves, and GPU
caches under `out/CUSA03219/`. Notable paths include:

- `out/CUSA03219/ps4rt.ini`: saved in-game graphics settings.
- `out/CUSA03219/user/savedata/`: game save data.
- `out/CUSA03219/user/savedata-backups/`: save backups, when present.
- `out/CUSA03219/user/gpu/cache/CUSA03219/`: generated GPU shaders
  and pipeline caches, when present.

**Never publish the `out/` directory.** Prepared game executables and
linked data can contain proprietary game material, while savedata and caches
belong to individual players. None of these belong in source control or
release archives.

## Credits, origins, and license

This Star Ocean-specific project is **adapted open-source compatibility
software**, not a clean-room implementation. Its runtime builds on work from:

- [FelipePrado0/gow3_pc](https://github.com/FelipePrado0/gow3_pc), the
  God of War III compatibility-runtime project from which the porting
  runtime was adapted.
- [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)
  and [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc),
  earlier Bloodborne PC runtime and launcher lineage.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) and related Vulkan
  renderer and shader-recompiler contributors.

Development also used [REA: Reverse Engineer Anything](https://github.com/morluto/rea)
and [Ghidra](https://github.com/NationalSecurityAgency/ghidra) as
reverse-engineering **analysis tools**, not embedded runtime code.

See [Upstream credits and notices](docs/UPSTREAM_CREDITS.md) for additional
contributors, source provenance, and third-party licenses. Original authors'
copyright and license notices remain in the source tree. The project's
runtime source is distributed under **GPL-2.0-or-later**; vendored components
retain their respective terms. A separate project name and fresh Git history
do not change those obligations.
