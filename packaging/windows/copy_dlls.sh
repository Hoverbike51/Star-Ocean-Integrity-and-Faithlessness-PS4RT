#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copies the CLANG64 DLLs that the given executables import, directly or through other DLLs, into
# the destination folder (build.ps1: out\, so out\ps4rt.exe runs with a system-only PATH).
#
# The import tables are read with llvm-objdump. MSYS2's ldd is Cygwin's ldd.exe, which starts the
# program under the Windows debugger and records the DLLs it loads. That runs the init code of
# about 100 DLLs, its output varied between runs, and the build once hung at that step. It also
# resolved DLLs from out\ once they were there, so updated CLANG64 DLLs were never copied again.
# Nothing is run here, and a DLL is copied whenever it differs from /clang64/bin.
# Usage: copy_dlls.sh <dest dir> <exe or dll>...
set -euo pipefail
dest=$1
shift
bin=${CLANG64_BIN:-/clang64/bin}

# Windows resolves DLL names case-insensitively.
declare -A available
for path in "$bin"/*.dll; do
    name=${path##*/}
    available[${name,,}]=$path
done

declare -A seen
queue=("$@")
copied=0 same=0 failed=0
while ((${#queue[@]})); do
    file=${queue[0]}
    queue=("${queue[@]:1}")
    # "DLL Name: x.dll" lines cover the import and delay-import tables.
    imports=$(llvm-objdump -p "$file") || { echo "Cannot inspect PE imports: $file" >&2; exit 1; }
    while read -r name; do
        key=${name,,}
        [[ -n ${seen[$key]+x} ]] && continue
        seen[$key]=1
        source=${available[$key]:-}
        if [[ -z $source ]]; then
            case "$key" in api-ms-win-*|ext-ms-*) continue ;; esac
            system=$(cygpath -u -S)/$name
            if [[ -f $system ]]; then continue; fi
            echo "copy_dlls: unresolved non-system dependency $name imported by $file" >&2
            failed=$((failed + 1)); continue
        fi
        # The Vulkan loader comes with the GPU driver (System32), not from CLANG64.
        [[ $key == vulkan-1.dll ]] && continue
        queue+=("$source")
        target=$dest/${source##*/}
        # Compare content hashes so equal timestamps cannot hide a stale dependency.
        # coreutils only; no execution of the inspected application.
        if [[ -f $target && $(sha256sum "$source" | cut -d' ' -f1) == $(sha256sum "$target" | cut -d' ' -f1) ]]; then
            same=$((same + 1))
        elif cp -fp "$source" "$target" 2>/dev/null; then
            copied=$((copied + 1))
        else
            # Usually a running game holding the old copy.
            echo "copy_dlls: cannot replace $target (in use?)" >&2
            failed=$((failed + 1))
        fi
    done < <(printf '%s\n' "$imports" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr -d '\r')
done
echo "copy_dlls: $copied copied, $same up to date, $failed in use -> $dest"
((failed == 0))
