# Changelog

This changelog covers the **native Windows branch** of Star Ocean 5 / PS4RT.
Version numbers refer to this Windows project, not to the PS4 game version or the original
upstream release tags. The runtime remains experimental and is not a reconstruction of
the original game's complete source code.


[README](README.md) · [Launcher guide](docs/LAUNCHER_GUIDE.md) · [In-game menu guide](docs/IN_GAME_MENU_GUIDE.md)

## 1.0.1 — SO5 hotfix 1

- Include verified HUD resource/object identities in the runtime, so compatible HUD
  presets work without an external authoring catalog.
- Acquire pinned, SHA-256-verified NVIDIA build dependencies for full builds and require
  both DLSS components and their license in the player package. Keep GPU compatibility
  checks and the explicit reduced-build opt-out.
- Implement the missing `sceKernelConvertUtcToLocaltime` import identified in the
  trophy-menu crash diagnostic, with timezone/DST and output-width regression checks.
- Improve update installation handoff, temporary file-lock handling and failure recovery.
- Create separate desktop shortcuts for the launcher and quick play with saved settings.
  Quick play stays hidden unless setup or diagnostics require attention.
- Add stable local-time console timestamps that survive copying and report export.
- Correct standalone public tests and PowerShell hashing portability.
- Complete the FSR 4 v07 bundle with 84 missing `4320`-tier shader modules for
  the existing six model variants, preserving existing shaders/weights. Verify
  the full asset inventory during packaging to catch incomplete downloads.


## 1.0.0 — Windows source update

### Rendering and compatibility

- Add native ultrawide scene rendering for `21:9` and `32:9`. Keep prerendered video at
  its original aspect ratio and retain centered game menus without stretching their artwork.
- Extend loading/menu backdrops while preserving foreground text and UI proportions.
- Support validated HUD JSON layouts through the launcher and the in-game menu, including
  `16:9`. Retain the original HUD when no custom layout is active.
- Add manual `720p`/`1080p`/`1440p`/`2160p` internal scene resolution with reconstruction off.
  FSR/`DLSS` continue to own the render size through their presets. Source UI textures
  and some guest post-processing stages retain their original resolution.
- Integrate finite-range GCN color/half-float export corrections and harden Windows fibers.
- Add optional `NVIDIA` `DLSS` Super Resolution through a separately built native bridge;
  gate it on device/SDK availability. Keep unavailable reconstruction options visible
  but disabled, retaining the explicit `FSR 3.1` override for lower-tier devices.
- Preserve stable submission/cache defaults and report memory peaks/refusals. Memory
  overrides are explicit; default allowances remain unchanged.
- Acknowledge video-recording metadata requests so the runtime can continue after the
  final movie. Optional trophy journaling remains a local, opt-in host feature.

### Launcher, controls and in-game menu

- Add matching `Cheats` and `Tweaks` tabs in the Cockpit launcher and in-game menu.
- Include eight verified-version native cheats: independent infinite HP/MP, fast level-up,
  maximum stats/money/SP, one-hit kills and movement speed. Remove imported profiles and
  the unsafe mass-inventory code. Check title/version and original executable bytes.
- Use a three-position movement control: off (`x1`), `x1.5` and `x2`. Keep the scripted-battle
  warning for one-hit kills. Previously gained items/stats are not undone by disabling a code.
- Add guarded exploration leader switching, `O`/`P` and `D-pad` cycling, camera-follow modes,
  and access to the `HUD preset` folder under `Tweaks`. These options are optional.
- Restore controller hot-plug/reconnection and custom keyboard/mouse bindings.
- Use `F2` or `R2+L2` for hold-to-accelerate, with focus/menu safeguards.
- Report the actual displayed-frame provider/preset, selected GPU and process CPU usage.
  Add independent device-name visibility and top/bottom-center overlay positions.
- Update all twelve UI languages, caption buttons and resize handling.
- Retain the clean player-folder layout: root launcher bootstrap, application binaries
  in bin, and a versioned dist folder produced by build-windows.cmd. Build outputs,
  settings, saves and captures are excluded from the source repository.


## 0.4.0 — Experimental Windows update

### Windows runtime and performance

- Continued specialization of the x86-64 game/runtime boundary for Windows 10/11.
- Reduced CPU overhead in the rendering submission path, including resource-binding,
  descriptor/state reuse and repeated bookkeeping in frequently executed draw preparation.
- Retained guarded transition snapshots and corrections to image ownership, aliasing,
  subresources and CPU/GPU writeback instead of discarding synchronization for a higher FPS count.
- Refined draw-preparation worker use and the separation between preparation and resource
  binding. Parallel preparation is not a claim that every stage is safely parallelizable.
