/* sys/statvfs.h, which picolibc lacks; libc++'s filesystem (space())
   needs it. statvfs is in ps2/ps2_posix.c and always fails. */
#ifndef PS2_SYS_STATVFS_H
#define PS2_SYS_STATVFS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif


struct statvfs {
  unsigned long f_bsize, f_frsize;
  fsblkcnt_t f_blocks, f_bfree, f_bavail;
  fsfilcnt_t f_files, f_ffree, f_favail;
  unsigned long f_fsid, f_flag, f_namemax;
};

int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);

#ifdef __cplusplus
}
#endif
#endif
