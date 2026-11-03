# hakios-thyme-mi10s

**Hakios on the Xiaomi Mi 10S (thyme)** — unmodified iOS 16.5 userland on a self-built XNU kernel.

[中文说明](#中文说明) · [Status](#status) · [Architecture](#architecture) · [Repository layout](#repository-layout) · [Building](#building) · [Roadmap](#roadmap) · [Legal](#legal-and-attribution)

A port of [Hakios](https://github.com/DBC1349/hackios-mi10ultra) to a **Xiaomi Mi 10S** — codename
*thyme*, Qualcomm **Snapdragon 870** (SM8250-AC "kona"), Adreno 650, UFS 3.1, 1080×2340 90 Hz AMOLED.
Apple's iOS 16.5 userland — launchd → backboardd → SpringBoard — runs unmodified on top of an XNU
kernel this project builds from source, on real hardware.

The **kernel and driver sources are in [`src/`](src/)**: `src/kernel/` is the port's file overlay
(the drivers and the XNU changes, copied over an XNU tree), `src/loader/` the EFI loader,
`src/userland/` the persistence saver. The iOS image and every vendor firmware blob are *not* in
this repository — see [Legal](#legal-and-attribution).

---

## 中文说明

这是一个把 **未改动的 iOS 16.5 用户态**（launchd → backboardd → SpringBoard）移植到
**小米 10S（thyme，骁龙 870 / SM8250-AC "kona"，Adreno 650，UFS 3.1，1080×2340 90Hz AMOLED）** 的项目：
内核是用源码自行构建的 XNU，运行在真实手机上。

**内核与驱动源码都在 [`src/`](src/)** 目录：`src/kernel/` 是端口的文件覆盖层（驱动 + XNU 修改），
`src/loader/` 是 EFI 引导器，`src/userland/` 是持久化保存器。

> 仓库中**不包含** iOS 系统本体与任何厂商固件：系统镜像需要使用者用苹果官方 IPSW 自己构建
>（见[法律与致谢](#legal-and-attribution)），固件从手机自己的安卓分区提取。

---

## Status

### Working / 已实现

| Item | 项目 | Notes / 说明 |
|---|---|---|
| Boot chain | 启动链 | UEFI (Project Silicium) → custom EFI loader → XNU 8796.121.2 (Darwin 22.5.0, iPhone10,3) |
| **iOS desktop** | **iOS 桌面** | launchd → backboardd → SpringBoard, icons, gestures, apps launch |
| Touch | 触摸 | ST FTS521 over I2C — multi-touch, gestures, 0 errors in runs |
| GPU | GPU | Adreno 650 via Mesa freedreno FD650 (GMU 587 MHz); CoreAnimation rendered by hardware |
| Display | 显示 | TE-synced 60.33 Hz (the panel's firmware default), 7083 ns/line, 2340 lines; frames follow the panel's scan (no tearing) |
| Screen off / buttons | 熄屏/按键 | PM8150 PON side button + volume keys; framebuffer power selector |
| Wi-Fi | WiFi | QCA6390 over PCIe/MHI/QRTR/QMI → ath11k on FreeBSD LinuxKPI; scanning works (SCAN_REQ/SCAN_RESULT answered) |
| **Chinese UI** | **中文界面** | The system image is built with the zh-Hans/zh-Hant localisations kept, and the PingFang **Medium** weight is kept (it is the icon-label font — without it the home-screen app names turn into tofu squares) |
| **Rootless jailbreak** | **越狱环境** | A Procursus prefix (Sileo) at `/jb`, grafted onto `/var/jb` by `init`; **Sileo opens and its packages install**. The jailbreak small image is trimmed so kernel+image fit the loader's 64 MiB payload |
| **USB boot (development)** | **USB 启动（开发）** | `fastboot boot loader.img` + staging the 2.4 GiB system image and a 53 MiB payload — the fastest test loop; no flash writes |
| Boot from the phone's own flash | 手机自主启动 | UEFI + loader on `boot_b`, a 4 GiB `darwin` partition carved off `userdata` carries payload + system image; the loader reads and boots them (no PC). *Not the current development path* — see Roadmap |
| Data persistence (save) | 数据持久化（保存）| `/private/var` packed and written into `darwin` at the end of a run (`var_store=1 ufs_write=1`); verified: 3 writes, 28 672 B, read back identical |
| Android kept intact | 安卓数据保留 | The carve keeps Android's `/data` (incl. a 3.3 GB Termux container); no other partition touched |
| A/B dual-boot wiring | A/B 双系统 | Android on slot `a` (`boot_a` untouched), the iOS chain on slot `b` (`boot_b`) |

### Not yet working / 未实现

| Item | 项目 | State / 现状 |
|---|---|---|
| **90 Hz refresh** | **90Hz 高刷** | The panel is the `j1u 42 02 0b` (Mi 10S) command-mode panel; the firmware leaves it at 60.33 Hz. The vendor's 90 Hz mode is known (see [Roadmap](#roadmap)): same pixel clock, DCS `2F 04` + `26 02` in HS mode. The kernel can transmit DSI commands (transmission completes) but the panel does not switch — the vendor's real path is the command-DMA buffer, which needs the display SMMU set up. **In progress.** |
| Persistence restore | 持久化恢复 | The save works; the load at boot hangs (init's `var_store load`) — the store format/unpack path needs investigation |
| Android ⇄ iOS switching without a PC | 无电脑切换 | Needs a PC today (`fastboot --set-active=a\|b`); the `misc` BCB slot encoding is not decoded |
| Full storage capacity | 完整存储容量 | iOS "About" shows ~2 GB (the RAM-loaded system image); the base project's bitmap block allocator is not ported |
| Third-party app installation | 第三方应用安装 | The jailbreak provides Sileo and package installation; a raw "app into /Applications" path (synthetic entry + bindfs) hangs Launch Services at boot |
| Phantom touches | 幻触 | Unescaped `0x23` motion events; the filters tried made the panel feel worse |
| Bluetooth / sound | 蓝牙 / 音频 | Not started (sound binaries exist in the image, no DSP bring-up yet) |
| Post-run USB / reboot hygiene | 运行收尾 | The loader now cold-resets after the run instead of hanging in USB bring-up |

---

## Architecture

```
  Power on
     │
     ▼
  ABL (Qualcomm bootloader, unlocked, A/B slots)          ── same as Android's
     │  slot b                                              (boot_a untouched)
     ▼
  boot_b: UEFI (Project Silicium, kona) + BootShim + XNUP payload
     │
     ▼
  xnuloader (the port's EFI loader)                       ── src/loader/
     │   • reads the `darwin` partition: header "IOSSTOR1" → payload (kernel + small image + cmdline)
     │   • or, in development, serves USB: oem ramdisk → stage system → stage payload → oem run
     │   • writes its log to the `logdump` partition; samples the kernel's console every 2 s
     ▼
  XNU (self-built) → stage0 → init → launchd → SpringBoard     ── src/kernel/ (drivers), src/userland/
     │
     │  storage:  userdata (shrunk 4 GiB, Android data kept)
     │            darwin: [4 KiB header][XNUP payload][2.4 GiB system image][var store 2×512 MiB]
     │            /private/var is a RAM filesystem, saved into the store at run end
     ▼
  iOS 16.5 userland
```

### Where each driver lives (in `src/kernel/`)

| Part | Files |
|---|---|
| Touch | `bsd/dev/thyme_st_touch.c`, `thyme_input.c`, `fts_port/` |
| Display / framebuffer sync | `bsd/dev/thyme_display.c`, `iokit/Kernel/MiniIOMobileFramebuffer.cpp` |
| GPU | `bsd/dev/thyme_adreno.c`, `iokit/Kernel/ThymeGPU.cpp`, `MiniIOSurface.cpp` |
| Storage (UFS + the `darwin` store) | `bsd/dev/thyme_ufs.c`, `thyme_ufshc.c` |
| Wi-Fi (QCA6390) | `bsd/dev/thyme_wifi.c`, `thyme_lx.c`, `thyme_qmi.c`, `umi_mhi.c`, `umi_pcie.c`, … |
| Filesystems (read-only APFS, ramfs, bindfs) | `bsd/miscfs/apfs/`, `bsd/miscfs/ramfs/`, `bsd/miscfs/bindfs/` |
| Stub AMFI | `libkern/amfi/` |
| Driver registration | `bsd/conf/files`, `libkern/conf/files` |

## Repository layout

```
src/
  kernel/          the port's kernel FILE OVERLAY: the drivers above and the XNU files changed for the
                   port, at their XNU-tree paths — copied over an XNU tree to build     [APSL 2.0]
  loader/          the XNUP loader: loader.c, build.sh, efi.h, mkpayload.py, mkidentity.py   [MIT]
  userland/        var_store.c — packs /private/var into the flash at run end               [MIT]
  sideload.c       the app-placement test tool (synthetic entry + bindfs)                     [MIT]
  kernel-patches/  the scripts that applied this port's last kernel/loader changes (record)    [MIT]
scripts/           build-image.py, deploy.md, loader-autoboot.patch, the jailbreak image
                   builders (build-jb-image*.sh) and the language patch (patch-keep-lang.py)   [MIT]
NOTICE.md          attribution and legal notes
LICENSE, LICENSE.APSL
```

See [`src/README.md`](src/README.md) for what each part is and how it is applied.

## Building

1. **Kernel** — apply `src/kernel/` over an XNU 8796.121.2 tree (the base project's overlay
   mechanism), configure the kona platform, build; the result is a 15.8 MB kernel image.
2. **Loader** — `src/loader/build.sh` compiles `loader.c` with clang/lld; `scripts/pack-boot.py`
   repacks an Android boot image: `kernel = gzip(BootShim + UEFI FD + XNUP payload) + DTB`.
3. **System image** — build from Apple's own IPSW (16.5, 20F66, iPhone10,3): the base project's
   `sysimage/unpack.sh` extracts the root filesystem, `select-system.py` keeps the files that fit
   (and the languages — `scripts/patch-keep-lang.py` adds zh-Hans), `userland/build.sh` lays out the
   volume, `mkapfs.py` writes `system.apfs`.
4. **Payload** — `[kernel][small image][cmdline]` in one XNUP container (≤ 64 MiB, the loader's
   payload limit).
5. **Phone (development loop)** — `fastboot boot loader.img`, `oem ramdisk`, `stage system.apfs`,
   `stage payload`, `oem run`; the loader stays in USB mode and serves the log. See
   `scripts/deploy.md`.

The boot arguments the port runs with are in `scripts/deploy.md` and the comments of the drivers
(`fb_scan=1 fb_wcomb=1 touchscreen=1 gl=1 cpufreq=1 pon=8 pon_power_bit=1 pon_vol_bit=2 buttons=1
wifi=1 …`).

## Roadmap

### 90 Hz (in progress)

What is known:

* The panel is **`j1u 42 02 0b`** (Mi 10S, command mode, DSC), identified from the Android kernel's
  own log (`[xiaomi 42 02 0b cmd mode dsc dsi panel]`, `mi,panel-id 0x4a3200420201`).
* The vendor's panel DTSI (`dsi-panel-j1u-42-02-0b-dsc-cmd.dtsi`) gives both modes:

  | mode | framerate | v-back | v-front | note |
  |---|---|---|---|---|
  | 60 Hz | 60 | **560** | **600** | the large vertical blanking is what slows the scan down |
  | 90 Hz | 90 | 8 | 4 | minimal blanking |

  The **pixel clock is the same** in both modes, so **no DSI PLL change is needed**.
* The switch is a DCS long write (`0x39`) of `2F 04` followed by `26 02`, sent in **HS mode**
  (`dsi_hs_mode` in the DTSI).

What is missing:

1. **Deliver the command reliably.** The vendor's path is a **command DMA buffer** (the packet in
   memory, its address in `DSI_DMA_CMD_OFFSET`, the display SMMU mapping it). This kernel currently
   uses the controller's embedded/FIFO path; commands are transmitted (the `CMD_MODE_DMA_DONE`
   interrupt fires) but the panel does not act. Next: set up a DMA command buffer (and the display
   SMMU mapping for it), or use a location the display's SMMU already maps.
2. **Send the pair** `2F 04` / `26 02` in HS mode.
3. **Re-measure** the scan after the switch (`thyme_display_scan_start()` again — the code is
   written and exercised through the `disp_cmd=` boot argument).
4. **Verify**: UFOTest shows 90; no tearing (the frame-copy window uses the new timing); touch and
   GPU unchanged.

Debug tooling built for this (in `thyme_display.c`): boot arg `disp_dump=1` (print the INTF/DSI
register state, repeated so the loader's console samples catch it) and `disp_cmd=2F04,2602` (send a
list of DCS writes a few seconds into the boot, then re-measure the scan and print the rate).

### Other open work

* **Persistence restore** — the load at boot hangs; make `init`'s restore time-limited so a bad store
  can never stop the boot.
* **Android ⇄ iOS without a PC** — decode the `misc` BCB slot field (dump `misc` before/after a
  `fastboot --set-active`), then write it from Android or from the loader.
* **Full capacity display** — port the base project's block allocator, or fake the volume statistics.
* **Third-party apps** — use the jailbreak's installer path (Sileo/apt) rather than bind-mounting a
  bundle into `/Applications`.
* **Phantom touches, Bluetooth, audio** — as above.

## Legal and attribution

* **No Apple code, binaries, or system images** are distributed here. The iOS image is Apple's
  copyrighted software; each user builds it from an IPSW they obtained themselves, for their own
  device.
* **No vendor firmware** is distributed. GPU zap shader, Wi-Fi firmware etc. are extracted from the
  user's own phone at build time.
* The kernel work is a derivative of **Apple's XNU** (APSL 2.0), of the base project
  **[DBC1349/hackios-mi10ultra](https://github.com/DBC1349/hackios-mi10ultra)** and of the
  **mainline HakiOS** project. The Wi-Fi stack also carries **Linux ath11k / LinuxKPI** code
  (GPL-2.0); `src/kernel/` is published under the APSL, with the licenses of the components it
  contains respected (see `NOTICE.md`).
* **Mainline HakiOS — [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel)**:
  the project HakiOS has grown into (drivers by subsystem, Metal-based CoreAnimation, the 120 Hz
  animation work, the UEFI and device packages, its documentation). This port is based on the
  original HackiOS/Mi 10 Ultra tree and follows the mainline where it applies; its documentation and
  its Metal/CoreAnimation work are the reference for this port's next steps.
* **Reference implementation: [azzlantern/hackios-thyme](https://github.com/azzlantern/hackios-thyme)** —
  the Wi-Fi bring-up approach and the boot/storage workflow followed it (its sources were read,
  audited, then adapted to this device).
* This is a research/hobby project for hardware you own. Do not use it to circumvent Apple's
  software licensing.
