# Native UI validation — 1.1.3

These checks exercise the launcher and the real ImGui Technical layout without
preparing game files, downloading updates, extracting a PKG or running the game.
The benchmark data and guide illustrations are synthetic UI fixtures.

## Run the isolated checks

From a configured Windows/MSYS2 CLANG64 checkout:

```powershell
cmake --build out/gpu --target ps4rt-launcher technical-overlay-test
ctest --test-dir out/gpu -R '^(technical-overlay|launcher-operation-ui)$' --output-on-failure
```

The normal `build.sh --test` target list includes the Technical executable before
CTest runs. The launcher executable is already built by the normal build workflow.
Reports and screenshots are written beneath the configured build directory, not
the source tree's game/user folders.

| Check | Coverage | Output |
| --- | --- | --- |
| `technical-overlay` | 12 languages × five ratios × six anchors × three scales; unavailable/changing counters, provider names and long hardware names | `technical-ui-reports/technical.json`, illustrative BMPs |
| `launcher-operation-ui` | 12 languages × two sizes × eight operation states; background tab blocking, disabled choices, modal geometry, cancel availability and partial/full pixel comparison | `operation-ui-reports/operations.json`, native PNGs |
| `--ui-benchmark` | Eight existing pages × 12 languages × two sizes, full and clipped hover repaint | Caller-selected JSON report and screenshots |

To exercise existing pages directly, pass an output filename and the source/assets root:

```powershell
& '.\out\gpu\Star Ocean 5 Launcher.exe' --ui-benchmark '.\out\gpu\pages.json' (Get-Location).Path
```

## Interpretation and measured limits

Local checks passed **7,560** changing-reading geometry comparisons. The operation
fixtures passed all **192** cases, including full/partial pixel equivalence for the
deterministic states. The existing-page benchmark also passed **192** pixel comparisons.
Indeterminate animations use real time and are excluded from deterministic pixel equality.

The final registered operation test measured a maximum per-case 95th percentile of
approximately **0.80 ms** for progress refresh and **0.27 ms** for actionable-button
hover. The Technical CPU draw path measured approximately **0.008 ms** at the 95th
percentile, excluding screenshot encoding and initial font setup.

> [!IMPORTANT]
> **3 ms is a repeated-draw target, not a universal latency guarantee.** The broader
> existing-page run included a **3.59 ms** hover 95th percentile on the Simplified
> Chinese Cheats page at 1720 × 1040 (median 0.79 ms). Full initial page paints reached
> roughly 11.2 ms in that run. Window presentation, GPU work, font texture uploads,
> image decoding, scheduling delays and package work are not certified by these CPU
> figures. Performance timings are reported rather than used as hardware-dependent
> pass/fail assertions in CTest.

The native overlay integration was also checked with the renderer's compile options
using syntax-only compilation. This UI validation does not build a complete player
package or replace an installed gameplay test. Residual rendering artifacts remain open.
