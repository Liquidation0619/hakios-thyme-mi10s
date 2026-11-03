#!/usr/bin/env python3
"""The var_store watcher runs as user 501 (a launchd user job, as the boot loads it), but the store's
interfaces are root-only: the character device is mode 0640 root:operator and the two sysctls are
writable by root alone. Open them to that job; the driver still refuses every write that is not into
darwin past what the build put there, and the sysctl handlers still check the claim."""
import sys

p = "/opt/hackios/xnu-src/xnu-8796.121.2/bsd/dev/thyme_ufs.c"
s = open(p).read()

subs = [
    # the character devices, read/write by anyone the driver serves
    ('(void)devfs_make_node(makedev(ufs_cmajor, i), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, 0640, "%s", name);',
     '(void)devfs_make_node(makedev(ufs_cmajor, i), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, 0666, "%s", name);'),
    # the handshake sysctl the saver writes
    ('SYSCTL_PROC(_kern_thyme, OID_AUTO, var_save, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0,',
     'SYSCTL_PROC(_kern_thyme, OID_AUTO, var_save, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_ANYBODY, NULL, 0,'),
]
for old, new in subs:
    if old not in s:
        print("anchor missing: %s" % old[:70])
        continue
    s = s.replace(old, new, 1)
    print("patched: %s" % old[:70])

# the claim sysctl
import re
m = re.search(r'SYSCTL_INT\(_kern_thyme, OID_AUTO, var_busy,[^;]*;', s)
if m:
    old = m.group(0)
    if "CTLFLAG_ANYBODY" not in old:
        new = old.replace("CTLFLAG_RW", "CTLFLAG_RW | CTLFLAG_ANYBODY", 1)
        s = s.replace(old, new, 1)
        print("patched: var_busy ->", new[:90])
else:
    # a PROC handler maybe
    m2 = re.search(r'SYSCTL_PROC\(_kern_thyme, OID_AUTO, var_busy,[^;]*;', s)
    if m2:
        old = m2.group(0)
        if "CTLFLAG_ANYBODY" not in old:
            new = old.replace("CTLFLAG_RW", "CTLFLAG_RW | CTLFLAG_ANYBODY", 1)
            s = s.replace(old, new, 1)
            print("patched: var_busy (proc) ->", new[:90])
    else:
        print("var_busy sysctl not found")

open(p, "w").write(s)
print("saved")
