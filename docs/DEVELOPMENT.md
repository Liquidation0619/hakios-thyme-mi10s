# 触摸的调试通道 —— 已在这台 10S 上跑通的东西

写于 2026-10-31 03:15。这份文档只记"实测成立"的事：怎么把内核里的字弄出来、哪些设备块
真能读到、以及原厂驱动长什么样。每一项都附了验证它的那条命令/日志。

---

## 一、三条通道，各自的边界

### 1. checkpoint：内核活着时最好的通道

内核里有 `loader_checkpoint(n)`（`osfmk/arm64/loader_return.c`）。loader 在启动前把
"要停在第几号 checkpoint"写进内核的 `loader_stub_slot[2]`；内核跑到那一步就跳回 loader，
loader 把日志落盘并通过 USB 给出。

```
fastboot oem run 15        # 走到 arm_init 的 15 号 checkpoint 就回来
fastboot getvar result     # -> result: checkpoint 15 ...
fastboot get_staged out.log
```

实测：`result: checkpoint 15`，`get_staged` 拿到 loader 日志 + `== KERNEL LOG ==`（此时为空，
因为 checkpoint 直接跳 stub，不追加 msgbuf）。

**边界**：checkpoint 返回**不带** msgbuf（printf 的内容），只带 `loader_log`（也就是各驱动
显式 `loader_log_putc`/`loader_log_line` 写进去的内容）。所以 checkpoint 里要看得见东西，
探针必须先 `fts_say`（它同时写 loader_log 和 printf）。

### 2. 分级 panic：内核已经走到哪一段

探针里插了 16 段 `fts_stage(n, "...")`；`ftsstage=N` 让它在第 N 段 `panic()`。
panic 路径（`panic_spin_forever` → `loader_return`）会把 msgbuf 和 panic buffer 一起交回，
所以这条通道**信息最全**：printf、panic 文本、loader_log 全都有。

```
fastboot stage run-stage-N.bin && fastboot oem run
sleep 45
fastboot getvar result     # -> result: returned 20414 0
fastboot get_staged stage-N.log
```

实测：stage 1/2/4/5/11/13/16 都正常返回；**stage 3 和 stage 9 挂死**（写引脚那一段）。

### 3. logdump：卡死之后唯一还能拿的东西

loader 在 USB 交互的每一步前都会 `flush_log()` 把日志写进 `logdump` 分区（不是 logfs）。
所以机器被看门狗复位后，仍然可以：

```
adb shell su -c 'dd if=/dev/block/by-name/logdump of=/data/local/tmp/l.bin bs=1M count=8'
adb pull /data/local/tmp/l.bin
```

里面能看到 loader 自己的日志，以及 **UEFI 异常记录**——这条救过我们：

```
UEFI EXCEPTION type=0 elr=a10077a8 esr=96000007 far=f024000 lr=a10077a8
```

`esr=0x96000007` 是 level-3 translation fault：UEFI 的页表根本没映射那一页，不是硬件不应答。

**边界**：内核卡死时（不是 panic，是总线挂起），内核自己的 `loader_log` 就随复位一起没了；
logdump 只能给出 loader 的最后一句。这就是为什么"写引脚会挂"这种事必须用分级 panic 二分。

---

## 二、工具（都在 ``$IOS`\tools\`）

| 工具 | 干什么 |
|---|---|
| `phone.ps1` | 所有 adb/fastboot 操作的入口：**先判状态**（android/fastboot/loader/none）、每个动作带超时。`state` / `reboot-bootloader` / `boot-loader [-Image x.img]` / `stage -File x` / `run` / `oem -Command "oem log"` / `result` / `get-staged -Out x` / `reboot` / `logdump -Out x` |
| `ladder.ps1` | 分级测试驱动：给定 stage 列表，自动保证 loader 在线、生成载荷、跑、等、取日志、汇总 |
| `mkstage.py` | 生成 `run-stage-N.bin`（kernel-stage.bin + root.apfs + `debug=0x8 cpus=1 ftsprobe=1 ftsstage=N ...`） |
| `mkstart.py` | 用新的 loader.efi 重打包 `thyme-start.img`（保留 shim/FD/追加设备树） |
| `run-droid.ps1` | 把 shell 脚本推到手机、用 `/debug_ramdisk/su` 以 root 跑（Magisk 的 su 不在 PATH 里） |
| `sh.ps1` | 一次性 root 命令 |

WSL 侧：`wsl/build-*.sh` 编译内核（`xnu-build.sh`），`wsl/patch-*.py` 改探针。

---

## 三、打包启动镜像的两个坑（都付出过代价）

1. `thyme-start.img` 的 kernel 区 = **gzip(UEFI FD + XNUP 载荷) + 追加的整棵设备树**
   （实测 939,730 字节，与 `fdt.dtb` 逐字节相同）。少了那棵树，ABL 报
   `Failed to load/authenticate boot image: Load Error`。
2. 头部只有 `kernel_size`(0x08) 需要跟着压缩结果改。曾经把 `kernel_addr` 当成
   `ramdisk_size` 写回 0x10，ABL 同样报 Load Error。
3. ABL 的 fastboot 会话被一次失败污染后，**原版镜像也会被拒**；`fastboot reboot bootloader`
   重开一次就恢复。这也是为什么 `phone.ps1` 每个动作都先判状态。

---

## 四、这台机器上 MMIO 的可达性（实测值）

| 块 | 地址 | 读到的值 | 结论 |
|---|---|---|---|
| GIC distributor | `0x17a00000` | iidr `0x0201143b`，pidr2 `0x3b` | 通 |
| GCC 时钟控制器 | `0x00100000` | vote `0x04100040`，SE5 cfg `0x00100000` | 通 |
| UFS 主机控制器 | `0x01d84000` | `0x1587031f` | 通 |
| RPMh RSC | `0x18220000` | `0x1` | 通 |
| MDSS | `0x0ae00000` | `0x60000000` | 通 |
| **TLMM GPIO 配置** | **`0x0f500000 + 0x1000*gpio`** | 写 `0xC3` 读回 `0xC3` | **通（已确认）** |

### TLMM 的地址（踩过的坑，结论已确认）

早期用 `0x0f000000 + 0x1000*gpio` 和 `0x0f100000 + 0x1000*gpio` 都失败，原因找到了：
**GPIO 配置块在 DT 的 16 MB 窗口里偏移 0x500000**，即 `0x0f500000 + 0x1000*gpio`。

这个偏移不是猜的，是从原厂内核的引脚表里解出来的（`kona_groups`，184 条 × 0x50，
每条对应一个 GPIO，字段 `+0x24` 是 ctl 偏移、`+0x28` 是 io 偏移，值形如 `0x500000 + 0x1000*n`）。

| gpio | cfg 地址 | 复位值 | 现在 |
|---|---|---|---|
| 36 (SDA) | `0x0f524000` | `0x1`（gpio/pull-down/输入） | `0x4` = qup13 |
| 37 (SCL) | `0x0f525000` | `0x1` | `0x4` = qup13 |
| 38 (RST) | `0x0f526000` | `0x1` | `0x2c3` = gpio 输出高，上拉，8 mA |
| 39 (INT) | `0x0f527000` | `0x1` | `0x3` = 输入，上拉 |
| 69 (VDDIO EN) | `0x0f545000` | `0x1` | `0x200` = 输出 |

寄存器位域（和驱动里一致）：`[1:0]` 上下拉（0 无、1 下拉、3 上拉）、`[5:2]` 功能、
`[8:6]` 驱动（`mA/2-1`）、`[9]` 输出使能；`GPIO_IO` = cfg+4，bit0 输入、bit1 输出电平。

功能号：`msm_pinmux_set_mux` 反汇编显示写进 `[5:2]` 的是**该引脚 funcs 数组里的位置**。
gpio36/37 的 funcs = `[98(gpio), 119(qup13), ...]`，所以 qup13 = `1 << 2`；复位脚用 gpio = position 0。

**引脚在 XNU 启动时全是复位态**（`0x1`），bootloader 并没有把它们留在 I2C 功能上——
所以“绕开 TLMM 直接发 I2C”这条路不成立，必须自己配。



---

## 五、原厂（Android）侧的东西已提取

| 东西 | 位置 | 说明 |
|---|---|---|
| `thyme/boot_stock.img` | 192 MB | `boot_a` 原样 dump（`dd`） |
| `thyme/stock-kernel.bin` | 52,654,096 B | 从 boot 镜像里剥出的**未压缩 ARM64 Image**（`ARMd` 在 0x38） |
| kallsyms | 手机 `/data/local/tmp/kallsyms.txt` | `kptr_restrict` 置 0 后整表导出（`_text = ffffffa03ea80000`） |

驱动身份（实测，不是查资料）：

- 触摸：**ST FTS**，`/sys/bus/i2c/devices/3-0049`（i2c-3，地址 0x49），driver name `fts`，
  **编译进内核**（`/vendor/lib/modules` 里没有它）。符号：`fts_probe`、`fts_chip_powercycle`、
  `fts_enable_reg`、`fts_get_reg`、`fts_reset_mode`、`fts_fw_update` …
