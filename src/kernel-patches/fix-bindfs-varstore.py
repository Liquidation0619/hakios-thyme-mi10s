#!/usr/bin/env python3
"""Two fixes in one go:
  1. bindfs: let root (our rc script, pid != 1) bind as well, not only process 1 / an entitled task;
  2. var_store: when started with no arguments (which is how the launchd job runs it) watch, and do
     not gate the watch on kern.bootargs -- the caller asked for the watcher explicitly."""
import sys

# --- 1. the kernel ---
p = "/opt/hackios/xnu-src/xnu-8796.121.2/bsd/miscfs/bindfs/bind_vfsops.c"
s = open(p).read()
old = "\tif (!IOCurrentTaskHasEntitlement(BINDFS_ENTITLEMENT) && proc_getpid(current_proc()) != 1) {\n\t\treturn EPERM;\n\t}"
new = ("\tif (!IOCurrentTaskHasEntitlement(BINDFS_ENTITLEMENT) && proc_getpid(current_proc()) != 1 &&\n"
       "\t    kauth_cred_getuid(vfs_context_ucred(ctx)) != 0) {\n\t\treturn EPERM;\n\t}")
if old in s:
    s = s.replace(old, new, 1)
    open(p, "w").write(s)
    print("kernel: root may bind")
else:
    print("kernel anchor missing (already patched?)")

# --- 2. var_store ---
p = "/opt/hackios/userland-varstore/var_store.c"
s = open(p).read()

old2 = '\tconst char *what = argc > 1 ? argv[1] : "show";'
new2 = '\tconst char *what = argc > 1 ? argv[1] : "watch";   /* launched with no words: be the watcher */'
if old2 in s:
    s = s.replace(old2, new2, 1)
    print("var_store: default command is watch")
else:
    print("var_store: main anchor missing")

# drop the boot-arg gate: whoever runs "watch" asked for it
import re
m = re.search(r'\t\tif \(strcmp\(what, "watch"\) == 0\) \{\n\t\t\tif \(!boot_arg_is\("var_store", "1"\)\) \{\n.*?\n\t\t\t\}\n\t\t\treturn watch\(.*?\n\t\t\}', s, re.S)
if m:
    old_block = m.group(0)
    new_block = ('\t\tif (strcmp(what, "watch") == 0) {\n'
                 '\t\t\treturn watch(argc > 2 ? (int)parse_number(argv[2]) : 0) ? 0 : 1;\n'
                 '\t\t}')
    s = s.replace(old_block, new_block, 1)
    print("var_store: watch no longer gated on boot args")
else:
    print("var_store: watch block not found")

open(p, "w").write(s)
print("ok")
