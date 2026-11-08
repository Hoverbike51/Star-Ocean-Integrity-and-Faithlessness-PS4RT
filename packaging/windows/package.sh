#!/usr/bin/env bash
# Create a redistributable Windows player folder and, unless requested otherwise, its ZIP.
# Run in MSYS2 CLANG64 after build.sh.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source ./msys2-env.sh

folder_only=false
case "${1:-}" in
--folder-only) folder_only=true ;;
"") ;;
*) echo "Usage: $0 [--folder-only]" >&2; exit 2 ;;
esac
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$PWD/scripts/verify-fsr4-assets.ps1")" \
    -Root "$(cygpath -w "$PWD")"
[[ -f 'out/Star Ocean 5.exe' ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
[[ -f 'out/Star Ocean 5 Launcher.exe' ]] || { echo 'Build the Windows launcher first: bash build.sh' >&2; exit 1; }
[[ -f out/PkgExtractor.exe ]] || { echo 'Build the branded PKG extractor first: bash build.sh' >&2; exit 1; }
[[ -f 'out/bootstrap/Star Ocean 5 Launcher.exe' ]] || { echo 'Build the native bootstrap first: bash build.sh' >&2; exit 1; }
[[ -f out/SO5-Updater.exe ]] || { echo 'Build the native updater first: bash build.sh' >&2; exit 1; }

version=$(tr -d '\r\n' < VERSION)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid package VERSION.' >&2; exit 1; }
name="ps4rt-star-ocean-5-windows-x64-v${version}-latest"
stage="dist/${name}"
mkdir -p dist
[[ "$(realpath -m -- "$stage")" == "$PWD/dist/$name" && ! -L dist && ! -L "$stage" ]] || {
    echo 'Refusing an unexpected or linked package staging directory.' >&2; exit 1;
}
if [[ -d "$stage" ]] && find "$stage" -type l -print -quit | grep -q .; then
    echo 'Refusing to replace a staging directory containing links.' >&2; exit 1;
fi
# A previously played player folder must never lose its settings, saves or caches.
if [[ -d "$stage/out" ]] && find "$stage/out" -type f -print -quit | grep -q .; then
    echo 'Player data exists in the old output folder. Move that folder before packaging.' >&2; exit 1;
fi
rm -rf -- "$stage"
mkdir -p "$stage/out" "$stage/gpu/assets" "$stage/docs" "$stage/launcher/assets" "$stage/bin"
llvm-strip --strip-debug -o "$stage/bin/Star Ocean 5.exe" 'out/Star Ocean 5.exe'
llvm-strip --strip-debug -o "$stage/bin/Star Ocean 5 Launcher.exe" 'out/Star Ocean 5 Launcher.exe'
llvm-strip --strip-debug -o "$stage/bin/PkgExtractor.exe" out/PkgExtractor.exe
llvm-strip --strip-debug -o "$stage/bin/SO5-Updater.exe" out/SO5-Updater.exe

llvm-strip --strip-debug -o "$stage/Star Ocean 5 Launcher.exe" 'out/bootstrap/Star Ocean 5 Launcher.exe'

# Copy every non-system DLL resolved from the CLANG64 tree. The packaged executable therefore
# starts from a normal Windows session without an MSYS2 PATH.
bash packaging/windows/copy_dlls.sh "$stage/bin" "$stage/bin/Star Ocean 5.exe" "$stage/bin/Star Ocean 5 Launcher.exe" "$stage/bin/PkgExtractor.exe"
if [[ ${PS4RT_PACKAGE_NO_DLSS:-0} != 1 ]] && [[ ! -f out/ps4rt_dlss.dll || ! -f out/nvngx_dlss.dll || ! -f out/dlss-notices/LICENSE.txt ]]; then
    echo 'Full player package requires the DLSS bridge, NVIDIA runtime and license. Run build-windows.cmd, or explicitly opt out with PS4RT_PACKAGE_NO_DLSS=1.' >&2; exit 1
fi
if [[ ${PS4RT_PACKAGE_NO_DLSS:-0} != 1 && -f out/ps4rt_dlss.dll && -f out/nvngx_dlss.dll ]]; then
    cp out/ps4rt_dlss.dll out/nvngx_dlss.dll "$stage/bin/"
fi

cp -r gpu/assets/. "$stage/gpu/assets/"
cp -r fsr4_shaders "$stage/"
cp -r launcher/assets/. "$stage/launcher/assets/"
cp LICENSE README.md CHANGELOG.md VERSION "$stage/"
cp packaging/windows/README-Windows.txt "$stage/"
cp docs/UPSTREAM_CREDITS.md docs/MINIZ_LICENSE.txt docs/LIBCXX_LICENSE.txt "$stage/docs/"
cp docs/LAUNCHER_GUIDE.md docs/IN_GAME_MENU_GUIDE.md "$stage/docs/"
mkdir -p "$stage/docs/images/readme"
cp -r docs/images/readme/launcher docs/images/readme/custom_menu "$stage/docs/images/readme/"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$PWD/packaging/windows/copy-notices.ps1")" \
    -Destination "$(cygpath -w "$PWD/$stage")" -Msys2Root "$(cygpath -w "${MSYS2_ROOT:-/c/msys64}")"
find "$stage" -name __pycache__ -prune -exec rm -rf {} +

# Game data, settings, captures and developer tracing tools must never enter a player release.
forbidden=$(find "$stage" \( -iname eboot.bin -o -iname sce_module -o -iname sce_sys \
    -o -iname savedata -o -iname user -o -iname cache -o -iname '*.log' -o -iname '*.pkg' \
    -o -iname '*.sprx' -o -iname '*.prx' -o -iname '*.etl' -o -iname '*.wpaprofile' \
    -o -iname '*.csv' -o -iname 'performance*.json' -o -iname '*settings*.ini' \
    -o -iname '*.ps1' -o -iname '*.cmd' -o -iname '*.py' \
    -o -iname '*HUD*Studio*' -o -iname 'HUD_*.md' -o -iname 'HUD_*.txt' \
    -o -iname 'HUD_*.m' -o -iname 'hud-map*.json' -o -iname 'hud-preset.schema.json' \
    -o -iname 'hud-private*.json' \) -print)
if [[ -n $forbidden ]]; then
    echo "Package contains private/generated files:" >&2
    echo "$forbidden" >&2
    exit 1
fi

if $folder_only; then
    echo "Created player folder $PWD/$stage"
    exit 0
fi

archive="dist/${name}.zip"
rm -f "$archive"
(cd dist && cmake -E tar cf "${name}.zip" --format=zip "$name")
echo "Created $PWD/$archive"