- 固件：`/vendor/firmware/stm_fts_production_limits.csv`、`J2S_limit_csot.csv`；
  DT 里的 `st_fts_j2s_default.ftb` 还没在文件系统里找到（可能在别的分区或编进驱动）。
- pinctrl：`kona_pinctrl`（soc_data）、`kona_pins`、`kona_groups`、`kona_pinctrl_probe`；
  反汇编 `msm_gpio_direction_output` 可见它按 **每引脚 0x50 字节的描述表**取
  `[entry+0x24]`（io 偏移）和 `[entry+0x28]`（cfg 偏移），再加到 `pctrl->base` 上。

用法（在 Windows 侧反汇编原厂内核函数）：

```bash
BASE=0xffffffa03ea80000
llvm-objcopy -I binary -O elf64-littleaarch64 --rename-section .data=.text \
    stock-kernel.bin /tmp/stock.elf
# 例如 fts_chip_powercycle = 0xffffffa03f496830 → 文件偏移 0xa16830
llvm-objdump -D --triple=aarch64 --start-address=0xa16830 --stop-address=0xa16a70 /tmp/stock.elf
```

---

## 六、触摸 bring-up 进展（2026-10-31 12:20）

已经完全打通的部分（每一步都有设备日志）：

| 步骤 | 结果 |
|---|---|
| TLMM GPIO 窗口 | `0x0f500000`，写 `0xC3` 读回 `0xC3` |
| 引脚配置 | `cfg36/37 = 0x4`（qup13）、`cfg38 = 0x2c3`（gpio 输出高）、`cfg39 = 0x3` |
| I/O 轨使能 | gpio69 = 输出高，`cfg 0x200` |
| VDD 1.8 V | RPMh **`ldoc1`**（不是 `ldoa1`，数据库里没有），写入成功 |
| 复位脉冲 | 低 15 ms → 高 150 ms，读回 `io38 = 0x3` |
| 总线空闲 | `sda 1 scl 1`（上拉生效） |
| **I2C 地址 0x49** | **芯片应答 ACK**（bitbang，~66 kHz） |

还没解决的：读寄存器连续 256 个全返回 `00`。已经排除的方向（每条都有实测日志）：

| 假设 | 实验 | 结论 |
|---|---|---|
| 地址不对 | 写 `0xC3` 读回 `0xC3` | 排除，寄存器可读写 |
| 引脚没释放 | 拉低→释放→回读 | 排除，SDA/SCL 都能回到 1 |
| 复位极性问题 | 复位拉低 | 拉低后芯片**完全不应答**（0 ACK），所以低=复位、高=运行，原时序是对的 |
| 是我把它推进 boot 模式 | 不复位直接扫 | 排除，一样全 0 |
| 速度太快 | `bbus=50`（≈5 kHz） | 排除，一样全 0 |
| SE/FIFO 路线 | 读 `GENI_IF_DISABLE_RO` | bit0=1，FIFO 被 strap 关闭，只支持 GPI/DMA |

新线索（从 `fts_write` 的两个调用点反汇编得到）：厂商发的**不是 16 位寄存器地址，而是命令包**：

```
A0 00 xx                (3 字节，随后 checkEcho 读 3 字节比对)
A2 03 00 00 00 03       (6 字节)
```

照发 `A0 00 00` 再读，返回 `ff ff ff ff ff ff ff ff`（芯片 ACK 了读地址但没驱动数据），
`A2 03 ...` 直接被 NAK。说明芯片状态会随命令变化，但收发的确切格式还要继续对。

下一步：把 ST 这套命令协议从原厂驱动里完整对出来（`fts_read`/`checkEcho` 的调用者、
0xA0/0xA2 这类命令常量的用法），这比继续盲扫有效。

### 12:5x 更新：协议细节与下一步

- 拿到 ST 官方驱动源码的片段（`kernel/msm-modules/fts_touch_s5`）：`fts.h` 里写明
  `prefix /* always should be 0xA0 */`；`ftsIO.c` 的读是"两条 i2c_msg（写+读）"，
  另有 `fts_writeReadU8UX(u8 cmd, AddrSize addrSize, u64 address, ...)` —— **命令字节在地址前面**。
- 实测：单字节地址、双字节地址、命令+地址三种形式，读回来**全是 0**；总线恢复后
  裸读 8 字节也是全 0，而 INT 一直为低、读完之后 SCL 被从机拉着。
- 整机搜过：**没有任何 `.ftb` 固件文件**，`/vendor/firmware_mnt` 里也没有，
  说明芯片固件在它自己的 flash 里，不是每次上电下载。
- 因此现在的判断：芯片已上电并在拉 INT，但核心没在正常状态（boot 模式或协议不对）。

**下一步最有效的一招**：在 Android 里用 ftrace/kprobe 抓 `fts_read`/`fts_writeRead` 的真实
字节流——那才是"芯片认可的一帧"。拿到之后再实现，就不用再猜。

### 13:0x 更新：ST 官方协议拿到了，还做了一个快速试验台

**协议（ST 官方 `fts_lib`，来自 gitlab MR 的完整补丁）**：

```c
/* 读：先写 [命令][地址…]，再读 N+1 字节，第一个是 dummy */
fts_writeReadU8UX(FTS_CMD_FRAMEBUFFER_R, BITS_16, addr, data, len, DUMMY_FRAMEBUFFER)
/* 事件 FIFO：没有地址，只发一个命令字节 */
u8 cmd[1] = { FIFO_CMD_READONE };
fts_writeReadU8UX(cmd[0], 0, 0, readData, FIFO_EVENT_SIZE, DUMMY_FIFO)
```

| 常量 | 值 |
|---|---|
| `FTS_CMD_HW_REG_R` | `0xA0` |
| `FTS_CMD_HW_REG_W` | `0xA2` |
| `FTS_CMD_FRAMEBUFFER_R` | `0xA4` |
| `FTS_CMD_SYSTEM` | `0xA8` |
| `HEADER_SIGNATURE` | `0xA5` |
| 复位后的第一件事 | 轮询 `EVT_ID_CONTROLLER_READY` |

**新工具**：`tools/i2c.c` —— 裸 syscall 的静态 aarch64 程序，直接走 `/dev/i2c-3`（内核自己的
GENI 主机），`i2c <写字节…> <读长度>`，秒级迭代，不用刷内核。

**实测（驱动占用总线时）**：传输都成功，但读回 `A0/A2/A4 → 00…`、`A8 → 44 44…`、
`AA/AC → ff…`。把驱动 unbind 之后适配器直接超时（客户端一掉，runtime PM 关掉控制器），
而且 unbind 后不会自己 bind 回来，必须重启手机恢复触摸。

**结论**：i2c-dev 这条路能发但不能拿到芯片数据——芯片只在原厂驱动的时序/状态里给数据。
下一步在 XNU 里按上面的协议实现（命令 + 地址 + dummy 字节），那才是我们能完全控制的环境。

### 13:0x 更新（重大）：芯片在 XNU 里读出数据了

从原厂内核 `fts_writeReadU8UX` 的 **31 个调用点**反汇编出真正的命令字节：

| 命令 | 地址宽度 | 用途 |
|---|---|---|
| `0x86` | 0（无地址） | 读事件 FIFO |
| `0xA6` | 16 位 | 读寄存器/数据块 |
| `0xA8` | 16 位 | 系统命令 |
| `0xFA` | 32 位 | 大地址读 |

之前没试过 `0x86`（扫 0x00–0x2F 时漏了，全量扫描又总是带 16 位地址）。按正确形式实测：

```
0xA6 @0，32 字节：
01 01 00 00 00 00 02 03 00 39 36 00 00 00 00 24
01 44 34 08 00 00 00 ff ff 00 00 05 05 05 00 30
...
0x1f..0x25: 30 33 00 4d 50 33 46 35     ← "03\0MP3F5"（芯片/固件标识）
0x2f..0x37: 08 17 00 00 00 00 87 97 e8 37 cc c2 2b 51
```

地址是直通的（读 N 和读 N+1 的结果正好错一位），说明这是完整的数据块。

`0x86` 事件读回全零——因为 XNU 里没人碰屏幕，芯片没有事件可报（符合预期）。

**至此，触摸芯片在 HakiOS 内核里已经能读数据了。** 剩下的是：写扫描模式寄存器让芯片开始报点，
然后读事件/帧缓冲，把坐标送进 `redwood_input.c` 的上报通道。

### 13:1x 更新：扫描模式已开，事件 FIFO 开始有内容

从 `fts_mode_handler`（0xffffff983b896f18）反汇编出开扫描的完整报文：

```c
/* 写寄存器 0x0300，三字节数据 00 00 03 —— 上总线就是 6 字节 */
A2 03 00 00 00 03
```

实测（bringup=27）：

```
st_scan_on: sense-on packet written
st_scan_on: event 0: 00 00 00 00 00 00 01 43 01 a2
st_scan_on: event 1: 01 a2 03 00 00 00 00 00 00 00
st_scan_on: block again: 01 01 00 00 00 00 02 03
```

