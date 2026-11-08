# Star Ocean 5 for Windows - v1.0.1-hotfix 1

**Windows 10/11 x64 player build**

[README](../README.md) | [Launcher guide](LAUNCHER_GUIDE.md) |
[In-game menu guide](IN_GAME_MENU_GUIDE.md) | [Full changelog](../CHANGELOG.md)

This Windows adaptation combines a native Cockpit launcher, C++ game preparation and a
game-specific compatibility runtime. It requires your own supported PS4 game files;
the original game's complete source code has not been reconstructed.

## Maintenance changes in v1.0.1

- Standalone HUD preset identities, complete default DLSS packaging and the missing
  time-conversion import reported by the trophy-menu crash diagnostic.
- More robust update installation handoff and temporary file-lock handling.
- Separate launcher/quick-play desktop shortcuts and timestamped console records.

These are source changes for validation. The published 1.0.0 asset is unchanged.

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
`ps4rt-star-ocean-5-windows-x64-v1.0.1-latest`.

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

Executable version resources follow **1.0.1 / 1.0.1.0**, with **HoverMods Vault** as the
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

This document describes the current **1.0.1 source update**, which is not yet compiled
or published. The maintenance changes were checked in an isolated source copy before
the version bump: 66 native checks and 42 Python checks passed (3 Python checks skipped),
followed by targeted shortcut/console checks and five synthetic launch scenarios.
No 1.0.1 executable or ZIP has been generated. Build and test the final player package,
including in-game HUD/trophy behavior and compatible NVIDIA hardware, before publishing it.

Report issues with the Title ID, APP_VER, Windows version, selected GPU/driver, active
reconstruction/preset, reproduction steps and an exported diagnostic. Keep an earlier
installation and a backup of your saves while testing.
