/* sys/termios.h for picolibc's <termios.h>: the PS2 has no terminal, the
   calls fail (linux/lnxapp.cpp only saves and restores its settings). */
#ifndef PS2_SYS_TERMIOS_H
#define PS2_SYS_TERMIOS_H

#include <errno.h>

typedef unsigned tcflag_t;
typedef unsigned char cc_t;
typedef unsigned speed_t;
#define NCCS 32
struct termios {
  tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
  cc_t c_cc[NCCS];
};
#define TCSANOW 0
#define ICANON 0000002
#define ECHO 0000010
#define VMIN 6
#define VTIME 5

static inline int tcgetattr(int fd, struct termios *t) {
  errno = ENOTTY;
  return -1;
}
static inline int tcsetattr(int fd, int act, const struct termios *t) {
  errno = ENOTTY;
  return -1;
}

#endif