- Reduced repeated hot-path diagnostic output that could block the game on the launcher's log pipe.
- Retained local shader/pipeline cache reuse and added cache inspection/capture infrastructure
  for developer investigations.
- Disabled the experimental bulk/parallel pipeline warm-up at normal game startup after an
  AMD Vulkan-driver crash. Existing cached pipelines remain usable on demand.
- Addressed observed transition-related visual regressions, including black character
  squares and corrupted scene textures. User retests reported those artifacts absent on
  the exercised routes; other scenes and drivers still require testing.
- User gameplay reports showed a substantial improvement over 0.3.5, including tested 4K
  FSR 4 / x16 anisotropy sessions and OBS recording without the earlier severe stutter.
  These are observations from particular systems and routes, not a universal 60 FPS guarantee.

### Launcher interface and Windows integration

- Updated the Cockpit identity to **STAR OCEAN 5 / WINDOWS** with the supplied high-quality
  icon, game logo and space background.
- Kept Home deliberately minimal: game logo, large Play button and bottom status dock.
- Enlarged text and controls, improved disabled/hover states and corrected overlapping or
  clipped controls, badges and translated button labels.
- Added a clear draggable top strip, proper minimize/close buttons, close confirmation and
  automatic minimization when the game starts.
- Adapted initial window size to the Windows work area and added scrolling to long pages.
- Improved hover responsiveness with retained drawing and smaller invalidation regions;
  unchanged logs and control geometry are not rebuilt for each mouse movement.
- Kept English console diagnostics while localizing user-facing launcher/menu controls in
  12 languages, with Windows-language selection on first launch.
- Kept launcher language independent of the game's language.
- Applied versioned PE metadata and application icons consistently. The displayed company
  is HoverMods Vault; bundled third-party code and game assets retain their own attribution.

### Game setup, native preparation and PKG import

- Retained and integrated the native C++ preparation path; no Python installation is needed
  on a player's machine.
- Replaced the external ShadPS4Plus extractor/wrapper with the independent native
  **PkgExtractor.exe** reader, using Windows cryptography and miniz support.
- Added extraction confirmation, a selectable/remembered destination, real expanded-byte
  progress, cancellation cleanup and automatic detection/preparation after extraction.
- Store successful imports under the detected Title ID. Existing game directories are not
  silently overwritten.
- Report unsupported formats, malformed content and unknown creation passcodes instead of
  treating a selected PKG as an already extracted game.
- Preserved the tested/untested/refused PS4 identity policy and display detected information
  only after validation. PS3 and unrelated titles remain rejected.
- Added branded desktop shortcuts through the launcher.

### Saves and backups

- Show the actual Windows live-save path, with Change, Default and Open actions.
- Refresh save images and metadata when returning to Game. Load preview images into detached
  memory so the launcher does not hold the live-save image file open.
- Unified pre-launch and supported game-requested backups through the existing native
  snapshot engine and one launcher switch, rather than maintaining a second backup format.
- Added direct access to the detected Title ID's backup folder.
- Keep live saves replaceable while the launcher is open; completed backup snapshots are
  protected against replacement/deletion for the launcher's lifetime without changing ACLs.
- Keep backup paths tied to user, Title ID and save slot, including with a custom live-save directory.
- Hide the old save-import button; replacing or migrating an incompatible save is not
  presented as an automatic conversion operation.
- Fixed the save-loading crash introduced by the title-screen observer: depth-only pipelines
  can lack a fragment shader, and title recognition now checks the optional stage safely.
  The affected user subsequently confirmed that save loading worked again.

### Display, monitors and GPU checks

- Added graphics-adapter and target-monitor selectors, remembered display identity and an
  available-monitor fallback after disconnection.
- Added output aspect filters for All, 4:3, `16:9`, 16:10, `21:9` and `32:9` when verified choices exist.
- Filter output dimensions against the selected monitor and the supported resolution catalog;
  exclusive fullscreen also requires an exact verified Windows/SDL mode.
- Include 480p, `720p`, `1080p`, `1440p` and `2160p` `16:9` choices when supported, plus verified
  catalog entries for the additional aspect families.
- Apply selected windowed output dimensions to the initial client size while allowing
  ordinary mouse resizing.
- Added Original/Stretch to window image fitting. This changes presentation, not camera
  field of view or native HUD layout.
- Kept VSync, presentation frame caps and asynchronous shader controls connected to the runtime.
- Strengthened FSR 4 capability/asset checks and safe fallback reporting.
- Added a conservative default availability policy for `FSR 3.1`, plus an off-by-default,
  confirmed force option for technically capable devices outside that policy. The override
  is bound to the selected GPU and cannot bypass missing Vulkan features.
- Repaired low-output reconstruction selection so a supported provider is not disabled
  merely because the output is below `1080p`; scaled-input validation still applies.
