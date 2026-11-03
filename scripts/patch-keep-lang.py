#!/usr/bin/env python3
"""Add the Chinese localisations to the system-image selection (the image build keeps only KEEP_LANG
lproj directories; without zh-Hans no Chinese UI is possible)."""
import shutil

p = "/opt/hakios-main/sysimage/select-system.py"
shutil.copyfile(p, p + ".orig")

old = "KEEP_LANG = ('en', 'Base', 'English', 'en_US', 'en_GB')"
new = "KEEP_LANG = ('en', 'Base', 'English', 'en_US', 'en_GB', 'zh-Hans', 'zh-Hant', 'zh_CN', 'zh_TW')"

t = open(p, encoding="utf-8").read()
assert old in t, "KEEP_LANG line not found"
t = t.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(t)

for i, line in enumerate(open(p, encoding="utf-8"), 1):
    if "KEEP_LANG =" in line:
        print("line %d: %s" % (i, line.strip()))