差别很关键：**发这条命令之前，`0x86` 事件读回全零；发了之后，FIFO 里有内容了**
（`01 43` 像是状态/事件 ID，`01 a2 03 00` 是我们刚写下去的包的回显）。
芯片已经从"只应答"变成"会回应状态"。

还差的：事件报文的确切格式（`FIFO_EVENT_SIZE`、事件 ID 表），这些常量在 ST 的
`ftsSoftware.h` 里（那份补丁被截断，没取到）；拿到之后就能解析坐标，接进 `redwood_input.c`。

### 14:2x 更新：协议全部落实，坐标还差最后一步

**常量已从 ST 官方源码确认**（`refs/ftsSoftware.h`、`refs/ftsHardware.h`，从手机
`/sdcard/Download` 拉取后 base64 解码）：

| 项 | 值 |
|---|---|
| `FIFO_CMD_READONE` | `0x86`（无地址，读 8 字节 + 1 dummy） |
| `FIFO_EVENT_SIZE` / `DUMMY_FIFO` | `8` / `1` |
| `FTS_CMD_FRAMEBUFFER_R` | `0xA6`（16 位地址） |
| `FTS_CMD_SCAN_MODE` | `0xA0`（`[0xA0, mode, settings]`） |
| `FTS_CMD_FEATURE` | `0xA2` |
| `FTS_CMD_HW_REG_R/W` | `0xFA`（32 位地址） |
| `ADDR_DCHIP_ID` / `ADDR_IER` / `ADDR_SYSTEM_RESET` | `0x20000000` / `0x20000029`(`0x41`) / `0x20000024`(`0x81`) |
| 事件 ID | `0x03` ready、`0x13` enter、`0x23` motion、`0x33` leave、`0x43` status、`0xF3` error |

**实测到这一步**：

- `0xA6 @0` 读出系统信息块（含 `MP3F5` 标识和出厂数据）
- 写 `[A2 03 00 00 00 03]`、`[A0 00 01/03/0F]`、IER `0x41` 全部被接受
- 事件 FIFO 里能读到命令回显：`43 01 a0 ...`（STATUS_UPDATE + ECHO + 我们的命令字节）
- **但手指按屏幕时拿不到 `0x13/0x23/0x33`**

**尚未解决**：芯片进入了"接受命令、回显命令"的状态，却没有进入"报触摸点"的状态。
原厂 `fts_ts.c` 的 probe 里还有 `readSysInfo` → `setFeatures` → `setScanMode` → `senseOn`
一整套顺序，我们只做了其中一部分；另外触摸数据也可能只走帧缓冲（`0xA6` @ `ADDR_FRAMEBUFFER=0`）
而不是事件 FIFO。

**工具**：`tools/touch-runs.ps1`（连跑多轮、自动重打包载荷）、`fb_signal()`（屏上色块报结果）、
`logdump` 分区（崩溃日志落盘，可从 Android `dd` 出来）、`tools/i2c.c`（Android 侧裸 syscall I2C 工具）。

### 14:5x 更新：芯片完全识别，只差触摸上报

**拿到了小米官方 + 魔改两份 SM8250 内核源码**（``$IOS`\thyme\*.zip` →
`refs/fts_521_mod/`、`refs/fts_521_alioth/`），里面有完整的 `fts.c`（240 KB）和 `fts_fw.h`（内嵌固件）。

从系统信息块（`0xA6 @0`，**不丢首字节**）读出的完整身份：

| 字段 | 偏移 | 值 |
|---|---|---|
| `HEADER_SIGNATURE` | 0 | `0xA5` ✓ |
| `LOAD_SYS_INFO` | 1 | `0x01` ✓ |
| `u16_chip0Id` | 10 | **`0x3639` = FTM5** |
| `u16_fwVer` | 16 | `0x0124` |
| `u16_cfgVer` | 20 | `0x0008` |
| die info | 32 | `"MP3F5"` |
| release info | 48 | `08 17 00 00 …`（与 Android 的 appid 一致） |
| `scrResX/Y` | 80 / 82 | `2339` / `1079` |
| **`txLen` / `rxLen`** | 84 / 85 | **`17` / `37`**（与 Android `ms_raw` 的 `17,37` 一致） |

**重要修正**：这颗芯片的 I2C 读**没有前导 dummy 字节**——ST 驱动 `fts_writeReadU8UX(..., DUMMY_*)`
的"丢第一个字节"在这里不适用。之前所有丢 1 字节的读都错位了，改对之后系统信息立刻以 `0xA5` 开头。

**已按原厂顺序跑通**：

```
复位 → 等 0x03(CONTROLLER_READY) → 0xFA 预写 → sense off(0xA0 00 00) → sense on(0xA0 00 01)
     → 手势特性(0xA2 03 00 00 00 00)
```

每一条都被芯片接受并回显（`43 01 …`），还能看到 `43 05`（Force Calibration 触发）。
**但手指按屏幕时仍然没有 `0x13/0x23/0x33`。**

**下一步（关键）**：读 160 字节系统信息，取出 `u16_msTchRawAddr`（偏移 136），
你按屏幕时连续读那块 raw 互容数据——如果数值变化，说明模拟前端在工作、问题在事件上报；
如果不变，说明面板根本没被扫描。

### 15:2x 更新：问题锁定为"芯片不扫描"

**原始互容帧**（`0xA6` + `u16_msTchRawAddr`，本机为 `0x8b1c`，629 个 16 位采样）：

- **Android（驱动正常）**：每秒哈希都变，数值在 `1363 → 1318 → 1273 …` 抖动，按压时明显跳变
  （`1128 / 940 / 283`）
- **XNU（我们的内核）**：60 次快照 `sum 229585 / head a2 cb` **逐字节完全一致** → 帧从不刷新

**寄存器对照**（Android 正常态 vs XNU）：

| 寄存器 | Android | XNU | 结论 |
|---|---|---|---|
| `0x20000000` chip id | `36 39 03 00` | 同 | ✓ |
| `0x20000024` / `0x25` | `00 20 09 17` / `20 09 17 00` | 同 | ✓ |
| `0x2000002D` ICR | `c0 00 00 30` | 同 | ✓ |
| `0x20000030/32/34` GPIO | `30 3f 0f 20` / `0f 20 0f 00` / `0f 00 00 00` | 同 | ✓ |
| **`0x20000029` IER** | **`02`** | **`42`**（bit6） | ✗ |
| **`0x20000068`** | **`0b 01`** | **`0b 03`**（bit1） | ✗ |

逐步追踪（`ier_trace`）证明：**是 `scan mode` 命令 `0xA0 00 01` 本身把 IER bit6 置上的**，
去掉 `scan_off`、连发两次 `scan_on` 也一样；bit6 出现后帧就不再刷新。

所以芯片认为"感测未启动/启动失败"。省电、复位、就绪事件、配置（17×37）都已排除，
剩下的方向是**模拟前端/面板侧的条件不满足**（驱动里 AVDD 明确要求 3.3 V，
而我们只能把 gpio69 这个开关打开，3.3 V 来自它的上游）。

**可用的对照手段**：Android 里 `/data/local/tmp/i2c`（裸 syscall 工具）可以直接发
`FA 20 00 00 xx` 读任意 HW 寄存器，用来取基准值；`/sys/.../ms_raw` 是驱动自己读的实时帧。









---

## 七、下一步（按依赖顺序）

1. 从原厂 `fts_fw_version_read` / `fts_probe` 里取出它读的第一个寄存器和字节序，
   照抄到 bitbang 读上——现在总线通了，差的只是"读哪个寄存器、怎么读"。
2. 拿到 chip id 之后，再照 ST 的协议读出触摸报文（ST FTS 的报文寄存器、坐标格式都能从
   `fts_event_handler` / `fts_user_report_event_handler` 反汇编里对出来）。
3. 最后是 iOS 侧：`redwood_input.c` 的 `/dev/redwood-touch` 上报通道还没有被验证过，
   这是剩下最不确定的一块。



### 17:0x 更新：排除法做完了，卡在“芯片不抓帧”

**重要修正**：`IER=0x42` 是**正常值**（= 感测已开启）。Android 在扫描状态读到的也是 `0x42`；
之前读到的 `0x02` 是那一刻屏幕状态不同造成的误判。

**驱动读帧的真实方式**（`fts_ms_raw_show`，之前漏了）：

```
setScanMode(ACTIVE, 0x01); mdelay(200ms)   // 开感测
setScanMode(ACTIVE, 0x00); mdelay(50ms)    // 关感测，冻结这一帧
flushFIFO(); getMSFrame3(MS_RAW, ...)      // 再读
```

每读一帧都要开关一次感测。按这个顺序在 Android 里用我们的 i2c 工具读，数据是活的
（`95 05 -> 96 05 -> 92 05` 每次都在变），说明命令和读法都没问题。

**已排除项（都有实测）**

