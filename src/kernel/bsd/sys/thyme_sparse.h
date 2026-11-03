/*
 * A block device over an image holding only some of its blocks: how the real iOS root
 * volume is read on the phone, which cannot hold all of it.  Only with boot-arg
 * apfs_sparse=<offset in md0>.  See bsd/dev/thyme_sparse.c.
 */
#ifndef _SYS_THYME_SPARSE_H_
#define _SYS_THYME_SPARSE_H_

void thyme_sparse_init(void);
void thyme_sparse_report(void);

#endif /* _SYS_THYME_SPARSE_H_ */