- Do not expose `DLSS`, `XeSS` or unsupported rendering features as functional UI choices.

### In-game Cockpit

- Replaced the old single-page settings layout with **Display, Image, Smoothness,
  Controller and Performance** tabs.
- Added keyboard/mouse and controller navigation, including `Q`/`E` or `L1/R1` tab changes.
- Open with Insert or `L2 + R3`; use Resume to return or Exit game for a confirmation before shutdown.
- Pass the launcher's selected language to the menu at game startup and support the required fonts.
- Improved the exit-confirmation layout, wrapping and default cancel behavior.
- Restore a usable pointer and release relative/grabbed mouse input while the menu is open,
  including after display-mode or resolution changes.
- Added anisotropy selection back to Image with separate requested/current-session values.
  Changes are saved for the next game launch, not advertised as live sampler replacement.
- Added optional Windows build-version reporting in the performance overlay and a separate
  title-screen version label based on recognized title-screen artwork/draws.

### Experimental reconstruction presets

- Added Original `1080`p, Native (100%), Quality, Balanced, Performance and Ultra Performance
  scene-input requests with per-preset explanations.
- Renamed the former Native AA label to **Native (100%)** to distinguish native-resolution
  temporal anti-aliasing from a lower-resolution Quality reconstruction preset.
- Show the requested input, observed render input, per-axis input scale and pixel percentage.
- Apply a requested preset when the menu closes, with frame-boundary resource updates,
  temporal-history reset and FSR 4 model/context selection based on the actual dimensions.
- Added scaled-target allocation checks, guarded color/depth input validation and safe
  preset restoration. A failed preset does not silently masquerade as a working native render size.
- Added `Scene preset refused` and `Scene preset inputs` diagnostics for real-game refusals.
- **Known limitation:** Native (100%), Quality, Balanced and Ultra Performance have been
  refused in tested scenes. Their presence in the list is not full gameplay validation.
  Original `1080`p remains the conservative baseline; at 4K, Performance requests the same input size.

### Input and vibration

- Corrected AZERTY/QWERTY keyboard-layout handling and the transfer of custom bindings to the runtime.
- Exposed complete keyboard-button and directional mappings, saved immediately in the launcher
  and applied on the next game launch.
- List detected SDL controllers and keyboard mode instead of decorative device profiles.
- Added an 8% resting-stick dead zone with continuous rescaling to reduce small unintended
  camera motion while preserving full stick travel.
- Preserved DS4/DualSense/Xbox-style SDL controls and touchpad-click emulation paths.
- Verified the native vibration export through an SDL virtual-controller test: independent
  motors, intensity scaling, zero/stop behavior, invalid arguments and driver-failure handling.
- Added bounded vibration-failure logging and summary counters. Standard rumble remains
  dependent on the real device, connection, driver and the game's vibration events.

### Diagnostics and performance capture

- Added an optional native gameplay probe with local CSV timelines and JSON summaries,
  including frame-time, shader and CPU-side rendering/queue diagnostics.
- Added **Performance capture tools** in Advanced, off by default. It only reveals Capture
  next session and Open captures; recording must be requested separately.
- Kept capture independent of the on-screen overlay and supported report finalization during
  normal custom-menu shutdown. Forced termination can still leave a partial report.
- Replaced the console display with a read-only native Rich Edit control supporting text
  selection, `Ctrl+A`, `Ctrl+C` and independent horizontal/vertical scrolling.
- Preserve the reader's selection/scroll position while output arrives instead of forcing
  every reader back to the newest line.
- Corrected premature console wrapping, duplicate presentation and badge/button layout.
- Export English diagnostic reports and keep personal captures, settings and logs out of player packages.

### Updates, packaging and source builds

- Added native GitHub release discovery for the Hoverbike51 fork, with localized download,
  progress, release-page and postponement dialogs.
- Verify package size, SHA-256 digest, Windows archive paths, executable versions and staged
  startup before installation. A tag or GitHub source ZIP alone is not an installable update.
- Added a static native update helper, controlled launcher shutdown/restart, per-file backups
  and transactional rollback of attempted replacements. Preserve player data and unknown files.
- Automatic installation targets standalone player folders, not development checkouts.
- Renamed the public applications to **Star Ocean 5.exe**, **Star Ocean 5 Launcher.exe**
  and **PkgExtractor.exe**, with **SO5-Updater.exe** as the internal installer.
- Moved internal executables and DLLs to `bin`. A small statically linked root launcher
  forwards startup to the real launcher without a visible shell.
- Adapted application lookup, shortcuts, update validation/handoff and smoke tests to the bin layout.
- `build-windows.cmd` now builds a clean player folder by default under
  `dist/ps4rt-star-ocean-5-windows-x64-v<VERSION>-latest/`, without creating a ZIP.