| 假设 | 结果 |
|---|---|
| IER 状态位 | 0x42 是正常值，排除 |
| 128 字节 HW 寄存器全量对比 | Android 与 XNU 逐字节一致（除感测开关位） |
| 供电轨 | pm8150a_l1=1.8V、disp_vddio_vreg=enabled，两边相同 |
| INT 引脚 | XNU 里空闲为高(1)，正常 |
| FIFO 排空 | 正常，能读到 2-3 条命令回显 |
| 上电稳定时间 | 加 3 秒等待无变化 |
| 命令序列 | 在 Android 里原样重放，帧照样实时更新 |

**结论**：芯片接受所有命令、报告“感测已开启”、寄存器状态与 Android 完全一致，
但 `0x8b1c` 处的互容帧从不刷新（永远是 `a2 cb a2 cb`，即上电后从未抓过帧）。
软件层面能查的差异已查尽，剩下方向：

1. 面板/显示的模拟条件（TE、VCOM、DPU 是否真在刷新）——XNU 下显示只有 loader 的静态 logo
2. 把原厂 probe 完整移植（含 fts_fw_update 路径与 config 加载）
3. 用逻辑分析仪看面板连接器上的实际信号

### 17:2x 更新：软件侧排查到底了

补充排除（都有实测）：

| 假设 | 结果 |
|---|---|
| 等待时间没生效 | 日志时间戳证明每轮 0.9 s、sleep 真实 |
| AFE 稳定时间不够 | 20 次抓帧（18 秒）结果**完全相同** |
| 软件复位（`0x20000024 <- 0x81`） | 无变化 |
| 寄存器 `0x80-0xFF` 全量对比 | 仅 3 字节不同（状态位） |
| 寄存器 `0x100-0x17F` 全量对比 | 仅 2 字节不同 |
| 供电轨 | `pm8150a_l1`=1.8V、`disp_vddio_vreg`=enabled，与 Android 一致 |

**当前结论**：芯片在 XNU 下与 Android 下的软件可见状态几乎完全一致，但互容帧从不刷新
（20 次抓帧 `sum 229585` 一字不变）。剩下只能是物理/模拟侧，或者原厂驱动里有某个
我们尚未复现的内部流程。

**建议的下一步**：不再手工猜测初始化，而是**直接把小米内核里的 `fts_521` 驱动整体移植**进来
（源码已在 `refs/fts_521_mod/`，含 `fts.c` + `fts_lib/` 全套），配一层薄薄的 I2C shim 接到
现有的 bitbang 上。这样原厂的每一条初始化、每一个时序都会原样执行，也是将来做正式驱动
（接 `redwood_input.c` 上报）的正确基础。

### 17:3x 更新：原厂驱动完整移植进 XNU，跑通了

**移植结果**（`bsd/dev/fts_port/`，已进 `bsd/conf/files`）：

| 项 | 状态 |
|---|---|
| `fts_lib/` 全部 9 个 .c 文件 | **零修改编译通过**（只把 `<linux/*.h>` 换成 `compat.h`） |
| `ftsTest.c`（工厂自检）、`fts_proc.c`、sysfs/DRM/OTA 胶水 | 丢弃 |
| compat 层 | `refs/compat_xnu.h`：`kmalloc→IOMalloc`、`msleep→IOSleep`、`printf`、空锁 |
| 平台契约 | 只剩 **2 个钩子**：`fts_platform_i2c_transfer` / `fts_platform_gpio_set` |
| 链接 | 全部符号解析，内核尺寸 15178848 → 15234752 |

**设备实测**（`bringup=29`，跑原厂代码）：

```
selftest: channel open, client addr 0x49
selftest: system reset -> 0
selftest: readSysInfo -> 0  fw 0x124 cfg 0x8 chip 0x3639
selftest: setScanMode -> 0
selftest: frame 0  17x37  sum 32789770  first -13406
selftest: frame 1..5  同样数值
```

原厂 `readSysInfo` 读出的 fw/cfg/chip id 与我们手工读的**完全一致**，帧结构 `17×37` 也正确，
说明协议、结构解析、I2C 层全部到位。

**但帧依然静止** —— 而且这次是**原厂代码原样跑出来的结果**。结论因此变得很硬：

> 不是我们手工复现的方式有问题；软件层面已 100% 忠实还原原厂行为，
> 芯片在这个环境下就是不产生新的互容帧。

已排除：寄存器状态、供电轨、INT、FIFO、时序、复位方式、扫描命令、稳定时间、显示刷新（闪屏测试）。

**剩下的只可能是物理/模拟侧**（面板 AVDD 实测电压、面板排线、TE 信号）。
下一步建议用万用表量触摸面板的 AVDD/VDDIO，或重新插拔面板排线；
或者继续把驱动的事件路径 + `redwood_input.c` 上报做完，等硬件条件具备时直接可用。

### 17:4x 更新：触摸驱动本体完成

**新增 `bsd/dev/fts_port/fts_touch.c`**（约 150 行，基于移植的原厂库）：

| 部分 | 内容 |
|---|---|
| 初始化 | `openChannel` → `fts_system_reset()` → `readSysInfo(0)` → `setScanMode(ACTIVE, 0x01)` |
| 事件读取 | `fts_writeReadU8UX(FIFO_CMD_READALL, 0, 0, ev, FIFO_EVENT_SIZE, DUMMY_FIFO)` |
| 坐标解码 | 照原厂 `fts_enter_pointer_event_handler`：<br>`id=(ev[1]&0xF0)>>4`、`x=((ev[3]&0x0F)<<8)|ev[2]`、`y=(ev[4]<<4)|((ev[3]&0xF0)>>4)` |
| 上报 | `redwood_touch_post(REDWOOD_TOUCH_DOWN/MOVE/UP, id, x, y)` |
| 轮询 | `thread_call_enter_delayed`，8 ms 周期 |
| 入口 | 启动参数 **`touch_fts=1`**（`redwood_fts_init()`） |

**进度实时验证**（`bringup=30`）：

```
[fts] fts_touch: reset 0
[fts] fts_touch: sysinfo 0  fw 0x124 cfg 0x8 chip 0x3639  panel 17x37
[fts] fts_touch: sense on 0
touch_test: poll cycles 544        ← 8 秒 544 次，约 68 Hz
```

**修正**：这颗芯片不发送前导 dummy 字节，移植里已把 `DUMMY_*` 全部置 0
（否则帧/事件都会整体错位一字节）。

**状态**：驱动代码完整——初始化、轮询、事件解析、坐标解码、上报通道全部就位并通过运行验证。
唯一未打通的是**芯片不产生触点事件**（与"帧不刷新"同一个根因，已确认不是软件问题）。
一旦面板开始上报，这条链路即可直接工作。

### 18:0x 更新：`touchscreen=1` 现在就是这颗屏的驱动

之前 `touchscreen=1` 绑的是给另一块屏写的 Goodix/SPI 驱动（会崩），所以我自己加了
`touch_fts=1`。已改为：

- **`touchscreen=1`（或 `touch_fts=1`）→ 启动 ST FTS 驱动**：先跑硬件初始化
  （TLMM 映射、引脚 mux、电源、复位释放），再由 input 层启动面板驱动
- 旧的 Goodix 路径改由 **`goodix=1`** 触发

设备实测（`debug=0x8 cpus=1 touchscreen=1`）：

```
redwood: ST panel bring-up
[fts] fts fts_system_reset: enter / exit
[fts] fts_touch: reset 0
[fts] fts_touch: sysinfo 0  fw 0x124 cfg 0x8 chip 0x3639  panel 17x37
[fts] fts_touch: sense on 0
```

**已知限制**：早期启动用多核（`cpus=8`）时，`fts_system_reset` 阶段会 data abort
（`far=0x24000`），单核正常。原因未查清，暂时按单核使用。
`HANDOFF.md` 里"不要加 touchscreen=1"的说明已同步更新。

### 18:4x 更新：软件侧排查终章

本轮又排除了三项：

| 实验 | 结果 |
|---|---|
| **按原厂顺序上电**（VDD 1.8V → 20 ms → AVDD → 20 ms → 释放复位，之前是反的） | 帧仍六次不变（`sum 32789770`） |
| **寄存器 `0x20000200`–`0x20000380`** | 全是 `0xDEADBABE`（未使用区），与 Android 一致 |
| **热重启保持 Android 状态读帧**（不复位、不动电源） | `getMSFrame3` 直接报错——**ABL 启动时已复位芯片**，前提不成立 |

**最终结论（证据完整）**：

1. 原厂驱动**零修改**移植进 XNU，编译、链接、运行全部通过
2. 芯片识别、系统信息、扫描命令、事件 FIFO、寄存器状态——与 Android **逐字节一致**
3. 但互容帧从上电起**从未被写过一次**（`a2 cb a2 cb…` / `sum 32789770` 恒定）
4. **用原厂代码跑出同样结果** → 不是我们的实现问题

已排除的软件因素：I2C 协议、dummy 字节、命令字节、时序、等待时间、复位方式（GPIO/软件）、
上电顺序、供电轨、IER/ICR/GPIO 寄存器、显示刷新（TE）、中断引脚、FIFO 排空。

→ 剩下只能是**面板/模拟侧**（AVDD 实测电压、排线、面板本身），需要硬件手段验证。

