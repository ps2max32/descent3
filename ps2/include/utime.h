/* utime.h: picolibc has none; utime is in ps2/ps2_posix.c (does nothing:
   the IOP file server keeps its own times). */
#ifndef PS2_UTIME_H
#define PS2_UTIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
struct utimbuf {
  time_t actime, modtime;
};
int utime(const char *path, const struct utimbuf *times);
#ifdef __cplusplus
}
#endif
#endif
