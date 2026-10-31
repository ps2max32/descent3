/* ps2_modules.h: the modules linked into the program (ps2_dl.cpp); the
   table is made by the Makefile from the modules' exports
   ($(OUT)/gen/ps2_modules.cpp). */
#ifndef PS2_MODULES_H
#define PS2_MODULES_H

struct ps2_export {
  const char *name;
  void *fn;
};
struct ps2_module {
  const char *name; /* the file name without its extension, e.g. "level1" */
  const ps2_export *exports;
};
/* ends with a NULL name */
extern const ps2_module ps2_modules[];

#endif