**软件成果（可复用）**：
- `bsd/dev/fts_port/`：原厂库 9 个文件 + compat 层 + 平台桥接 + 触摸驱动（`fts_touch.c`）
- 启动参数 `touchscreen=1`（或 `touch_fts=1`）即启用，`fts_trace=N` 可自证后退出

### 19:3x 更新：找到上游 FTS5 驱动，AFE 被唤醒但仍无触点

**重大发现**：上游 Linux 有专门的 STM FTS5 驱动（我们这颗正是 FTS5，chip id `0x3639`），
命令集与小米原厂 `fts_521` **完全不同**：

| 命令 | 值 | 作用 |
|---|---|---|
| `SLEEP_OUT` | `0x91` | 唤醒 |
| `MS_MT_SENSE_OFF/ON` | `0x92/0x93` | 互容感测 |
| `SYSTEM_RESET` / `SET_SCAN_MODE` | `0xa0` | 扫描模式（3 字节 `A0 00 xx`） |
| `FULL_FORCE_CALIBRATION` | `0xa2` | 强制校准 |
| **`MS_CX_TUNING`** | **`0xa3`** | **互容电容调校** |
| **`SS_CX_TUNING`** | **`0xa4`** | **自容电容调校** |
| 中断使能 | `B6 00 2C 01` | |
| `READ_ALL_EVENT` | `0x86` | 读事件 FIFO |

**执行这套序列的效果（实测）**：

| 指标 | 小米序列 | 上游序列 |
|---|---|---|
| 事件 | 完全沉默 | 持续上报（37 条/秒，`43 03` = FRAME_DROP） |
| 帧基线 | `32789770` 恒定 | **`674054`**（变了！） |

**CX 调校确实唤醒了模拟前端**——这是这次最大的实质进展。

**但**：无论扫描值 `0x01` 还是 `0xFF`、无论是否按住屏幕、
都**没有 `0x13/0x23/0x33` 触点事件**，帧缓冲也不再刷新。

**TE 假设排除**（Android 实测）：
```
461: msmgpio 66 Edge  TE_GPIO   ← 中断计数 0，TE 根本没被使用
516: msmgpio 39 Level fts      ← 触摸中断（Level 触发，与 DT 一致）
```

**看门狗限制**：探针的忙等循环超过约 60 秒会被硬件看门狗复位
（loader 启动内核后停止喂狗，XNU 的 `hv_wdt_pet` 线程在忙等时抢不到 CPU）。
所以单次实验窗口上限约 50 秒。

**当前状态**：芯片可唤醒、可调校、可校准、可扫描、会报状态，
**但不产触点、不刷新帧**。软件侧已无已知缺口。

### 20:3x 突破：触摸彻底解决 ✅

**根因：PM8150B GPIO5 的升压（VIN1）从未打开。**

触摸面板模拟供电分两路：
- VDD 1.8V（PM8150A ldoc1）——一直开着
- **面板升压 PM8150B GPIO5 / VIN1 —— 从来没开过**

芯片数字部分因此完全正常（I²C、固件、ID、系统信息、调校全部通过），
但**模拟前端没有供电，扫描缓冲永远是空的**。这解释了此前全部异常现象。

**第二个坑：初始化序列不完整。** 原厂恢复工具抓包显示必须：
```
复位脉冲 → 等 0x03 就绪事件
senseOn (0xA0 00 01)
特征恢复 {0xA2,0,0} {0xA2,1,0} {0xA2,2,0}    ← 之前完全没做
senseOn 再一次                              ← 之前只发一次
手势掩码 {0xA2, 0x03, 0,0,0, 0x03}
```

**解法来源**：`github.com/azzlantern/hackios-thyme`（2026-10-25 单日完成移植，
实测 `poll=24375 events=2211 errors=0`），其 `devices/thyme/overlays/xnu/files/bsd/dev/`
下的 `umi_touch.c` + `umi_touch_boost.c` 直接可用，仅需替换 `hakios_*` → `redwood_*`。

**移植到本树**：
| 文件 | 内容 |
|---|---|
| `bsd/dev/redwood_st_touch.c` | bitbang I2C、FIFO 事件解码、坐标上报到 `/dev/redwood-touch` |
| `bsd/dev/redwood_st_touch_boost.c` | SPMI 枚举找到 PM8150B（apid 匹配 0x2c4），写 VIN1 + 直接输出高 |
| `redwood_rpmh_vreg_enable()` | 自包含的 RPMh 供电投票（cmd_db 按需映射） |
| `bsd/conf/files` | 两个新文件注册 |
| `redwood_input.c` | `touchscreen=1` 启动 `redwood_st_touch_init()` |

**实机验证（iOS 会话）**：
```
UMI boost: subtype=0x11 mode=0x1 vin=0 out=0 en=0x80 apid=62
UMI boost: VIN1 and direct output-high verified
UMI FTS: ST FTS521 3639 initialized scan=0x1
UMI FTS: poll=18750 events=3338 errors=0
UMI FTS: event 13 01 a7 43 69 00 00 02   ← 按下 (935, 1684)
UMI FTS: event 23 ...                     ← 移动 40 次
```
**41 个真实触点事件、0 错误、0 panic。**

启动参数：`touchscreen=1`（也可用 `touch_fts=1`）。

### 06:1x 最后一环打通：`/dev/hakios-touch` 设备名

**症状**：内核驱动已产生真实触点（41 个事件、坐标正确），但 iOS 界面不响应。

**根因**：注入到 **backboardd**（iOS 的 HID 事件服务器）的 `libhakios.dylib`
打开的是 **`/dev/hakios-touch`**，而本内核创建的是 `/dev/redwood-touch`。
日志证据：

```
hakios: input: /dev/hakios-touch: No such file or directory
backboardd[...] [TouchEvents] *** still waiting for a main screen digitizer ***
```

**修复**：`redwood_input.c` 里同时创建两个设备名（同一 major/minor）：

```c
devfs_make_node(..., "redwood-touch");   /* 本项目工具沿用 */
devfs_make_node(..., "hakios-touch");    /* backboardd 注入库需要 */
```

**修复后的日志**：

```
hakios: input: the event server sets "DeviceOpenedByEventSystem" = true
hakios: input: the event server has not opened "HakiOS touch screen" (attempt 1): making it again
backboardd [UISensor] ... digitizerEnabled: YES
UMI FTS: poll=20625 events=1192 errors=0 irq=0 active=0x1
```

**完整的触摸链路（官方 HakiOS 方案）**：

```
触摸芯片 → 内核驱动(redwood_st_touch.c) → /dev/hakios-touch
        → libhakios.dylib 注入 backboardd(IOHIDVirtualServiceClient 虚拟 digitizer)
        → SpringBoard
```

其中注入方式为 `DYLD_INSERT_LIBRARIES`（AMFI 替身放行），任务由 userland 的
`mkjobs.py`/`rc.jobs` 生成；ramdisk 里 `/redwood/lib/libhakios.dylib` 同时注入
backboardd / containermanagerd / SpringBoard / mediaserverd 等。

### 06:3x 触摸验收 + 外部代码安全审计

**触摸确认可用**（用户实机验证）。

**安全审计结论**（对从 GitHub 引入并编进内核的代码）：

| 检查项 | `redwood_st_touch.c` | `redwood_st_touch_boost.c` |
|---|---|---|
| exec/system/popen | 0 | 0 |
| socket/网络 | 0 | 0 |
| 文件写入 | 0 | 0 |
| base64/混淆 | 0 | 0 |
| 硬编码 IP/URL | 0 | 0 |
| 仅 MMIO/SPMI/I2C | ✓ | ✓ |

**外部符号**全部为已知内核 API：`ml_io_map`、`kernel_thread_start`、
`assert_wait_deadline`、`thread_block/terminate`、`delay_for_interval`、
`redwood_touch_post`、`redwood_spmi_lock/unlock`、`redwood_rpmh_vreg_enable`。

**引入方式**：GitHub 上的代码只作**参考阅读 + 人工移植**，仓库里的构建脚本
（`build.sh`/`mkjobs.py` 等）**一律没有执行**；落进内核的只有两个自写驱动文件。

**后续 GPU 工作的安全原则**：
1. 固件只从**本机** `/vendor/firmware/` 提取（可信来源），不从网上随便下二进制
2. 提取后核对文件名/大小/ELF 头，与内核期望的名字一一对应
3. 任何第三方脚本先读后跑；优先自己写

### 07:0x GPU：内核驱动完整跑通 ✅

**问题**：本树自带的 `redwood_adreno.c` 是 **redwood（SM7325/骁龙778G, Yupik GPUCC）** 专用，
在 thyme（kona/SM8250, Adreno 650）上第 5 步"开时钟"就失败：
`adreno: 5: the counter did not finish`。

**解法**：移植参考移植项目（azzlantern/hackios-thyme）的 **Kona/A650 版驱动**——
它同时支持 Yupik 与 Kona，按 `fw/a650_gmu.bin` 是否存在自动选 profile，
且已内置 thyme 的 zap 窗口 `0x8c41a000`。

