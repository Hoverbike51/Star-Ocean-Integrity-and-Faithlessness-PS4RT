#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out

reset_relocated_cmake_build_tree() {
    local build_dir="$PWD/out/gpu"
    local cache_file="$build_dir/CMakeCache.txt"
    local cached_source cached_build current_source current_build

    [[ -f "$cache_file" ]] || return 0

    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file" | tail -n 1)
    cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$cache_file" | tail -n 1)
    current_source="$PWD/gpu"
    current_build="$build_dir"

    normalize_windows_path() {
        printf '%s' "$1" | sed 's#\\#/#g; s#/$##' | tr '[:upper:]' '[:lower:]'
    }

    if [[ -z "$cached_source" || -z "$cached_build" ||
          "$(normalize_windows_path "$cached_source")" != "$(normalize_windows_path "$current_source")" ||
          "$(normalize_windows_path "$cached_build")" != "$(normalize_windows_path "$current_build")" ]]; then
        printf 'Relocated or invalid CMake cache detected.\n' >&2
        printf 'Recreating the generated build directory: %s\n' "$build_dir" >&2
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
    ninja -C out/gpu -j "${PS4RT_BUILD_JOBS:-4}" ps4rt ps4rt-launcher ps4rt-pkg-extractor
    cp out/gpu/ps4rt.exe out/ps4rt.exe
    cp out/gpu/PS4RT-Launcher.exe out/PS4RT-Launcher.exe
    cp out/gpu/PS4RT-PkgExtractor.exe out/PS4RT-PkgExtractor.exe
    # Keep both development executables directly runnable outside an MSYS2 shell.
    (ldd out/ps4rt.exe; ldd out/PS4RT-Launcher.exe; ldd out/PS4RT-PkgExtractor.exe) |
    awk '/\/clang64\/bin\// {print $3}' | sort -u |
    while IFS= read -r dll; do
        [[ -n $dll ]] && cp -u "$dll" out/
    done
    if [[ ${1:-} == --test ]]; then
        ninja -C out/gpu -j "${PS4RT_BUILD_JOBS:-4}" display-sample-gpu-test \
            transition-snapshot-gpu-test cpu-image-writeback-test color-export-test \
            image-subresource-test gpu-workarounds-test camera-layout-test \
            scene-camera-history-test menu-input-test menu-sounds-test graphics-settings-test
        ctest --test-dir out/gpu --output-on-failure
        python -m unittest discover -s tests -p 'test_*.py' -v
    fi
    echo "Built $PWD/out/ps4rt.exe"
    echo "Built $PWD/out/PS4RT-Launcher.exe"
    echo "Built $PWD/out/PS4RT-PkgExtractor.exe"
    exit 0
    ;;
esac
command -v cmake >/dev/null
command -v ninja >/dev/null
pkg-config --exists vulkan sdl3 libavcodec libavutil libswscale
cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS4RT_PGO=off -DPS4RT_LTO="${PS4RT_LTO:-OFF}"
ninja -C out/gpu -j "${PS4RT_BUILD_JOBS:-4}" ps4rtgpu
read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3 libavcodec libavutil libswscale)"
read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3 libavcodec libavutil libswscale)"
compiler=${CC:-cc}
mkdir -p out/atrac9
for source in third_party/LibAtrac9/C/src/*.c; do
    "$compiler" -std=c99 -O2 -w -c "$source" -o "out/atrac9/$(basename "${source%.c}").o"
done
ar rcs out/libatrac9.a out/atrac9/*.o
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -mgeneral-regs-only -c src/isa_emulate.c -o out/isa_emulate.o
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -pthread -no-pie -DPS4RT_RUNTIME "${includes[@]}" -I. -Isrc \
    src/loader.c src/runtime*.c src/cpu_check.c out/isa_emulate.o src/isa_rewrite.c out/libatrac9.a \
    -lm -Lout/gpu -lps4rtgpu -Wl,-rpath,'$ORIGIN/gpu' -rdynamic "${libraries[@]}" -o out/ps4rt
if [[ ${1:-} == --test ]]; then
    ninja -C out/gpu -j "${PS4RT_BUILD_JOBS:-4}" display-sample-gpu-test transition-snapshot-gpu-test \
        cpu-image-writeback-test color-export-test image-subresource-test gpu-workarounds-test \
        camera-layout-test scene-camera-history-test menu-input-test menu-sounds-test graphics-settings-test
    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc \
        tests/test_content.c src/runtime_content.c -o out/content-test
    ctest --test-dir out/gpu --output-on-failure
    python3 -m unittest discover -s tests -p 'test_*.py' -v
fi
