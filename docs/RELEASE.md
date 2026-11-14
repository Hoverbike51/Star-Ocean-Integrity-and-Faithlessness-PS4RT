# Star Ocean 5 for Windows - v1.1.1

**Windows 10/11 x64 player build**

[README](../README.md) | [Launcher guide](LAUNCHER_GUIDE.md) |
[In-game menu guide](IN_GAME_MENU_GUIDE.md) | [Full changelog](../CHANGELOG.md)

This Windows adaptation combines a native Cockpit launcher, C++ game preparation and a
game-specific compatibility runtime. It requires your own supported PS4 game files;
the original game's complete source code has not been reconstructed.

## Changes in v1.1.1

- Diagnostic maintenance release: more reliable CPU/GPU readings and Technical overlay.
- Capture format 7 appends presentation-stage timings, completed runtime synchronization
  waits, sampled applied render size and requested/effective reconstruction IDs to the CSV.
- JSON groups frames over 100 ms into bounded contexts, retaining sampled busy threads and
  runtime wait snapshots at the worst frame. It also counts frames over 100/250/500 ms.
- Detailed tracing activates only during a performance capture. Fixed context/thread limits
  and explicit sampling age help keep collection bounded and interpretation honest.
- Illustrated guides are retained, with an appended [1.1.1 procedure](LAUNCHER_GUIDE.md#performance-probe-111).
- This release gathers evidence for issue #11; it does not claim to fix that slowdown or
  the known location-dependent Fidel follower issue. No rendering/gameplay algorithm is changed.

## Changes in v1.1.0

- Validated correction for the official AMD FSR Vulkan black scene: preserve actual
  input resource formats and correctly declare SDR/HDR color data.
- Clear reconstruction names: AMD FSR 3.1.5, AMD FSR 4 v7 (INT8), AMD FSR 4.1.2,
  and DLSS. Hardware/component checks remain in effect. The SDK may select a
  compatible fallback provider; the performance overlay reports that effective name.
- Responsive launcher scaling, reduced video-page clutter and contextual in-game help.
- Persistent Original/Extended background sizing in compatible HUD JSON presets,
  with explicit JSON choices taking precedence over automatic sizing. Loading blackout
  remains full-output; foreground UI, illustrations and videos retain their proportions.

## Fixes in v1.0.2

- Fix the crash before the ending credits caused by missing PS4 GameLiveStreaming imports.
- The ending credits and subsequent videos have been validated in gameplay.
- Add regression tests for the ending capture-service calls and strict import resolution.
  Console broadcast requests do not start network streaming or control host recording.

## Previous maintenance changes in v1.0.1

- Standalone HUD preset identities, complete default DLSS packaging and the missing
  time-conversion import reported by the trophy-menu crash diagnostic.
- More robust update installation handoff and temporary file-lock handling.
- Separate launcher/quick-play desktop shortcuts and timestamped console records.

These changes are available in the source checkout. Build the 1.1.1 player package locally; this documentation update does not publish or replace any GitHub release asset.

## Highlights since v0.4.0

- Native 21:9 and 32:9 scene rendering, while keeping prerendered videos in their original
  aspect ratio and game menus centered without stretching their illustrations.
- Extended loading/menu backdrops and compatible HUD JSON presets, including 16:9 layouts.
  Without an active custom layout, the original HUD is retained.
- Manual 720p, 1080p, 1440p and 2160p internal scene resolution with reconstruction disabled.
  With FSR or DLSS enabled, provider presets determine the scene input resolution.
- FSR 3.1 and FSR 4 v07 availability checks, plus optional NVIDIA DLSS Super Resolution
  through a separately built bridge. Unavailable modes remain visible but disabled.
- Finite-range shader color/half-float corrections and Windows fiber hardening.
- New **Cheats** and **Tweaks** tabs in both the launcher and the in-game Cockpit menu.
- Eight version-checked native cheats: infinite HP, infinite MP, fast level-up, maximum
  stats, maximum money, maximum SP, one-hit kills and movement speed (x1, x1.5 or x2).
  Imported cheat profiles and the unsafe mass-inventory code have been removed.
- **Change leader (Let Me Lead! Fidel!)** for exploration, with O/P or D-pad left/right
  cycling when allowed by the current party/game state and guards against repeated transitions.
- **Camera fixes** with Original, Light damping and Damped follow modes. These adjust
  positional follow while preserving manual rotation, FOV and native collision handling.
- Controller hot-plug/reconnection, custom keyboard/mouse bindings and hold-to-accelerate
  using F2 or R2+L2 when enabled.
- Performance overlay showing the actual presented-frame reconstruction mode/preset,
  selected GPU and game-process CPU usage. Device names can be hidden independently;
  top-center and bottom-center positions are now available.
- Updated English guides, screenshots and twelve-language interface text.

## Installation and first launch

1. Extract the entire player archive into a new writable folder. Keep its structure intact.
2. Open **Star Ocean 5 Launcher.exe** at the root of that folder.
3. Select your own supported PS4 game folder, or select a supported PKG and confirm its
   extraction destination. Prepare the game if requested, then choose **Play**.
4. Press **Insert** or **L2+R3** in game to open the custom Cockpit menu.

No Python installation is required on a player's machine. A compatible Vulkan 1.3 driver
is required. Reconstruction modes have additional device/provider requirements. DLSS needs
a compatible NVIDIA GPU, the optional bridge and NVIDIA runtime; it is not available in
every build. A published source archive or Git tag is not a ready-to-play package.

CUSA03219 is the tested PS4 Title ID. Other recognized Star Ocean 5 PS4 editions are
marked untested; unrelated games are rejected. Individual patches/cheats additionally
require a matching executable/version. PKGs with unsupported formats or unknown creation
passcodes cannot be imported by the native extractor.

## Clean player folder

The expected distribution folder is
`ps4rt-star-ocean-5-windows-x64-v1.1.1-latest`.

| Entry | Purpose |
| --- | --- |
| Star Ocean 5 Launcher.exe | Root entry point; starts the graphical launcher in bin. |
| bin/Star Ocean 5 Launcher.exe | Cockpit launcher and native preparation. |
| bin/Star Ocean 5.exe | Game runtime. |
| bin/PkgExtractor.exe | Native PKG reader. |
| bin/SO5-Updater.exe | Internal update installer. |

Keep the supplied DLLs, assets and notices with these application files. Do not distribute
game data, personal settings, saves, caches or diagnostic captures. The optional performance
probe is disabled by default; its session controls appear when enabled in Advanced.
For source builds, **build-windows.cmd** creates the player folder under **dist**.

Executable version resources follow **1.1.1 / 1.1.1.0**, with **HoverMods Vault** as the
application company. Third-party code, shader models and game artwork retain their own
authors, licenses and notices. Preserve corresponding-source obligations when redistributing.

## Known limitations

- FSR 4 v07 can lose smoothing and display a bottom-edge line outdoors or after a zone
  transition. No validated fix is included. Disabling reconstruction avoids the reported
  line but reduces temporal smoothing.
- The title-screen globe and brief 16:9 composition during some zone transitions are not
  fully adapted to ultrawide. Subtitle layout control remains incomplete.
- Higher internal scene resolution does not replace original HUD textures. Some guest
  compute/post-processing stages can retain their original dimensions.
- Camera damping does not lock camera height or completely eliminate terrain-induced
  vertical movement. FOV control, XeSS and frame generation are not implemented.
- One-hit kills can block scripted story battles. Disable that cheat for those battles.
  Progression/currency changes already earned remain after a cheat is disabled.
- Hardware-wide stability and visual quality still require testing. A fixed frame rate,
  complete playthrough compatibility and sub-3-ms launcher latency are not guaranteed.

## Validation status

The ending-crash correction was validated in gameplay: credits and subsequent videos
play without crashing. All 18 loader regression tests and the two targeted trophy/time
checks passed before that gameplay validation.

The source version is now **1.1.1**. Isolated compilation and regression checks do not
replace the installed test build or generate a player ZIP. Build and validate the final player package
before publishing it; broader hardware and rendering issues remain separate.

Report issues with the Title ID, APP_VER, Windows version, selected GPU/driver, active
reconstruction/preset, reproduction steps and an exported diagnostic. Keep an earlier
installation and a backup of your saves while testing.