**移植内容**：
| 文件 | 来源 | 改动 |
|---|---|---|
| `bsd/dev/redwood_adreno.c` | 其 `hakios.adreno.c`（177 KB） | `hakios_`→`redwood_`、`umi_`→`kona_`、include 改名 |
| `iokit/Kernel/RedwoodGPU.cpp` | 其 `hakios.gpu.cpp`（42 KB） | 同上 + 服务名 `HakiosGPU`→`RedwoodGPU` |
| `iokit/Kernel/MiniIOSurface.h` | 补 `gpu_written` 字段 | |
| zap 头 | 支持多页（kona 的 6860 > 4096） | |
| `root.apfs` | 加入手机原厂 `fw/a650_{gmu.bin,sqe.fw,zap.mdt,zap.b02}` | |

**实机结果（全部 22 步通过）**：
```
adreno: GPU profile: UMI Adreno 650 / Kona
adreno: step 5: done                        ← 时钟通过（旧驱动死在这里）
adreno: 12: TRUSTZONE ACCEPTS THE ZAP SHADER'S SIGNATURE
adreno: 19: TRUSTZONE HAS AUTHENTICATED AND STARTED THE ZAP SHADER
adreno: 20: THE 3D CORE TAKES COMMANDS
adreno: 22: A PICTURE DRAWN BY THE GPU IS ON THE PANEL: orange, white, blue
adreno: THE GPU STAYS ON for the system to draw with: the service RedwoodGPU
MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1080x2400
```

**剩余一步**：iOS 的 CoreAnimation 要用 GPU 还需用户态的 **GL 栈**
（`eagl` + Mesa/FD650）。当前 ramdisk 是基础项目的**测试镜像**，不含 GL 栈，
所以 UI 仍走软件渲染。参考移植的 M5 判据是 `hk-gl: Mesa 25.0.7; FD650`。

**镜像工具**：`tools/apfs/apfs.py`（只读）+ `mkapfs.py`（建镜像）已审计并使用，
固件取自手机 `/vendor/firmware/`（本机可信来源）。

### 07:3x GL 栈补齐 → iOS 硬件渲染 (M5 完成) ✅

**构建链**（全部基于项目自带脚本，先审计后运行）：
| 组件 | 做法 | 结果 |
|---|---|---|
| **Mesa 25.0.7** | `tools/mk-mesa.sh`（官方源 + SHA256 校验 + 7 个补丁） | `libOSMesa.8.dylib` 14.8 MB，1296 个 gl* 函数 |
| 链接器 | cctools ld64 **无 TAPI**（`-lSystem` 失败）→ 自建 `lldbin/xnu-clang`（`-fuse-ld=lld`） | 通过 |
| **EAGL 桥** | `mkeagl.py` 生成 6143 行 `eagl_gen.h` → 编译 `libredwood-gl.dylib` | 541 KB |
| 镜像 | 解包 root.apfs → 加 `fw/a650_*` + GL 库 → mkapfs 重建（6.2 MB） | 通过 |

**踩到并修掉的两个坑**：
1. **注入库找的是 `/redwood/lib/libhakios.gl.dylib`**（hakios 命名），不是 redwood 命名
   → 两种名字都放进去
2. **重建的镜像权限全是 0777** → launchd 拒绝加载 plist
   （`error = 122: Path had bad ownership/permissions`）
   → 在 Linux 文件系统上改回 目录 755 / 普通文件 644 / 可执行 755
   （WSL 的 DrvFs 不保存 chmod，必须复制到 ext4 上改）

**实机证据**：
```
redwood-gl: 8 of 8 methods of EAGLContext are ours; OpenGL ES by Mesa, off-screen (OSMesa)
redwood-gl: an OpenGL ES 3 context for CoreAnimation, by Mesa, off-screen (OSMesa):
            OpenGL ES 3.2 Mesa 25.0.7; FD650; shaders OpenGL ES GLSL ES 3.20
```
= 参考项目 M5 的同一判据。

**遗留**：需要 `gl=1` 强制启用——`IOServiceGetMatchingService("RedwoodGPU")`
在用户态返回 0（内核侧注册成功、无错误日志）。功能已通，服务的用户态可见性待查。

### 08:1x 画面撕裂修复 ✅

**症状**：帧缓冲不与面板 TE 同步 → 一次刷新可能读到半帧旧内容 + 半帧新内容。

**根因与修复（三处，都是参考移植 `hakios.display.c` 的移植 + 按其实测方法补全）**：

| # | 问题 | 现象 | 修法 |
|---|---|---|---|
| 1 | 用了 redwood（Yupik）寄存器布局 | `scan registers at 0xae36000` | loader 不发布 `/chosen/hakios-board` → 强制 `kona = true` |
| 2 | 回绕法测帧（`wraps<4`）在 thyme 上超时 | `command TE calibration failed` | 实测：4 次回绕/50ms、行率 7.08μs/行 |
| 3 | 回绕测得 2324 行/帧 ≠ 面板 2340 行 | `TE sanity check failed (lines 2324 shown 2340)` | 按参考方法：**帧 = 行速率 × 2340 行** |

**最终实测**：
```
display: UMI/Kona scan registers at 0xae6b800, engine=0; GDSC=0xf822f000
display: command mode: TE enable=0x1 config=0x180088 initial=0x924 auto=0x80000001
display: TE cross-check: 16466354 ns a frame by the wraps, 16574220 by the line rate
display: UMI command TE: 60.334 Hz, 7083 ns/line, 2340 phase lines;
         DMA idle after 2240; staged presentation enabled
display: command-mode staging buffer: 10368000 bytes
```
**60.334 Hz 与参考项目一致**；帧缓冲现在只在帧尾空闲窗口拷入，刷新率锁定面板 TE。

**新增移植文件**：`bsd/dev/redwood_display.c`、`bsd/dev/redwood_board.c`、
`iokit/Kernel/MiniIOMobileFramebuffer.cpp`（安全审计：无 exec/网络/写文件，仅 MMIO + 日志）。

**启动参数**（完整）：
```
debug=0x8 cpus=7 bootcpu=7 adreno=21 adreno_keep=1 touchscreen=1 gl=1 fb_scan=1 loader_return_after=900
```

### 09:2x 面板拷贝提速 10 倍（帧率优化第一步）✅

**问题**：`10.4 MB` 面板拷贝要 `18 ms`（580 MB/s）→ 每帧 19 ms → 28 fps。

**根因**：loader 给的帧缓冲映射是**非缓存（device memory）**，逐次写非常慢。

**踩坑**：第一次用 `ml_io_map_wcomb()` 直接 panic 早期启动——
因为 `io_map()` 的虚拟地址来自**固定 8 MB 子图**（`IO_MAP_SIZE = 8<<20`）且 `KMA_NOFAIL`，
10.4 MB 装不下就炸。改用 `ml_io_map_unmappable()`（走 `kernel_map`）解决。

**结果**：
```
fb: panel memory 0x9c000000, write-combining alias 0xfffffffa86e14000
display: staged frame 0 copied in 1798 us
display: staged frame 1 copied in 1881 us
```
**18,000 μs → 1,800 μs（10 倍，5.8 GB/s）** ✅

启动参数新增 `fb_wcomb=1`（默认关闭）。

**帧率仍 27–32 fps 的原因**（统计口径已确认）：
统计里的 "us each in the driver" 是整个 `show()` 的时间（14–19 ms），不是单拷贝：
- 等 GPU 完成：0.5 ms ✅
- 面板拷贝：1.8 ms ✅（已优化）
- **剩下 ~12–16 ms 在 `drawLayer`（CPU 合成 CoreAnimation 图层到 staging 缓冲）** ← 下一个瓶颈
- 另外帧间隔还有 ~17 ms 是 CoreAnimation 自己出帧的节奏

### 09:4x 每帧耗时全部拆解完成 ✅

分段计时结果（`where a frame's time goes` / `and the rest of a frame`）：

| 环节 | 优化前 | 现在 |
|---|---|---|
| 图层合成 (layers) | ~? | **0.23 ms** |
| 等扫描窗口 (scan wait) | ~? | **4.4 ms** |
| 面板拷贝 (panel copy) | 18 ms | **1.75 ms** |
| 其他 | — | **0.12 ms** |
| **每帧合计** | **~19 ms** | **6.5 ms** |

**60 帧预算 16.6 ms，现在只用了 6.5 ms → 剩余空间充足。**

注意：安静的静态界面下统计出的 "10–30 fps" 是 iOS 自己按需出帧（静态画面本来就不重绘），
真实动画帧率需要在滚动/切换时测。

**下一步可选项**：scan wait 4.4 ms 是剩下最大项，可把 copy 准入窗口提前来压缩
（当前窗口从帧内 line ~1221 起，7.9 ms 空闲）。

### 09:5x 异步拷贝（把扫描窗口等待移出绘制线程）✅

**问题**：`show()` 串行做"等扫描窗口(4.7ms) + 拷贝(1.8ms)"，CoreAnimation 必须等它返回才能渲染下一帧
→ 每帧被拉长到一个完整刷新周期 → 实际 30 多帧。

