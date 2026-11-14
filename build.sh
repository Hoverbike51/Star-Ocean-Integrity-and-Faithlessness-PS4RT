#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
case $(uname -s) in
MINGW*|MSYS*) ;;
*) echo 'This checkout builds the Windows x64 port. Use build-windows.cmd with MSYS2 CLANG64.' >&2; exit 1 ;;
esac
mkdir -p out

# CMake build trees are not portable: CMakeCache.txt and the generated Ninja files contain
# absolute source and build paths.  A copied checkout may therefore still point at the machine
# or directory where it was compiled previously.  Keep incremental builds in place when the
# paths still match, but discard only the generated GPU build tree after a relocation.
reset_relocated_cmake_build_tree() {
    local build_dir="$PWD/out/gpu"
    local cache_file="$build_dir/CMakeCache.txt"
    [[ -f "$cache_file" ]] || return 0

    local cached_source cached_build current_source current_build
    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file" | head -n 1)
    cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$cache_file" | head -n 1)
    current_source=$(cd gpu && pwd -P)
    current_build="$build_dir"

    case $(uname -s) in
    MINGW*|MSYS*)
        current_source=$(cygpath -m "$current_source")
        current_build=$(cygpath -m "$current_build")
        cached_source=${cached_source//\\//}
        cached_build=${cached_build//\\//}
        cached_source=${cached_source,,}
        cached_build=${cached_build,,}
        current_source=${current_source,,}
        current_build=${current_build,,}
        ;;
    esac

    cached_source=${cached_source%/}
    cached_build=${cached_build%/}
    current_source=${current_source%/}
    current_build=${current_build%/}

    if [[ -z "$cached_source" || -z "$cached_build" ||
          "$cached_source" != "$current_source" || "$cached_build" != "$current_build" ]]; then
        printf '%s\n' \
            "Relocated or invalid CMake cache detected." \
            "Recreating the generated build directory: $build_dir"
        [[ "$build_dir" == "$PWD/out/gpu" ]] || {
            echo "Refusing to remove an unexpected build directory: $build_dir" >&2
            exit 1
        }
        rm -rf -- "$build_dir"
    fi
}

reset_relocated_cmake_build_tree

# Native Windows build: clang/MinGW-w64 supplies the System V ABI attribute, GNU assembly and
# winpthreads required by the guest-code bridge. MSVC is not ABI-compatible with that bridge.
case $(uname -s) in
MINGW*|MSYS*)
    # shellcheck source=msys2-env.sh
    source ./msys2-env.sh
    for tool in clang clang++ cmake ninja pkg-config; do
        command -v "$tool" >/dev/null || {
            echo "Need $tool from MSYS2 CLANG64 (packaging/windows/README.md)." >&2
            exit 1
        }
    done
    cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPS4RT_PGO=off -DPS4RT_LTO="${PS4RT_LTO:-OFF}" \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    ninja -C out/gpu -j "${PS4RT_BUILD_JOBS:-4}" ps4rt ps4rt-launcher ps4rt-pkg-extractor so5-updater so5-bootstrap
    cp -r out/gpu/amd-fsr-sdk24 out/
    cp 'out/gpu/Star Ocean 5.exe' 'out/Star Ocean 5.exe'
    cp 'out/gpu/Star Ocean 5 Launcher.exe' 'out/Star Ocean 5 Launcher.exe'
    cp out/gpu/PkgExtractor.exe out/PkgExtractor.exe
    cp out/gpu/SO5-Updater.exe out/SO5-Updater.exe
    mkdir -p out/bootstrap
    cp 'out/gpu/bootstrap/Star Ocean 5 Launcher.exe' 'out/bootstrap/Star Ocean 5 Launcher.exe'
    # Keep both development executables directly runnable outside an MSYS2 shell.
    bash packaging/windows/copy_dlls.sh out 'out/Star Ocean 5.exe' 'out/Star Ocean 5 Launcher.exe' out/PkgExtractor.exe
    if [[ ${1:-} == --test ]]; then
        ninja -C out/gpu -j "${PS4RT_BUILD_JOBS:-4}" \
            hud-standalone-test runtime-time-test hud-live-test hud-projection-test ultrawide-test upscaler-support-test \
            window-presentation-test native-pkg-test trophy-file-test savedata-backups-test \
            save-backup-guard-test launcher-shortcuts-test launcher-updater-test display-sample-gpu-test fsr4-rcas-gpu-test \
            dlss-validation-test dlss-device-gate-test transition-snapshot-gpu-test cpu-image-writeback-test \
            color-export-test image-subresource-test gpu-workarounds-test camera-layout-test \
            camera-probe-test camera-comfort-test native-tweaks-test exploration-leader-test \
            scene-camera-history-test menu-input-test pad-input-test menu-sounds-test \
            graphics-settings-test memory-budget-test cockpit-test ahsl-cache-test \
            shader-capture-test scene-targets-gpu-test log-console-test fiber-test \
            half-precision-test native-cheats-test game-languages-test fast-forward-test \
            perf-sampling-test diagnostic-trace-test performance-report-test fsr-sdk24-gpu-test user-paths-test
        ctest --test-dir out/gpu --output-on-failure
        python -m unittest discover -s tests -p 'test_*.py' -v
    fi
    echo "Built $PWD/out/Star Ocean 5.exe"
    echo "Built $PWD/out/Star Ocean 5 Launcher.exe"
    echo "Built $PWD/out/PkgExtractor.exe"
    exit 0
    ;;
esac
