# Notice — attribution, licensing and what is NOT here

## What this repository contains

This repository holds **documentation and our own small tools** for a hobby port of iOS's
userland to a Xiaomi Mi 10S. It intentionally contains **no Apple software, no system images, no
vendor firmware and no third-party source trees**.

## Not included (and why)

| Thing | Why not |
|---|---|
| iOS system image / rootfs / dyld cache / apps | Apple's copyrighted software. Each user builds an image from an IPSW they obtained themselves, for their own device. |
| Vendor firmware blobs (GPU zap shader, QCA6390 Wi-Fi firmware, ..) | Extracted from the user's own phone's Android partitions. Not redistributable. |
| The XNU kernel tree and the port's kernel drivers | Derived from Apple's XNU (**APSL 2.0**) and from the base project; the tree also carries **GPL-2.0** Linux `ath11k`/LinuxKPI glue. Keep those with their own licenses, not here. |
| The `xnuloader` loader in full | Base project code; only our patch is included (`scripts/loader-autoboot.patch`). |

## Attribution

* Base port: **DBC1349/hackios-mi10ultra** — XNU port, UEFI/Silicium setup, loader, drivers,
  userland build. The port builds on it directly.
* **Reference implementation — [azzlantern/hackios-thyme](https://github.com/azzlantern/hackios-thyme)**:
  the driver approach followed it. Its QCA6390 Wi-Fi bring-up (PCIe → MHI → QRTR → QMI/WLFW →
  ath11k on FreeBSD LinuxKPI, with the project's own `umi_*` glue and `lx_*` layer) and its
  boot/storage workflow were read, audited for anything unexpected (exec, sockets, file writes),
  and then adapted to this device with the port's own `redwood_*` naming. No file from that
  repository is redistributed here — and neither are Apple's or the vendor's binaries from it.
* XNU: Apple, APSL 2.0.
* Mesa (freedreno FD650), Linux ath11k / LinuxKPI, FreeBSD net80211: used by the port's kernel with
  their own licenses.
* UEFI firmware: Project Silicium / Mu-Silicium (BSD-2-Clause-Patent), plus vendor drivers from the
  phone's own XBL.

## The stub-AMFI and code signing

The port's kernel implements a stub AMFI (see the base project's `xnu/files/libkern/amfi/`): code
signing is not enforced, entitlements are read from a signature when one is present. That is a
research device setting — not a security model, and not a way to run software you did not obtain
legally.

## Using this

Everything here is for **hardware you own**, as a research/hobby project. Do not use it to
circumvent Apple's licensing or to run software you are not entitled to.