- Keep `out` as developer intermediates; refuse to overwrite a previously played player
  folder containing data under its `out` directory.
- Retain relocated-CMake-cache recovery and path-independent build-script discovery.
- Keep the required clang/MinGW-w64 System V guest boundary, GNU assembly and winpthreads.
  MSVC is not a supported replacement compiler for that boundary.

### Source cleanup and documentation

- Removed obsolete Linux launch/control scripts, container packaging, Linux player guides
  and the superseded extractor wrapper/binaries.
- Removed inactive non-Windows branches from the standalone runtime and local shims,
  and specialized the adapted renderer platform/thread/file/page helpers for Windows.
- Removed the unused adaptive pthread-mutex helper while preserving the Windows SpinLock path.
- Corrected Linux/script-era descriptions without removing required PS4 ABI contracts,
  vendored dependency sources, copyright or license notices.
- Removed the Linux-only protected-page writeback fixture from the Windows target: it ran
  no assertions on Windows. Portable writeback checks remain; native Windows fault coverage
  still needs a separate equivalent.
- Added public English launcher and in-game-menu guides with the supplied screenshots and
  portable relative image links. Updated public documentation references and packaging
  to avoid restoring documents kept exclusively for development.

### Validation and remaining limits

- Earlier test builds passed 34 native CTest cases; the Python developer suite ran 42
  cases with 3 skipped. Those results predate the latest bin-layout/source-cleanup changes.
- The later cleanup received C/C++ syntax checks for 34 source files, plus targeted
  rechecks. These checks do not replace a complete rebuild, package smoke test or gameplay test.
- Windows 10 Acer VX-15 testing with a GTX 1050 and seventh-generation Core i5 reported
  roughly 50–60 FPS in tested `1080`p scenes without reconstruction. `FSR 3.1` reduced performance
  on that system. Delayed textures and a black-scene incident occurred with async enabled;
  their exact cause was not established by that report.
- User tests on the higher-end AMD system do not establish full compatibility on every
  `NVIDIA`, Intel, laptop, driver version, region or game update.
- A complete playthrough and final validation of the newly built distributable remain outstanding.
- Native ultrawide camera/HUD changes, general texture/mesh mods, cheats, free camera,
  UI replacement, leader-switching mods and Android/iOS/macOS ports are not shipped features.

## 0.3.6 — Internal experimental development

This was a private development stage later superseded by 0.4.0, not a separately verified
public release in this repository.

- Added runtime-connected video checks, graphics-adapter selection and performance/overlay tools.
- Investigated severe frame-time spikes, shader compilation, CPU rendering overhead and
  transition/resource correctness through repeated real gameplay captures.
- Explored cache reuse and precompilation/warm-up strategies; reverted unsafe startup
  warm-up behavior after a driver-side crash prevented game launch.
- Began correcting input-profile overrides, camera drift symptoms and visual regressions
  found during extended testing.
- Promoted the ongoing experimental work to 0.4.0 when the Windows-specialization phase began.

## 0.3.5 — Tagged Windows experimental baseline

The local repository contains the `v0.3.5` tag. It does not imply that a matching installable
Windows Release asset was published, or that the entire game was validated.

- Established the native Windows x64 branch with the Cockpit launcher and native preparation.
- Included game-folder/PKG selection, remembered paths, video/input preferences, saves and diagnostics.
- Used the older external PKG extractor integration, subsequently replaced in 0.4.0.
- Corrected Logs badge overlap and showed the bare Title ID only after validation.
- Kept CUSA03219 green, accepted the recognized alternative PS4 editions with an orange
  untested warning, and refused unrelated/PS3 titles.
- Prepared English Windows documentation and documented the experimental testing status.
- Severe gameplay stutter remained a reason for the subsequent runtime/performance work.

## 0.3.0–0.3.4 — Early Windows development milestones

These entries summarize the development sequence rather than inventing individual release
dates or claiming a separately published package for every patch number.

- Introduced the native Windows graphical Cockpit concept, supplied artwork and six-tab launcher.
- Iterated on scaling, text size, translations, window chrome, dropdowns and genuine editable input controls.
- Replaced the player's Python-based preparation requirement with native C++ preparation.
- Addressed executable/DLL placement, save detection, localized interface selection and runtime integration.
- Added PKG import UI, larger save previews, extraction-destination selection and diagnostic usability fixes.
- Refined the minimal Home page with the game logo, Play button and status dock.

## Upstream history

The inherited `v1.0` and `v1.1` tags belong to the original project's history and are not
Windows-native update numbers. Original PS4RT, the adapted shadPS4 renderer and third-party
components retain their own authorship and licenses. See [Upstream credits](docs/UPSTREAM_CREDITS.md).
