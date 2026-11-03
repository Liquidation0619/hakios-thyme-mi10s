#!/bin/bash
# Build the jailbreak small image with the prefix TRIMMED so that kernel + image fit the loader's
# 64 MiB payload:
#   - drop the prefix's locale, man pages, docs, dict, zsh data, examples and include headers
#     (not needed to start Sileo/apt; they can be reinstalled later through Sileo/apt if wanted)
#   - keep everything that makes it work: usr/bin, usr/lib, usr/libexec, Applications (Sileo),
#     Library, etc, terminfo (xterm only), and the set-uid bits
# Everything else follows the user's build-jb-image.sh: the patched init grafts the prefix from
# /redwood/jb, bin/binunion unions /redwood/jb-bin over /bin.
set -o pipefail
exec 2>&1

SRC=/mnt/d/Liquid/ios/thyme/small-image-tree
TREE=/root/small-image-tree-jbtrim
JB=/opt/hakios-main/jb/root/var/jb
TOOLS=/mnt/d/Liquid/ios/refs/hackios_userland/apfs
OUT=/mnt/d/Liquid/ios/thyme/root-jb-trim.apfs

[ -d "$JB" ] || { echo "missing $JB" >&2; exit 1; }

rm -rf "$TREE"
cp -a "$SRC" "$TREE"

echo "== the prefix, trimmed"
rm -rf "$TREE/jb"
cp -a "$JB" "$TREE/jb"
# not needed to boot or to run apt/Sileo
rm -rf "$TREE/jb/usr/share/locale" "$TREE/jb/usr/share/man" "$TREE/jb/usr/share/doc" \
       "$TREE/jb/usr/share/dict" "$TREE/jb/usr/share/zsh" "$TREE/jb/usr/share/examples" \
       "$TREE/jb/usr/share/bash-completion" "$TREE/jb/usr/include"
# terminfo: keep the terminal entries interactive programs need
if [ -d "$TREE/jb/usr/share/terminfo" ]; then
    mkdir -p /tmp/ti
    for t in xterm xterm-256color xterm-color vt100 vt220 linux screen screen-256color; do
        find "$TREE/jb/usr/share/terminfo" -name "$t" -exec cp -a {} /tmp/ti/ \; 2>/dev/null
    done
    rm -rf "$TREE/jb/usr/share/terminfo"
    mkdir -p "$TREE/jb/usr/share/terminfo"
    cp -a /tmp/ti/. "$TREE/jb/usr/share/terminfo/" 2>/dev/null
fi
mkdir -p "$TREE/jb-bin"
for c in chmod cp mkdir mv rm; do
    ln -sf /var/jb/usr/bin/$c "$TREE/jb-bin/$c"
done
echo "   prefix now $(du -sh "$TREE/jb" | cut -f1)"

echo "== init (prefix at /redwood/jb) and binunion"
cp /mnt/d/Liquid/ios/.recon/init-jb "$TREE/sbin/init"
cp /mnt/d/Liquid/ios/.recon/binunion "$TREE/bin/binunion"
chmod 755 "$TREE/sbin/init" "$TREE/bin/binunion"
if ! grep -a -q '/redwood/jb' "$TREE/sbin/init"; then
    echo "the init does not know /redwood/jb" >&2
    exit 1
fi

echo "== modes"
find "$TREE" -path "$TREE/jb" -prune -o -type d -exec chmod 755 {} \;
find "$TREE" -path "$TREE/jb" -prune -o -type f -exec chmod 644 {} \;
chmod 755 "$TREE"/bin/* "$TREE"/sbin/* "$TREE"/lib/*.dylib "$TREE"/fw/* 2>/dev/null
chmod -R 755 "$TREE"/apps/*.app 2>/dev/null
find "$TREE" -path "$TREE/jb" -prune -o -exec chown -h root:root {} +
for f in Applications/Sileo.app/giveMeRoot usr/bin/sudo usr/bin/login usr/bin/su usr/bin/passwd; do
    mode=$(stat -c %a "$TREE/jb/$f" 2>/dev/null) || continue
    if [ "$(( (8#$mode & 8#4000) != 8#4000 ))" -ne 0 ]; then
        echo "warning: $f lost its set-uid bit (mode $mode)" >&2
    fi
done

echo "== image"
python3 "$TOOLS/mkapfs.py" -o "$OUT" -z --plain fw redwood="$TREE" 2>&1 | tail -2
ls -l "$OUT"
echo
echo "   kernel + this image = $(( $(stat -c%s /mnt/d/Liquid/ios/thyme/kernel-stage.bin) + $(stat -c%s "$OUT") )) bytes (limit 67108864)"