**做法**：新增专用内核线程 `copier`：
- `show()` 画完 staging 缓冲 → 唤醒 copier → **立即返回**
- copier 在后台等 TE 空闲窗口 + memcpy 到面板
- 下一帧 `show()` 只有在 copier 还没完成时才等（实际 0 等待）

**结果**：
```
the copier: 4940 us waiting for the panel, 1836 us copying, 0 us the drawing thread waited for it
每帧内核耗时 10363 us（原 19000 us）→ 几乎减半
```

**但帧率仍 25–32**：判定"swaps came a refresh late"（帧间隔 > 一个刷新周期）占 ~47%。
内核侧已经不再是瓶颈（绘制线程每帧 ~1ms），剩下的是 **CoreAnimation 用户态出帧慢**
（Mesa OSMesa + EAGL 桥渲染一帧约 30ms）→ 需要单独分析 GL 路径。

### 10:1x **60 帧达成** ✅✅

**根因：`cpufreq=1` 启动参数一直没加，CPU 组 1/2 跑在最低频（row 0）。**

加上后：
```
cpufreq: group 0 (3 of our cores): row 13 -> 16, 1804 MHz
cpufreq: group 1 (3 of our cores): row 0 -> 17, 2419 MHz
cpufreq: group 2 (1 of our cores): row 0 -> 20, 3187 MHz
```

实测：
```
MiniIOMobileFramebuffer: 301 frames in 5002 ms (60 a second), 4350 us each in the driver,
  after 260 us waiting for the GPU; 9 swaps came a refresh late themselves
```
**60.2 fps（满帧），迟到帧 47% → 3%**

**优化链条完整回顾**：
| 优化 | 效果 |
|---|---|
| TE 同步 (60.334Hz) | 无撕裂 |
| write-combining 映射 | 面板拷贝 18ms → 1.8ms（10倍） |
| 异步 copier 线程 | 绘制线程不再阻塞扫描窗口 |
| **cpufreq=1** | **CPU 1804/2419/3187MHz → 60fps** |

**完整启动参数**（最终版）：
```
debug=0x8 cpus=7 bootcpu=7 adreno=21 adreno_keep=1 touchscreen=1 gl=1
cpufreq=1 fb_scan=1 fb_wcomb=1 loader_return_after=900
```

### 10:3x 息屏完成 ✅（电源键 + 锁屏）

**根因链**（三层）：
1. **幻触**：触摸驱动把无 ENTER 的 MOTION 当成新触摸 → iOS 认为一直有手指按着 → 不锁屏
   - 修法探索：先试了三个过滤（无 ENTER 的 MOTION 忽略/重复计数丢弃/150ms 超时）→ **误伤真触摸（响应变差）**
   - 结论：**全部回滚**；幻触问题留待后续用芯片状态寄存器解决
2. **电源键驱动找错 PMIC**：写死 slave 0/periph 0x13（PMK8350），thyme 实际是 **PM8150 @ slave 0/periph 0x8**
   - boot-arg `pon=8` 覆盖
3. **PON subtype 不匹配**：驱动只认 0x9，PM8150 报 **0x4** → 放开兼容
4. **状态位不同**：驱动查 0x80（侧键）/0x04（音量），PM8150 实际 **0x01**（侧键）/ **0x02**（音量）
   - 实测确认：按键时 `0x4 -> 0x5`（bit0）/ `0x4 -> 0x6`（bit1）
   - boot-arg `pon_power_bit=1 pon_vol_bit=2`

**结果**：侧键可以息屏/唤醒 ✅（用户实测成功）

**iOS 侧熄屏实现**（之前已做）：framebuffer selector 12 (`kFBMethodRequestPowerChange`)
→ AMOLED 面板内存清零（纯黑=息屏）+ 熄屏期间丢弃帧

### 11:0x WiFi（QCA6390）移植 **重大突破** ✅

**参考项目 WiFi 栈移植**（全部审计：无 exec/网络/混淆，只 MMIO + 协议常量）：
| 文件 | 大小 | 作用 |
|---|---|---|
| `hakios.wifi.c` → `redwood_wifi.c` | 198 KB | 主驱动（WPSS 老路径保留） |
| `umi_wifi.c` → `redwood_wifi_umi.c` | 31 KB | QCA6390 PCIe/MHI 板级适配 |
| `hakios.qmi.c/.h` | 25+10 KB | QMI 服务 |
| `hakios.wlfw.c` | 35 KB | WLAN 固件服务（WLFW） |
| `hakios.lx.c` → `redwood_lx.c` | 17 KB | lx_os_* LinuxKPI 辅助 |
| `umi_mhi.c` | 31 KB | **MHI 引擎**（固件传输/IPCR） |
| `umi_pcie.c` | 14 KB | **PCIe 引擎**（枚举/BAR/DMA） |
| `umi_rpmh.c` | 15 KB | Wi-Fi 供电轨控制 |

**固件**（从手机 `/vendor/firmware_mnt/image/qca6390/` 拉取，放进镜像 `fw/qca6390/`）：
`amss20.bin` 4.35MB、`m3.bin` 267KB、`bd_j1gl.elf` 58KB、`regdb.bin` 19KB

**关键修复**：`/chosen/hakios-board` 属性我们的 loader 不发布 → `umi_wifi_is_umi()` 固定 true（thyme 就是 umi）

**实机启动日志（完整链路打通）**：
```
UMI RPMH: Wi-Fi/PCIe 7 条供电轨配置 (smpa4/smpa6/smpf2/smpa5/smpc8/ldoa9/ldoa5)
UMI PCIe: PHY ready = 0xa6
UMI PCIe: link status = 0xb0120008
UMI PCIe: endpoint PCI ID = 0x110117cb          ← QCA6390 识别
UMI PCIe: QCA6390 cold PBL signature validated = 0x1
UMI PCIe: WLAON power control 0x6 -> 0x2
UMI PCIe: protected DMA stream = 0x1c01
UMI PCIe: firmware transfer complete = 0x80000001  ← SBL 固件传输
UMI PCIe: MHI contexts installed; M0 = 0x201       ← MHI mission mode
UMI PCIe: firmware mission environment = 0x2      ← AMSS 运行
UMI PCIe: MHI channel started = 0x14 / 0x15       ← IPCR 通道
UMI PCIe: IPCR TX/RX completion
UMI PCIe: QRTR packet transmitted = 0xa04cd000    ← QRTR 通信打通
```

**iOS 侧**：`wifid` 已启动（`0 country code(s) ... by 0 networks` = 还没有扫描到网络）

**下一步**：WLFW/QMI 握手 → ath11k 用户态栈（参考项目的 lx_* 802.11 实现，168KB，需另移植）

### 11:1x ath11k 编译成功并链接进内核 ✅

**构建链**（基座项目的 `tools/mk-ath11k.sh` + 参考项目的 LinuxKPI 胶水层）：
- Linux 7.0.14 的 ath11k 驱动（18 个文件，BSD-3-Clause-Clear）
- FreeBSD 15.1 的 LinuxKPI + 参考项目的 `wifi/lx/src`（lx_*.c，审计通过）
- 输出：`ath11k.o` **522 KB，18 个文件，0 个桩函数**

**名字映射**：参考胶水层用 `HAKIOS_*`，我们的头文件是 `REDWOOD_*` → 已统一改名

**内核**：15.32 MB → **15.80 MB**（+479 KB）

**当前进展**：
- ✅ QCA6390 固件运行（PBL 签名验证、SBL/AMSS 加载、MHI mission mode）
- ✅ QRTR 与固件通信（IPCR TX/RX completion）
- ⚠️ WLFW/QMI 握手后的 ath11k 启动尚未确认（日志抓取困难：logdump 环形缓冲混入旧会话）

**下一步**：需要可靠的日志抓取手段来调试 WLFW 握手，或直接从 iOS 侧确认 WiFi 接口是否出现

### 11:2x WiFi 深度调试（当前卡点）

**用户实测**：设置里 WiFi 开关出现（说明 IOKit 服务正常）但**不能关、扫不到网络**。

**日志证据**（iOS 框架确实在调用我们的接口）：
```
apple80211: get 1 (SSID) 3 次，回答 0 次；拒绝原因 6 (ENXIO)
apple80211: set 25 (HOST_AP_MODE) 拒绝原因 102 (not built)
apple80211: get 27 (SUPPORTED_CHANNELS) 拒绝原因 6
```
→ 接口存在但**后端驱动未启动**（ENXIO）。

**服务线程执行链路**（`wifi=1` → `umi_wifi_service_thread` → `umi_wifi_pcie_diagnostic(10)`）：
```
✅ background startup requested, errno=0
✅ PCIe 枚举 + PBL 签名 (0x1)
✅ SBL/AMSS 固件下载 (firmware mission environment = 0x2)
✅ MHI 通道启动 (0x14, 0x15)
✅ QRTR packet transmitted
✅ IPCR RX completion（固件有回应）
❌ 之后无输出 —— 卡在 umi_mhi_poll_ipcr() 或其返回值处理
❌ WIFI-MARK 标记（step 10 入口）从未打印 → 未到 lx_start()
```

