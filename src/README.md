# src — the port's code that this repository carries

| Path | What it is | Licence |
|---|---|---|
| `kernel/` | The port's kernel **file overlay**: the drivers and XNU modifications that turn the XNU tree into this port — `bsd/dev/redwood_*` (touch, display, GPU-facing, UFS, UART, buttons, battery, Wi-Fi glue, …), `iokit/Kernel/{RedwoodGPU,MiniIOMobileFramebuffer,MiniIOSurface,…}`, `bsd/miscfs/{apfs,ramfs,bindfs}`, `libkern/amfi` (the stub AMFI), `osfmk/minicrypto`, and `bsd/conf/files` with the driver entries. These are applied by copying over the base XNU tree. | APSL 2.0 (derivative of Apple's XNU) |
| `loader/` | The XNUP loader (the EFI application the firmware starts), as this port uses it: `loader.c` (base loader + this port's patch — booting the installation from the phone's `darwin` partition, the on-screen colour progress, the post-run reset), `build.sh`, `efi.h`, `mkpayload.py`, `mkidentity.py`. `identity.h` is **not** carried: `mkidentity.py` generates it from the user's own firmware. | MIT (base project's loader) |
| `userland/` | `var_store.c` — the saver that packs `/private/var` into the phone's flash at the end of a run, adapted to this port's `darwin` partition, slot sizes and the kernel's `kern.redwood.*` handshake. | MIT (base project's userland) |
| `sideload.c` | A tiny static tool run as root from `/redwood/etc/rc` before launchd starts. For each `*.app` in `/redwood/apps` it makes an **APFS synthetic entry** on the read-only root volume at `/Applications/NAME.app` (`kern.apfs_synthetic`) and **bindfs**-mounts the real bundle on it. It works — the app is placed — but the boot then hangs (see `docs/ROADMAP.md` §1), so it is **disabled in the current image**. | MIT |
| `kernel-patches/` | The scripts that applied this port's last kernel/loader changes, kept as a record of *why* each change was made (the sources in `kernel/` and `loader/` already contain the result). | MIT |

Nothing in this directory redistributes Apple's or the vendor's binaries: the overlay is source
only, and the firmware blobs the drivers load are extracted from the phone itself at build time.
