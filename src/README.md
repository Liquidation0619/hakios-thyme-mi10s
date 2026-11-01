# src — the port's own code that this repository carries

| File | What it is |
|---|---|
| `sideload.c` | A tiny static tool run as root from `/redwood/etc/rc` before launchd starts. For each `*.app` in `/redwood/apps` it makes an **APFS synthetic entry** on the read-only root volume at `/Applications/NAME.app` (`kern.apfs_synthetic`) and **bindfs**-mounts the real bundle on it. It works — the app is placed — but the boot then hangs (see `docs/ROADMAP.md` §1), so it is **disabled in the current image**: the `sideload` line in `etc/rc` is commented out. |
| `kernel-patches/` | The kernel changes this port needed, as the scripts that apply them (they are small, textual and self-documenting). They are the diffs to run against the base project's XNU tree: |
| └ `kernel-darwin.py` | point the UFS write store at this port's `darwin` partition (first block, block count, write offset); |
| └ `fix-darwin-lba.py` | correct those values after the first attempt used 512-byte units instead of the UFS's 4096-byte blocks; |
| └ `kernel-open-store.py` | let the (non-root) userspace saver reach `/dev/rufs_darwin` and the two handshake sysctls; |
| └ `fix-bindfs-varstore.py` | let root (pid ≠ 1) bind; the stock check only allows pid 1 or an entitled task; |
| └ `fix-unaligned-read.py` *(loader, kept here for context)* | the system image begins at `4096 + payload_len`, which is not block aligned — read the leading partial block first; |
| └ `loader-darwin2.py`, `loader-postrun2.py`, `loader-oemfastboot.py` *(loader)* | the loader's side of the same: partition identity check for `darwin`, cold reset after the run instead of a USB bring-up that can hang, and an `oem fastboot` command. |

The **loader patch as a whole** is `scripts/loader-autoboot.patch`: it adds reading the installation
from the phone's own partition, the colour-coded progress on screen (so a phone with no serial port
can be debugged by looking at it), and the post-run reset.

Nothing in this directory redistributes the base project's or Apple's code: the patches are
expressed as edits to files that live in the user's own tree.
