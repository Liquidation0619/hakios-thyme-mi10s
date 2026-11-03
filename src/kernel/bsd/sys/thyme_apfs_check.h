/*
 * Mount the APFS volume on /dev/rvblk0 and walk it, to compare with what the host reader
 * sees in the same image.  Only with boot-arg apfs_check.
 * See bsd/dev/thyme_apfs_check.c.
 */
#ifndef _SYS_THYME_APFS_CHECK_H_
#define _SYS_THYME_APFS_CHECK_H_

void thyme_apfs_check_init(void);

#endif /* _SYS_THYME_APFS_CHECK_H_ */