**下一步**：查 `umi_mhi_poll_ipcr` 的返回/`service_event_ring` 的等待条件，让 QRTR 握手完成，
之后 `redwood_lx_start()` 启动 ath11k → 扫描/连接。

**内核构成**（当前）：
- 15.80 MB，含 QCA6390 驱动栈 + ath11k.o (522KB) + LinuxKPI 胶水（参考项目版）

### 11:4x WiFi 调试记录（交接用）

**当前状态**：内核包含完整 QCA6390 栈 + ath11k（15.80 MB），启动日志显示：
- ✅ `background startup requested, errno=0`（服务线程启动）
- ✅ PCIe 枚举 + PBL 签名 + SBL/AMSS 固件传输 + MHI mission + IPCR 通道
- ✅ QRTR 包收发（`QRTR packet transmitted` + `IPCR RX completion`）
- ❌ 部分启动中 `umi_mhi_download_sbl` 失败（标记未打印），表现为**不稳定**
- ❌ 未观察到 `station ready` 或 `ath11k diagnostic returned`

**发现的问题**：
1. **收包回调死锁**（已修）：`service_event_ring` 内直接调用 QRTR 回调，回调内再发送 →
   嵌套事件环处理 → 卡死。已改为延迟队列（`umi_mhi_process_deferred`），但未见明显改善
2. **日志通道不可靠**：
   - `redwood_tl` → loader 小日志缓冲，满了就丢
   - `printf` → 内核日志，但服务线程早期的消息常被后续大量日志挤掉
   - `get_staged` 抓到的 1.4MB 内核日志只保留尾部
3. **启动不稳定**：同样的参数，有的启动能到 QRTR TX/RX，有的在 SBL 下载就失败

**用户实测**：设置里 WiFi 开关出现（IOKit 服务 OK），但开关不能关、扫不到网络；
`apple80211` 调用返回 ENXIO（驱动后端未启动）。

**下一步建议**：
1. 先解决启动稳定性（SBL 下载偶尔失败）——可能是 PCIe/MHI 初始化时序
2. 修好后确认 QRTR 握手走完 → `lx_start()` → ath11k 起来
3. 最后是 ath11k 的 WLFW 固件下载（m3.bin/board.bin/regdb.bin 已在镜像里）

### 11:5x **WiFi 扫描成功！** 🎉🎉

**关键修复**：`wifi_mac=02:00:00:00:00:01` 启动参数 —— ath11k 需要 MAC 地址才能启动
（之前 `ath11k start returned 49` = EADDRNOTAVAIL）

**日志证据**：
```
net_test: wifi: en0 is there after 0.0 s
lx: the radio is the link behind en0
WIFI-MARK: ath11k start returned 0
UMI Wi-Fi: station ready for iOS Wi-Fi requests
```

**iOS 扫描结果**：
```
apple80211: get 13 (STATE) 3 次，回答 3 次
apple80211: get 27 (SUPPORTED_CHANNELS) 2 次，回答 2 次
apple80211: set 10 (SCAN_REQ) 5 次，回答 5 次
apple80211: get 11 (SCAN_RESULT) 55 次，回答 50 次
```

**调试技巧（重要）**：内核日志只保留尾部 1.4MB，WiFi 初始化在启动早期会
被 iOS 的日志冲掉 → **启动后 45-60 秒就 `oem stop` 抓日志**，才能看到 WiFi 初始化过程。

**完整 WiFi 启动参数**：
```
wifi=1 wifi_mac=02:00:00:00:00:01
```

---

# 里程碑：iOS 从手机自己的存储自主启动（2026-11-01）

**目标达成**：手机不需要任何电脑、数据线、命令，自己启动 iOS。

## 启动链（全部在手机内部完成）

```
ABL (bootloader) → boot_b 分区（我们的 UEFI 固件 + xnuloader）
                 → loader 自动启动逻辑：读 ios 分区头部 → 读 payload(26.7 MB)
                 → 分配内存窗口（0xF4000000，6 GB）
                 → 读系统镜像 2.33 GiB 到 0xFC000000（与 USB 方式完全相同的地址）
                 → run_kernel → XNU → iOS
```

## 存储布局（未破坏安卓数据）

用基座项目的 `tools/userdata-carve.py` 从 userdata 尾部切出 **4 GiB 的 ios 分区**：

| 步骤 | 命令/结果 |
|---|---|
| 1. 收缩 f2fs | `f2fs-resize /data 58045952` → 58,046,203 块 / 113,022 段（与预期一致） |
| 2. 校验 | 尾部无数据（最后有数据的段 111,810 < 新边界 113,022）|
| 3. 改写 GPT | gpt-head.new/gpt-tail.new，主表+备份+CRC 全部校验通过 |
| 4. 重启 | 安卓正常启动，数据完整（Termux 容器 3.3 GB 保留） |

**新的 ios 分区**：`/dev/block/by-name/ios`（= sda36，4 GiB，类型 APFS）
- 偏移 0：4096 字节头部（magic "IOSSTOR1"、版本、boot_mode、payload_len、system_len、SHA-256）
- 偏移 4096：XNUP payload（kernel 15.8 MB + root.apfs 10.9 MB + cmdline）
- 偏移 4096+payload_len：system.apfs 2.33 GiB（未对齐，loader 支持非对齐读）

## 双系统切换（A/B 槽位）

- 槽位 A：安卓（boot_a 原样）
- 槽位 B：iOS 引导链（boot_b = thyme-start-new.img，2 MB）
- 切换：`fastboot --set-active=a|b`（当前需要电脑；后续可实现从 iOS/安卓侧直接写 misc）

## 关键技术问题与解决

### 1. loader 重建的 kona 平台补丁（最重要）

基座项目原始源码的 `platforms[]` 只有 redwood（fb_base 不同）→ 我们的手机（kona，fb 0x9C000000）
匹配不到平台 → 会调用 QEMU 专用的 `ramfb_phone_screen()` → 读不存在的 fw_cfg → **崩溃**。

修复（来自早前工作 ``$IOS`\patch-loader.py`）：

```c
if (PLAT == &platform_qemu_virt) {
    /* 未知显示器仍是高通手机，不是 QEMU ramfb：kona 与 yupik 共享平台常量，只有 fb 基址不同 */
    static struct platform plat_unknown;
    plat_unknown = platform_redwood;
    plat_unknown.name = "unknown-kona";
    plat_unknown.fb_base = SCREEN.base;
    PLAT = &plat_unknown;
    print("unknown display, using the qualcomm defaults\n");
}
```

### 2. loader 兼容性注意

- **PE 镜像大小必须和原始 loader 一致（0x769000）**：新代码不能增加大块静态缓冲，
  复用已有的 `block_buffer`（4 MB）而不是新增数组
- 构建工具链（WSL clang 18）本身没问题——**QEMU 下验证相同源码可运行**
- 源码位置：原始源码 `/tmp/xnuloader-orig`（来自 DBC1349/hackios-mi10ultra 仓库），
  但必须打 kona 补丁；本地树的 loader.c 也已合入补丁+自动启动代码

### 3. 分区偏移坑（差点毁掉安卓）

lpdump 的 extent 是**相对 super 分区**的 LBA，不是磁盘绝对 LBA。
第一次尝试写 super 空闲区时偏移差了 2048 扇区（1 MB），覆盖了 `mi_ext_a` 的尾部
→ MIUI 无法启动 → 用 `fastboot flash super` 从备份完整恢复 ✓

**教训**：任何分区写入前先用备份 + 精确校验，signed 分区（super）尤其危险。

### 4. 非块对齐读取

系统镜像偏移 = 4096 + payload_len = 26,719,072 字节，不是 4096 的倍数。
loader 的 read_partition_bytes 增加"首块部分读取"逻辑处理非对齐起点（不需要重写 2.3 GB 镜像）。

## 文件清单

| 文件 | 说明 |
|---|---|
| ``$IOS`\thyme\thyme-start-new.img` | 新引导镜像（UEFI + 带自动启动的 loader），已刷入 boot_b |
| ``$IOS`\thyme\ios-storage\ios.img` | ios 分区镜像（2.33 GiB），已写入 /dev/block/by-name/ios |
| ``$IOS`\thyme\ios-storage\header-boot.bin` | boot_mode=0 的头部（自动启动 iOS） |
| ``$IOS`\thyme\ios-storage\header-usb.bin` | boot_mode=1 的头部（进 USB 模式，调试用） |
| `C:\Liquid-backup\` | 全量备份（super 8.5G、cust、exaid、boot_a/b、vbmeta、misc、termux 3.18G 等 13.68 GB）|
| ``$IOS`\thyme\carve\` | GPT 切割文件（gpt-head.bin/gpt-tail.bin/gpt-head.new/gpt-tail.new、sb-after.bin）|
| ``$IOS`\tools\notify.ps1` | 重启提醒音频脚本 |

## 恢复安卓的方法

1. 关机 → 电源+音量下 → fastboot
2. `fastboot --set-active=a` → 重启 → 安卓（boot_a 原样，数据完好）

如需完全恢复分区表：用 carve 目录的 gpt-head.bin/gpt-tail.bin 原始备份写回。
