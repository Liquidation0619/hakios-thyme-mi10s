#!/usr/bin/env python3
"""Point the kernel's UFS write store at our partition geometry."""
import sys

p = "/opt/hackios/xnu-src/xnu-8796.121.2/bsd/dev/thyme_ufs.c"
s = open(p).read()
subs = [
    ("#define UFS_WRITE_FIRST    2042368ULL                  /* darwin's blocks of 4096 */",
     "#define UFS_WRITE_FIRST    611200ULL                   /* darwin's blocks of 4096: past this port's iOS image */"),
    ("#define UFS_DARWIN_FIRST   25374720ULL                 /* its first LBA on the unit, as the GPT has it */",
     "#define UFS_DARWIN_FIRST   7603968ULL                  /* its first LBA on the unit, as the GPT has it */"),
    ("#define UFS_DARWIN_BLOCKS  5242875ULL",
     "#define UFS_DARWIN_BLOCKS  1048576ULL"),
]
for old, new in subs:
    if old not in s:
        sys.exit("anchor missing: %s" % old[:60])
    s = s.replace(old, new, 1)
    print("patched: %s" % old[:55])
open(p, "w").write(s)
print("ok")
