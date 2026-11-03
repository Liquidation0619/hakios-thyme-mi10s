#!/bin/bash
# Build the small image (root.apfs, the volume mounted on /redwood) with the rootless jailbreak
# prefix in it: Procursus + Sileo + ElleKit from the base project's tools/mk-jb.sh, at /redwood/jb.
#
# Changes against the port's plain small image (build-image-perms-linux.sh):
#   * sbin/init is the binary patched by .recon/init-patch.py: it grafts the prefix from
#     /redwood/jb (the base init looks at /jb and /private/preboot/jb only);
#   * bin/binunion is a new tool: it binds /redwood/jb-bin over /bin as a union, so Sileo's
#     /bin/mv, /bin/cp, /bin/rm, /bin/mkdir, /bin/chmod exist;
#   * jb/ is the prefix itself (real files, read only in the image; init links it into /private/var/jb);
#   * jb-bin/ holds those five names as links into the prefix.
#
# The mode normalisation skips jb/: its set-uid bits (login, sudo, giveMeRoot) are what make the
# prefix work, and mk-jb.sh put them there.
set -o pipefail
exec 2>&1

SRC=/mnt/d/Liquid/ios/thyme/small-image-tree
TREE=/root/small-image-tree
JB=/opt/hakios-main/jb/root/var/jb
TOOLS=/mnt/d/Liquid/ios/refs/hackios_userland/apfs

[ -d "$JB" ] || { echo "build-jb-image: $JB is missing: run /opt/hakios-main/tools/mk-jb.sh first" >&2; exit 1; }

rm -rf "$TREE"
cp -a "$SRC" "$TREE"

echo "== the package manager's prefix"
rm -rf "$TREE/jb"
cp -a "$JB" "$TREE/jb"
mkdir -p "$TREE/jb-bin"
for c in chmod cp mkdir mv rm; do
	ln -sf /var/jb/usr/bin/$c "$TREE/jb-bin/$c"
done
echo "   $(du -sh "$TREE/jb" | cut -f1), $(find "$TREE/jb" | wc -l) entries"

echo "== init (prefix at /redwood/jb) and binunion"
cp /mnt/d/Liquid/ios/.recon/init-jb "$TREE/sbin/init"
cp /mnt/d/Liquid/ios/.recon/binunion "$TREE/bin/binunion"
chmod 755 "$TREE/sbin/init" "$TREE/bin/binunion"
if ! grep -a -q '/redwood/jb' "$TREE/sbin/init"; then
	echo "build-jb-image: the init in the tree does not know /redwood/jb" >&2
	exit 1
fi

echo "== normalising modes (the prefix keeps its own)"
find "$TREE" -path "$TREE/jb" -prune -o -type d -exec chmod 755 {} \;
find "$TREE" -path "$TREE/jb" -prune -o -type f -exec chmod 644 {} \;
chmod 755 "$TREE"/bin/* "$TREE"/sbin/* "$TREE"/lib/*.dylib "$TREE"/fw/* 2>/dev/null
chmod -R 755 "$TREE"/apps/*.app 2>/dev/null
# chown clears the set-uid bits, so the prefix is left out of it: mk-jb.sh ran as root and its
# files already belong to root, with the set-uid bits it put there.
find "$TREE" -path "$TREE/jb" -prune -o -exec chown -h root:root {} +
# the bits that matter in the prefix: the set-uid ones, checked rather than assumed
stat -c '%a %n' "$TREE/jb/Applications/Sileo.app/giveMeRoot" "$TREE/jb/usr/bin/sudo" "$TREE/jb/usr/bin/login"
for f in Applications/Sileo.app/giveMeRoot usr/bin/sudo usr/bin/login usr/bin/su usr/bin/passwd; do
	mode=$(stat -c %a "$TREE/jb/$f")
	# arithmetic, not test: 04000 is octal here, and 8# turns the mode string into a number
	if [ "$(( (8#$mode & 8#4000) != 8#4000 ))" -ne 0 ]; then
		echo "build-jb-image: $f lost its set-uid bit (mode $mode)" >&2
		exit 1
	fi
done

echo
echo "== building the image from it"
python3 "$TOOLS/mkapfs.py" -o /root/root-jb.apfs -z --plain fw redwood="$TREE" 2>&1 | tail -3
[ -f /root/root-jb.apfs ] || { echo "build-jb-image: mkapfs wrote nothing" >&2; exit 1; }
cp /root/root-jb.apfs /mnt/d/Liquid/ios/thyme/root-jb.apfs
ls -l /mnt/d/Liquid/ios/thyme/root-jb.apfs

echo
echo "== what the image says"
python3 "$TOOLS/apfs.py" ls /mnt/d/Liquid/ios/thyme/root-jb.apfs / 2>&1 | head -8
python3 "$TOOLS/apfs.py" ls /mnt/d/Liquid/ios/thyme/root-jb.apfs /jb-bin 2>&1 | head -8
