#!/usr/bin/env bash
# Create a redistributable Windows tree and ZIP. Run in MSYS2 CLANG64 after build.sh.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source ./msys2-env.sh
[[ -f out/ps4rt.exe ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
[[ -f out/PS4RT-Launcher.exe ]] || { echo 'Build the Windows launcher first: bash build.sh' >&2; exit 1; }
[[ -f out/PS4RT-PkgExtractor.exe ]] || { echo 'Build the branded PKG extractor first: bash build.sh' >&2; exit 1; }

version=$(tr -d '\r\n' < VERSION)
stage="out/stage/ps4rt-star-ocean-5-windows-x64-v${version}"
rm -rf -- out/stage
mkdir -p "$stage/out" "$stage/gpu/assets" "$stage/docs" "$stage/launcher/assets" "$stage/tools"
llvm-strip --strip-debug -o "$stage/ps4rt.exe" out/ps4rt.exe
llvm-strip --strip-debug -o "$stage/PS4RT-Launcher.exe" out/PS4RT-Launcher.exe
llvm-strip --strip-debug -o "$stage/PS4RT-PkgExtractor.exe" out/PS4RT-PkgExtractor.exe

# Copy every non-system DLL resolved from the CLANG64 tree. The packaged executable therefore
# starts from a normal Windows session without an MSYS2 PATH.
(ldd "$stage/ps4rt.exe"; ldd "$stage/PS4RT-Launcher.exe"; ldd "$stage/PS4RT-PkgExtractor.exe") |
awk '/\/clang64\/bin\// {print $3}' | sort -u |
while IFS= read -r dll; do
    [[ -n $dll ]] && cp -u "$dll" "$stage/"
done

cp -r gpu/assets/. "$stage/gpu/assets/"
cp -r fsr4_shaders "$stage/"
cp -r launcher/assets/. "$stage/launcher/assets/"
cp -r tools/pkg-extractor "$stage/tools/"
cp LICENSE README.md "$stage/"
cp packaging/windows/README-Windows.txt "$stage/"
cp docs/UPSTREAM_CREDITS.md "$stage/docs/"
find "$stage" -name __pycache__ -prune -exec rm -rf {} +

# Game binaries, saves and generated shader caches are private and must never enter a release.
forbidden=$(find "$stage" \( -iname eboot.bin -o -iname sce_module -o -iname sce_sys \
    -o -iname savedata -o -iname user -o -iname cache -o -iname '*.log' -o -iname '*.pkg' \
    -o -iname '*.sprx' -o -iname '*.prx' \) -print)
if [[ -n $forbidden ]]; then
    echo "Package contains private/generated files:" >&2
    echo "$forbidden" >&2
    exit 1
fi

mkdir -p dist
archive="dist/ps4rt-star-ocean-5-windows-x64-v${version}.zip"
rm -f "$archive"
(cd out/stage && cmake -E tar cf "../../$archive" --format=zip "ps4rt-star-ocean-5-windows-x64-v${version}")
echo "Created $PWD/$archive"
