# Sourced by Windows build/package scripts. MSYS2_ROOT may override C:/msys64.
msys2_root=${MSYS2_ROOT:-/c/msys64}
msys2_root=${msys2_root%/}
if [[ -z $msys2_root ]]; then
    # cygpath maps the directory containing the active MSYS2 installation to '/'.
    clang64_bin=/clang64/bin
    msys_usr_bin=/usr/bin
else
    clang64_bin=$msys2_root/clang64/bin
    msys_usr_bin=$msys2_root/usr/bin
fi
if [[ -d $clang64_bin ]]; then
    export PATH="$clang64_bin:$msys_usr_bin:$PATH"
    export MSYSTEM=CLANG64
fi
