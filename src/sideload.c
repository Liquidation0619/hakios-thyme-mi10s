/*
 * sideload: put the apps in /redwood/apps into /Applications, which is on the read-only root volume.
 *
 * Runs from /redwood/etc/rc as root, before launchd starts, so Launch Services' scan of
 * /Applications finds them. Each app gets two things:
 *   - a synthetic entry of the root APFS volume at /Applications/NAME.app (kern.apfs_synthetic):
 *     a directory that is not on the volume, which is a mount point;
 *   - a bindfs of the app's real directory on that entry.
 * Nothing is written to the system volume.
 */
#include "mini.h"

#define APPS  "/redwood/apps"
#define DEST  "/Applications"

static void
join2(char *out, const char *a, const char *b)
{
	strcpy(out, a);
	strcpy(out + strlen(out), "/");
	strcpy(out + strlen(out), b);
}

static int
sideload_one(const char *name)
{
	char target[256], from[256];
	int ok;

	if (strlen(name) < 5 || strlen(name) > 128) {
		return 0;
	}
	if (strcmp(name + strlen(name) - 4, ".app") != 0) {
		return 0;
	}
	join2(target, DEST, name);
	join2(from, APPS, name);
	if (sysctlbyname("kern.apfs_synthetic", NULL, NULL, target, strlen(target)) != 0) {
		printf("sideload: %s: synthetic entry failed: %s\n", target, strerror(errno));
		return 0;
	}
	ok = mount("bindfs", target, MNT_RDONLY, (void *)from) == 0;
	if (!ok) {
		printf("sideload: bindfs %s on %s failed: %s\n", from, target, strerror(errno));
		return 0;
	}
	printf("sideload: %s -> %s\n", name, target);
	return 1;
}

int
main(int argc, char **argv, char **envp)
{
	static char buf[8192];
	struct dirent *de;
	int fd, n, done = 0;
	off_t pos = 0;

	(void)argc;
	(void)argv;
	(void)envp;
	fd = open(APPS, O_RDONLY | O_DIRECTORY, 0);
	if (fd < 0) {
		printf("sideload: no %s: %s\n", APPS, strerror(errno));
		return 0;
	}
	while ((n = getdirentries(fd, buf, sizeof(buf), &pos)) > 0) {
		for (char *p = buf; p < buf + n;) {
			de = (struct dirent *)(void *)p;
			if (de->d_reclen == 0) {
				break;
			}
			p += de->d_reclen;
			if (de->d_namlen == 0 || de->d_name[0] == '.') {
				continue;
			}
			if (sideload_one(de->d_name)) {
				done++;
			}
		}
	}
	close(fd);
	printf("sideload: %d app(s) placed in %s\n", done, DEST);
	return 0;
}
