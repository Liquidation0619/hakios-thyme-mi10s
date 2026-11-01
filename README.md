# hackios-thyme 鈥?HackiOS on the Xiaomi Mi 10S (thyme / SM8250 "kona")

[涓枃璇存槑](#涓枃璇存槑) 路 [Status](#status) 路 [Architecture](#architecture) 路 [Repository layout](#repository-layout) 路 [Legal](#legal-and-attribution)

A port of [HackiOS](https://github.com/DBC1349/hackios-mi10ultra) 鈥?unmodified iOS 16.5 userland
(launchd 鈫?backboardd 鈫?SpringBoard) on top of a self-built XNU 鈥?to a **Xiaomi Mi 10S**
(codename *thyme*, Qualcomm SM8250 "kona", Adreno 650, UFS 3.1).

This repository is the **port's working record**: how the phone was brought up, the scripts that
build and deploy it, and 鈥?most importantly 鈥?an honest list of what works and what does not yet.

> iOS itself is **not** in this repository. The system image has to be built by whoever runs it,
> from Apple's own IPSW, on their own hardware (see [Legal](#legal-and-attribution)). The same
> goes for every vendor firmware blob: they are extracted from the phone's own Android
> partitions.

---

## 涓枃璇存槑

杩欐槸涓€涓妸 **鏈敼鍔ㄧ殑 iOS 16.5 鐢ㄦ埛鎬?*锛坙aunchd 鈫?backboardd 鈫?SpringBoard锛夌Щ妞嶅埌
**灏忕背 10S锛坱hyme锛岄獊榫?865 / SM8250 "kona"锛孉dreno 650锛孶FS 3.1锛?* 鐨勯」鐩紝鍐呮牳涓鸿嚜琛屾瀯寤虹殑 XNU銆?
鏈粨搴撴槸**绉绘杩囩▼鐨勫伐浣滆褰?*锛氬惎鍔ㄩ摼銆佹瀯寤?閮ㄧ讲鑴氭湰锛屼互鍙婃渶閲嶈鐨勨€斺€?*宸插疄鐜板拰鏈疄鐜扮洰鏍囩殑娓呭崟**銆?
> 浠撳簱涓?*涓嶅寘鍚?* iOS 绯荤粺鏈綋銆傜郴缁熼暅鍍忛渶瑕佷娇鐢ㄨ€呰嚜宸辩敤鑻规灉瀹樻柟 IPSW 鍦ㄨ嚜宸辩殑璁惧涓婄敓鎴?>锛堣[娉曞緥涓庤嚧璋(#legal-and-attribution)锛夈€傛墍鏈夊巶鍟嗗浐浠讹紙GPU/WiFi/闊抽绛?blob锛夐兘浠庢墜鏈?> 鑷繁鐨勫畨鍗撳垎鍖烘彁鍙栵紝鍚屾牱涓嶉殢浠撳簱鍒嗗彂銆?
---

## Status

### Working / 宸插疄鐜?
| Item | 椤圭洰 | Notes / 澶囨敞 |
|---|---|---|
| Boot chain | 鍚姩閾?| UEFI (Project Silicium on kona) 鈫?custom EFI loader 鈫?XNU 8796.121.2 (Darwin 22.5.0, iPhone10,3) |
| Touch | 瑙︽懜 | ST FTS521 over I2C, multi-touch, gestures, 0 errors in runs |
| GPU | GPU | Adreno 650 via Mesa freedreno FD650, GMU 587 MHz, hardware-rendered CoreAnimation |
| Display | 鏄剧ず | AMOLED, TE-synced 60.33 Hz, 7083 ns/line, 2340 lines; 60.2 fps measured with `cpufreq=1` |
| Screen off / buttons | 鐔勫睆/鎸夐敭 | PM8150 PON side button + volume keys, framebuffer power selector 12 |
| Wi-Fi | WiFi | QCA6390 over PCIe/MHI/QRTR/QMI 鈫?ath11k on FreeBSD LinuxKPI; scanning works (SCAN_REQ/SCAN_RESULT answered) |
| **Boot from the phone's own flash** | **鎵嬫満鑷富鍚姩** | UEFI + loader on `boot_b`; a 4 GiB `darwin` partition carved off `userdata` carries payload + 2.33 GiB system image; the loader reads them and boots 鈥?**no PC needed** |
| **Data persistence (save)** | **鏁版嵁鎸佷箙鍖栵紙淇濆瓨锛?* | `/private/var` packed and written into `darwin` at the end of a run (`var_store=1 ufs_write=1`); first successful save verified: 3 writes, 28 672 bytes, read back identical |
| Android kept intact | 瀹夊崜鏁版嵁淇濈暀 | The carve keeps Android's `/data` (including a 3.3 GB Termux container), no repartition of other entries |
| A/B dual-boot wiring | A/B 鍙岀郴缁熸帴绾?| Android on slot `a` (`boot_a` untouched), iOS chain on slot `b` (`boot_b`) |

### Not yet working / 鏈疄鐜帮紙"TODO"锛?
| Item | 椤圭洰 | Current state / 鐜扮姸 |
|---|---|---|
| **Third-party app installation** | **绗笁鏂瑰簲鐢ㄥ畨瑁?* | A test app can be placed into `/Applications` via APFS synthetic entry + `bindfs` (verified: `sideload: Hello.app -> /Applications/Hello.app`), but the **boot then hangs** 鈥?Launch Services' scan of the bind-mounted bundle is suspected. Needs: a Launch Services-friendly install (installd XPC with the stub-AMFI entitlement, or the base project's Sileo/rootless-JB path), not a raw bind mount. |
| **Persistence restore** | **鎸佷箙鍖栨仮澶?* | The save works; the **load at boot hangs** when a store is present (init's `var_store load`). The store format/unpack path needs investigation. Erasing the two slot headers restores (empty) booting. |
| Android 鈬?iOS switching without a PC | 鏃犵數鑴戝垏鎹㈠畨鍗?iOS | Today: `fastboot --set-active=a|b` needs a PC. The `misc` BCB slot encoding has not been decoded yet; writing it from the loader/Android would make the switch self-service. |
| Full storage capacity display | 瀹屾暣瀛樺偍瀹归噺鏄剧ず | iOS "About" shows ~2 GB (the RAM-loaded system image). The base project presents a 128 GiB data volume via a bitmap block allocator; that has not been ported. |
| Phantom touches | 骞昏Е | Unescaped `0x23` motion events cause phantom touches; the three filters tried made the panel feel worse and were reverted. |
| Bluetooth (M7) | 钃濈墮 | Not started (Wi-Fi shares the QCA6390 but Bluetooth needs its own M7/PMU bring-up). |
| Sound / DSP | 闊抽 | Not part of this port's goals yet. |
| Post-run USB / reboot hygiene | 杩愯缁撴潫鍚庣殑鏀跺熬 | The loader used to hang trying to bring USB up after the kernel returned; it now cold-resets instead (see `scripts/loader-autoboot.patch`). |

---

## Architecture

```
  Power on
     鈹?     鈻?  ABL (Qualcomm bootloader, A/B slots, unlocked)
     鈹? slot b
     鈻?  boot_b: UEFI (Project Silicium, kona drivers) + BootShim + XNUP payload
     鈹?     鈻?  xnuloader (the port's EFI loader)
     鈹?  reads the `darwin` partition: header ("IOSSTOR1") 鈫?payload (kernel + small image + cmdline)
     鈹?                                鈫?2.33 GiB system image into RAM at 0xFC000000
     鈹?  cmdline: ufs=2 ufs_write=1 var_store=1 鈥?     鈻?  XNU (self-built) 鈫?iOS userland (launchd, SpringBoard, 鈥?

  Storage:
    userdata  鈹€鈹€ shrunk by 4 GiB with f2fs resize (Android data kept)
    darwin    鈹€鈹€ new partition: [4 KiB header][XNUP payload][2.33 GiB system image][var store: 2脳512 MiB slots]
    boot_a    鈹€鈹€ Android (untouched)          boot_b 鈹€鈹€ the iOS chain
```

### The two storage mechanisms

* **System image**: loaded into RAM by the loader at `0xFC000000` (same address the USB-based
  workflow used), so the kernel sees it as `md0` 鈥?the root.
* **Data volume**: `/private/var` is a RAM filesystem rebuilt at every boot. A saver
  (`var_store watch`, started from `/redwood/etc/rc` as root) registers with the kernel
  (`kern.redwood.var_save`); when the run ends the kernel asks it to pack `/private/var` and write
  it through `/dev/rufs_darwin` (the loader serves the writes, verifies each read-back) into the
  store's two alternating slots. At boot `init` runs `var_store load` before launchd.

---

## Repository layout

```
README.md                 this file (bilingual)
docs/
  DEVELOPMENT.md          the port's full development log (Chinese; the working record)
  ROADMAP.md              the TODO list, in more detail (bilingual)
scripts/
  loader-autoboot.patch   our loader changes: boot iOS from the phone's own `darwin` partition,
                          partition identity check, post-run reset
  build-loader.sh         build the patched loader and repack the boot image
  build-image.py          build the `darwin` partition image (header + payload + system)
  deploy.md               how the image and the chain get onto the phone
  patch-kernel-*.py       the kernel changes as applied patches (see src/kernel-patches/README.md)
src/
  sideload.c              the test "install an app into /Applications" tool (synthetic entry + bindfs)
  loader/                 our loader code additions (the same content as the patch, per function)
tools/                    mkrun-style helpers, phone state checker, audio reminder for reboots
NOTICE.md                 attribution and legal notes
LICENSE                   MIT for this repo's own code
```

## Building (outline)

1. **XNU**: the base project's build (`xnu-build.sh` with the port's file overlay) 鈥?the port's
   kernel sources live outside this repo (derived from Apple's XNU and the base project).
2. **Loader**: `scripts/build-loader.sh` 鈥?compiles `xnuloader/loader.c` (base project + our patch)
   with clang/lld and repacks the Android boot image.
3. **System image**: build the iOS rootfs from Apple's IPSW with the base project's userland build,
   then `scripts/build-image.py` wraps it: `[header][XNUP payload][system image]`.
4. **Phone**: shrink `userdata` by 4 GiB (f2fs resize, keep Android data), carve a `darwin`
   partition (GPT with CRCs), write the image, flash the chain to `boot_b`, set the active slot.
   All steps and the exact values are in docs/DEVELOPMENT.md.

## Legal and attribution

* **No Apple code or binaries** are distributed here. The iOS system image is Apple's copyrighted
  software; each user builds it from an IPSW they obtained themselves, for their own device.
* **No vendor firmware** is distributed. GPU zap shader, Wi-Fi firmware, etc. are extracted from
  the user's own phone's Android partitions at build time.
* The kernel work is a derivative of **Apple's XNU** (APSL 2.0) and of the base project
  **[DBC1349/hackios-mi10ultra](https://github.com/DBC1349/hackios-mi10ultra)**; the port files
  and the loader come from there. The Wi-Fi stack uses **Linux ath11k** (GPL-2.0) through a
  FreeBSD LinuxKPI layer 鈥?this repository therefore contains no kernel/Wi-Fi source, only the
  notes and our own glue scripts.
* Reference port (read, not copied): **[azzlantern/hackios-thyme](https://github.com/azzlantern/hackios-thyme)**.
* This is a research/hobby project for hardware you own. Do not use it to circumvent Apple's
  software licensing.
