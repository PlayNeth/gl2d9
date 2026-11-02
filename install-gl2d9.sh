#!/usr/bin/env bash
# Installs gl2d9's Glide DLLs into a Wine prefix's 32-bit system directory.
#
#   install-gl2d9.sh install [folder with gl2d9's DLLs]    gl2d9 for every game in the prefix
#   install-gl2d9.sh uninstall                            removes gl2d9 from the prefix
#
# Installs glide.dll, glide2x.dll and glide3x.dll (default folder: the script's own). Glide
# DLLs already there (another wrapper, such as nGlide or dgVoodoo) are kept as *.pre-gl2d9 and
# put back by uninstall. Also creates C:\windows\gl2d9.ini in the prefix if there is none
# (uninstall keeps it). The prefix is taken from $WINEPREFIX (default: ~/.wine).
set -euo pipefail

prefix="${WINEPREFIX:-$HOME/.wine}"
dlls=(glide.dll glide2x.dll glide3x.dll)
backup_suffix=".pre-gl2d9"

die() { echo "error: $*" >&2; exit 1; }

usage() {
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

# Prints "<machine> <characteristics>" from the PE header (machine 014c = 32-bit x86).
pe_info() {
    local f="$1" off
    [ "$(head -c 2 "$f")" = "MZ" ] || { echo "none 0"; return; }
    off=$(od -An -t u4 -j 60 -N 4 "$f" | tr -d ' ')
    [ "$(od -An -c -j "$off" -N 2 "$f" | tr -d ' ')" = "PE" ] || { echo "none 0"; return; }
    echo "$(od -An -t x2 -j $((off + 4)) -N 2 "$f" | tr -d ' ') $(od -An -t u2 -j $((off + 22)) -N 2 "$f" | tr -d ' ')"
}

require_x86_dll() {
    local f="$1" machine flags
    read -r machine flags <<<"$(pe_info "$f")"
    [ "$machine" = "014c" ] || die "not a 32-bit Windows binary (PE machine: $machine): $f"
    (( flags & 0x2000 )) || die "not a DLL: $f"
}

# A gl2d9 DLL (its version string: "gl2d9 1.0.1, ...").
is_gl2d9() { grep -a -q -E 'gl2d9 [0-9]+\.[0-9]+' "$1"; }

# 32-bit DLLs live in syswow64 in a 64-bit prefix and in system32 in a 32-bit one.
sysdir() {
    [ -d "$prefix/drive_c/windows" ] || die "no Wine prefix at $prefix (set WINEPREFIX)"
    if [ -d "$prefix/drive_c/windows/syswow64" ]; then
        echo "$prefix/drive_c/windows/syswow64"
    else
        echo "$prefix/drive_c/windows/system32"
    fi
}

cmd_install() {
    [ $# -le 1 ] || usage
    local src="${1:-$(dirname "$(readlink -f "$0")")}"
    [ -d "$src" ] || die "not a folder: $src"
    local d n=0
    for d in "${dlls[@]}"; do
        [ -f "$src/$d" ] || continue
        require_x86_dll "$src/$d"
        is_gl2d9 "$src/$d" || die "not a gl2d9 DLL: $src/$d"
        n=$((n + 1))
    done
    (( n > 0 )) || die "no glide.dll, glide2x.dll or glide3x.dll in $src"

    local dir
    dir=$(sysdir)
    echo "prefix: $prefix"
    echo "system directory: $dir"
    for d in "${dlls[@]}"; do
        [ -f "$src/$d" ] || continue
        if [ -f "$dir/$d" ] && ! is_gl2d9 "$dir/$d" && [ ! -f "$dir/$d$backup_suffix" ]; then
            mv "$dir/$d" "$dir/$d$backup_suffix"
            echo "kept the $d that was there as $d$backup_suffix"
        fi
        cp -f "$src/$d" "$dir/$d"
        echo "installed $d"
    done

    local ini="$prefix/drive_c/windows/gl2d9.ini" example=""
    for f in "$src/gl2d9.ini" "$src/gl2d9.ini.example"; do
        [ -f "$f" ] && { example="$f"; break; }
    done
    if [ -f "$ini" ]; then
        echo "config: $ini (kept)"
    elif [ -n "$example" ]; then
        cp "$example" "$ini"
        echo "config: $ini"
    fi
    echo "note: a Glide DLL next to a game's exe is loaded instead of these; remove it to use gl2d9"
}

cmd_uninstall() {
    [ $# -eq 0 ] || usage
    local dir d
    dir=$(sysdir)
    for d in "${dlls[@]}"; do
        if [ -f "$dir/$d" ] && is_gl2d9 "$dir/$d"; then
            rm -f "$dir/$d"
            echo "removed $d"
        fi
        if [ -f "$dir/$d$backup_suffix" ] && [ ! -f "$dir/$d" ]; then
            mv "$dir/$d$backup_suffix" "$dir/$d"
            echo "put back the earlier $d"
        fi
    done
}

case "${1:-}" in
    install) shift; cmd_install "$@" ;;
    uninstall) shift; cmd_uninstall "$@" ;;
    *) usage ;;
esac
