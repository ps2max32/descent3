/* ps2_port.h: included before every C++ source of the game (-include).
   Built without exceptions: a try block always runs, its catch blocks never
   (the errors they catch end the game instead: cfile's ThrowCFileError). */
#ifndef PS2_PORT_H
#define PS2_PORT_H

#define try if (true)
#define catch(...) else if (false)

#endif
