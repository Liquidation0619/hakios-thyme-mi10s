#!/usr/bin/env python3
"""mkrun.py OUT.bin "cmdline" [kernel] - an XNUP payload for the loader (kernel + root + cmdline)."""
import struct, sys, os
IOS = os.environ.get('IOS', os.path.expanduser('~/hackios'))

out = sys.argv[1]
cmd = sys.argv[2] if len(sys.argv) > 2 else "debug=0x8 cpus=1"
kernel = sys.argv[3] if len(sys.argv) > 3 else os.path.join(IOS, "thyme", "kernel-stage.bin")


def add(body, name, data):
    return body + struct.pack("<16sII", name.encode(), len(data), 0) + data + b"\0" * (-len(data) % 16)


body = b""
body = add(body, "kernel", open(kernel, "rb").read())
body = add(body, "ramdisk", open(os.path.join(IOS, "thyme", "root.apfs"), "rb").read())
body = add(body, "cmdline", cmd.encode() + b"\0")
body += b"\0" * 24
open(out, "wb").write(struct.pack("<II", 0x50554E58, len(body)) + body)
print(f"{out}: {os.path.getsize(out)} bytes, cmdline: {cmd}")
