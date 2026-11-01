# Windows 0.4.0 experimental player build

This is a native Windows 10/11 x64 host for the game-specific compatibility runtime.
It is not a reconstruction of the original game's complete source code.

## Player entry points

- Star Ocean 5 Launcher.exe: static native root entry; starts bin/Star Ocean 5 Launcher.exe.
- bin/Star Ocean 5 Launcher.exe: graphical Cockpit launcher and native preparation.
- bin/Star Ocean 5.exe: game runtime; its running window title remains unchanged.
- bin/PkgExtractor.exe: independent HoverMods Vault native package reader.
- bin/SO5-Updater.exe: internal native update installer.

All executable resources share VERSION 0.4.0 / 0.4.0.0 and the publisher HoverMods Vault.
Upstream code, libraries, shader models and game artwork retain their authors and terms.

## Changes since 0.3.5

- Windows-specific build orchestration, relocation recovery and clean player output.
- Native C++ preparation and PKG import without a player Python installation.
- Cockpit interface with supplied artwork, localized controls, draggable header, native caption
  buttons, close confirmation, game-start minimization and corrected disabled-row hover.
- GPU selection, real runtime video settings, windowed Original/Stretch and initial client
  sizing that follows the selected output resolution without preventing manual resizing.
- Capability-based FSR 4 rejection in launcher and runtime, plus existing provider fallback.
- Controller/keyboard bindings, game identity validation and unified runtime save backups.
- Validated native-scene renderer optimizations and optional local performance capture,
  disabled by default. Capture controls are shown only when enabled in Advanced.
- Application version in the performance overlay and coherent Explorer version resources.
- Native GitHub release checking, verified download/install, backup, rollback and renamed
  executable migration. A tag or source ZIP is not a player update.
- Windows dependency/license inventory, while retaining required ABI and threading bridges.

## Installing and testing

Extract the entire player ZIP into a new writable directory. Start Star Ocean 5 Launcher.exe
and select your own supported PS4 game folder or PKG. No game data, personal configuration,
saves, shader cache, performance reports or developer tools are shipped. Keep every program
file and subdirectory together. The default performance capture is off.

Existing experimental settings, saves and caches are not copied into a fresh player ZIP.
Preserve your earlier installation while testing. OBS Game Capture must target Star Ocean 5.exe.

## Verification and limits

The preceding test build passed 34 native tests and the Python developer suite ran 42
cases with 3 skipped. The subsequent bin-layout/bootstrap and default dist-output changes
have only received static checks: they have not been compiled or runtime-tested yet.
Run build-windows.cmd to build and smoke-test the new standalone player folder.

User testing reported smooth AMD RX 9070 XT gameplay at 4K output with FSR 4 and 16x AF,
including OBS recording. This is not a complete playthrough, a guarantee for other hardware
or a physical NVIDIA validation. Windows 10 Aspire VX-15 testing remains separate.

Original scene input remains 1080p. Experimental variable-resolution presets are guarded
and can be refused for unsupported game passes. True ultrawide field of view/HUD adaptation
and texture/mesh mods remain deferred to 0.4.1+.
Known creation-passcode limits for PKG import remain explicit. Public update installation
cannot be validated against a published 0.4.0 release until the owner publishes one.

The matching source checkout is prepared locally on windows-native. No GitHub publication,
commit, push or tag is performed by this delivery. Redistributors must provide the exact
corresponding project/dependency sources and preserve the included notices.
