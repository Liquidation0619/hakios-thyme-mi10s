#!/usr/bin/env python3
"""The GPT LBAs on this phone are in the UFS's 4096-byte logical blocks -- the same unit the UEFI's
partition info and the loader's block table use -- so darwin's first/last LBA and its block count are
the values the survey prints, not those divided by 8."""
import sys

# loader
p = "/opt/hackios/xnuloader/loader.c"
s = open(p, newline="").read()
subs = [
    ("#define BLOCK_DARWIN_FIRST 7603968ULL", "#define BLOCK_DARWIN_FIRST 60831744ULL"),
    ("#define BLOCK_DARWIN_LAST  8652543ULL", "#define BLOCK_DARWIN_LAST  61880314ULL"),
]
for old, new in subs:
    if old not in s:
        sys.exit("loader anchor missing: %s" % old)
    s = s.replace(old, new, 1)
    print("loader: %s -> %s" % (old.split()[-1], new.split()[-1]))
open(p, "w", newline="").write(s)

# kernel
p = "/opt/hackios/xnu-src/xnu-8796.121.2/bsd/dev/thyme_ufs.c"
s = open(p, newline="").read()
subs = [
    ("#define UFS_DARWIN_FIRST   7603968ULL", "#define UFS_DARWIN_FIRST   60831744ULL"),
    ("#define UFS_DARWIN_BLOCKS  1048576ULL", "#define UFS_DARWIN_BLOCKS  1048571ULL"),
]
for old, new in subs:
    if old not in s:
        sys.exit("kernel anchor missing: %s" % old)
    s = s.replace(old, new, 1)
    print("kernel: %s -> %s" % (old.split()[-1], new.split()[-1]))
open(p, "w", newline="").write(s)
print("ok")
