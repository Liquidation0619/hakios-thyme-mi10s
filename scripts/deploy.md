# Deploying to the phone 鈥?閮ㄧ讲姝ラ

The state this describes is the one at the end of the port's last session: Android on slot `a`,
the iOS chain on slot `b`, a 4 GiB `darwin` partition at the end of `userdata` holding the image.
Adjust the values to your own device; the exact numbers this port used are in
`docs/DEVELOPMENT.md`.

## 0. Prerequisites / 鍓嶆彁

* Unlocked bootloader (`verifiedbootstate = orange`), `adb` + `fastboot` on the host.
* **Backups first**: `super` (8.5 GB), `cust`, `exaid`, `boot_a/b`, `vbmeta_a/b`, `misc`,
  `dtbo_a/b`, `vendor_boot_a/b`, and the user's `userdata` data you want to keep. The port's own
  backup set was 13.7 GB and saved the project twice (see DEVELOPMENT.md: a wrong LBA wrote over
  the tail of `mi_ext_a` and Android stopped booting until `super` was restored from the backup).
* 澶囦唤浼樺厛锛歴uper 鍏ㄩ噺銆乧ust銆乪xaid銆乥oot 涓や釜妲姐€乿bmeta銆乵isc 绛夈€傜Щ妞嶈繃绋嬩腑鏇惧洜鍐欓敊 1 MB 瑕嗙洊
  `mi_ext_a` 瀵艰嚧瀹夊崜鏃犳硶鍚姩锛岄潬 super 澶囦唤鎭㈠銆?
## 1. Carve the `darwin` partition (keep Android's data) / 鍒囧嚭 darwin 鍒嗗尯

The base project's `tools/userdata-carve.py` does this: it shrinks the f2fs **in place through
Android's own kernel** (`f2fs-resize`, an ioctl program), checks that nothing lives in the tail
that is cut off, then rewrites both GPT copies with correct CRCs.

```
# on the phone, read-only collection
adb shell su -c 'dd if=/dev/block/sda of=/data/local/tmp/gpt-head.bin bs=4096 count=8'
adb shell su -c 'dd if=/dev/block/sda of=/data/local/tmp/gpt-tail.bin bs=4096 skip=$((BLOCKS-8)) count=8'
adb shell su -c 'dd if=/dev/block/dm-39 of=/data/local/tmp/userdata-meta-before.bin bs=4096 count=1'
adb shell su -c 'cat /proc/fs/f2fs/dm-39/segment_info > /data/local/tmp/segment_info-before.txt'

python3 tools/userdata-carve.py DIR 4 darwin     # prints the shrink command and the plan
#   then: f2fs-resize /data <blocks>; save sb-after; re-run to make the new table; dd it back
```

Notes from this port:
* userdata here is **f2fs behind dm-default-key (metadata encryption)**: the shrink runs through
  `/dev/block/dm-39`; the raw partition is encrypted, so the tool's filesystem checks must read the
  plaintext view (this port adapted that part).
* The GPT LBAs are in the UFS's **4096-byte logical blocks** 鈥?the same unit UEFI and the loader
  use. Dividing by 8 for 512-byte sectors made the first attempt point 1 MB too early.
* GPT 鐨?LBA 鍗曚綅鏄?4096 瀛楄妭閫昏緫鍧楋紱鎸?512 瀛楄妭鎹㈢畻浼氬鑷村亸绉婚敊璇€?
## 2. Write the image / 鍐欏叆闀滃儚

```
python3 scripts/build-image.py                  # -> $IOS/thyme/ios-storage/ios.img  (鈮?.5 GB)
adb push ios.img /data/local/tmp/
adb shell su -c 'dd if=/data/local/tmp/ios.img of=/dev/block/by-name/darwin bs=4M conv=notrunc,fsync'
```

## 3. Flash the chain / 鍒峰叆寮曞閾?
```
adb reboot bootloader
fastboot flash boot_b thyme-start-new.img       # the image from scripts/build-loader.sh
fastboot --set-active=b                          # boot iOS next
fastboot reboot
```

To go back to Android: `fastboot --set-active=a` (needs a PC for now 鈥?see ROADMAP 搂3).

## 4. Debugging switches / 璋冭瘯寮€鍏?
* The header's `boot_mode` (byte 12 of the image's first 4096 bytes): `0` boots iOS, `1` makes the
  loader stop and serve USB (`fastboot get_staged` then gives its log).
* The loader writes its log to the `logdump` partition at every stage, so a hung boot can be
  inspected from Android: `adb shell su -c 'dd if=/dev/block/by-name/logdump of=/data/local/tmp/ld.bin bs=1M count=4'`.
* `oem fastboot` in the loader writes `bootonce-bootloader` into `misc` and resets 鈥?a way to reach
  the bootloader without the key combo.
