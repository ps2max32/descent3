/* sys/dirent.h for picolibc's <dirent.h>, which has none for this target:
   directories of the IOP file server (ps2/ps2_posix.c, on mx_fio_dopen). */
#ifndef PS2_SYS_DIRENT_H
#define PS2_SYS_DIRENT_H

#include <sys/types.h>

#define MAXNAMLEN 255

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

struct dirent {
  ino_t d_ino;
  unsigned char d_type;
  char d_name[MAXNAMLEN + 1];
};

typedef struct ps2_dir DIR;

#endif
