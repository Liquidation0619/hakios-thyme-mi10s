#!/usr/bin/env python3
"""After the kernel returns (the run is over, the volume saved), just reset: trying to bring USB up
there hangs the loader, and the next boot is what should load the store back."""
p = "/opt/hackios/xnuloader/loader.c"
s = open(p, newline="").read()

old = '''	/* the run is over (and the data volume saved): serve USB if a host is attached, else reset */
	flush_log();
	if (usb_start()) {
		usb_command_loop();
	}
	reboot();'''
new = '''	/* the run is over (and the data volume saved): reset, so the next boot loads the store back */
	flush_log();
	reboot();'''
if old not in s:
    print("anchor missing (already patched?)")
else:
    s = s.replace(old, new, 1)
    open(p, "w", newline="").write(s)
    print("patched: post-run reboot")
