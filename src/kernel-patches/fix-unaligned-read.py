#!/usr/bin/env python3
"""read_partition_bytes refused an offset that is not block-aligned, and the system image starts at
4096 + payload_len, which is not. Handle the leading partial block instead of refusing."""
import sys

p = "/opt/hackios/xnuloader/loader.c"
s = open(p, newline="").read()

start = s.find("/* Read bytes of a partition")
if start < 0:
    start = s.find("read_partition_bytes(efi_block_io *bio")
    # back up to the start of the function's comment or definition
    start = s.rfind("\n", 0, s.rfind("\n", 0, start)) 
end = s.find("/*\n * Boot the iOS installation", start)
assert start > 0 and end > start, "read_partition_bytes not found"

new = '''/* Read bytes of a partition from byte offset `at` into `dest`, in blocks through the loader's own
 * storage buffer. `at` need not be block-aligned: the leading partial block is read too. */
static int
read_partition_bytes(efi_block_io *bio, uint64_t at, uint64_t len, uint8_t *dest)
{
	uint64_t bs = bio->media->block_size;
	uint64_t done = 0;

	if (bio == NULL || bs == 0 || bs > BLOCK_BUFFER_SIZE) {
		print("ios: bad partition read (%d bytes at %x)\\n", len, at);
		return 0;
	}
	if ((at % bs) != 0) {
		uint64_t head = bs - (at % bs);
		efi_status st;

		if (head > len) {
			head = len;
		}
		st = bio->read_blocks(bio, bio->media->media_id, at / bs, bs, block_buffer);
		if (EFI_ERROR(st)) {
			print("ios: reading the leading block at %x: error %x\\n", at, (uint64_t)st);
			return 0;
		}
		memcpy(dest, block_buffer + (at % bs), (size_t)head);
		done = head;
	}
	while (done < len) {
		uint64_t pos = at + done;
		uint64_t want = len - done < BLOCK_BUFFER_SIZE ? (len - done + bs - 1) / bs * bs : BLOCK_BUFFER_SIZE;
		efi_status st = bio->read_blocks(bio, bio->media->media_id, pos / bs, want, block_buffer);

		if (EFI_ERROR(st)) {
			print("ios: reading (at %x, %d bytes): error %x\\n", pos, want, (uint64_t)st);
			return 0;
		}
		memcpy(dest + done, block_buffer, len - done < want ? len - done : want);
		done += len - done < want ? len - done : want;
		if (wdt_armed) {
			wdt_pet();
		}
	}
	return 1;
}

'''
s = s[:start] + new + s[end:]
open(p, "w", newline="").write(s)
print("patched: unaligned partition reads supported")
