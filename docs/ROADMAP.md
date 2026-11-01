# Roadmap / 未完成目标

Everything below is open work as of the port's last session (2026-11-01). Each item says what is
known, what was tried, and what the next step probably is. 下面是遗留的未完成目标，每项写明现状、试过什么、下一步思路。

---

## 1. Install third-party apps — 安装第三方软件

**State / 现状**
* The kernel's AMFI is a stub (`xnu/files/libkern/amfi/miniamfi.c`): unsigned code runs, entitlements
  are read from the code signature when present.
* A test app was placed into `/Applications` at boot: an APFS **synthetic entry**
  (`kern.apfs_synthetic`) + **bindfs** mount of the bundle from `/redwood/apps`. The tool
  (`src/sideload.c`) succeeded — `sideload: Hello.app -> /Applications/Hello.app` — but **the boot
  then hangs** (no iOS), and no log is produced (the kernel/init never reach the return path).
* Erasing the persistence store did not help; disabling the `sideload` line made the boot work
  again, so the bind-mounted bundle (or Launch Services' scan of it) is the prime suspect.
* boot 卡死；清空持久化存储无效；去掉 sideload 行后启动正常 → 嫌疑在 bind 挂载的 app 或
  Launch Services 对它的扫描。

**Tried / 试过**
* `bindfs` over a synthetic dir — EPERM at first (only PID 1 or an entitled task); relaxed the
  kernel check to allow root, which made the mount succeed. Then the boot hang.
* Suspicion: the mount is not what LS expects (the base project explicitly uses synthetic entries,
  **not** mounts, for exactly this reason), or SpringBoard crashes on the unsigned bundle.

**Next / 下一步**
* Inspect LS/SpringBoard behaviour with the mount in place (log traffic; check whether `lsd` or
  SpringBoard spin), maybe mount the app only *after* LS has started.
* Proper route: install through `installd`'s XPC (MobileInstallation) with the
  `com.apple.private.mobileinstall.allowedSPI` entitlement. With the stub AMFI, a binary carrying a
  signature blob with that entitlement should be accepted; `ldid` is not yet available in this
  environment (Theos ships it, but it is not installed here).
* Or port the base project's **rootless jailbreak environment** (Sileo + `/var/jb` + tweak loader).
  The pieces exist upstream (`userland/src/inject_tweaks.c`, `mkjobs.py --jb`), our image already
  carries `lib/libhakios.tweaks.dylib`.

## 2. Persistence restore — 持久化恢复

**State / 现状**
* **Save works**: at run end the kernel asks `var_store` (`kern.redwood.var_save`), the saver packs
  `/private/var` and writes it through `/dev/rufs_darwin`; the loader verifies each write.
  Verified: `ufs: 3 writes into darwin, 28672 bytes, read back the same; 0 refused`, and the saver's
  own report `slot 0: generation 1, 209 entries, 19336 bytes, crc 5814710e`.
* **Load hangs**: with a store present, the boot does not complete. The store's slot headers were
  zeroed to test, which restored the boot — so the load path (`init` → `var_store load`) is where it
  stops.
* 保存成功、恢复卡死；清空槽头后可正常启动。问题在 init 的 `var_store load` 路径。

**Next / 下一步**
* Log the load step: build a `var_store load` with verbose output, or run it by hand from a debug
  init; check the archive format (slot header, CRC, compression) and whether the unpack blocks on a
  filesystem operation (xattr, chown, long names).
* Make `init`'s restore non-blocking/time-limited so a bad store can never stop the boot (the base
  project's design intends exactly this: "a bad store cannot keep the phone from starting").

## 3. Android ⇄ iOS switching without a PC — 无电脑切换安卓/iOS

**State / 现状**
* Android lives on slot `a` (`boot_a` untouched), the iOS chain on slot `b`. Switching today is
  `fastboot --set-active=a|b` — needs a PC.
* The `misc` partition carries bootloader control blocks; the A/B slot marker's encoding was not
  decoded (`fastboot --set-active` writes it). `misc` at 0x0 historically held
  `bootonce-bootloader`, at 0x1000 `boot-system0`.

**Next / 下一步**
* Diff `misc` before/after a `fastboot --set-active=b` (dump from Android, then from the other slot)
  to find the slot field; then:
  * Android side: a root script (or Magisk action) writes it and reboots.
  * iOS side: the loader (or a small kernel driver) writes it at the next boot.
* The loader can already write `misc` (it writes the log to `logdump` through the same UEFI block
  I/O; an `oem fastboot` command was added that writes `bootonce-bootloader` into `misc`).

## 4. Full storage capacity display — 完整存储容量显示

**State / 现状**
* iOS "About" shows ≈ 2 GB — it reports the RAM-loaded system image (`md0`).
* The base project presents a 128 GiB data volume through a bitmap block allocator with a
  cross-session bitmap; not ported.

**Next / 下一步**
* Port the base project's block allocator + data volume presentation
  (`bsd/dev/…ufs*`, the "128 GiB UFS data volume" in the base README), or fake the capacity in the
  volume's statistics once the data volume is flash-backed.

## 5. Phantom touches — 幻触

**State / 现状**
* Unescaped `0x23` motion events surface as phantom touches; they also keep the screen awake so the
  phone does not auto-lock.
* Three filters were tried (duplicate counter drop, MOTION-without-ENTER drop, 150 ms timeout) and
  all made the panel feel worse — reverted.

**Next / 下一步**
* Understand the panel's actual event sequences (log raw reports for a session, compare with the
  vendor driver's handling), then filter only the impossible sequences (e.g. motion before touch
  down, or after up, with correct timing).

## 6. Bluetooth (M7) — 蓝牙

**State / 现状**
* Not started. The QCA6390 radio is shared with Wi-Fi; Bluetooth needs the M7/PMU bring-up and the
  BT firmware (`bd_j1gl.elf` is already in the small image for Wi-Fi's use).

## 7. Other open ends — 其他

* **`var_store` cleanup**: the diagnostic prints (`DIAG …`) added while debugging are still in the
  binary; remove them. Consider a periodic save (`watch SECS`) for long runs.
* **Store size**: the current slots are 512 MiB each in the ~1.7 GiB free after the system image;
  a real session's `/private/var` is ~100–200 MB, which fits, but the store should report/refuse
  gracefully when it does not.
* **`boot_b` chain robustness**: if the image or the store is bad the phone can stop at the loader;
  the loader should fall back to USB mode (a PC) or to Android after a timeout.
* **Kernel build**: `xnu-build.sh` reports a symbolset check failure (`exported name not in import
  list`) on every build but still produces the kernel; the check should be understood/fixed so the
  build's exit code means something.
* **Touch/display**: the existing port carries a lot of experimental tuning (TE calibration, line
  timings, copy phases); a cleanup pass with the measured numbers would help.
