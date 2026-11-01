#!/usr/bin/env python3
"""Point the loader at our darwin partition geometry (the earlier attempt exited before writing the
file, so none of it was saved)."""
import sys

p = "/opt/hackios/xnuloader/loader.c"
s = open(p, newline="").read()

subs = [
    ("#define BLOCK_WRITE_FIRST  2042368ULL                  /* darwin's blocks of 4096: past both cryptexes */",
     "#define BLOCK_WRITE_FIRST  611200ULL                   /* darwin's blocks of 4096: past this port's iOS image */"),
    ("#define BLOCK_DARWIN_FIRST 25374720ULL                 /* its first and last LBA of unit 0, as the GPT has them */",
     "#define BLOCK_DARWIN_FIRST 7603968ULL                  /* its first and last LBA of unit 0, as the GPT has them */"),
    ("#define BLOCK_DARWIN_LAST  30617594ULL",
     "#define BLOCK_DARWIN_LAST  8652543ULL"),
    ("#define BLOCK_DARWIN_UNIQ  { 0xfd6854ad, 0x24aa, 0x4fc3, { 0x94, 0xfe, 0x29, 0x01, 0x16, 0x85, 0x8d, 0x68 } }",
     "#define BLOCK_DARWIN_UNIQ  { 0x987d3b20, 0x8cc1, 0x4d2e, { 0x91, 0x0f, 0xb1, 0xb4, 0x06, 0xf2, 0x12, 0xde } }"),
    ('efi_block_io *ios = find_partition_bio(u"ios");', 'efi_block_io *ios = find_partition_bio(u"darwin");'),
]
ok = True
for old, new in subs:
    if old not in s:
        print("anchor missing: %s" % old[:55])
        ok = False
        continue
    s = s.replace(old, new, 1)
    print("patched: %s" % old[:55])
if not ok:
    sys.exit("nothing written")
open(p, "w", newline="").write(s)
print("saved")
