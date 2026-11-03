#!/usr/bin/env python3
"""Build the `darwin` partition image for the phone: the boot payload and the system image in one
flat blob the loader understands.

    [ 4096-byte header "IOSSTOR1" ][ XNUP payload ][ system image ]

The header carries what the loader needs (sizes, a SHA-256 of the payload+image) and the boot
arguments for this run.

Inputs (override with environment variables):
  KERNEL   self-built XNU Mach-O for kona           default: $IOS/thyme/kernel-stage.bin
  SMALL    the small image (root.apfs: /thyme)    default: $IOS/thyme/root.apfs
  SYSTEM   the big system image (root, md0)         default: $IOS/thyme/system.apfs
  OUT      where the image is written               default: $IOS/thyme/ios-storage/ios.img
  CMDLINE  kernel command line (see below)          default: this port's working set

The command line matters: `ufs=2` starts the loader's block service (the kernel reads the phone's
storage through it), `ufs_write=1` lets the kernel write the var store into `darwin`,
`var_store=1` asks init/var_store to keep /private/var there. `loader_return_after` is the run's
own time limit (seconds) after which the kernel returns to the loader and the store is saved.
"""
import hashlib
import os
import struct

IOS = os.environ.get("IOS", os.path.expanduser("~/hackios"))
KERNEL = os.environ.get("KERNEL", os.path.join(IOS, "thyme", "kernel-stage.bin"))
SMALL = os.environ.get("SMALL", os.path.join(IOS, "thyme", "root.apfs"))
SYSTEM = os.environ.get("SYSTEM", os.path.join(IOS, "thyme", "system.apfs"))
OUT = os.environ.get("OUT", os.path.join(IOS, "thyme", "ios-storage", "ios.img"))

DEFAULT_CMDLINE = (
    "debug=0x8 cpus=7 bootcpu=7 adreno=21 adreno_keep=1 touchscreen=1 gl=1 cpufreq=1 "
    "fb_scan=1 fb_wcomb=1 pon=8 pon_power_bit=1 pon_vol_bit=2 buttons=1 wifi=1 "
    "wifi_mac=02:00:00:00:00:01 loader_return_after=900 ufs=2 ufs_write=1 var_store=1"
)
CMDLINE = os.environ.get("CMDLINE", DEFAULT_CMDLINE).encode()

HEADER = 4096
MAGIC = b"IOSSTOR1"


def entry(name, body):
    """one XNUP payload entry: [16-byte name][len][reserved][body padded to 16]"""
    return struct.pack("<16sII", name.encode(), len(body), 0) + body + b"\0" * (-len(body) % 16)


kernel = open(KERNEL, "rb").read()
small = open(SMALL, "rb").read()
system = open(SYSTEM, "rb").read()

payload = struct.pack("<II", 0x50554E58, 0)  # magic + size, size patched below
body = entry("kernel", kernel) + entry("ramdisk", small) + entry("cmdline", CMDLINE + b"\0")
payload = struct.pack("<II", 0x50554E58, len(body)) + body

digest = hashlib.sha256()
digest.update(payload)
digest.update(system)

hdr = bytearray(HEADER)
hdr[0:8] = MAGIC
struct.pack_into("<I", hdr, 8, 1)                 # version
struct.pack_into("<I", hdr, 12, 0)                # boot_mode: 0 = boot iOS, 1 = stay in the loader (USB)
struct.pack_into("<Q", hdr, 16, len(payload))     # payload length
struct.pack_into("<Q", hdr, 24, len(system))      # system image length
struct.pack_into("<Q", hdr, 32, len(system))      # (single-region layout: part1 = everything)
struct.pack_into("<Q", hdr, 40, 0)
struct.pack_into("<Q", hdr, 48, 0)
struct.pack_into("<Q", hdr, 56, 0)
hdr[96:128] = digest.digest()

image = bytes(hdr) + payload + system
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "wb") as f:
    f.write(image)

print("payload : kernel %d + small %d + cmdline %d = %d bytes"
      % (len(kernel), len(small), len(CMDLINE), len(payload)))
print("system  : %d bytes" % len(system))
print("image   : %d bytes -> %s" % (len(image), OUT))
print("cmdline : %s" % CMDLINE.decode())
