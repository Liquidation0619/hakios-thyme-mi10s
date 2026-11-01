#!/bin/bash
# Build sideload (static, mini libc) and see which helpers the mini libc actually has.
set -o pipefail
exec 2>&1
D=/opt/hackios/sideload-build
rm -rf "$D" && mkdir -p "$D" && cd "$D" || exit 1

CC=/opt/hackios/lldbin/xnu-clang
M=/opt/hakios-main/userland/libmini
CFLAGS="-arch arm64 -Os -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra"
LD="-Wl,-e,_start -Wl,-pagezero_size,0x100000000"

echo "== which helpers does mini.h declare?"
grep -cE 'snprintf|mount|getdirentries|sysctlbyname' "$M/mini.h"
grep -nE 'snprintf|int mount|getdirentries|sysctlbyname' "$M/mini.h" | head -10

echo
echo "== build"
$CC $CFLAGS -I$M -c $M/mini.c -o mini.o 2>&1 | head -5
$CC $CFLAGS -c $M/start.S -o start.o 2>&1 | head -5
cp /mnt/d/Liquid/ios/wsl/sideload.c .
$CC $CFLAGS -I$M -c sideload.c -o sideload.o 2>&1 | head -20
echo "compile exit: $?"
$CC $CFLAGS $LD -o sideload sideload.o mini.o start.o 2>&1 | head -15
echo "link exit: $?"
ls -la sideload 2>/dev/null
