# Windows 1.0.1-hotfix 1 source and player-package audit

Audit date: 2026-11-08

## Confirmed defects and corrections

### DLSS missing from full builds

The published 1.0.0 player ZIP omitted both `ps4rt_dlss.dll` and `nvngx_dlss.dll`.
The previous wrapper silently omitted DLSS when a locally installed SDK was absent.
Full Windows builds now acquire pinned, SHA-256-verified NVIDIA SDK build files,
compile the separate bridge, and require the DLL pair and NVIDIA license before
packaging. An offline SDK path and an explicit `-NoDlss` reduced build remain
available. NVIDIA feature/device checks remain enabled. This fixes packaging;
successful DLSS rendering still requires an eligible NVIDIA GPU and driver.

### HUD preset portability

Group and object overrides need the same verified resource identities in every
installation. The runtime now includes compact resource/object fingerprints,
parent relationships, native object types and bounds. It does not require an
external authoring catalog. Cached resource clones can be recognized by their
complete hierarchy. Unknown object identities are rejected; presets from another
aspect ratio remain rejected. The original HUD remains the default without an
active matching preset. No game artwork or layout XML is bundled.

### Trophy menu exit

The diagnostic attached to [issue #4](https://github.com/Hoverbike51/Star-Ocean-Integrity-and-Faithlessness-PS4RT/issues/4)
stops at the unresolved `sceKernelConvertUtcToLocaltime` import, called from
`libc.prx`. This was not evidence of malformed trophy JSON. The native Windows
implementation now supplies the guest time conversion, exact output widths,
historical Windows timezone rules, DST offsets and invalid-date handling.
Regression tests cover the imported entry point and winter/summer conversions.
An in-game trophy-menu and end-credit retest is still required.

### Update installation handoff

The launcher now starts the verified helper from the downloaded package rather
than reusing an older installed helper. The helper runs outside the installation,
waits for the exact launcher process to exit, and retries temporary preflight
sharing violations for a bounded interval. It never retries a partly applied
transaction. Startup-handshake failure cancels that child; launcher-exit timeout
records an error. Failed installation preserves staging for diagnosis; program
file backup/rollback and protection of saves/settings/cache remain in place.
The regression test covers a running bin launcher, an obsolete installed helper,
a temporary file lock, restart, rollback, and preservation of user data.

### Build and packaging checks

An unused dependency on an unavailable authoring header was removed from the
public HUD test. File hashing in SDK/notice scripts uses .NET directly, avoiding
PowerShell module-autoload differences between Windows PowerShell and PowerShell 7.

## GitHub issue review before compilation

| Issue | Status after this audit |
| --- | --- |
| [#2: speed / cutscene skipping](https://github.com/Hoverbike51/Star-Ocean-Integrity-and-Faithlessness-PS4RT/issues/2) | Fast-forward already exists (F2 / L2+R2). Dedicated cutscene skipping and automatic sprint are separate requests, not fixes implemented in this audit. |
| [#3: pixelation / output resolution](https://github.com/Hoverbike51/Star-Ocean-Integrity-and-Faithlessness-PS4RT/issues/3) | Missing DLSS components are corrected. The separate ultrawide pixelation report remains open; these changes do not establish its rendering cause or resolve the previously reported FSR transition artifacts. |
| [#4: trophy-menu crash](https://github.com/Hoverbike51/Star-Ocean-Integrity-and-Faithlessness-PS4RT/issues/4) | Reported missing import implemented and regression-tested; actual trophy-menu retest pending. |

## Verification and limits

- Built an isolated copy of the public tracked sources with empty build/dependency
  directories, without reusing development binaries or external HUD data.
- The full wrapper obtained and verified the pinned NVIDIA files and compiled
  the runtime, launcher, native extractor, updater and separate DLSS bridge.
- All **66 native CTest checks passed**, including the new standalone HUD,
  time-conversion and updater-process scenarios. The Python developer suite ran
  45 checks: 42 passed and 3 fixture-dependent checks were skipped.
- Created a player folder only, without a release ZIP. The package smoke test
  passed with the MSYS2 directories removed from PATH and required the DLSS pair.
  Executable versions, publisher fields and third-party notices were checked.
- This is not a complete visual playthrough or proof of correct rendering on
  every GPU. DLSS image quality requires NVIDIA hardware testing. HUD placement,
  trophy-menu operation and FSR artifacts require in-game validation.
- No published release, tag, commit or GitHub issue was modified by this audit.
