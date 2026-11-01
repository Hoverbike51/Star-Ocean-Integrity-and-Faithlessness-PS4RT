Star Ocean 5 - Windows x64 - Version 0.4.0
==================================================

No game files are included. Use your own decrypted and extracted Star Ocean 5 PS4 directory
containing eboot.bin, sce_module, and sce_sys. CUSA03219 is the tested edition; other recognized
PS4 editions are clearly marked as untested.

Player requirements
- Windows 10/11 x64 and an x86-64 processor supporting the instructions checked at startup.
- A recent graphics driver with Vulkan 1.3 support.
- No Python, MSYS2, PowerShell, or other external runtime is required.
- The required C/C++ DLLs are included next to the executables.

Recommended launch method
  Double-click Star Ocean 5 Launcher.exe at the root.
  This native entry starts bin\Star Ocean 5 Launcher.exe without a visible console.
  Extract into a new writable folder and keep the complete folder together.
  Select your own game directory or import a supported PKG on first launch.
  OBS Game Capture must target Star Ocean 5.exe after this filename change.

The Cockpit launcher remembers the game directory, prepares the game, and starts it without a
visible PowerShell window. Its Video tab lists the Vulkan 1.3 graphics adapters detected on the
computer and applies the selected adapter when the game starts. Automatic selection is recommended.

Graphics are controlled in-game. Hold L2 and click the right stick (R3) to open the graphics
settings. On a keyboard, press Insert.

The launcher also controls display mode, FSR, frame-rate limits, VSync, anisotropic filtering,
and asynchronous shaders. The game renders its scene at a fixed 1080p input; output resolution
and reconstruction are separate settings. It shows the latest save, supports a custom save
directory, and can export diagnostics.
In windowed mode, Image fitting selects Original (fit with bars) or Stretch to window.
Stretch includes the HUD and changes final presentation, not internal scene resolution.
Target monitor selects the game's display and remembers it across launches. With one display,
that monitor is shown automatically. Aspect ratio filters All / 4:3 / 16:9 / 16:10 / 21:9 /
32:9 output choices; unsupported families and sizes are omitted. The game scene/HUD remain
16:9 with bars in Original mode, or stretching when selected in windowed mode. Exclusive
fullscreen requires an exact Windows display mode; borderless keeps the desktop mode.
Experimental scene presets can request a different rendering input. Some game passes do not
support every preset; rejected presets restore safe rendering. Original 1080p remains the default.
PKG import displays actual expanded-byte progress and stores the completed game under its
detected Title ID in the selected parent directory. Existing Title ID folders are not replaced.

Automatic save backups on the Game tab controls the runtime's existing save-backup mechanism.
It uses one native backup engine for a pre-launch snapshot and supported game save backups.
Switch changes apply on the next game launch. Turning it off preserves normal save/load.
Open backups opens the detected Title ID's folder under:
  out\CUSA03219\user\savedata-backups\1\<title-id>\
This remains the backup location when the live save directory is customized. Snapshots contain
data\ and optional sce_sys\ metadata; existing snapshots are not automatically pruned.
Legacy save-backups copies are preserved, but new copies are no longer written there.

The PKG selection button uses HoverMods Vault's independent native PkgExtractor.exe.
It supports ordinary PS4 PFS/PFSC packages with the standard creation passcode. Unknown
passcodes and unsupported formats produce a clear error; see docs\LAUNCHER_GUIDE.md for limits.
By default, the extracted content is placed in:
  %LOCALAPPDATA%\PS4RT\Games\Imports\<title-id>
The launcher asks for confirmation, lets the user choose and remember the extraction directory,
shows timed progress, and can cancel while cleaning up partial files. A supported extracted game
directory is detected and remembered automatically. Extraction runs without a visible console,
and its English messages are available under Logs & diagnostics.
Only use packages for which you legally own a copy.

By default, prepared files, settings, saves, and caches are stored under
out\CUSA03219\. The save directory can be customized on the Game tab.
The integrated native C++ preparer creates the working image without modifying the original game
directory. Keep the entire player folder together. Application executables and DLLs are in bin;
the root launcher is a small native entry point. Assets and out remain at the player root.
Keep VERSION and bin\SO5-Updater.exe with the player folder as well. Advanced >
Check now queries Hoverbike51's published Windows releases. A tag or source archive alone
is not a player update. Download starts a verified update and restarts the launcher after
installation; Open GitHub only opens the release page, and No postpones the update.
Automatic installation is available in standalone player folders, not source checkouts.
Saves, settings and caches are preserved. See docs\LAUNCHER_GUIDE.md for details.
The program is experimental and has not been validated across the entire game or on every GPU.

Optional performance diagnostics
The native performance probe is integrated into the runtime and is disabled by default on a new
installation. Enable Performance capture tools in Advanced to show the two buttons under
Logs & diagnostics. Select Capture next session only when collecting a report. Showing the
buttons does not enable recording by itself.
Recording creates local CSV/JSON files in out\CUSA03219\user\gpu\performance; nothing is uploaded.
Disable capture after the test. The on-screen performance overlay is a separate setting.
Personal reports, Windows CPU tracing tools and developer profilers are excluded from the player
package. Players do not need Python or any tracing software to use the integrated capture.

Licenses and source
The project, adapted renderer and bundled libraries retain their original copyrights.
See LICENSE, docs\UPSTREAM_CREDITS.md and docs\third-party\DEPENDENCIES.txt, with the
corresponding notices beside that inventory. FSR model notices remain in fsr4_shaders\.
When redistributing a build, provide its exact corresponding project and dependency sources;
a different or older repository revision does not identify this build's source.

Illustrated documentation
- docs\LAUNCHER_GUIDE.md explains all six launcher tabs with screenshots.
- docs\IN_GAME_MENU_GUIDE.md explains the custom menu and when settings take effect.
- CHANGELOG.md records Windows changes and known experimental limitations.
Keep docs\images with the guides to retain the illustrations.
