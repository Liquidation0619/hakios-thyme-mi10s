#!/usr/bin/env python3
"""Add `oem fastboot` to the loader: write Android's boot command block ("bootonce-bootloader") into
the `misc` partition and reset, so the next boot goes to the bootloader without key combos."""
import sys

p = "/opt/hackios/xnuloader/loader.c"
s = open(p, newline="").read()

if 'oem fastboot' in s:
    print("already patched")
    sys.exit(0)

anchor = '''		} else if (starts_with(cmd, "reboot") || starts_with(cmd, "oem reboot")) {'''
assert anchor in s, "reboot handler not found"
new = '''		} else if (starts_with(cmd, "oem fastboot")) {
			/* write Android's boot command block: the next reset goes to the bootloader */
			efi_block_io *misc = find_partition_bio(u"misc");
			static char bcb[4096];
			efi_status st = 1;

			memset(bcb, 0, sizeof(bcb));
			memcpy(bcb, "bootonce-bootloader", 20);
			if (misc != NULL) {
				st = misc->write_blocks(misc, misc->media->media_id, 0, sizeof(bcb), bcb);
				if (!EFI_ERROR(st)) {
					misc->flush_blocks(misc);
				}
			}
			print("ios: oem fastboot: bcb written (%x), resetting\\n", (uint64_t)st);
			flush_log();
			fb_reply(EFI_ERROR(st) ? "FAIL" : "OKAY", "");
			reboot();
		} else if (starts_with(cmd, "reboot") || starts_with(cmd, "oem reboot")) {'''
s = s.replace(anchor, new, 1)
open(p, "w", newline="").write(s)
print("patched: oem fastboot")
