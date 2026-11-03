# Notice — attribution, licensing and what is NOT here

## What this repository contains

This repository holds **documentation and our own small tools** for a hobby port of iOS's
userland to a Xiaomi Mi 10S. It intentionally contains **no Apple software, no system images, no
vendor firmware and no third-party source trees**.

## Licensing of this repository

Two licences apply, the same split the base HakiOS project uses (see `LICENSE`):

* **MIT** — the documentation, the scripts, the build tools, `src/sideload.c` and the loader patch.
* **APSL 2.0** — `src/kernel-patches/`, the modifications to the port's kernel tree, which is a
  derivative of Apple's XNU. The full text is in `LICENSE.APSL`; the Apple notices stay with the
  files those patches are applied to.

## Not included (and why)

| Thing | Why not |
|---|---|
| iOS system image / rootfs / dyld cache / apps | Apple's copyrighted software. Each user builds an image from an IPSW they obtained themselves, for their own device. |
| Vendor firmware blobs (GPU zap shader, QCA6390 Wi-Fi firmware, ..) | Extracted from the user's own phone's Android partitions. Not redistributable. |
| The XNU base tree itself | Apple's XNU (APSL 2.0). `src/kernel/` is only the port's overlay of new/changed files, to be copied over the base project's tree. |
| `xnu/.../loader/identity.h` | Generated from the firmware's device tree by `mkidentity.py`; every user generates their own. |
| Development backups (`*.bak` variants of driver files) | Working copies of other vendor variants; not part of the port. |

## Third-party code carried in `src/kernel/`

* `bsd/dev/thyme_lx.c`, `thyme_wifi*.c`, `thyme_qmi.c`, `umi_*` — the Wi-Fi glue sits between
  the kernel and code derived from Linux (`ath11k`, LinuxKPI; **GPL-2.0**) and from the FreeBSD
  net80211 layer. Keep those licences in mind if you redistribute these files; the upstream Linux
  sources are not carried here.
* `bsd/miscfs/apfs/libzbitmap`, `libdeflate` — upstream decompressors (see the notices in those
  files / directories).
* `bsd/miscfs/bindfs` — the bind filesystem, from the port's base project.

## Attribution

* Base port: **DBC1349/hackios-mi10ultra** — XNU port, UEFI/Silicium setup, loader, drivers,
  userland build. The port builds on it directly.
* **Mainline HakiOS — [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel)**:
  the project HakiOS has grown into — drivers by subsystem (`drivers/`), the Metal-based
  CoreAnimation work, the 120 Hz animation research, the UEFI and device packages, and the
  documentation this port's own docs follow where they apply. This port is based on the original
  HackiOS/Mi 10 Ultra tree and follows the mainline's direction for the parts still open (Metal,
  refresh rate, driver structure).
* **Reference implementation — [azzlantern/hackios-thyme](https://github.com/azzlantern/hackios-thyme)**:
  the driver approach followed it. Its QCA6390 Wi-Fi bring-up (PCIe → MHI → QRTR → QMI/WLFW →
  ath11k on FreeBSD LinuxKPI, with the project's own `umi_*` glue and `lx_*` layer) and its
  boot/storage workflow were read, audited for anything unexpected (exec, sockets, file writes),
  and then adapted to this device with the port's own `thyme_*` naming. No file from that
  repository is redistributed here — and neither are Apple's or the vendor's binaries from it.
* XNU: Apple, APSL 2.0.
* Mesa (freedreno FD650), Linux ath11k / LinuxKPI, FreeBSD net80211: used by the port's kernel with
  their own licenses.
* UEFI firmware: Project Silicium / Mu-Silicium (BSD-2-Clause-Patent), plus vendor drivers from the
  phone's own XBL.

## Naming

The port's own names are `thyme_*` (the phone's codename): driver files, functions, the
`/thyme` volume, the `kern.thyme.*` sysctls. The base projects used the POCO X5 Pro's codename
(`redwood`) for the same files; those were renamed here, so a tree built from this repository uses
`thyme_*` everywhere.

## The stub-AMFI and code signing

The port's kernel implements a stub AMFI (see the base project's `xnu/files/libkern/amfi/`): code
signing is not enforced, entitlements are read from a signature when one is present. That is a
research device setting — not a security model, and not a way to run software you did not obtain
legally.

## Using this

Everything here is for **hardware you own**, as a research/hobby project. Do not use it to
circumvent Apple's licensing or to run software you are not entitled to.
